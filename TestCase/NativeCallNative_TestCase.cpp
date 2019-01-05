#include "stdafx.h"
#include <atlbase.h>
#include <stdio.h>
#include "NativeCallNative_TestCase.h"

CNativeCallNative_TestCase::CNativeCallNative_TestCase()
{
}

CNativeCallNative_TestCase::~CNativeCallNative_TestCase()
{
	if (_caller_thread)
	{
		_caller_thread->join();
	}
	if (_work_thread)
	{
		_work_thread->join();
	}
}

#define CMMNO_FIBON_REQ (0x01)
#define CMMNO_FIBON_RES (0x02)
#define CMMNO_WORK_READY (0x03)

void	CNativeCallNative_TestCase::DoTest()
{
	ATLASSERT(!_caller_thread);
	//1，主线程创建Native线程
	_caller_thread = std::make_shared<std::thread>(&CNativeCallNative_TestCase::NativeCallerRoutinue, this);
}

void	CNativeCallNative_TestCase::NativeCallerRoutinue()
{
	SetCurrentName(_T("Caller_Thread"));
	ATLASSERT(!_work_thread);
	std::shared_ptr<std::thread> _work_thread = std::make_shared<std::thread>(&CNativeCallNative_TestCase::NativeWorkerRoutinue, this);

	//2，Native线程创建Wrap(托管)线程
	//3, Native线程注册结果函数，用于获取Wrap线程结果	
	ThreadErrorCode tec = RegMsgSink(CMMNO_FIBON_RES, std::bind(&CNativeCallNative_TestCase::CallFunc_FibonMathSink, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	tec = RegMsgSink(CMMNO_WORK_READY, std::bind(&CNativeCallNative_TestCase::CallFunc_WorkReadySink, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

	RunBaseLoop();
}


void	CNativeCallNative_TestCase::NativeWorkerRoutinue()
{
	SetCurrentName(_T("Work_Thread"));
	//5，Wrap线程注册任务函数，用于接收对应任务
	ThreadErrorCode tec = RegMsgSink(CMMNO_FIBON_REQ, std::bind(&CNativeCallNative_TestCase::WorkFunc_FibonMathSink, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

	PostMsg(_T("Caller_Thread"), CMMNO_WORK_READY, nullptr);

	//6，消息Pump运作
	RunBaseLoop();
}

void CNativeCallNative_TestCase::WorkFunc_FibonMathSink(const task_id_t& sender_id, const task_cmd_t& cmd, const task_data_t& data)	//接收者注册的回调函数
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
	task_data_t	res_data = std::make_shared<std::string>();
	res_data->resize(sizeof(s));
	memcpy((char*)res_data->data(), &s, res_data->size());
	PostMsg(sender_id, CMMNO_FIBON_RES, res_data);
}

void	CNativeCallNative_TestCase::CallFunc_WorkReadySink(const task_id_t& sender_id, const task_cmd_t& cmd, const task_data_t& data)
{
	//4，要求Wrap线程开始计算
	PostMsg(_T("Work_Thread"), CMMNO_FIBON_REQ, nullptr, task_flag_null, std::bind(&CNativeCallNative_TestCase::CallFunc_FibonMathEcho, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
}

void	CNativeCallNative_TestCase::CallFunc_FibonMathSink(const task_id_t& sender_id, const task_cmd_t& cmd, const task_data_t& data)
{
	//9，从Wrap线程的计算结果回调
	if (data->size() != sizeof(UINT64))
	{
		ATLASSERT(FALSE);
		return;
	}

	UINT64	s;
	memcpy(&s, (char*)data->data(), data->size());

	printf("Fibon 1000 value: %lld\n", s);
}

void	CNativeCallNative_TestCase::CallFunc_FibonMathEcho(const task_id_t& receiver_id, const task_cmd_t& cmd, const task_data_t& data, const ThreadErrorCode& err)	//发送者发送后，对于发送结果的回调通知
{
	//8，Native线程的本次请求结果回调，只是看任务是否异常
}