#pragma once

//�ṩ�����ܣ�����(�߳�)���������ͨ��
//	���ݽ���������ģʽ��ǰ���ʺ��첽��Ϣͨ�ų��ϣ�������������Ҫ��ʱ�Ӻ͸����µĳ��ϣ�����ģʽ�ܹ���
//		������Ϣͨ��PostMsg/FetchMsg(����)
//		�����̲߳���task_param_t�Ķ������ݹ���ͽ�������Ҫ����úͱ������߳���Ӫ���ݽ������������������
//
//	����ͨ��task_id_t��ʶ�����ܴ����������л������״̬��������������̻߳����������
//		�����ͨ��AddManagedTask������Task����������Ϊ�й������ǰ���task_routinefunc_t�ģ�������ʵ��
//			�����߳̿��ܻᴮ�����ж�����񣬵����û���˵�������֪�䱳���̻߳���
//		����Ϊ���й��������뵱ǰ�̰߳󶨣�һ��ִ�п�ĺ����������Զ�����һ��task_id_t��ֱ���߳������ڽ�
//			���Ž���󶨹�ϵ�������'����'��ͬ��������й�������ʵ����ֻ��һ��task_id_t��Ϊid����ʶ����
//			�ް�task_routinefunc_t
//		����ʵ���ϣ��й������ǰ����˷��й�����������ݽṹ�ģ�������һ���������Ӽ��Ĺ�ϵ
//
//	�߳̿⽫��Ҫ��Ƚ�����Ϣѭ�����ṩ����ѭ������ģʽ���Զ���ѭ��ģʽ->�ֶ�����DispatchInternal��Ĭ��ѭ��
//		ģʽ->����RunXXXLoop������Ҫע�����������������ҪSetWaitExit�����˳�
//
//	�߳̿��ṩһ����Group/Cls�ļ�������������ÿ��������Լ����Ի��ġ��̳߳ء����������������������ע
//		��Ŀǰÿ����̳߳ص��߳���Ĭ��Ϊ 2
//
//	ɾ���й��߳�ʱ�򣬻���ϵ����������̵߳���Ϣѭ���ص��������ĺô��Ǳ����߳�֮����Ȼ����ͨ��Windows��Ϣ
//		����DestroyWindowͨ�ţ�����֮�����ڼ�Ȼ�������̻߳�����Ӧ��Ϣ��ô����Ҫ���"�������̴߳����ɾ����
//		�ñ���������Ӱ������ݵ�����"����
//
//	�����ӿ��ǻ��ڵ�ǰ�߳������Ľ��еĲ���������: 
//		SetManagedClsAttri��AddManagedTask��GetTaskState��GetTaskByName��PrintMeta��

#include <basetsd.h>
#include <wtypes.h>
#include <memory>
#include <functional>
#include <string>

namespace ThreadPoolV4
{
	using task_cmd_t = UINT16;

	using task_id_t = UINT64;	//��������������Ψһ
	const task_id_t task_id_null = task_id_t(0);
	const task_id_t task_id_self = task_id_t(-1);
	const task_id_t task_id_broadcast_allothers = task_id_t(-2);
	const task_id_t task_id_broadcast_sameclsothers = task_id_t(-3);
	#define	IsSingleTaskID(x) ((x) != task_id_null && (x) != task_id_broadcast_allothers && (x) != task_id_broadcast_sameclsothers)

	class task_data_base
	{
	public:
		task_data_base() {}
		virtual ~task_data_base() {}
	};
	using task_data_t = std::shared_ptr<task_data_base>;

	using task_flag_t = UINT32;
	const task_flag_t task_flag_null = 0x00;
	const task_flag_t task_flag_debug = 0x01;

	class task_param_base
	{
	public:
		task_param_base() {}
		virtual ~task_param_base() {}
		virtual void	Print(LPCTSTR prix) {}
	};
	using task_param_t = std::shared_ptr<task_param_base>;

	class msgsink_userdata_base
	{
	public:
		msgsink_userdata_base() {}
		virtual ~msgsink_userdata_base() {}
		virtual void	Print(LPCTSTR prix) {}
	};
	using msgsink_userdata_t = std::shared_ptr<msgsink_userdata_base>;

	using task_cls_t = std::wstring;
	const task_cls_t	task_cls_default = _T("default");

	using task_name_t = std::wstring;

	using thread_id_t = UINT;	//ԭ���߳�id

	using timer_id_t = UINT64;
	using timer_sinkfunc_t = std::function<void(const timer_id_t& tid)>;

	using idle_id_t = UINT64;
	using idle_sinkfunc_t = std::function<void(const idle_id_t& iid)>;

	enum TaskErrorCode
	{
		TEC_SUCCEED = 0,
		TEC_FAILED = 1,				//ͨ��ʧ��
		TEC_TIMEOUT = 2,
		TEC_NAME_EXIST = 3,
		TEC_ALLOC_FAILED = 4,			//������Դʧ��
		TEC_INVALID_THREADID = 5,		//�߳�id������
		TEC_EXIT_STATE = 6,				//�˳�״̬�²��ܴ����߳�
		TEC_NOT_EXIST = 7,
		TEC_INVALID_ARG = 8,			//��������
		TEC_MANAGED_DELETE_SELF = 9,	//�й��̲߳����Լ�ɾ���Լ�
		TEC_NO_RECEIVER = 10,			//�����߲����ڻ�ܾ���Ϣ
	};
	enum TaskWorkState
	{
		TWS_NONE = 0,
		TWS_AWAIT = 1,					//�ȴ�װ���߳�
		TWS_ACTIVE = 2,					//�����߳�������
	};
	
	//������ע��Ļص�����
	using task_sinkfunc_t = std::function<void(const task_id_t& sender_id, const task_cmd_t& cmd, const task_data_t& data, const msgsink_userdata_t& userdata)>;
	//�����߷��ͺ󣬶��ڷ��ͽ���Ļص�֪ͨ
	using task_echofunc_t = std::function<void(const task_id_t& receiver_id, const task_cmd_t& cmd, const task_data_t& data, const TaskErrorCode& err)>;	
	//�й��߳���ڣ���Ҫ������ѯIsExitLoop���Ƿ��ⲿҪ�����˳�
	using task_routinefunc_t = std::function<void(const task_id_t& self_id, const task_param_t& param)>;	
	//�Զ���һ��ѭ������������FALSE��ʾϣ��ѭ������
	using task_userloop_t = std::function<BOOL()>;

	//---------��Ϣ�շ�-----------
	//֧�ֽ�����Ϊtask_id_xxx��ͨ���Ͷ��
	TaskErrorCode	PostMsg(const task_id_t& receiver_task_id, const task_cmd_t& cmd, task_data_t data, const task_flag_t& flags = task_flag_null, const task_echofunc_t& echofunc = nullptr);
	TaskErrorCode	PostMsg(const task_name_t& receiver_task_name, const task_cmd_t& cmd, task_data_t data, const task_flag_t& flags = task_flag_null, const task_echofunc_t& echofunc = nullptr);

	//��Խ�����
	TaskErrorCode	RegDefaultMsgSink(const task_sinkfunc_t& sinkfunc, const msgsink_userdata_t& userdata);	//û�н���������Ϣ����������
	TaskErrorCode	UnregDefaultMsgSink();
	TaskErrorCode	RegMsgSink(const task_cmd_t& cmd, const task_sinkfunc_t& sinkfunc, const msgsink_userdata_t& userdata);
	TaskErrorCode	UnregMsgSink(const task_cmd_t& cmd);
	TaskErrorCode	DispatchInternal(const BOOL& triggle_idle = TRUE, BOOL* real_empty_handle = nullptr);//�ڲ����ݷַ����ⲿָʾ�Ƿ񴥷�idle�����������ʾ�����Ƿ�ʵ���д���ҵ��

	//---------�̳߳�-----------
	void			SetManagedClsAttri(const task_cls_t& cls, const UINT16& thread_limit_max_num, const UINT32& unhandle_msg_timeout);//�����߳�����
	task_id_t		GetCurrentTaskID();//��ȡ��ǰ������TaskID
	TaskErrorCode	SetCurrentName(const task_name_t& name);//��Ϊ�й��̻߳���AddManagedTask�ṩ��������������Ҳ˳������й��߳�һ�����᣺���ӱ����Ա��ѯ
	TaskErrorCode	SetCurrentAttri(const UINT32& unhandle_msg_timeout);
	TaskErrorCode	RunBaseLoop(const task_userloop_t& user_loop_func = nullptr);
	TaskErrorCode	RunWinLoop(const task_userloop_t& user_loop_func = nullptr);
	TaskErrorCode	SetExitLoop(const BOOL& enable);//�����˳�RunXXXLoop��ǣ�����������й��߳����п���Ȩ�����Իص�task_routinefunc_t
	TaskErrorCode	IsExitLoop(BOOL& enable);

	//---------�������----------
	TaskErrorCode	AddManagedTask(const task_cls_t& cls, const task_name_t& name, const task_param_t& param, const task_routinefunc_t& routine, task_id_t& id);
	//�����ȴ�Ŀ����ɣ�������й�����/�̵߳��ô˺�������Ҫ��ע�����룬��Ϊ���������Լ�ɾ���Լ������ҿ����л���ɾ���Է�����������Ӧ�þ������ⲿ����
	TaskErrorCode	DelManagedTask(const task_id_t& id, const task_userloop_t& user_loop_func = nullptr);
	//ͬDelManagedTaskע������
	TaskErrorCode	ClearManagedTask(const task_userloop_t& user_loop_func = nullptr);
	TaskErrorCode	GetTaskState(const task_id_t& id, TaskWorkState& state);
	TaskErrorCode	GetTaskByName(const task_name_t& name, task_id_t& id);//Name��������Сдȫ��Ψһ

	//---------��ʱ����IDLE----------
	TaskErrorCode	AllocTimer(timer_id_t& tid);
	TaskErrorCode	StartTimer(const timer_id_t& tid, const UINT32& millisecond, const timer_sinkfunc_t& cb, BOOL immediate = FALSE);
	TaskErrorCode	StopTimer(const timer_id_t& tid);
	TaskErrorCode	ExistTimer(const timer_id_t& tid, BOOL& exist);

	TaskErrorCode	AllocIdle(idle_id_t& iid);
	TaskErrorCode	StartIdle(const idle_id_t& iid, const idle_sinkfunc_t& cb);
	TaskErrorCode	StopIdle(const idle_id_t& iid);
	TaskErrorCode	ExistIdle(const idle_id_t& iid, BOOL& exist);

	//Ϊ�˼����������Ҫ����������ڱ������䱳������/�߳���������
	class CTaskTimerHelper
	{
	public:
		CTaskTimerHelper();
		virtual ~CTaskTimerHelper();

		BOOL Start(const UINT32& millisecond, const BOOL& immediate = FALSE);
		void Stop();
		BOOL IsActive();
		void SetCallBack(const timer_sinkfunc_t& cb);

	protected:
		timer_id_t			_timer_id;
		timer_sinkfunc_t*	_cb;	//��Ϊָ����Ϊ�˷��㵼��
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
		void SetCallBack(const idle_sinkfunc_t& cb);

	protected:
		idle_id_t			_idle_id;
		idle_sinkfunc_t*	_cb;//��Ϊָ����Ϊ�˷��㵼��
		task_id_t			_belongs_task_id;
		 
		void	OnIdle(const idle_id_t& iid);
	};

	//---------Debug---------
	TaskErrorCode	PrintMeta();
}
