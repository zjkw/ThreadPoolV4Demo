#include "stdafx.h"
#include <atlbase.h>
#include <stdio.h>
#include "..\RawTaskData.h"
#include "WrapCallNative_TestCase.h"

CWrapCallNative_TestCase::CWrapCallNative_TestCase()
{
}

CWrapCallNative_TestCase::~CWrapCallNative_TestCase()
{
	if (_work_thread)
	{
		_work_thread->join();
	}
}

#define CMMNO_FIBON_REQ (0x01)
#define CMMNO_FIBON_RES (0x02)
#define CMMNO_WORK_READY (0x03)

void	CWrapCallNative_TestCase::DoTest()
{
	//2��Native�̴߳���Wrap(�й�)�߳�
	task_id_t id = 0;
	TaskErrorCode tec = AddManagedTask(_T("Test"), _T("Caller"), nullptr, std::bind(&CWrapCallNative_TestCase::NativeCallerRoutinue, this, std::placeholders::_1, std::placeholders::_2), id);
	if (tec == TEC_SUCCEED)
	{
	}
}

void	CWrapCallNative_TestCase::NativeCallerRoutinue(const task_id_t& self_id, const task_param_t& param)
{
	//3��Wrap�߳�ע�������������ڽ��ն�Ӧ����
	TaskErrorCode tec = RegMsgSink(CMMNO_FIBON_RES, std::bind(&CWrapCallNative_TestCase::CallFunc_FibonMathSink, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), nullptr);
	tec = RegMsgSink(CMMNO_WORK_READY, std::bind(&CWrapCallNative_TestCase::CallFunc_WorkReadySink, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), nullptr);

	ATLASSERT(!_work_thread);
	_work_thread = std::make_shared<std::thread>(&CWrapCallNative_TestCase::WorkRoutine, this);

	//6����ϢPump����
	RunBaseLoop();
}

void	CWrapCallNative_TestCase::WorkRoutine()
{
	//5��Wrap�߳�ע�������������ڽ��ն�Ӧ����
	SetCurrentName(_T("Work_Thread"));
	TaskErrorCode tec = RegMsgSink(CMMNO_FIBON_REQ, std::bind(&CWrapCallNative_TestCase::WorkFunc_FibonMathSink, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), nullptr);

	PostMsg(_T("Caller"), CMMNO_WORK_READY, nullptr);

	//6����ϢPump����
	RunBaseLoop();
}

void	CWrapCallNative_TestCase::CallFunc_WorkReadySink(const task_id_t& sender_id, const task_cmd_t& cmd, const task_data_t& data)
{
	//4��Ҫ��Wrap�߳̿�ʼ����
	PostMsg(_T("Work_Thread"), CMMNO_FIBON_REQ, nullptr, task_flag_null, std::bind(&CWrapCallNative_TestCase::CallFunc_FibonMathEcho, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
}

void CWrapCallNative_TestCase::WorkFunc_FibonMathSink(const task_id_t& sender_id, const task_cmd_t& cmd, const task_data_t& data)	//������ע��Ļص�����
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
	std::shared_ptr<CRawTaskData>	res_data = std::make_shared<CRawTaskData>();
	(*res_data)->resize(sizeof(s));
	memcpy((char*)(*res_data)->data(), &s, (*res_data)->size());
	PostMsg(sender_id, CMMNO_FIBON_RES, res_data);
}

void	CWrapCallNative_TestCase::CallFunc_FibonMathSink(const task_id_t& sender_id, const task_cmd_t& cmd, const task_data_t& data)
{
	std::shared_ptr<CRawTaskData>	raw_data = std::dynamic_pointer_cast<CRawTaskData>(data);
	//9����Wrap�̵߳ļ������ص�
	if ((*raw_data)->size() != sizeof(UINT64))
	{
		ATLASSERT(FALSE);
		return;
	}

	UINT64	s;
	memcpy(&s, (char*)(*raw_data)->data(), (*raw_data)->size());

	printf("Fibon 1000 value: %lld\n", s);
}

void	CWrapCallNative_TestCase::CallFunc_FibonMathEcho(const task_id_t& receiver_id, const task_cmd_t& cmd, const task_data_t& data, const TaskErrorCode& err)	//�����߷��ͺ󣬶��ڷ��ͽ���Ļص�֪ͨ
{
	//8��Native�̵߳ı����������ص���ֻ�ǿ������Ƿ��쳣
}