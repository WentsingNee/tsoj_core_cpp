/*
 * ExerciseRejudgeJobBase.cpp
 *
 *  Created on: 2018年11月20日
 *      Author: peter
 */

#include <ExerciseRejudgeJobBase.hpp>

#include "logger.hpp"

#ifndef MYSQLPP_MYSQL_HEADERS_BURIED
#	define MYSQLPP_MYSQL_HEADERS_BURIED
#endif

#include <mysql++/query.h>

extern std::ofstream log_fp;

ExerciseRejudgeJobBase::ExerciseRejudgeJobBase(int jobType, ojv4::s_id_type s_id, const kerbal::redis::RedisContext & redisConn,
		std::unique_ptr<mysqlpp::Connection> && mysqlConn):
			RejudgeJobBase(jobType, s_id, redisConn, std::move(mysqlConn))
{
	LOG_DEBUG(jobType, s_id, log_fp, BOOST_CURRENT_FUNCTION);
}

ojv4::s_result_enum ExerciseRejudgeJobBase::move_orig_solution_to_rejudge_solution()
{
	LOG_DEBUG(jobType, s_id, log_fp, BOOST_CURRENT_FUNCTION);

	ojv4::s_result_enum orig_result;
	ojv4::s_time_in_milliseconds orig_time;
	ojv4::s_mem_in_byte orig_mem;
	ojv4::s_similarity_type orig_similarity;
	{
		mysqlpp::Query query = mysqlConn->query(
				"select s_result, s_time, s_mem, s_similarity_percentage "
				"from solution where s_id = %0");
		query.parse();

		mysqlpp::StoreQueryResult res = query.store(this->s_id);

		if (query.errnum() != 0) {
			MysqlEmptyResException e(query.errnum(), query.error());
			EXCEPT_FATAL(jobType, s_id, log_fp, "Query original result failed!", e);
			throw e;
		}

		if (res.empty()) {
			MysqlEmptyResException e(query.errnum(), query.error());
			EXCEPT_FATAL(jobType, s_id, log_fp, "Empty original result!", e);
			throw e;
		}

		const auto & row = res[0];
		orig_result = ojv4::s_result_enum(ojv4::s_result_in_db_type(row["s_result"]));
		orig_time = ojv4::s_time_in_milliseconds(row["s_time"]);
		orig_mem = ojv4::s_mem_in_byte(row["s_mem"]);
		orig_similarity = row["s_similarity_percentage"];
	}

	mysqlpp::Query insert = mysqlConn->query(
			"insert into rejudge_solution "
			"(ct_id, s_id, orig_result, orig_time, orig_mem, orig_similarity_percentage, rejudge_time) "
			"values(%0, %1, %2, %3, %4, %5, %6q)"
	);
	insert.parse();

	mysqlpp::SimpleResult res = insert.execute(0, s_id, ojv4::s_result_in_db_type(orig_result), orig_time.count(),
												orig_mem.count(), orig_similarity, rejudge_time);

	if(!res) {
		MysqlEmptyResException e(insert.errnum(), insert.error());
		EXCEPT_FATAL(jobType, s_id, log_fp, "Insert original result into rejudge_solution failed!", e);
		throw e;
	}

	return orig_result;
}

void ExerciseRejudgeJobBase::update_solution()
{
	LOG_DEBUG(jobType, s_id, log_fp, BOOST_CURRENT_FUNCTION);

    mysqlpp::Query update = mysqlConn->query(
            "update solution "
            "set s_result = %0, s_time = %1, s_mem = %2, s_similarity_percentage = %3 "
            "where s_id = %4"
    );
    update.parse();
	mysqlpp::SimpleResult res = update.execute(ojv4::s_result_in_db_type(result.judge_result), result.cpu_time.count(),
												result.memory.count(), similarity_percentage, this->s_id);


	if (!res) {
		MysqlEmptyResException e(update.errnum(), update.error());
		EXCEPT_FATAL(jobType, s_id, log_fp, "Update solution failed!", e);
		throw e;
	}
}

void ExerciseRejudgeJobBase::rejudge_compile_info(ojv4::s_result_enum orig_result)
{
	LOG_DEBUG(jobType, s_id, log_fp, BOOST_CURRENT_FUNCTION);

	if(this->result.judge_result == ojv4::s_result_enum::COMPILE_ERROR)
	{
		mysqlpp::Query update = mysqlConn->query(
				"insert into compile_info(s_id, compile_error_info) "
				"values(%0, %1q) "
				"on duplicate key "
				"update compile_error_info = %1q"
		);
		update.parse();

		kerbal::redis::RedisReply ce_info;
		try {
			ce_info = this->get_compile_info();
		} catch (const std::exception & e) {
			EXCEPT_FATAL(jobType, s_id, log_fp, "Get compile error info failed!", e);
			throw;
		}

		mysqlpp::SimpleResult res = update.execute(s_id, ce_info->str);

		if(!res) {
			MysqlEmptyResException e(update.errnum(), update.error());
			EXCEPT_FATAL(jobType, s_id, log_fp, "Update compile info failed!", e);
			throw e;
		}
	} else {
		mysqlpp::Query del = mysqlConn->query(
				"delete from compile_info "
				"where s_id = %0"
		);
		del.parse();

		mysqlpp::SimpleResult res = del.execute(s_id);

		if(!res) {
			LOG_WARNING(jobType, s_id, log_fp, "Delete compile info failed!");
			// 删除失败可以不做 failed 处理
		}
	}
}
