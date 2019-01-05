#pragma once

//�����ṩ�����ܣ���������첽��Ϣͨ��
//	����ͨ��task_id_t��ʶ�����ܴ����������л������״̬��������������̻߳����������
//		�����ͨ��AddManagedTask������Task����������Ϊ�й������ǰ���task_routinefunc_t�ģ�������ʵ�������߳̿��ܻᴮ�����ж������
//			�����û���˵�������֪�䱳���̻߳���
//		����Ϊ���й��������뵱ǰ�̰߳󶨣�һ��ִ�п�ĺ����������Զ�����һ��task_id_t��ֱ���߳������ڽ����Ž���󶨹�ϵ�������'����'
//			��ͬ��������й�������ʵ����ֻ��һ��task_id_t��Ϊid���֣����ް�task_routinefunc_t
//	����ʵ���ϣ��й������ǰ����˷��й�����������ݽṹ�ģ�������һ���������Ӽ��Ĺ�ϵ
//	ע��Ŀǰÿ����߳���Ĭ��Ϊ2
//�����ӿ��ǻ��ڵ�ǰ�߳������Ľ��еĲ���������: SetClsAttri��AddManagedTask��GetTaskState��GetTaskByName��PrintMeta��

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
	const task_id_t task_id_broadcast_all = task_id_t(-2);
	const task_id_t task_id_broadcast_samecls = task_id_t(-3);

	using task_data_t = std::shared_ptr<std::string>;

	using task_flag_t = UINT32;
	const task_flag_t task_flag_null = 0x00;
	const task_flag_t task_flag_debug = 0x01;

	using task_param_t = std::shared_ptr<std::string>;

	using task_cls_t = std::wstring;
	const task_cls_t	task_cls_default = _T("default");

	using task_name_t = std::wstring;

	using thread_id_t = DWORD;	//ԭ���߳�id

	enum ThreadErrorCode
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
	};
	enum ThreadTaskState
	{
		TTS_NONE = 0,
		TTS_AWAIT = 1,					//�ȴ�װ���߳�
		TTS_ACTIVE = 2,					//�����߳�������
	};

	//�ڲ��о���ʵ��
	class ThreadCtrlBlock
	{
	public:
		ThreadCtrlBlock(const thread_id_t& thread_id = 0) : _force_exit(FALSE), _print_once(FALSE), _thread_id(thread_id)
		{
		}
		virtual ~ThreadCtrlBlock()
		{
		}
		virtual BOOL	IsWaitExit() = 0;
		virtual void	SetWaitExit() = 0;
		virtual void	Print(LPCTSTR prix) = 0;

	protected:
		BOOL			_force_exit;
		BOOL			_print_once;
		thread_id_t		_thread_id;
	};

	//������ע��Ļص�����
	using task_sinkfunc_t = std::function<void(const task_id_t& sender_id, const task_cmd_t& cmd, const task_data_t& data)>;	
	//�����߷��ͺ󣬶��ڷ��ͽ���Ļص�֪ͨ
	using task_echofunc_t = std::function<void(const task_id_t& receiver_id, const task_cmd_t& cmd, const task_data_t& data, const ThreadErrorCode& err)>;	
	//�й��߳���ڣ���Ҫ������ѯThreadCtrlBlock���Ƿ��ⲿҪ�����˳�
	using task_routinefunc_t = std::function<void(const task_id_t& self_id, std::shared_ptr<ThreadCtrlBlock> tcb, const task_param_t& param)>;	

	//---------��Ϣ�շ�-----------
	//��Է����ߣ������Լ����Լ���
	ThreadErrorCode PostMsg(const task_id_t& target_id, const task_cmd_t& cmd, task_data_t data, const task_flag_t& flags = task_flag_null, const task_echofunc_t& echofunc = nullptr);//target_idΪ0��ʾ�㲥
	ThreadErrorCode PostMsg(const task_name_t& target_name, const task_cmd_t& cmd, task_data_t data, const task_flag_t& flags = task_flag_null, const task_echofunc_t& echofunc = nullptr);

	//��Խ�����
	ThreadErrorCode RegRubbishMsgSink(const task_sinkfunc_t& sinkfunc);	//û�н���������Ϣ����������
	ThreadErrorCode UnregRubbishMsgSink();
	ThreadErrorCode RegMsgSink(const task_cmd_t& cmd, const task_sinkfunc_t& sinkfunc);
	ThreadErrorCode UnregMsgSink(const task_cmd_t& cmd);
	ThreadErrorCode DispatchMsg(size_t& count);//�Զ��ַ���ǰ�߳��յ�����Ϣ��������windows��Ϣ

	//---------�̳߳�-----------
	void			SetClsAttri(const task_cls_t& cls, const UINT16& thread_num, const UINT32& unhandle_msg_timeout);						//�����߳�����
	task_id_t		GetCurrentTaskID();//��ȡ��ǰ�̶߳�Ӧ��TaskID
	ThreadErrorCode SetCurrentName(const task_name_t& name);//��Ϊ�й��̻߳���AddManagedTask�ṩ��������������Ҳ˳������й��߳�һ�����᣺���ӱ����Ա��ѯ
	ThreadErrorCode RunBaseLoop();
	ThreadErrorCode RunWinLoop();
	ThreadErrorCode ExitLoop();

	//---------�������----------
	ThreadErrorCode AddManagedTask(const task_cls_t& cls, const task_name_t& name, const task_param_t& param, const task_routinefunc_t& routine, task_id_t& id);
	//�����ȴ�Ŀ����ɣ�������й�����/�̵߳��ô˺�������Ҫ��ע�����룬��Ϊ���������Լ�ɾ���Լ������ҿ����л���ɾ���Է�����������Ӧ�þ������ⲿ����
	ThreadErrorCode DelManagedTask(const task_id_t& id);
	//ͬDelManagedTaskע������
	ThreadErrorCode	ClearManagedTask();
	ThreadErrorCode	GetTaskState(const task_id_t& id, ThreadTaskState& state);
	ThreadErrorCode GetTaskByName(const task_name_t& name, task_id_t& id);

	//---------Debug---------
	ThreadErrorCode PrintMeta(const task_id_t& id = task_id_self);
}
