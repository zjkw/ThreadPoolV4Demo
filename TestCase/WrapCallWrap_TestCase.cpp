#include "stdafx.h"
#include <atlbase.h>
#include <stdio.h>
#include "WrapCallWrap_TestCase.h"

CWrapCallWrap_TestCase::CWrapCallWrap_TestCase()
{
}

CWrapCallWrap_TestCase::~CWrapCallWrap_TestCase()
{
}

#define CMMNO_FIBON_REQ (0x01)
#define CMMNO_FIBON_RES (0x02)

void	CWrapCallWrap_TestCase::DoTest()
{
	//2��Native�̴߳���Wrap(�й�)�߳�
	task_id_t id = 0;
	ThreadErrorCode tec = AddManagedTask(_T("Test"), _T("Caller"), nullptr, std::bind(&CWrapCallWrap_TestCase::CallerRoutine, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), id);
	if (tec == TEC_SUCCEED)
	{
	}
}

void	CWrapCallWrap_TestCase::CallerRoutine(const task_id_t& self_id, std::shared_ptr<ThreadCtrlBlock> tcb, const task_param_t& param)
{
	//3, Native�߳�ע�������������ڻ�ȡWrap�߳̽��	
	ThreadErrorCode tec = RegMsgSink(CMMNO_FIBON_RES, std::bind(&CWrapCallWrap_TestCase::CallFunc_FibonMathSink, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

	task_id_t id = 0;
	tec = AddManagedTask(_T("Test"), _T("Worker"), nullptr, std::bind(&CWrapCallWrap_TestCase::WorkRoutine, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), id);
	if (tec == TEC_SUCCEED)
	{
		//4��Ҫ��Wrap�߳̿�ʼ����
		PostMsg(id, CMMNO_FIBON_REQ, nullptr, task_flag_null, std::bind(&CWrapCallWrap_TestCase::CallFunc_FibonMathEcho, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
	}

	//��ϢPump����
	RunLoopBase();
}

void	CWrapCallWrap_TestCase::WorkRoutine(const task_id_t& self_id, std::shared_ptr<ThreadCtrlBlock> tcb, const task_param_t& param)
{
	//5��Wrap�߳�ע�������������ڽ��ն�Ӧ����
	ThreadErrorCode tec = RegMsgSink(CMMNO_FIBON_REQ, std::bind(&CWrapCallWrap_TestCase::WorkFunc_FibonMathSink, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

	//6����ϢPump����
	RunLoopBase();
}

void CWrapCallWrap_TestCase::WorkFunc_FibonMathSink(const task_id_t& sender_id, const task_cmd_t& cmd, const task_data_t& data)	//������ע��Ļص�����
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

void	CWrapCallWrap_TestCase::CallFunc_FibonMathSink(const task_id_t& sender_id, const task_cmd_t& cmd, const task_data_t& data)
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

void	CWrapCallWrap_TestCase::CallFunc_FibonMathEcho(const task_id_t& receiver_id, const task_cmd_t& cmd, const task_data_t& data, const ThreadErrorCode& err)	//�����߷��ͺ󣬶��ڷ��ͽ���Ļص�֪ͨ
{
	//8��Native�̵߳ı����������ص���ֻ�ǿ������Ƿ��쳣
}