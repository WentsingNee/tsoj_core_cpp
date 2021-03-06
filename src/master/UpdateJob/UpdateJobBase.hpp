/*
 * UpdateJobBase.hpp
 *
 *  Created on: 2018年9月4日
 *      Author: peter
 */

#ifndef SRC_MASTER_UPDATEJOBBASE_HPP_
#define SRC_MASTER_UPDATEJOBBASE_HPP_

#include "JobBase.hpp"
#include "Result.hpp"
#include "db_typedef.hpp"
#include "mysql_empty_res_exception.hpp"

#include <boost/current_function.hpp>

#ifndef MYSQLPP_MYSQL_HEADERS_BURIED
#	define MYSQLPP_MYSQL_HEADERS_BURIED
#endif

#include <mysql++/connection.h>

#include <kerbal/redis/operation.hpp>
#include <redis_conn_factory.hpp>

/**
 * @brief JobBase 的子类，同时是所有类型的 update 类的基类。
 * @note 此类为虚基类，update_solution 等函数需要由子类实现，此类不可实例化
 */
class UpdateJobBase: public JobBase
{
	protected:
		ojv4::u_id_type u_id; ///< @brief user id

		std::string s_posttime; ///< @brief post time

		SolutionDetails result;

		ojv4::s_similarity_type similarity_percentage;

	protected:

		/**
		 * @brief 通用基类的构造函数，除父类 JobBase 构造函数获得的信息除外，还额外获取更新类别任务的额外信息如 u_id,
		 * cid, postTime等信息。
		 */
		UpdateJobBase(int jobType, ojv4::s_id_type s_id, kerbal::redis_v2::connection & redis_conn);

	public:
		/**
		 * @brief 执行任务
		 * @warning 本方法实现了基类中规定的 void handle() 接口, 且子类不可再覆盖
		 */
		virtual void handle() override;

	protected:
		/**
		 * @brief 将本次提交记录更新至 solution 表
		 * @warning 仅规定 update solution 表的接口, 具体操作需由子类实现
		 */
		virtual void update_solution(mysqlpp::Connection & mysql_conn) = 0;

		/**
		 * @brief 将提交代码更新至 mysql
		 * @warning 仅规定 update source_code 表的接口, 具体操作需由子类实现
		 */
		virtual void update_source_code(mysqlpp::Connection & mysql_conn) = 0;

		/**
		 * @brief 将编译错误信息更新至 mysql
		 * @warning 仅规定 update compile_info 表的接口, 具体操作需由子类实现
		 */
		virtual void update_compile_info(mysqlpp::Connection & mysql_conn) = 0;

		/**
		 * @brief 从 redis 中取得编译错误信息
		 * @return Redis 返回集. 注意! 返回集的类型只可能为字符串类型, 否则该方法会通过抛出异常报告错误
		 * @throws RedisUnexpectedCaseException 如果取得的结果不为字符串类型 (包括空类型), 则抛出此异常
		 * @throws std::exception 该方法执行过程中还会因 redis 操作失败
		 */
		kerbal::redis_v2::reply get_compile_info() const;

		/**
		 * @brief 更新用户的提交数, 通过数
		 * @warning 仅规定 update user 表的接口, 具体操作需由子类实现
		 */
		virtual void update_user(mysqlpp::Connection & mysql_conn) = 0;

		/**
		 * @brief 更新题目的提交数, 通过数
		 * @warning 仅规定 update problem 表的接口, 具体操作需由子类实现
		 */
		virtual void update_problem(mysqlpp::Connection & mysql_conn) = 0;

		/**
		 * @brief 更新此用户此题的完成状态
		 * @detail 用户所见到的每题 AC, TO_DO, ATTEMPT 三种状态即为user problem 表所提供的服务
		 * @warning 仅规定 update user problem 表的接口, 具体操作需由子类实现
		 */
		virtual void update_user_problem(mysqlpp::Connection & mysql_conn) = 0;

		/**
		 * @brief 更新此用户此题的完成状态 [新]
		 * @detail 用户所见到的每题 AC, TO_DO, ATTEMPT 三种状态即为user problem 表所提供的服务
		 * @warning 仅规定 update user problem 表的接口, 具体操作需由子类实现
		 */
		virtual void update_user_problem_status(mysqlpp::Connection & mysql_conn) = 0;

		/**
		 * @brief 删除 redis 中本条 job 不再需要的信息
		 */
 		void clear_this_jobs_info_in_redis() noexcept;

		/**
		 * @brief 如果 update job 的过程中产生失败, 则将记录插入 core_update_failed 表中
		 * @throws 本方法承诺不抛出任何异常. 因为它本身就是对错误结果的处理函数
		 * @warning 若此方法在更新表时失败, 则说明 mysql 连接可能已失效, 接下来可能会发生一系列未定义行为 (TODO)
		 */
		void core_update_failed_table(const kerbal::redis::RedisReply & source_code, const kerbal::redis::RedisReply & compile_error_info) noexcept;

	private:
		/**
		 * @brief 删除 redis 中本条 job 向前推 600 条的 job 信息
		 */
 		void clear_previous_jobs_info_in_redis() noexcept;

	public:
		virtual ~UpdateJobBase() noexcept = default;
};

/**
 * @brief 根据传入的 jobType, sid 构造一个具体的 UpdateJob 对象, 并返回指向所构造对象的智能指针
 * @return 指向构造对象的智能指针.
 *         如果 job 为练习, 则指针实际指向的是一个 ExerciseUpdateJob 对象
 *         如果 job 为课程, 则指针实际指向的是一个 CourseUpdateJob 对象
 *         如果 job 为竞赛, 则指针实际指向的是一个 ContestUpdateJob 对象
 * @warning 返回的对象具有多态性, 请谨慎处理!
 */
std::unique_ptr<UpdateJobBase>
make_update_job(int jobType, ojv4::s_id_type s_id, kerbal::redis_v2::connection & redis_conn);

#endif /* SRC_MASTER_UPDATEJOBBASE_HPP_ */
