/*
 * CourseUpdateJob.cpp
 *
 *  Created on: 2018年9月4日
 *      Author: peter
 */

#include "CourseUpdateJob.hpp"
#include "logger.hpp"

extern std::ostream log_fp;


CourseUpdateJob::CourseUpdateJob(int jobType, int sid, const kerbal::redis::RedisContext & redisConn,
		std::unique_ptr<mysqlpp::Connection> && mysqlConn) : supper_t(jobType, sid, redisConn, std::move(mysqlConn))
{
	LOG_DEBUG(jobType, sid, log_fp, "CourseUpdateJob::CourseUpdateJob");
}

void CourseUpdateJob::update_solution()
{
	LOG_DEBUG(jobType, sid, log_fp, "CourseUpdateJob::update_solution");

	mysqlpp::Query insert = mysqlConn->query(
			"insert into solution "
			"(s_id, u_id, p_id, s_lang, s_result, s_time, s_mem, s_posttime, c_id, s_similarity_percentage)"
			"values (%0, %1, %2, %3, %4, %5, %6, %7q, %8, %9)"
	);
	insert.parse();
	mysqlpp::SimpleResult res = insert.execute(sid, uid, pid, (int) lang, (int) result.judge_result,
											(int)result.cpu_time.count(), (int)result.memory.count(), postTime, cid,
												result.similarity_percentage);
	if (!res) {
		MysqlEmptyResException e(insert.errnum(), insert.error());
		EXCEPT_FATAL(jobType, sid, log_fp, "Select status from user_problem failed!", e);
		throw e;
	}
}

void CourseUpdateJob::update_problem_submit_and_accept_num_in_this_course()
{
	std::set<int> accepted_users;
	int submit_num = 0;

	{
		mysqlpp::Query query = mysqlConn->query(
				"select u_id, s_result from solution where p_id = %0 and c_id = %1 order by s_id"
		);
		query.parse();

		mysqlpp::StoreQueryResult res = query.store(this->pid, this->cid);

		if (query.errnum() != 0) {
			MysqlEmptyResException e(query.errnum(), query.error());
			EXCEPT_FATAL(jobType, sid, log_fp, "Query problem's solutions failed!", e);
			throw e;
		}

		for (const auto & row : res) {
			int u_id_in_this_row = row["u_id"];
			// 此题已通过的用户的集合中无此条 solution 对应的用户
			if (accepted_users.find(u_id_in_this_row) == accepted_users.end()) {
				UnitedJudgeResult result = UnitedJudgeResult(int(row["s_result"]));
				switch(result) {
					case UnitedJudgeResult::ACCEPTED:
						accepted_users.insert(u_id_in_this_row);
						++submit_num;
						break;
					case UnitedJudgeResult::SYSTEM_ERROR: // ignore system error
						break;
					default:
						++submit_num;
						break;
				}
			}
		}
	}

	{
		mysqlpp::Query update = mysqlConn->query(
				"update course_problem set c_p_submit = %0, c_p_accept = %1 where p_id = %2 and c_id = %3"
		);
		update.parse();

		mysqlpp::SimpleResult update_res = update.execute(submit_num, accepted_users.size(), this->pid, this->cid);
		if (!update_res) {
			MysqlEmptyResException e(update.errnum(), update.error());
			EXCEPT_FATAL(jobType, sid, log_fp, "Update problem's submit and accept number in this course failed!", e, ", course id: ", this->cid);
			throw e;
		}
	}
}

void CourseUpdateJob::update_user_submit_and_accept_num_in_this_course()
{
	std::set<int> accepted_problems;
	int submit_num = 0;

	{
		mysqlpp::Query query = mysqlConn->query(
				"select p_id, s_result from solution where u_id = %0 and c_id = %1 order by s_id"
		);
		query.parse();

		mysqlpp::StoreQueryResult res = query.store(this->uid, this->cid);

		if (query.errnum() != 0) {
			MysqlEmptyResException e(query.errnum(), query.error());
			EXCEPT_FATAL(jobType, sid, log_fp, "Query user's solutions failed!", e);
			throw e;
		}

		for (const auto & row : res) {
			int p_id_in_this_row = row["p_id"];
			if (accepted_problems.find(p_id_in_this_row) == accepted_problems.end()) {
				// 此用户已通过的题的集合中无此条 solution 对应的题
				UnitedJudgeResult result = UnitedJudgeResult(int(row["s_result"]));
				switch(result) {
					case UnitedJudgeResult::ACCEPTED:
						accepted_problems.insert(p_id_in_this_row);
						++submit_num;
						break;
					case UnitedJudgeResult::SYSTEM_ERROR: // ignore system error
						break;
					default:
						++submit_num;
						break;
				}
			}
		}
	}

	{
		mysqlpp::Query update = mysqlConn->query(
				"update course_user set c_submit = %0, c_accept = %1 where u_id = %2 and c_id = %3"
		);
		update.parse();

		mysqlpp::SimpleResult update_res = update.execute(submit_num, accepted_problems.size(), this->uid, this->cid);
		if (!update_res) {
			MysqlEmptyResException e(update.errnum(), update.error());
			EXCEPT_FATAL(jobType, sid, log_fp, "Update user's submit and accept number in this course failed!", e, ", course id: ", this->cid);
			throw e;
		}
	}
}


void CourseUpdateJob::update_user_and_problem()
{
	LOG_DEBUG(jobType, sid, log_fp, "CourseUpdateJob::update_user_and_problem");

	try {
		this->update_user_submit_and_accept_num_in_this_course();
	} catch (const std::exception & e) {
		EXCEPT_FATAL(jobType, sid, log_fp, "Update user submit and accept number in this course failed!", e, ", course id: ", this->cid);
		throw;
	}

	try {
		this->update_problem_submit_and_accept_num_in_this_course();
	} catch (const std::exception & e) {
		EXCEPT_FATAL(jobType, sid, log_fp, "Update problem submit and accept number in this course failed!", e, ", course id: ", this->cid);
		throw;
	}

	// 更新练习视角用户的提交数和通过数
	// 使 course_user 中某个 user 的提交数与通过数同步到 user 表中
	this->supper_t::update_user_and_problem();
}

user_problem_status CourseUpdateJob::get_course_user_problem_status()
{
	LOG_DEBUG(jobType, sid, log_fp, "CourseUpdateJob::get_course_user_problem_status");

	mysqlpp::Query query = mysqlConn->query(
			"select status from user_problem "
			"where u_id = %0 and p_id = %1 and c_id = %2"
	);
	query.parse();

	mysqlpp::StoreQueryResult res = query.store(uid, pid, cid);

	if (res.empty()) {
		if (query.errnum() != 0) {
			MysqlEmptyResException e(query.errnum(), query.error());
			EXCEPT_FATAL(jobType, sid, log_fp, "Select status from user_problem failed!", e);
			throw e;
		}
		return user_problem_status::TODO;
	}
	switch((int) res[0][0]) {
		case 0:
			return user_problem_status::ACCEPTED;
		case 1:
			return user_problem_status::ATTEMPTED;
		default:
			LOG_FATAL(jobType, sid, log_fp, "Undefined user's problem status!");
			throw std::logic_error("Undefined user's problem status!");
	}
}

void CourseUpdateJob::update_user_problem_status()
{
	LOG_DEBUG(jobType, sid, log_fp, "CourseUpdateJob::update_user_problem");

	// 先更新练习视角的用户通过情况表
	// 使课程中（cid 非空） user_problem 中某个 user 对某题的状态同步到非课程的 user_problem 中（cid 为空）
	this->supper_t::update_user_problem_status();

	if (this->result.judge_result == UnitedJudgeResult::SYSTEM_ERROR) { // ignore system error
		return;
	}

	bool is_ac = this->result.judge_result == UnitedJudgeResult::ACCEPTED ;

	user_problem_status old_status = user_problem_status::TODO;
	try {
		old_status = this->get_course_user_problem_status();
	} catch (const std::exception & e) {
		EXCEPT_FATAL(jobType, sid, log_fp, "Get user problem status failed!", e);
		throw;
	}

	switch(old_status) {
		case user_problem_status::TODO: {
			mysqlpp::Query insert = mysqlConn->query("insert into user_problem (u_id, p_id, c_id, status) "
					"values (%0, %1, %2, %3)");
			insert.parse();
			mysqlpp::SimpleResult ret = insert.execute(uid, pid, cid, is_ac ? 0 : 1);
			if (!ret) {
				MysqlEmptyResException e(insert.errnum(), insert.error());
				EXCEPT_FATAL(jobType, sid, log_fp, "Update user_problem failed!", e);
				throw e;
			}
			break;
		}
		case user_problem_status::ATTEMPTED: {
			mysqlpp::Query update = mysqlConn->query(
					"update user_problem set status = %0 "
					"where u_id = %1 and p_id = %2 and c_id = %3"
			);
			update.parse();
			mysqlpp::SimpleResult ret = update.execute(is_ac ? 0 : 1, uid, pid, cid);
			if (!ret) {
				MysqlEmptyResException e(update.errnum(), update.error());
				EXCEPT_FATAL(jobType, sid, log_fp, "Update user_problem failed!", e);
				throw e;
			}
			break;
		}
		case user_problem_status::ACCEPTED:
			return;
		default:
			LOG_FATAL(jobType, sid, log_fp, "Undefined user's problem status!");
			throw std::logic_error("Undefined user's problem status!");
	}
}
