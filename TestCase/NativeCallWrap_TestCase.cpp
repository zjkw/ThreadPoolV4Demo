#include "stdafx.h"
#include <atlbase.h>
#include <stdio.h>
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
	//1�����̴߳���Native�߳�
	ATLASSERT(!_caller_thread);
	_caller_thread = std::make_shared<std::thread>(&CNativeCallWrap_TestCase::NativeCallerRoutinue, this);
}

void	CNativeCallWrap_TestCase::WorkRoutine(const task_id_t& self_id, const task_param_t& param)
{
	//5��Wrap�߳�ע�������������ڽ��ն�Ӧ����
	TaskErrorCode tec = RegMsgSink(CMMNO_FIBON_REQ, std::bind(&CNativeCallWrap_TestCase::WorkFunc_FibonMathSink, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), nullptr);

	//6����ϢPump����
	RunBaseLoop();
}

void CNativeCallWrap_TestCase::WorkFunc_FibonMathSink(const task_id_t& sender_id, const task_cmd_t& cmd, const task_data_t& data)	//������ע��Ļص�����
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

void	CNativeCallWrap_TestCase::NativeCallerRoutinue()
{
	//2��Native�̴߳���Wrap(�й�)�߳�
	task_id_t id = 0;
	TaskErrorCode tec = AddManagedTask(_T("Test"), _T("Febri"), nullptr, std::bind(&CNativeCallWrap_TestCase::WorkRoutine, this, std::placeholders::_1, std::placeholders::_2), id);
	if (tec == TEC_SUCCEED)
	{
		//3, Native�߳�ע�������������ڻ�ȡWrap�߳̽��	
		tec = RegMsgSink(CMMNO_FIBON_RES, std::bind(&CNativeCallWrap_TestCase::CallFunc_FibonMathSink, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), nullptr);

		//4��Ҫ��Wrap�߳̿�ʼ����
		PostMsg(id, CMMNO_FIBON_REQ, nullptr, task_flag_null, std::bind(&CNativeCallWrap_TestCase::CallFunc_FibonMathEcho, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
	}

	//��ϢPump����
	RunBaseLoop();
}

void	CNativeCallWrap_TestCase::CallFunc_FibonMathSink(const task_id_t& sender_id, const task_cmd_t& cmd, const task_data_t& data)
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

void	CNativeCallWrap_TestCase::CallFunc_FibonMathEcho(const task_id_t& receiver_id, const task_cmd_t& cmd, const task_data_t& data, const TaskErrorCode& err)	//�����߷��ͺ󣬶��ڷ��ͽ���Ļص�֪ͨ
{
	//8��Native�̵߳ı����������ص���ֻ�ǿ������Ƿ��쳣
}