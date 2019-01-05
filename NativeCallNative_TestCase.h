#pragma once

#include <thread>
#include <memory>
#include "ThreadPoolV4.h"

using namespace ThreadPoolV4;

class CNativeCallNative_TestCase
{
public:
	CNativeCallNative_TestCase();
	virtual ~CNativeCallNative_TestCase();

	void	DoTest();
	
private:
	void	NativeCallerRoutinue();
	void	NativeWorkerRoutinue();
	void	WorkFunc_FibonMathSink(const task_id_t& sender_id, const task_cmd_t& cmd, const task_data_t& data);	//接收者注册的回调函数
	void	CallFunc_FibonMathEcho(const task_id_t& receiver_id, const task_cmd_t& cmd, const task_data_t& data, const ThreadErrorCode& err);
	void	CallFunc_FibonMathSink(const task_id_t& sender_id, const task_cmd_t& cmd, const task_data_t& data);
	void	CallFunc_WorkReadySink(const task_id_t& sender_id, const task_cmd_t& cmd, const task_data_t& data);

private:
	std::shared_ptr<std::thread> _caller_thread;
	std::shared_ptr<std::thread> _work_thread;
};

