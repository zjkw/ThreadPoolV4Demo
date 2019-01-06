#pragma once

//本库提供两大功能：任务管理，异步消息通信
//	任务通过task_id_t标识，可能处于正在运行或待运行状态，而这个和所处线程环境密切相关
//		如果是通过AddManagedTask创建的Task，我们命名为托管任务，是绑定了task_routinefunc_t的，所处的实际运行线程可能会串行运行多个任务，
//			但对用户来说，无需感知其背景线程环境
//		否则为非托管任务，其与当前线程绑定，一旦执行库的函数，将会自动创建一个task_id_t，直到线程生命期结束才解除绑定关系，但这个'任务'
//			不同于上面的托管任务，其实际是只用一个task_id_t作为id区分，而无绑定task_routinefunc_t
//	具体实现上，托管任务是包括了非托管任务相关数据结构的，两者是一个超集和子集的关系
//	注意目前每类别线程数默认为2
//多数接口是基于当前线程上下文进行的操作，除了: SetManagedClsAttri，AddManagedTask，GetTaskState，GetTaskByName，PrintMeta等

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

	using thread_id_t = DWORD;	//原生线程id

	using timer_id_t = UINT64;
	using timer_function_t = std::function<void(const timer_id_t& tid)>;

	using idle_id_t = UINT64;
	using idle_function_t = std::function<void(const idle_id_t& iid)>;

	enum TaskErrorCode
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
		TEC_FAILED = 11,				//通用失败
	};
	enum TaskWorkState
	{
		TWS_NONE = 0,
		TWS_AWAIT = 1,					//等待装入线程
		TWS_ACTIVE = 2,					//正在线程中运行
	};
	
	//接收者注册的回调函数
	using task_sinkfunc_t = std::function<void(const task_id_t& sender_id, const task_cmd_t& cmd, const task_data_t& data)>;	
	//发送者发送后，对于发送结果的回调通知
	using task_echofunc_t = std::function<void(const task_id_t& receiver_id, const task_cmd_t& cmd, const task_data_t& data, const TaskErrorCode& err)>;	
	//托管线程入口，需要定期轮询ThreadCtrlBlock，是否外部要求其退出
	using task_routinefunc_t = std::function<void(const task_id_t& self_id, const task_param_t& param)>;	

	//---------消息收发-----------
	//支持接收者为task_id_xxx的通配符投递
	TaskErrorCode	PostMsg(const task_id_t& target_id, const task_cmd_t& cmd, task_data_t data, const task_flag_t& flags = task_flag_null, const task_echofunc_t& echofunc = nullptr);
	TaskErrorCode	PostMsg(const task_name_t& target_name, const task_cmd_t& cmd, task_data_t data, const task_flag_t& flags = task_flag_null, const task_echofunc_t& echofunc = nullptr);

	//针对接收者
	TaskErrorCode	RegRubbishMsgSink(const task_sinkfunc_t& sinkfunc);	//没有接收器的消息进入垃圾箱
	TaskErrorCode	UnregRubbishMsgSink();
	TaskErrorCode	RegMsgSink(const task_cmd_t& cmd, const task_sinkfunc_t& sinkfunc);
	TaskErrorCode	UnregMsgSink(const task_cmd_t& cmd);
	TaskErrorCode	DispatchMsg(size_t& count);//自动分发当前线程收到的消息，不包括windows消息

	//---------线程池-----------
	void			SetManagedClsAttri(const task_cls_t& cls, const UINT16& thread_num, const UINT32& unhandle_msg_timeout);//调节线程数量
	task_id_t		GetCurrentTaskID();//获取当前线程对应的TaskID
	TaskErrorCode	SetCurrentName(const task_name_t& name);//因为托管线程会在AddManagedTask提供名字能力，这里也顺便给非托管线程一个机会：增加别名以便查询
	TaskErrorCode	RunBaseLoop();
	TaskErrorCode	RunWinLoop();
	TaskErrorCode	SetExitLoop();//设置退出线程/任务标记位，也会顺便退出Loop
	TaskErrorCode	IsExitLoop(BOOL& enable);

	//---------任务管理----------
	TaskErrorCode	AddManagedTask(const task_cls_t& cls, const task_name_t& name, const task_param_t& param, const task_routinefunc_t& routine, task_id_t& id);
	//阻塞等待目标完成，如果是托管任务/线程调用此函数，需要关注返回码，因为不会允许自己删除自己，而且可能有互相删除对方导致死锁，应该尽量在外部避免
	TaskErrorCode	DelManagedTask(const task_id_t& id);
	//同DelManagedTask注意事项
	TaskErrorCode	ClearManagedTask();
	TaskErrorCode	GetTaskState(const task_id_t& id, TaskWorkState& state);
	TaskErrorCode	GetTaskByName(const task_name_t& name, task_id_t& id);

	//---------定时器和IDLE----------
	TaskErrorCode	AllocTimer(timer_id_t& tid);
	TaskErrorCode	StartTimer(const timer_id_t& tid, const UINT32& millisecond, const timer_function_t& cb, BOOL immediate = FALSE);
	TaskErrorCode	StopTimer(const timer_id_t& tid);
	TaskErrorCode	ExistTimer(const timer_id_t& tid, BOOL& exist);

	TaskErrorCode	AllocIdle(idle_id_t& iid);
	TaskErrorCode	StartIdle(const idle_id_t& iid, const idle_function_t& cb);
	TaskErrorCode	StopIdle(const idle_id_t& iid);
	TaskErrorCode	ExistIdle(const idle_id_t& iid, BOOL& exist);

	//为了避免错误，我们要求对象生命期必须在其背景任务/线程生命期内
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
