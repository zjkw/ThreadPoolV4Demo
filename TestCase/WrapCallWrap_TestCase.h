#pragma once

#include "ThreadPoolV4.h"

using namespace ThreadPoolV4;

class CWrapCallWrap_TestCase
{
public:
	CWrapCallWrap_TestCase();
	virtual ~CWrapCallWrap_TestCase();

	void	DoTest();

private:
	void	CallerRoutine(const task_id_t& self_id, const task_data_t& param);
	void	WorkRoutine(const task_id_t& self_id, const task_data_t& param);
	void	WorkFunc_FibonMathSink(const task_id_t& sender_id, const task_cmd_t& cmd, const task_data_t& data);	//接收者注册的回调函数
	void	CallFunc_FibonMathEcho(const task_id_t& receiver_id, const task_cmd_t& cmd, const task_data_t& data, const TaskErrorCode& err);
	void	CallFunc_FibonMathSink(const task_id_t& sender_id, const task_cmd_t& cmd, const task_data_t& data);

};

