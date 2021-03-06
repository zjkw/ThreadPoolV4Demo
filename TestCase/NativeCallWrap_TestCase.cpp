#include "stdafx.h"
#include <atlbase.h>
#include <stdio.h>
#include "..\RawTaskData.h"
#include "NativeCallWrap_TestCase.h"

CNativeCallWrap_TestCase::CNativeCallWrap_TestCase()
{

}

CNativeCallWrap_TestCase::~CNativeCallWrap_TestCase()
{
	if (_caller_thread)
	{
		_caller_thread->join();
	}
}

#define CMMNO_FIBON_REQ (0x01)
#define CMMNO_FIBON_RES (0x02)

void	CNativeCallWrap_TestCase::DoTest()
{
	//1，主线程创建Native线程
	ATLASSERT(!_caller_thread);
	_caller_thread = std::make_shared<std::thread>(&CNativeCallWrap_TestCase::NativeCallerRoutinue, this);
}

void	CNativeCallWrap_TestCase::WorkRoutine(const task_id_t& self_id, const task_data_t& param)
{
	//5，Wrap线程注册任务函数，用于接收对应任务
	TaskErrorCode tec = RegMsgSink(CMMNO_FIBON_REQ, std::bind(&CNativeCallWrap_TestCase::WorkFunc_FibonMathSink, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), nullptr);

	//6，消息Pump运作
	RunBaseLoop();
}

void CNativeCallWrap_TestCase::WorkFunc_FibonMathSink(const task_id_t& sender_id, const task_cmd_t& cmd, const task_data_t& data)	//接收者注册的回调函数
{
	//7，Wrap线程输出第1000项的结果，并投递到Native线程
	UINT64 l = 1;
	UINT64 r = 2;
	UINT64 s = 0;
	for (size_t i = 0; i < 1000; i++)
	{
		s = l + r;
		l = r;
		r = s;
	}
	std::shared_ptr<CRawTaskData>	res_data = std::make_shared<CRawTaskData>();
	(*res_data)->resize(sizeof(s));
	memcpy((char*)(*res_data)->data(), &s, (*res_data)->size());
	PostMsg(sender_id, CMMNO_FIBON_RES, res_data);
}

void	CNativeCallWrap_TestCase::NativeCallerRoutinue()
{
	//2，Native线程创建Wrap(托管)线程
	task_id_t id = 0;
	TaskErrorCode tec = AddManagedTask(_T("Test"), _T("Febri"), nullptr, std::bind(&CNativeCallWrap_TestCase::WorkRoutine, this, std::placeholders::_1, std::placeholders::_2), id);
	if (tec == TEC_SUCCEED)
	{
		//3, Native线程注册结果函数，用于获取Wrap线程结果	
		tec = RegMsgSink(CMMNO_FIBON_RES, std::bind(&CNativeCallWrap_TestCase::CallFunc_FibonMathSink, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), nullptr);

		//4，要求Wrap线程开始计算
		PostMsg(id, CMMNO_FIBON_REQ, nullptr, task_flag_null, std::bind(&CNativeCallWrap_TestCase::CallFunc_FibonMathEcho, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
	}

	//消息Pump运作
	RunBaseLoop();
}

void	CNativeCallWrap_TestCase::CallFunc_FibonMathSink(const task_id_t& sender_id, const task_cmd_t& cmd, const task_data_t& data)
{
	std::shared_ptr<CRawTaskData>	raw_data = std::dynamic_pointer_cast<CRawTaskData>(data);
	//9，从Wrap线程的计算结果回调
	if ((*raw_data)->size() != sizeof(UINT64))
	{
		ATLASSERT(FALSE);
		return;
	}

	UINT64	s;
	memcpy(&s, (char*)(*raw_data)->data(), (*raw_data)->size());

	printf("Fibon 1000 value: %lld\n", s);
}

void	CNativeCallWrap_TestCase::CallFunc_FibonMathEcho(const task_id_t& receiver_id, const task_cmd_t& cmd, const task_data_t& data, const TaskErrorCode& err)	//发送者发送后，对于发送结果的回调通知
{
	//8，Native线程的本次请求结果回调，只是看任务是否异常
}