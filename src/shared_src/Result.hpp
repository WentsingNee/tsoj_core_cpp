/*
 * Result.hpp
 *
 *  Created on: 2018年6月11日
 *      Author: peter
 */

#ifndef SRC_RESULT_HPP_
#define SRC_RESULT_HPP_

#include "db_typedef.hpp"
#include <iostream>
#include <chrono>

struct SolutionDetails
{
		ojv4::s_result_enum judge_result;
		ojv4::s_time_in_milliseconds cpu_time;
		ojv4::s_time_in_milliseconds real_time;
		ojv4::s_mem_in_byte memory;

		SolutionDetails() :
				judge_result(ojv4::s_result_enum::ACCEPTED),
				cpu_time(0),
				real_time(0),
				memory(0)
		{
		}

		friend std::ostream& operator<<(std::ostream& out, const SolutionDetails & src)
		{
			return out << "result: " << src.judge_result
						<< " cpu_time: " << src.cpu_time.count() << " ms"
						<< " real_time: " << src.real_time.count() << " ms"
						<< " memory: " << src.memory.count() << " Byte";
		}

};

struct Result : public SolutionDetails
{
		RunnerError error;
		int signal;
		int exit_code;

		/**
		 * @brief Result 构造函数，结果设定为 AC，资源用量、信号、退出代码、相似度置为 0
		 */
		Result() :
				SolutionDetails(),
				error(RunnerError::SUCCESS),
				signal(0), exit_code(0)
		{
		}
	
		/**
		 * @brief 将本结构体的 RunnerError 置为 err， 并将评测结果标为 UnitedJudgeResult::SYSTEM_ERROR
		 * @param err 表示 RunnerError 的类型
		 */
		void setErrorCode(RunnerError err)
		{
			this->error = err;
			this->judge_result = UnitedJudgeResult::SYSTEM_ERROR;
		}

		/**
		 * @brief 重载 << 运算符
		 */
		friend std::ostream& operator<<(std::ostream& out, const Result & src)
		{
			return out << static_cast<const SolutionDetails&>(src)
						<< " error: " << src.error
						<< " signal: " << src.signal
						<< " exit_code: " << src.exit_code;
		}

};

#endif /* SRC_RESULT_HPP_ */
