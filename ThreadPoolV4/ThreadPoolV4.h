#pragma once

//提供两大功能：任务(线程)管理，任务间通信
//	数据交换有两种模式：前者适合异步消息通信场合，后者适用于需要低时延和高吞吐的场合；两种模式能共存，对
//	应的,task_data_t既能通过PostMsg通信，又能通过AddManagedTask任务参数传递
//		基于消息通信PostMsg/FetchMsg(内置)
//		基于线程参数task_data_t的对象数据共享和交换，这要求调用和被调用线程自营数据交换对象的生产和消费,
//			但是需要参数是否前后线程复用，如无必要，需要在新线程创建后清理残留在"管道"的数据			
//
//	任务通过task_id_t标识，可能处于正在运行或待运行状态，而这个和所处线程环境密切相关
//		如果是通过AddManagedTask创建的Task，我们命名为托管任务，是绑定了task_routinefunc_t的，所处的实际
//			运行线程可能会串行运行多个任务，但对用户来说，无需感知其背景线程环境
//		否则为非托管任务，其与当前线程绑定，一旦执行库的函数，将会自动创建一个task_id_t，直到线程生命期结
//			束才解除绑定关系，但这种'任务'不同于上面的托管任务，其实际是只用一个task_id_t作为id来标识，并
//			无绑定task_routinefunc_t
//		具体实现上，托管任务是包括了非托管任务相关数据结构的，两者是一个超集和子集的关系
//
//	线程库将需要深度介入消息循环，提供两种循环管理模式：自定义循环模式->手动调用DispatchInternal；默认循环
//		模式->调用RunXXXLoop，但需要注意这个阻塞函数是需要SetWaitExit才能退出
//
//	线程库提供一个按Group/Cls的简单类别管理能力，每个类别有自己个性化的“线程池”，与其他类别区隔开来，注
//		意目前每类别线程池的线程数默认为 2
//
//	删除托管线程时候，特别需要注意两个问题：
//		//1，支持异步删除，即通知线程池删除立即返回，实际删除可能是此后，这要求调用者需要明确，线程所占有的
//				资源可能没法立即清理，以及下次复用该线程可能是要较长时间才能实际被"调度"到
//		//2，同步情况下，会带上调用者所在线程的消息循环回调，这样的好处是便于线程之间仍然可以通过Windows消息
//				进行DestroyWindow通信，不足之处在于既然调用者线程还能响应消息那么就需要解决"调用者线程处理好删除调
//				用本身甚至受影响的数据的重入"问题
//
//	如果外部比如主线程是通过一个task_id_t来控制工作线程生命期的，且存在工作线程可能会自己退出或额外其他线程
//		导致其退出的情况下，必须注意同步task_id_t的有效性，一般来说可以通知主线程来DelManagedTask而不是自行
//		退出，或者主线程轮询GetTaskState
//
//	多数接口是基于当前线程上下文进行的操作，除了: 
//		SetManagedClsAttri，AddManagedTask，GetTaskState，GetTaskByName，PrintMeta等

#include <basetsd.h>
#include <wtypes.h>
#include <memory>
#include <functional>
#include <string>

namespace ThreadPoolV4
{
	using task_cmd_t = UINT16;

	using task_id_t = UINT64;	//本进程生命期内唯一
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
		virtual void	Print(LPCTSTR prix) {}
	};
	using task_data_t = std::shared_ptr<task_data_base>;

	using task_flag_t = UINT32;
	const task_flag_t task_flag_null = 0x00;
	const task_flag_t task_flag_debug = 0x01;
	

	using task_cls_t = std::wstring;
	const task_cls_t	task_cls_default = _T("default");

	using task_name_t = std::wstring;

	using thread_id_t = UINT;	//原生线程id

	using timer_id_t = UINT64;
	using timer_sinkfunc_t = std::function<void(const timer_id_t& tid)>;

	using idle_id_t = UINT64;
	using idle_sinkfunc_t = std::function<void(const idle_id_t& iid)>;

	enum TaskErrorCode
	{
		TEC_SUCCEED = 0,
		TEC_FAILED = 1,				//通用失败
		TEC_TIMEOUT = 2,
		TEC_NAME_EXIST = 3,
		TEC_ALLOC_FAILED = 4,			//申请资源失败
		TEC_INVALID_THREADID = 5,		//线程id不存在
		TEC_EXIT_STATE = 6,				//退出状态下不能创建线程
		TEC_NOT_EXIST = 7,
		TEC_INVALID_ARG = 8,			//参数错误
		TEC_MANAGED_DELETE_SELF = 9,	//托管线程不能自己删除自己
		TEC_NO_RECEIVER = 10,			//接收者不存在或拒绝消息
	};
	enum TaskWorkState
	{
		TWS_NONE = 0,
		TWS_AWAIT = 1,					//等待装入线程
		TWS_ACTIVE = 2,					//正在线程中运行
	};
	enum DelTaskMode
	{
		DTM_NONE = 0,
		DTM_AUTOSIGN_SYNC = 1,				//线程库内部发退出信号，并同步等待删除
		DTM_CUSTOMSIGN_SYNC = 2,			//应用层自行发退出信号，并同步等待删除
		DTM_AUTOSIGN_ASYNC = 3,				//线程库内部发退出信号，不等待其是否确认删除，直接返回
		DTM_CUSTOMSIGN_ASYNC = 4,			//应用层自行发退出信号，不等待其是否确认删除，直接返回
	};
	
	//接收者注册的回调函数
	using task_sinkfunc_t = std::function<void(const task_id_t& sender_id, const task_cmd_t& cmd, const task_data_t& data, const task_data_t& userdata)>;
	//发送者发送后，对于发送结果的回调通知
	using task_echofunc_t = std::function<void(const task_id_t& receiver_id, const task_cmd_t& cmd, const task_data_t& data, const TaskErrorCode& err)>;	
	//托管线程入口，需要定期轮询IsExitLoop，是否外部要求其退出
	using task_routinefunc_t = std::function<void(const task_id_t& self_id, const task_data_t& param)>;	
	//自定义一轮循环函数，返回FALSE表示希望循环结束
	using task_userloop_t = std::function<BOOL()>;

	//---------消息收发-----------
	//支持接收者为task_id_xxx的通配符投递
	TaskErrorCode	PostMsg(const task_id_t& receiver_task_id, const task_cmd_t& cmd, task_data_t data, const task_flag_t& flags = task_flag_null, const task_echofunc_t& echofunc = nullptr);
	TaskErrorCode	PostMsg(const task_name_t& receiver_task_name, const task_cmd_t& cmd, task_data_t data, const task_flag_t& flags = task_flag_null, const task_echofunc_t& echofunc = nullptr);

	//针对接收者
	TaskErrorCode	RegDefaultMsgSink(const task_sinkfunc_t& sinkfunc, const task_data_t& userdata);	//没有接收器的消息进入垃圾箱
	TaskErrorCode	UnregDefaultMsgSink();
	TaskErrorCode	RegMsgSink(const task_cmd_t& cmd, const task_sinkfunc_t& sinkfunc, const task_data_t& userdata);
	TaskErrorCode	UnregMsgSink(const task_cmd_t& cmd);
	TaskErrorCode	DispatchInternal(const BOOL& triggle_idle = TRUE, BOOL* real_triggle = nullptr);//内部数据分发，外部指示是否触发idle，输出参数表示函数是否实际有处理业务

	//---------线程池-----------
	void			SetManagedClsAttri(const task_cls_t& cls, const UINT16& thread_limit_max_num, const UINT32& unhandle_msg_timeout);//调节线程数量
	task_id_t		GetCurrentTaskID();//获取当前所处的TaskID
	TaskErrorCode	SetCurrentName(const task_name_t& name);//因为托管线程会在AddManagedTask提供名字能力，这里也顺便给非托管线程一个机会：增加别名以便查询
	TaskErrorCode	SetCurrentAttri(const UINT32& unhandle_msg_timeout);
	TaskErrorCode	RunBaseLoop(const task_userloop_t& user_loop_func = nullptr);
	TaskErrorCode	RunWinLoop(const task_userloop_t& user_loop_func = nullptr);
	TaskErrorCode	SetExitLoop(const BOOL& enable);//设置退出RunXXXLoop标记，这样如果是托管线程运行控制权将可以回到task_routinefunc_t
	TaskErrorCode	IsExitLoop(BOOL& enable);

	//---------任务管理----------
	TaskErrorCode	AddManagedTask(const task_cls_t& cls, const task_name_t& name, const task_data_t& param, const task_routinefunc_t& routine, task_id_t& id);
	//阻塞等待目标完成，如果是托管任务/线程调用此函数，需要关注返回码，因为不会允许自己删除自己，而且可能有互相删除对方导致死锁，应该尽量在外部避免
	//但此刻实际线程可能还在运行甚至还会依据实际工作任务继续持续一段时间，外部调用者需要自行确保安全性和线程复用成本
	//需要明确的是如果自己关闭自己或关闭非托管线程，将会是强制改成异步
	TaskErrorCode	DelManagedTask(const task_id_t& id, const task_userloop_t& user_loop_func = nullptr, const DelTaskMode& dtm = DTM_AUTOSIGN_SYNC);
	//同DelManagedTask注意事项，但只支持同步阻塞模式，这是基于资源需要先清理的考虑
	TaskErrorCode	ClearManagedTask(const task_userloop_t& user_loop_func = nullptr);
	TaskErrorCode	GetTaskState(const task_id_t& id, TaskWorkState& state);
	TaskErrorCode	GetTaskByName(const task_name_t& name, task_id_t& id);//Name不分区大小写全局唯一

	//---------定时器和IDLE----------
	TaskErrorCode	AllocTimer(timer_id_t& tid);
	TaskErrorCode	StartTimer(const timer_id_t& tid, const UINT32& millisecond, const timer_sinkfunc_t& cb, BOOL immediate = FALSE);
	TaskErrorCode	StopTimer(const timer_id_t& tid);
	TaskErrorCode	ExistTimer(const timer_id_t& tid, BOOL& exist);

	TaskErrorCode	AllocIdle(idle_id_t& iid);
	TaskErrorCode	StartIdle(const idle_id_t& iid, const idle_sinkfunc_t& cb);
	TaskErrorCode	StopIdle(const idle_id_t& iid);
	TaskErrorCode	ExistIdle(const idle_id_t& iid, BOOL& exist);

	//为了简单起见，我们要求对象生命期必须在其背景任务/线程生命期内
	class CTaskTimerHelper
	{
	public:
		CTaskTimerHelper();
		virtual ~CTaskTimerHelper();

		BOOL Start(const UINT32& millisecond, const BOOL& immediate = FALSE);
		void Stop();
		BOOL IsActive();
		void SetCallBack(const timer_sinkfunc_t& cb);
		UINT32 GetInterval();

	protected:
		timer_id_t			_timer_id;
		timer_sinkfunc_t*	_cb;	//改为指针是为了方便导出
		task_id_t			_belongs_task_id;
		UINT32				_millisecond;

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
		idle_sinkfunc_t*	_cb;//改为指针是为了方便导出
		task_id_t			_belongs_task_id;
		 
		void	OnIdle(const idle_id_t& iid);
	};

	//---------Debug---------
	TaskErrorCode	PrintMeta();
}
