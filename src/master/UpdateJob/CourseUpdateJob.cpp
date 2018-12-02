/*
 * CourseUpdateJob.cpp
 *
 *  Created on: 2018年9月4日
 *      Author: peter
 */

#include "CourseUpdateJob.hpp"
#include "logger.hpp"
#include "CourseManagement.hpp"
#include "ExerciseManagement.hpp"

#ifndef MYSQLPP_MYSQL_HEADERS_BURIED
#	define MYSQLPP_MYSQL_HEADERS_BURIED
#endif

#include <mysql++/query.h>

extern std::ofstream log_fp;


CourseUpdateJob::CourseUpdateJob(int jobType, ojv4::s_id_type s_id, ojv4::c_id_type c_id, const kerbal::redis::RedisContext & redisConn,
		std::unique_ptr<mysqlpp::Connection> && mysqlConn) :
				ExerciseUpdateJobBase(jobType, s_id, redisConn, std::move(mysqlConn)),
				c_id(c_id)
{
	LOG_DEBUG(jobType, s_id, log_fp, BOOST_CURRENT_FUNCTION);
}

void CourseUpdateJob::update_solution()
{
	LOG_DEBUG(jobType, s_id, log_fp, BOOST_CURRENT_FUNCTION);

	mysqlpp::Query insert = mysqlConn->query(
			"insert into solution "
			"(s_id, u_id, p_id, s_lang, s_result, s_time, s_mem, s_posttime, c_id, s_similarity_percentage)"
			"values (%0, %1, %2, %3, %4, %5, %6, %7q, %8, %9)"
	);
	insert.parse();
	mysqlpp::SimpleResult res = insert.execute(s_id, u_id, p_id, (int) lang, (int) result.judge_result,
											(int)result.cpu_time.count(), (int)result.memory.count(), s_posttime, c_id,
												similarity_percentage);
	if (!res) {
		MysqlEmptyResException e(insert.errnum(), insert.error());
		EXCEPT_FATAL(jobType, s_id, log_fp, "Update solution failed!", e);
		throw e;
	}
}

void CourseUpdateJob::update_user()
{
	LOG_DEBUG(jobType, s_id, log_fp, BOOST_CURRENT_FUNCTION);

	try {
		CourseManagement::update_user_s_submit_and_accept_num(*this->mysqlConn, this->c_id, this->u_id);
	} catch (const std::exception & e) {
		EXCEPT_FATAL(jobType, s_id, log_fp, "Update course-user's submit and accept number failed!", e);
		throw;
	}

	// 更新练习视角用户的提交数和通过数
	// 使课程中某个 user 的提交数与通过数同步到 user 表中
	try {
		ExerciseManagement::update_user_s_submit_and_accept_num(*this->mysqlConn, this->u_id);
	} catch (const std::exception & e) {
		EXCEPT_FATAL(jobType, s_id, log_fp, "Update course-user's submit and accept number in exercise view failed!", e);
		throw;
	}
}

void CourseUpdateJob::update_problem()
{
	LOG_DEBUG(jobType, s_id, log_fp, BOOST_CURRENT_FUNCTION);

	try {
		CourseManagement::update_problem_s_submit_and_accept_num(*this->mysqlConn, this->c_id, this->p_id);
	} catch (const std::exception & e) {
		EXCEPT_FATAL(jobType, s_id, log_fp, "Update course-problem's submit and accept number failed!", e);
		throw;
	}

	// 更新练习视角题目的提交数和通过数
	// 使课程中某个 problem 的提交数与通过数同步到 problem 表中
	try {
		ExerciseManagement::update_problem_s_submit_and_accept_num(*this->mysqlConn, this->p_id);
	} catch (const std::exception & e) {
		EXCEPT_FATAL(jobType, s_id, log_fp, "Update course-problem's submit and accept number in exercise view failed!", e);
		throw;
	}
}


void CourseUpdateJob::update_user_problem()
{
	LOG_DEBUG(jobType, s_id, log_fp, BOOST_CURRENT_FUNCTION);

	try {
		CourseManagement::update_user_problem(*this->mysqlConn, this->c_id, this->u_id, this->p_id);
	} catch (const std::exception & e) {
		EXCEPT_FATAL(jobType, s_id, log_fp, "Update user-problem failed!", e);
		throw;
	}

	// 更新练习视角的用户通过情况表
	try {
		ExerciseManagement::update_user_problem(*this->mysqlConn, u_id, p_id);
	} catch (const std::exception & e) {
		EXCEPT_FATAL(jobType, s_id, log_fp, "Update user-problem in exercise view failed!", e);
		throw;
	}
}

void CourseUpdateJob::update_user_problem_status()
{
	LOG_DEBUG(jobType, s_id, log_fp, BOOST_CURRENT_FUNCTION);

	try {
		CourseManagement::update_user_problem_status(*this->mysqlConn, this->c_id, this->u_id, this->p_id);
	} catch (const std::exception & e) {
		EXCEPT_FATAL(jobType, s_id, log_fp, "Update course-user's problem status failed!", e);
		throw;
	}

	// 更新练习视角的用户通过情况表
	try {
		ExerciseManagement::update_user_problem_status(*this->mysqlConn, this->u_id, this->p_id);
	} catch (const std::exception & e) {
		EXCEPT_FATAL(jobType, s_id, log_fp, "Update course-user's problem status in exercise view failed!", e);
		throw;
	}
}
