#pragma once

//�����ṩ�����ܣ���������첽��Ϣͨ��
//	����ͨ��task_id_t��ʶ�����ܴ����������л������״̬��������������̻߳����������
//		�����ͨ��AddManagedTask������Task����������Ϊ�й������ǰ���task_routinefunc_t�ģ�������ʵ�������߳̿��ܻᴮ�����ж������
//			�����û���˵�������֪�䱳���̻߳���
//		����Ϊ���й��������뵱ǰ�̰߳󶨣�һ��ִ�п�ĺ����������Զ�����һ��task_id_t��ֱ���߳������ڽ����Ž���󶨹�ϵ�������'����'
//			��ͬ��������й�������ʵ����ֻ��һ��task_id_t��Ϊid���֣����ް�task_routinefunc_t
//	����ʵ���ϣ��й������ǰ����˷��й�����������ݽṹ�ģ�������һ���������Ӽ��Ĺ�ϵ
//	ע��Ŀǰÿ����߳���Ĭ��Ϊ2
//�����ӿ��ǻ��ڵ�ǰ�߳������Ľ��еĲ���������: SetManagedClsAttri��AddManagedTask��GetTaskState��GetTaskByName��PrintMeta��

#include <basetsd.h>
#include <wtypes.h>
#include <memory>
#include <functional>
#include <mutex>
#include <vector>
#include <string>

namespace ThreadPoolV4
{
	using task_cmd_t = UINT16;

	using task_id_t = UINT64;	//�����̴�Χ��Ψһ
	const task_id_t task_id_null = task_id_t(0);
	const task_id_t task_id_self = task_id_t(-1);
	const task_id_t task_id_broadcast_allothers = task_id_t(-2);
	const task_id_t task_id_broadcast_sameclsothers = task_id_t(-3);

	using task_data_t = std::shared_ptr<std::string>;

	using task_flag_t = UINT32;
	const task_flag_t task_flag_null = 0x00;
	const task_flag_t task_flag_debug = 0x01;

	using task_param_t = std::shared_ptr<std::string>;

	using task_cls_t = std::wstring;
	const task_cls_t	task_cls_default = _T("default");

	using task_name_t = std::wstring;

	using thread_id_t = DWORD;	//ԭ���߳�id

	using timer_id_t = UINT64;
	using timer_function_t = std::function<void(const timer_id_t& tid)>;

	using idle_id_t = UINT64;
	using idle_function_t = std::function<void(const idle_id_t& iid)>;

	enum TaskErrorCode
	{
		TEC_SUCCEED = 0,
		TEC_TIMEOUT = 1,
		TEC_NAME_EXIST = 3,
		TEC_ALLOC_FAILED = 5,			//������Դʧ��
		TEC_INVALID_THREADID = 6,		//�߳�id������
		TEC_EXIT_STATE = 7,				//�˳�״̬�²��ܴ����߳�
		TEC_NOT_EXIST = 8,
		TEC_INVALID_ARG = 9,			//��������
		TEC_MANAGED_DELETE_SELF = 10,	//�й��̲߳����Լ�ɾ���Լ�
		TEC_FAILED = 11,				//ͨ��ʧ��
	};
	enum TaskWorkState
	{
		TWS_NONE = 0,
		TWS_AWAIT = 1,					//�ȴ�װ���߳�
		TWS_ACTIVE = 2,					//�����߳�������
	};
	
	//������ע��Ļص�����
	using task_sinkfunc_t = std::function<void(const task_id_t& sender_id, const task_cmd_t& cmd, const task_data_t& data)>;	
	//�����߷��ͺ󣬶��ڷ��ͽ���Ļص�֪ͨ
	using task_echofunc_t = std::function<void(const task_id_t& receiver_id, const task_cmd_t& cmd, const task_data_t& data, const TaskErrorCode& err)>;	
	//�й��߳���ڣ���Ҫ������ѯThreadCtrlBlock���Ƿ��ⲿҪ�����˳�
	using task_routinefunc_t = std::function<void(const task_id_t& self_id, const task_param_t& param)>;	

	//---------��Ϣ�շ�-----------
	//֧�ֽ�����Ϊtask_id_xxx��ͨ���Ͷ��
	TaskErrorCode	PostMsg(const task_id_t& target_id, const task_cmd_t& cmd, task_data_t data, const task_flag_t& flags = task_flag_null, const task_echofunc_t& echofunc = nullptr);
	TaskErrorCode	PostMsg(const task_name_t& target_name, const task_cmd_t& cmd, task_data_t data, const task_flag_t& flags = task_flag_null, const task_echofunc_t& echofunc = nullptr);

	//��Խ�����
	TaskErrorCode	RegRubbishMsgSink(const task_sinkfunc_t& sinkfunc);	//û�н���������Ϣ����������
	TaskErrorCode	UnregRubbishMsgSink();
	TaskErrorCode	RegMsgSink(const task_cmd_t& cmd, const task_sinkfunc_t& sinkfunc);
	TaskErrorCode	UnregMsgSink(const task_cmd_t& cmd);
	TaskErrorCode	DispatchMsg(size_t& count);//�Զ��ַ���ǰ�߳��յ�����Ϣ��������windows��Ϣ

	//---------�̳߳�-----------
	void			SetManagedClsAttri(const task_cls_t& cls, const UINT16& thread_num, const UINT32& unhandle_msg_timeout);//�����߳�����
	task_id_t		GetCurrentTaskID();//��ȡ��ǰ�̶߳�Ӧ��TaskID
	TaskErrorCode	SetCurrentName(const task_name_t& name);//��Ϊ�й��̻߳���AddManagedTask�ṩ��������������Ҳ˳������й��߳�һ�����᣺���ӱ����Ա��ѯ
	TaskErrorCode	RunBaseLoop();
	TaskErrorCode	RunWinLoop();
	TaskErrorCode	SetExitLoop();//�����˳��߳�/������λ��Ҳ��˳���˳�Loop
	TaskErrorCode	IsExitLoop(BOOL& enable);

	//---------�������----------
	TaskErrorCode	AddManagedTask(const task_cls_t& cls, const task_name_t& name, const task_param_t& param, const task_routinefunc_t& routine, task_id_t& id);
	//�����ȴ�Ŀ����ɣ�������й�����/�̵߳��ô˺�������Ҫ��ע�����룬��Ϊ���������Լ�ɾ���Լ������ҿ����л���ɾ���Է�����������Ӧ�þ������ⲿ����
	TaskErrorCode	DelManagedTask(const task_id_t& id);
	//ͬDelManagedTaskע������
	TaskErrorCode	ClearManagedTask();
	TaskErrorCode	GetTaskState(const task_id_t& id, TaskWorkState& state);
	TaskErrorCode	GetTaskByName(const task_name_t& name, task_id_t& id);

	//---------��ʱ����IDLE----------
	TaskErrorCode	AllocTimer(timer_id_t& tid);
	TaskErrorCode	StartTimer(const timer_id_t& tid, const UINT32& millisecond, const timer_function_t& cb, BOOL immediate = FALSE);
	TaskErrorCode	StopTimer(const timer_id_t& tid);
	TaskErrorCode	ExistTimer(const timer_id_t& tid, BOOL& exist);

	TaskErrorCode	AllocIdle(idle_id_t& iid);
	TaskErrorCode	StartIdle(const idle_id_t& iid, const idle_function_t& cb);
	TaskErrorCode	StopIdle(const idle_id_t& iid);
	TaskErrorCode	ExistIdle(const idle_id_t& iid, BOOL& exist);

	//Ϊ�˱����������Ҫ����������ڱ������䱳������/�߳���������
	class CTaskTimerHelper
	{
	public:
		CTaskTimerHelper();
		virtual ~CTaskTimerHelper();

		BOOL Start(UINT32 millisecond, BOOL immediate = FALSE);
		void Stop();
		BOOL IsActive();
		void SetCallBack(const timer_function_t& cb);

	protected:
		timer_id_t			_timer_id;
		timer_function_t	_cb;
		task_id_t			_belongs_task_id;

		void	OnTimer(const timer_id_t& tid);
	};
	class CTaskIdleHelper
	{
	public:
		CTaskIdleHelper();
		virtual ~CTaskIdleHelper();

		BOOL Start();
		void Stop();
		BOOL IsActive();
		void SetCallBack(const idle_function_t& cb);

	protected:
		idle_id_t			_idle_id;
		idle_function_t		_cb;
		task_id_t			_belongs_task_id;

		void	OnIdle(const idle_id_t& iid);
	};

	//---------Debug---------
	TaskErrorCode	PrintMeta();
}
