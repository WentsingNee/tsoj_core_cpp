/*
 * RejudgeJobBase.cpp
 *
 *  Created on: 2018年11月20日
 *	  Author: peter
 */

#include "RejudgeJobBase.hpp"
#include "logger.hpp"

#ifndef MYSQLPP_MYSQL_HEADERS_BURIED
#	define MYSQLPP_MYSQL_HEADERS_BURIED
#endif

#include <mysql++/transaction.h>

#include <mysql_conn_factory.hpp>

extern std::ofstream log_fp;

RejudgeJobBase::RejudgeJobBase(int jobType, ojv4::s_id_type s_id, kerbal::redis_v2::connection & redis_conn) :
		UpdateJobBase(jobType, s_id, redis_conn), rejudge_time(mysqlpp::DateTime::now())
{
	LOG_DEBUG(jobType, s_id, log_fp, BOOST_CURRENT_FUNCTION);
}


void RejudgeJobBase::handle()
{
	LOG_DEBUG(jobType, s_id, log_fp, BOOST_CURRENT_FUNCTION);

	std::exception_ptr move_solution_exception = nullptr;
	std::exception_ptr update_solution_exception = nullptr;
	std::exception_ptr update_compile_error_info_exception = nullptr;
	std::exception_ptr update_user_exception = nullptr;
	std::exception_ptr update_problem_exception = nullptr;
	std::exception_ptr update_user_problem_exception = nullptr;
	std::exception_ptr update_user_problem_status_exception = nullptr;
	std::exception_ptr commit_exception = nullptr;

	// 本次更新任务的 mysql 连接
	auto mysql_conn_handle = sync_fetch_mysql_conn();
	mysqlpp::Connection & mysql_conn = *mysql_conn_handle;

	mysqlpp::Transaction trans(mysql_conn);
	try {
		this->move_orig_solution_to_rejudge_solution(mysql_conn);
	} catch (const std::exception & e) {
		move_solution_exception = std::current_exception();
		EXCEPT_FATAL(jobType, s_id, log_fp, "Move original solution failed!", e);
		//DO NOT THROW
	}

	if (move_solution_exception == nullptr) {

		try {
			this->update_solution(mysql_conn);
		} catch (const std::exception & e) {
			move_solution_exception = std::current_exception();
			EXCEPT_FATAL(jobType, s_id, log_fp, "Update solution failed!", e);
			//DO NOT THROW
		}

		if (update_solution_exception == nullptr) {
			try {
				this->update_compile_info(mysql_conn);
			} catch (const std::exception & e) {
				update_compile_error_info_exception = std::current_exception();
				EXCEPT_FATAL(jobType, s_id, log_fp, "Update compile error info failed!", e);
				//DO NOT THROW
			}

			try {
				this->update_user(mysql_conn);
			} catch (const std::exception & e) {
				update_user_exception = std::current_exception();
				EXCEPT_FATAL(jobType, s_id, log_fp, "Update user failed!", e);
				//DO NOT THROW
			}

			try {
				this->update_problem(mysql_conn);
			} catch (const std::exception & e) {
				update_problem_exception = std::current_exception();
				EXCEPT_FATAL(jobType, s_id, log_fp, "Update problem failed!", e);
				//DO NOT THROW
			}

			try {
				this->update_user_problem(mysql_conn);
			} catch (const std::exception & e) {
				update_user_problem_exception = std::current_exception();
				EXCEPT_FATAL(jobType, s_id, log_fp, "Update user problem failed!", e);
				//DO NOT THROW
			}

//			try {
//				this->update_user_problem_status(mysql_conn);
//			} catch (const std::exception & e) {
//				update_user_problem_status_exception = std::current_exception();
//				EXCEPT_FATAL(jobType, s_id, log_fp, "Update user problem status failed!", e);
//				//DO NOT THROW
//			}

			try {
				this->send_rejudge_notification(mysql_conn);
			} catch (const std::exception & e){
				EXCEPT_WARNING(jobType, s_id, log_fp, "Send rejudge notification failed!", e);
				//DO NOT THROW
			}

		}

		try {
			trans.commit();
		} catch (const std::exception & e) {
			EXCEPT_FATAL(jobType, s_id, log_fp, "MySQL commit failed!", e);
			commit_exception = std::current_exception();
			//DO NOT THROW
		} catch (...) {
			UNKNOWN_EXCEPT_FATAL(jobType, s_id, log_fp, "MySQL commit failed!");
			commit_exception = std::current_exception();
			//DO NOT THROW
		}
	}

	this->clear_redis_info();
}

void RejudgeJobBase::clear_redis_info() noexcept try
{
	this->UpdateJobBase::clear_this_jobs_info_in_redis();

    auto redis_conn_handler = sync_fetch_redis_conn();
	kerbal::redis_v2::operation opt(*redis_conn_handler);

	try {
		boost::format judge_status_templ("judge_status:%d:%d");
		if (opt.del((judge_status_templ % jobType % s_id).str()) == false) {
			LOG_WARNING(jobType, s_id, log_fp, "Doesn't delete judge_status actually!");
		}
	} catch (const std::exception & e) {
		LOG_WARNING(jobType, s_id, log_fp, "Exception occurred while deleting judge_status!");
		//DO NOT THROW
	}

	try {
		boost::format similarity_details("similarity_details:%d:%d");
		if (opt.del((similarity_details % jobType % s_id).str()) == false) {
//			LOG_WARNING(jobType, s_id, log_fp, "Doesn't delete similarity_details actually!");
		}
	} catch (const std::exception & e) {
		LOG_WARNING(jobType, s_id, log_fp, "Exception occurred while deleting similarity_details!");
		//DO NOT THROW
	}

	try {
		boost::format job_info("job_info:%d:%d");
		if (opt.del((job_info % jobType % s_id).str()) == false) {
//			LOG_WARNING(jobType, s_id, log_fp, "Doesn't delete job_info actually!");
		}
	} catch (const std::exception & e) {
		LOG_WARNING(jobType, s_id, log_fp, "Exception occurred while deleting job_info!");
		//DO NOT THROW
	}
} catch (...) {
	UNKNOWN_EXCEPT_FATAL(jobType, s_id, log_fp, "Clear this jobs info in redis failed!");
}
