#pragma once

//本库提供两大功能：任务管理，异步消息通信
//	任务通过task_id_t标识，可能处于正在运行或待运行状态，而这个和所处线程环境密切相关
//		如果是通过AddManagedTask创建的Task，我们命名为托管任务，是绑定了task_routinefunc_t的，所处的实际运行线程可能会串行运行多个任务，
//			但对用户来说，无需感知其背景线程环境
//		否则为非托管任务，其与当前线程绑定，一旦执行库的函数，将会自动创建一个task_id_t，直到线程生命期结束才解除绑定关系，但这个'任务'
//			不同于上面的托管任务，其实际是只用一个task_id_t作为id区分，而无绑定task_routinefunc_t
//	具体实现上，托管任务是包括了非托管任务相关数据结构的，两者是一个超集和子集的关系
//	注意目前每类别线程数默认为2
//多数接口是基于当前线程上下文进行的操作，除了: SetClsAttri，AddManagedTask，GetTaskState，GetTaskByName，PrintMeta等

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

	using task_id_t = UINT64;	//本进程存活范围内唯一
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

	using thread_id_t = DWORD;	//原生线程id

	enum ThreadErrorCode
	{
		TEC_SUCCEED = 0,
		TEC_TIMEOUT = 1,
		TEC_NAME_EXIST = 3,
		TEC_ALLOC_FAILED = 5,			//申请资源失败
		TEC_INVALID_THREADID = 6,		//线程id不存在
		TEC_EXIT_STATE = 7,				//退出状态下不能创建线程
		TEC_NOT_EXIST = 8,
		TEC_INVALID_ARG = 9,			//参数错误
		TEC_MANAGED_DELETE_SELF = 10,	//托管线程不能自己删除自己
	};
	enum ThreadTaskState
	{
		TTS_NONE = 0,
		TTS_AWAIT = 1,					//等待装入线程
		TTS_ACTIVE = 2,					//正在线程中运行
	};

	//内部有具体实现
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

	//接收者注册的回调函数
	using task_sinkfunc_t = std::function<void(const task_id_t& sender_id, const task_cmd_t& cmd, const task_data_t& data)>;	
	//发送者发送后，对于发送结果的回调通知
	using task_echofunc_t = std::function<void(const task_id_t& receiver_id, const task_cmd_t& cmd, const task_data_t& data, const ThreadErrorCode& err)>;	
	//托管线程入口，需要定期轮询ThreadCtrlBlock，是否外部要求其退出
	using task_routinefunc_t = std::function<void(const task_id_t& self_id, std::shared_ptr<ThreadCtrlBlock> tcb, const task_param_t& param)>;	

	//---------消息收发-----------
	//针对发送者，允许自己给自己发
	ThreadErrorCode PostMsg(const task_id_t& target_id, const task_cmd_t& cmd, task_data_t data, const task_flag_t& flags = task_flag_null, const task_echofunc_t& echofunc = nullptr);//target_id为0表示广播
	ThreadErrorCode PostMsg(const task_name_t& target_name, const task_cmd_t& cmd, task_data_t data, const task_flag_t& flags = task_flag_null, const task_echofunc_t& echofunc = nullptr);

	//针对接收者
	ThreadErrorCode RegRubbishMsgSink(const task_sinkfunc_t& sinkfunc);	//没有接收器的消息进入垃圾箱
	ThreadErrorCode UnregRubbishMsgSink();
	ThreadErrorCode RegMsgSink(const task_cmd_t& cmd, const task_sinkfunc_t& sinkfunc);
	ThreadErrorCode UnregMsgSink(const task_cmd_t& cmd);
	ThreadErrorCode DispatchMsg(size_t& count);//自动分发当前线程收到的消息，不包括windows消息

	//---------线程池-----------
	void			SetClsAttri(const task_cls_t& cls, const UINT16& thread_num, const UINT32& unhandle_msg_timeout);						//调节线程数量
	task_id_t		GetCurrentTaskID();//获取当前线程对应的TaskID
	ThreadErrorCode SetCurrentName(const task_name_t& name);//因为托管线程会在AddManagedTask提供名字能力，这里也顺便给非托管线程一个机会：增加别名以便查询
	ThreadErrorCode RunBaseLoop();
	ThreadErrorCode RunWinLoop();
	ThreadErrorCode ExitLoop();

	//---------任务管理----------
	ThreadErrorCode AddManagedTask(const task_cls_t& cls, const task_name_t& name, const task_param_t& param, const task_routinefunc_t& routine, task_id_t& id);
	//阻塞等待目标完成，如果是托管任务/线程调用此函数，需要关注返回码，因为不会允许自己删除自己，而且可能有互相删除对方导致死锁，应该尽量在外部避免
	ThreadErrorCode DelManagedTask(const task_id_t& id);
	//同DelManagedTask注意事项
	ThreadErrorCode	ClearManagedTask();
	ThreadErrorCode	GetTaskState(const task_id_t& id, ThreadTaskState& state);
	ThreadErrorCode GetTaskByName(const task_name_t& name, task_id_t& id);

	//---------Debug---------
	ThreadErrorCode PrintMeta(const task_id_t& id = task_id_self);
}
