/*
 * ExerciseUpdateJobBase.cpp
 *
 *  Created on: 2018年9月4日
 *      Author: peter
 */

#include "ExerciseUpdateJobBase.hpp"
#include "logger.hpp"

extern std::ostream log_fp;


ExerciseUpdateJobBase::ExerciseUpdateJobBase(int jobType, int sid, const RedisContext & redisConn,
		std::unique_ptr<mysqlpp::Connection> && mysqlConn) : supper_t(jobType, sid, redisConn, std::move(mysqlConn))
{
	LOG_DEBUG(jobType, sid, log_fp, "ExerciseUpdateJobBase::ExerciseUpdateJobBase");
}

void ExerciseUpdateJobBase::update_source_code(const char * source_code)
{
	if (source_code == nullptr) {
		LOG_WARNING(jobType, sid, log_fp, "empty source code!");
		return;
	}

	mysqlpp::Query insert = mysqlConn->query(
			"insert into source (s_id, source_code)"
			"values (%0, %1q)"
	);
	insert.parse();
	mysqlpp::SimpleResult res = insert.execute(sid, source_code);
	if (!res) {
		MysqlEmptyResException e(insert.errnum(), insert.error());
		EXCEPT_FATAL(jobType, sid, log_fp, "Update source code failed!", e);
		throw e;
	}
}

void ExerciseUpdateJobBase::update_compile_info(const char * compile_info)
{
	if (compile_info == nullptr) {
		LOG_WARNING(jobType, sid, log_fp, "empty compile info!");
		return;
	}

	mysqlpp::Query insert = mysqlConn->query(
			"insert into compile_info (s_id, compile_error_info) values (%0, %1q)"
	);
	insert.parse();
	mysqlpp::SimpleResult res = insert.execute(sid, compile_info);
	if (!res) {
		MysqlEmptyResException e(insert.errnum(), insert.error());
		EXCEPT_FATAL(jobType, sid, log_fp, "Update compile info failed!", e);
		throw e;
	}
}

void ExerciseUpdateJobBase::update_exercise_problem_submit(int delta)
{
	LOG_DEBUG(jobType, sid, log_fp, "ExerciseUpdateJobBase::update_exercise_problem_submit");

	mysqlpp::Query update = mysqlConn->query(
			"update problem set p_submit = p_submit + %0 where p_id = %1"
	);
	update.parse();
	mysqlpp::SimpleResult res = update.execute(delta, this->pid);
	if (!res) {
		MysqlEmptyResException e(update.errnum(), update.error());
		EXCEPT_FATAL(jobType, sid, log_fp, "Update problem submit failed!", e);
		throw e;
	}
}

void ExerciseUpdateJobBase::update_exercise_problem_accept(int delta)
{
	LOG_DEBUG(jobType, sid, log_fp, "ExerciseUpdateJobBase::update_exercise_problem_accept");

	mysqlpp::Query update = mysqlConn->query(
			"update problem set p_accept = p_accept + %0 where p_id = %1"
	);
	update.parse();
	mysqlpp::SimpleResult res = update.execute(delta, this->pid);
	if (!res) {
		MysqlEmptyResException e(update.errnum(), update.error());
		EXCEPT_FATAL(jobType, sid, log_fp, "Update problem p_accept failed!", e);
		throw e;
	}
}

void ExerciseUpdateJobBase::update_exercise_user_submit(int delta)
{
	LOG_DEBUG(jobType, sid, log_fp, "ExerciseUpdateJobBase::update_exercise_user_submit");

	mysqlpp::Query update = mysqlConn->query(
			"update user set u_submit = u_submit + %0 where u_id = %1"
	);
	update.parse();
	mysqlpp::SimpleResult res = update.execute(delta, uid);
	if (!res) {
		MysqlEmptyResException e(update.errnum(), update.error());
		EXCEPT_FATAL(jobType, sid, log_fp, "Update user submit failed!", e);
		throw e;
	}
}

void ExerciseUpdateJobBase::update_exercise_user_accept(int delta)
{
	LOG_DEBUG(jobType, sid, log_fp, "ExerciseUpdateJobBase::update_exercise_user_accept");

	mysqlpp::Query update = mysqlConn->query(
			"update user set u_accept = u_accept + %0 where u_id = %1"
	);
	update.parse();
	mysqlpp::SimpleResult res = update.execute(delta, uid);
	if (!res) {
		MysqlEmptyResException e(update.errnum(), update.error());
		EXCEPT_FATAL(jobType, sid, log_fp, "Update user u_accept failed!", e);
		throw e;
	}
}

void ExerciseUpdateJobBase::update_user_and_problem()
{
	LOG_DEBUG(jobType, sid, log_fp, "ExerciseUpdateJobBase::update_user_and_problem");

	bool already_AC = false;
	try {
		if(this->ExerciseUpdateJobBase::get_exercise_user_problem_status() == user_problem_status::ACCEPTED) {
			already_AC = true;
		} else {
			already_AC = false;
		}
	} catch (const std::exception & e) {
		EXCEPT_WARNING(jobType, sid, log_fp, "Query already ac before failed!", e);
		//DO NOT THROW
		already_AC = false;
	}

	// AC后再提交不增加submit数
	if (already_AC == false) {
		this->update_exercise_user_submit(1);
		this->update_exercise_problem_submit(1);
		if (result.judge_result == UnitedJudgeResult::ACCEPTED) {
			this->update_exercise_user_accept(1);
			this->update_exercise_problem_accept(1);
		}
	}
}

user_problem_status ExerciseUpdateJobBase::get_exercise_user_problem_status()
{
	LOG_DEBUG(jobType, sid, log_fp, "ExerciseUpdateJobBase::get_exercise_user_problem_status");

	mysqlpp::Query query = mysqlConn->query(
		"select status from user_problem "
		"where u_id = %0 and p_id = %1 and c_id is NULL"
	);
	query.parse();

	mysqlpp::StoreQueryResult res = query.store(uid, pid);

	if (res.empty()) {
		if (query.errnum() != 0) {
			MysqlEmptyResException e(query.errnum(), query.error());
			EXCEPT_FATAL(jobType, sid, log_fp, "Query user problem status failed!", e);
			throw e;
		}
		return user_problem_status::TODO;
	}

	switch ((int) res[0][0]) {
		case 0:
			return user_problem_status::ACCEPTED;
		case 1:
			return user_problem_status::ATTEMPTED;
		default:
			LOG_FATAL(jobType, sid, log_fp, "Undefined user's problem status!");
			throw std::logic_error("Undefined user's problem status!");
	}
}

void ExerciseUpdateJobBase::update_user_problem()
{
	LOG_DEBUG(jobType, sid, log_fp, "ExerciseUpdateJobBase::update_user_problem");

	bool is_ac = this->result.judge_result == UnitedJudgeResult::ACCEPTED ? true : false;
	user_problem_status old_status = this->ExerciseUpdateJobBase::get_exercise_user_problem_status();
	switch (old_status) {
		case user_problem_status::TODO: {
			// 未做过此题时，user_problem 表中无记录，使用 insert
			mysqlpp::Query insert = mysqlConn->query(
					"insert into user_problem (u_id, p_id, status)"
					"values (%0, %1, %2)"
			);
			insert.parse();
			mysqlpp::SimpleResult ret = insert.execute(uid, pid, is_ac == true ? 0 : 1);
			if (!ret) {
				MysqlEmptyResException e(insert.errnum(), insert.error());
				EXCEPT_FATAL(jobType, sid, log_fp, "Update user_problem failed!", e);
				throw e;
			}
			break;
		}
		case user_problem_status::ATTEMPTED: {
			// 尝试做过此题时，user_problem 表中有记录，使用 update
			mysqlpp::Query update = mysqlConn->query(
					"update user_problem set status = %0 "
					"where u_id = %1 and p_id = %2 and c_id is NULL"
			);
			update.parse();
			mysqlpp::SimpleResult ret = update.execute(is_ac == true ? 0 : 1, uid, pid);
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
