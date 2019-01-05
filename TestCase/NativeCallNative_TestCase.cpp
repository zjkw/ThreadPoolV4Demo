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
	//1�����̴߳���Native�߳�
	_caller_thread = std::make_shared<std::thread>(&CNativeCallNative_TestCase::NativeCallerRoutinue, this);
}

void	CNativeCallNative_TestCase::NativeCallerRoutinue()
{
	SetCurrentName(_T("Caller_Thread"));
	ATLASSERT(!_work_thread);
	std::shared_ptr<std::thread> _work_thread = std::make_shared<std::thread>(&CNativeCallNative_TestCase::NativeWorkerRoutinue, this);

	//2��Native�̴߳���Wrap(�й�)�߳�
	//3, Native�߳�ע�������������ڻ�ȡWrap�߳̽��	
	ThreadErrorCode tec = RegMsgSink(CMMNO_FIBON_RES, std::bind(&CNativeCallNative_TestCase::CallFunc_FibonMathSink, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	tec = RegMsgSink(CMMNO_WORK_READY, std::bind(&CNativeCallNative_TestCase::CallFunc_WorkReadySink, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

	RunBaseLoop();
}


void	CNativeCallNative_TestCase::NativeWorkerRoutinue()
{
	SetCurrentName(_T("Work_Thread"));
	//5��Wrap�߳�ע�������������ڽ��ն�Ӧ����
	ThreadErrorCode tec = RegMsgSink(CMMNO_FIBON_REQ, std::bind(&CNativeCallNative_TestCase::WorkFunc_FibonMathSink, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

	PostMsg(_T("Caller_Thread"), CMMNO_WORK_READY, nullptr);

	//6����ϢPump����
	RunBaseLoop();
}

void CNativeCallNative_TestCase::WorkFunc_FibonMathSink(const task_id_t& sender_id, const task_cmd_t& cmd, const task_data_t& data)	//������ע��Ļص�����
{
	//7��Wrap�߳������1000��Ľ������Ͷ�ݵ�Native�߳�
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
	//4��Ҫ��Wrap�߳̿�ʼ����
	PostMsg(_T("Work_Thread"), CMMNO_FIBON_REQ, nullptr, task_flag_null, std::bind(&CNativeCallNative_TestCase::CallFunc_FibonMathEcho, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
}

void	CNativeCallNative_TestCase::CallFunc_FibonMathSink(const task_id_t& sender_id, const task_cmd_t& cmd, const task_data_t& data)
{
	//9����Wrap�̵߳ļ������ص�
	if (data->size() != sizeof(UINT64))
	{
		ATLASSERT(FALSE);
		return;
	}

	UINT64	s;
	memcpy(&s, (char*)data->data(), data->size());

	printf("Fibon 1000 value: %lld\n", s);
}

void	CNativeCallNative_TestCase::CallFunc_FibonMathEcho(const task_id_t& receiver_id, const task_cmd_t& cmd, const task_data_t& data, const ThreadErrorCode& err)	//�����߷��ͺ󣬶��ڷ��ͽ���Ļص�֪ͨ
{
	//8��Native�̵߳ı����������ص���ֻ�ǿ������Ƿ��쳣
}