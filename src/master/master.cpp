/*
 * master.cpp
 *
 *  Created on: 2018年9月4日
 *      Author: peter
 */

#include "logger.hpp"
#include "load_helper.hpp"
#include "UpdateJobBase.hpp"
#include "mkdir_p.hpp"

#include <iostream>
#include <fstream>
#include <csignal>
#include <thread>
#include <kerbal/redis/redis_data_struct/list.hpp>
#include <kerbal/redis/redis_data_struct/set.hpp>
#include <kerbal/system/system.hpp>
#include <kerbal/compatibility/chrono_suffix.hpp>

#include "ContestManagement.hpp"
#include "CourseManagement.hpp"
#include "ExerciseManagement.hpp"

#ifndef MYSQLPP_MYSQL_HEADERS_BURIED
#	define MYSQLPP_MYSQL_HEADERS_BURIED
#endif

#include <mysql++/query.h>


using namespace kerbal::compatibility::chrono_suffix;

std::ofstream log_fp;

constexpr std::chrono::minutes EXPIRE_TIME = 2_min;

namespace
{
	std::string host_name; ///< 本机主机名
	std::string ip; ///< 本机 IP
	std::string user_name; ///< 本机当前用户名
	int listening_pid; ///< 本机监听进程号
	std::string judge_server_id; ///< 评测服务器id，定义为 host_name:ip
	bool loop = true; ///< 主工作循环
	std::string log_file_name; ///< 日志文件名
	std::string updater_lock_file; ///< lock_file,用于保证一次只能有一个 updater 守护进程在运行
	std::string mysql_hostname; ///< mysql 主机名
	std::string mysql_username; ///< mysql 用户名
	std::string mysql_passwd; ///< mysql 密码
	std::string mysql_database; ///< mysql 数据库名称
	int redis_port; ///< redis 端口号
	std::string redis_hostname; ///< redis 主机名
}


/**
 * @brief 发送心跳进程
 * 每隔一段时间，将本机信息提交到数据库中表示当前在线的评测机集合中，表明自身正常工作，可以处理评测任务。
 * @throw 该函数保证不抛出任何异常
 */
void register_self() noexcept try
{
	using namespace std::chrono;
	using namespace kerbal::compatibility::chrono_suffix;

	kerbal::redis::RedisContext regist_self_conn(redis_hostname.c_str(), redis_port, 1500_ms);
	if (!regist_self_conn) {
		LOG_FATAL(0, 0, log_fp, "Regist_self context connect failed.");
		return;
	}
	LOG_INFO(0, 0, log_fp, "Regist_self service get context.");

	static kerbal::redis::Operation opt(regist_self_conn);
	while (true) {
		try {
			const time_t now = time(NULL);
			const std::string confirm_time = get_ymd_hms_in_local_time_zone(now);
			opt.hmset("judge_server:" + judge_server_id,
					  "host_name", host_name,
					  "ip", ip,
					  "user_name", user_name,
					  "listening_pid", listening_pid,
					  "last_confirm", confirm_time);
			opt.set("judge_server_confirm:" + judge_server_id, confirm_time);
			opt.expire("judge_server_confirm:" + judge_server_id, 30_s);

			static kerbal::redis::Set<std::string> s(regist_self_conn, "online_judger");
			s.insert(judge_server_id);

			std::this_thread::sleep_for(10_s);
		} catch (const kerbal::redis::RedisException & e) {
			EXCEPT_FATAL(0, 0, log_fp, "Register self failed.", e);
		}
	}
} catch (...) {
	LOG_FATAL(0, 0, log_fp, "Register self failed. Error information: ", UNKNOWN_EXCEPTION_WHAT);
	exit(2);
}

/**
 * @brief SIGTERM 信号的处理函数
 * 当收到 SIGTERM 信号，在代评测队列末端加上 type = 0, id = -1 的任务，用于标识结束 master 工作的意愿。之后在 loop
 * 循环中会据此判断是否结束 master 进程。
 * @param signum 信号编号
 * @throw 该函数保证不抛出任何异常
 */
void regist_SIGTERM_handler(int signum) noexcept
{
	using namespace kerbal::compatibility::chrono_suffix;
	if (signum == SIGTERM) {
		kerbal::redis::RedisContext main_conn(redis_hostname.c_str(), redis_port, 1500_ms);
		kerbal::redis::List<std::string> job_list(main_conn, "judge_queue");
		job_list.push_back(JobBase::getExitJobItem());
		LOG_WARNING(0, 0, log_fp,
					"Master has received the SIGTERM signal and will exit soon after the jobs are all finished!");
	}
}

/**
 * @brief 加载 master 工作的配置
 * 根据 updater.conf 文档，读取工作配置信息。loadConfig 的工作原理详见其文档。
 */
void load_config(const char * config_file)
{
	using namespace kerbal::utility::costream;
	const auto & ccerr = costream<std::cerr>(LIGHT_RED);

	std::ifstream fp(config_file, std::ios::in); //BUG "re"
	if (!fp) {
		ccerr << "can't not open updater.conf" << std::endl;
		exit(0);
	}

	LoadConfig<std::string, int> loadConfig;

	auto castToInt = [](const std::string & src) {
		return std::stoi(src);
	};

	auto stringAssign = [](const std::string & src) {
		return src;
	};

	loadConfig.add_rules(log_file_name, "log_file", stringAssign);
	loadConfig.add_rules(updater_lock_file, "updater_lock_file", stringAssign);
	loadConfig.add_rules(mysql_hostname, "mysql_hostname", stringAssign);
	loadConfig.add_rules(mysql_username, "mysql_username", stringAssign);
	loadConfig.add_rules(mysql_passwd, "mysql_passwd", stringAssign);
	loadConfig.add_rules(mysql_database, "mysql_database", stringAssign);
	loadConfig.add_rules(redis_port, "redis_port", castToInt);
	loadConfig.add_rules(redis_hostname, "redis_hostname", stringAssign);

	std::string buf;
	while (getline(fp, buf)) {
		std::string key, value;

		std::tie(key, value) = parse_buf(buf);
		if (key != "" && value != "") {
			if (loadConfig.parse(key, value) == false) {
				ccerr << "unexpected key name: "
					  << key << " = " << value << std::endl;
			} else {
				std::cout << key << " = " << value << std::endl;
			}
		}
	}

	fp.close();

	if (log_file_name.empty()) {
		ccerr << "empty log file name!" << std::endl;
		exit(0);
	}

	log_fp.open(log_file_name, std::ios::app);
	if (!log_fp) {
		ccerr << "log file open failed" << std::endl;
		exit(0);
	}

	host_name = get_host_name();
	ip = get_addr_list(host_name).front();
	user_name = get_user_name();
	listening_pid = getpid();
	judge_server_id = host_name + ":" + ip;

	LOG_INFO(0, 0, log_fp, "judge_server_id: ", judge_server_id);
	LOG_INFO(0, 0, log_fp, "listening pid: ", listening_pid);
}

void update_contest_scoreboard_handler()
{
	// 本次更新任务的 redis 连接
	kerbal::redis::RedisContext redis_conn(redis_hostname.c_str(), redis_port, 100_ms);
	if (!redis_conn) {
		LOG_FATAL(0, 0, log_fp, "REDIS connection connect failed!");
		loop = false;
		exit(0);
	}

	kerbal::redis::List<ojv4::ct_id_type> update_queue(redis_conn, "update_contest_scoreboard_queue");

	while (loop) {
		ojv4::ct_id_type ct_id(0);

		try {
			ct_id = update_queue.block_pop_front(0_s);
		} catch (const std::exception & e) {
			EXCEPT_FATAL(0, 0, log_fp, "Fail to fetch job.", e);
			continue;
		} catch (...) {
			UNKNOWN_EXCEPT_FATAL(0, 0, log_fp, "Fail to fetch job.");
			continue;
		}

		try {
			auto start = std::chrono::steady_clock::now();
			mysqlpp::Connection mysql_conn(false);
			mysql_conn.set_option(new mysqlpp::SetCharsetNameOption("utf8"));
			if (!mysql_conn.connect(mysql_database.c_str(), mysql_hostname.c_str(), mysql_username.c_str(), mysql_passwd.c_str())) {
				LOG_FATAL(0, 0, log_fp, "MYSQL connection connect failed!");
				continue;
			}
			ContestManagement::update_scoreboard(mysql_conn, redis_conn, ct_id);
			auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
			LOG_INFO(0, 0, log_fp, "Update contest scoreboard success, time consume: ", ms.count(), " ms, ct_id: ", ct_id);
		} catch (const std::exception & e) {
			EXCEPT_FATAL(0, 0, log_fp, "Error occurred while updating scoreboard.", e);
			continue;
		} catch (...) {
			UNKNOWN_EXCEPT_FATAL(0, 0, log_fp, "Error occurred while updating scoreboard.");
			continue;
		}
	}
}


int already_running(const char * lock_file)
{
//	int fd = open(lock_file, O_RDWR | O_CREAT, LOCKMODE);
//	if (fd < 0) {
//		LOG_FATAL(0, 0, log_fp, "can't open [", lock_file, "], errnum: ", strerror(errno));
//		exit(-1);
//	}
//
//	flock fl {
//		.l_type = F_WRLCK,
//		.l_start = 0,
//		.l_whence = SEEK_SET,
//		.l_len = 0
//	};
//
//	if (fcntl(fd, F_SETLK, &fl) < 0) {
//		if (errno == EACCES || errno == EAGAIN) {
//			close(fd);
//			return 1;
//		}
//		LOG_FATAL(0, 0, log_fp, "can't lock [", lock_file, "], errnum: ", strerror(errno));
//		exit(1);
//	}
//	if (ftruncate(fd, 0)) {
//		LOG_WARNING(0, 0, log_fp, "ftruncate failed!");
//	}
//
//	std::string buf = std::to_string(getpid());
//	size_t len = buf.size() + 1;
//	ssize_t res = write(fd, buf.data(), len);
//	if (res != len) {
//		LOG_WARNING(0, 0, log_fp, "write failed: ", lock_file);
//	}
	return 0;
}

/**
 * @brief master 端主程序循环
 * 加载配置信息；连接数据库；取待评测任务信息，交由子进程并评测；创建并分离发送心跳线程 // to be done
 * @throw UNKNOWN_EXCEPTION
 */
int main(int argc, const char * argv[]) try
{
	using namespace kerbal::compatibility::chrono_suffix;

	if (argc > 1 && argv[1] == std::string("--version")) {
		std::cout << "version: " __DATE__ " " __TIME__ << std::endl;
		exit(0);
	}

	using namespace kerbal::utility::costream;
	const auto & ccerr = costream<std::cerr>(LIGHT_RED);

	// 运行必须具有 root 权限
	uid_t uid = getuid();
	if (uid != 0) {
		ccerr << "root required!" << std::endl;
		exit(-1);
	}

	std::cout << std::boolalpha;
	load_config("/etc/ts_judger/updater.conf"); // 提醒: 此函数运行结束以后才可以使用 log 系列宏, 否则 log_fp 没打开
	LOG_INFO(0, 0, log_fp, "Configuration load finished!");

	// 连接 mysql
	LOG_INFO(0, 0, log_fp, "Connecting main MYSQL connection...");
	mysqlpp::Connection mainMysqlConn(false);
	mainMysqlConn.set_option(new mysqlpp::SetCharsetNameOption("utf8"));
	if(!mainMysqlConn.connect(mysql_database.c_str(), mysql_hostname.c_str(), mysql_username.c_str(), mysql_passwd.c_str()))
	{
		LOG_FATAL(0, 0, log_fp, "Main MYSQL connection connect failed!");
		exit(-1);
	}
	LOG_INFO(0, 0, log_fp, "Main MYSQL connection connected!");

	// 连接 redis
	LOG_INFO(0, 0, log_fp, "Connecting main REDIS connection...");
	kerbal::redis::RedisContext mainRedisConn(redis_hostname.c_str(), redis_port, 100_ms);
	if (!mainRedisConn) {
		LOG_FATAL(0, 0, log_fp, "Main REDIS connection connect failed!");
		exit(-1);
	}
	LOG_INFO(0, 0, log_fp, "Main REDIS connection connected!");

	try {
		// 创建并分离发送更新竞赛榜单线程
		std::thread(register_self).detach();
	} catch (const std::exception & e) {
		EXCEPT_FATAL(0, 0, log_fp, "Register self service process fork failed.", e);
		exit(-1);
	}


	{
		mysqlpp::Query query = mainMysqlConn.query(
				"select c_id from course where c_id not in(16, 17, 22, 23)"
		);
		mysqlpp::StoreQueryResult res = query.store();
		if (query.errnum() != 0) {
			MysqlEmptyResException e(query.errnum(), query.error());
			EXCEPT_FATAL(0, 0, log_fp, "Query courses' information failed!", e);
			throw e;
		}

		for (const auto & row : res) {
			ojv4::c_id_type c_id = row["c_id"];
			LOG_INFO(0, 0, log_fp, "Refresh all users' submit and accept num in course: ", c_id, " ...");
			CourseManagement::refresh_all_users_submit_and_accept_num_in_course(mainMysqlConn, c_id);
		}
		for (const auto & row : res) {
			ojv4::c_id_type c_id = row["c_id"];
			LOG_INFO(0, 0, log_fp, "Refresh all problems' submit and accept num in course: ", c_id, " ...");
			CourseManagement::refresh_all_problems_submit_and_accept_num_in_course(mainMysqlConn, c_id);
		}
	}

	LOG_INFO(0, 0, log_fp, "Refresh all users' submit and accept num in exercise...");
	ExerciseManagement::refresh_all_users_submit_and_accept_num(mainMysqlConn);
	LOG_INFO(0, 0, log_fp, "Refresh all problems' submit and accept num in exercise...");
	ExerciseManagement::refresh_all_problems_submit_and_accept_num(mainMysqlConn);


	LOG_INFO(0, 0, log_fp, "updater starting...");

	try {
		std::thread update_contest_scoreboard_thread(update_contest_scoreboard_handler);
		update_contest_scoreboard_thread.detach();
	} catch(const std::exception & e) {
		EXCEPT_FATAL(0, 0, log_fp, "Register self service process fork failed.", e);
		exit(-1);
	}

	signal(SIGTERM, regist_SIGTERM_handler);

	// 连接 redis 的 update_queue 队列，master 据此进行更新操作
	kerbal::redis::List<std::string> update_queue(mainRedisConn, "update_queue");

	while (loop) {

		int jobType(0);
		ojv4::s_id_type s_id(0);

		/*
		 * 当收到 SIGTERM 信号时，会在评测队列末端加一个特殊的评测任务用于标识停止测评。此处若检测到停止
		 * 工作的信号，则结束工作loop
		 * 若取得的是正常的评测任务则继续工作
		 */
		try {
			std::string job_item = update_queue.block_pop_front(0_s);
			if (JobBase::isExitJob(job_item) == true) {
				loop = false;
				LOG_INFO(0, 0, log_fp, "Get exit job.");
				continue;
			}
			LOG_DEBUG(jobType, s_id, log_fp, "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<");
			LOG_DEBUG(jobType, s_id, log_fp, "Master get update job: ", job_item);
			try {
				std::tie(jobType, s_id) = JobBase::parseJobItem(job_item);
			} catch (const std::exception & e) {
				EXCEPT_FATAL(0, 0, log_fp, "Fail to parse job item.", e, "job_item: ", job_item);
				continue;
			}
		} catch (const std::exception & e) {
			EXCEPT_FATAL(0, 0, log_fp, "Fail to fetch job.", e);
			continue;
		} catch (...) {
			UNKNOWN_EXCEPT_FATAL(0, 0, log_fp, "Fail to fetch job.");
			continue;
		}

		// 本次更新开始时间
		auto start(std::chrono::steady_clock::now());
		LOG_DEBUG(jobType, s_id, log_fp, "Update job start");

		// 本次更新任务的 redis 连接
		kerbal::redis::RedisContext redisConn(redis_hostname.c_str(), redis_port, 100_ms);
		if (!redisConn) {
			LOG_FATAL(jobType, s_id, log_fp, "Redis connection connect failed!");
			continue;
		}

		// 本次更新任务的 mysql 连接
		std::unique_ptr <mysqlpp::Connection> mysqlConn(new mysqlpp::Connection(false));
		mysqlConn->set_option(new mysqlpp::SetCharsetNameOption("utf8"));
		if (!mysqlConn->connect(mysql_database.c_str(), mysql_hostname.c_str(), mysql_username.c_str(), mysql_passwd.c_str())) {
			LOG_FATAL(jobType, s_id, log_fp, "MYSQL connection connect failed!");
			continue;
		}

		std::unique_ptr <UpdateJobBase> job = nullptr;
		try {
			// 根据 jobType 与 sid 获取 UpdateJobBase 通用类型的 update job 信息
			// 信息从 redis 数据库读入，并且将一个 mysql 数据库的连接控制权转移至实例化的 job 当中
			// 在实例化之前就建立好 mysql 连接再转移给它，避免了进入更新操作内部之后才发现无法连接 mysql 数据库
			// 避免了无效操作的消耗与可能导致的其他逻辑混乱
			// 此外，unique_ptr 的特性决定了其不应该使用值传递的方式。
			// 而使用 std::move 将其强制转换为右值引用，是为了使用移动构造函数来优化性能（吗？这个不确定）
			job = make_update_job(jobType, s_id, redisConn, std::move(mysqlConn));
		} catch (const std::exception & e) {
			EXCEPT_FATAL(jobType, s_id, log_fp, "Job construct failed!", e);
			continue;
		} catch (...) {
			UNKNOWN_EXCEPT_FATAL(jobType, s_id, log_fp, "Job construct failed!");
			continue;
		}

		try {
			// 执行本次 update job
			job->handle();
		} catch (const std::exception & e) {
			EXCEPT_FATAL(jobType, s_id, log_fp, "Job handle failed!", e);
			continue;
		} catch (...) {
			UNKNOWN_EXCEPT_FATAL(jobType, s_id, log_fp, "Job handle failed!");
			continue;
		}

		// 本次更新结束时间
		auto end(std::chrono::steady_clock::now());
		// 本次更新耗时
		auto time_consume = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
		LOG_DEBUG(jobType, s_id, log_fp, "Update consume: ", time_consume.count());
		LOG_DEBUG(jobType, s_id, log_fp, ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
	}
	return 0;
} catch (const std::exception & e) {
	EXCEPT_FATAL(0, 0, log_fp, "An uncaught exception caught by main.", e);
	throw;
} catch (...) {
	UNKNOWN_EXCEPT_FATAL(0, 0, log_fp, "An uncaught exception caught by main.");
	throw;
}


