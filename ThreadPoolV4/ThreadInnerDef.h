#pragma once

#include <map>
#include <mutex>
#include <vector>
#include "ThreadPoolV4.h"

#define DEFAULT_THREAD_NUM_BY_CLS			(2)
#define DEFAULT_UNHANDLMSG_TIMEOUT_BY_CLS	(20 * 1000)

using namespace ThreadPoolV4;

//内部有具体实现
class ThreadCtrlBlock
{
public:
	ThreadCtrlBlock()
	{
	}
	virtual ~ThreadCtrlBlock()
	{
	}
	virtual BOOL	IsWaitExit() = 0;
	virtual void	SetWaitExit(const BOOL& enable) = 0;
	virtual void	Print(LPCTSTR prix) = 0;
};

class ManagedThreadCtrlBlock : public ThreadCtrlBlock
{
public:
	ManagedThreadCtrlBlock(const thread_id_t& thread_id = 0) : _force_exit(FALSE), _print_once(FALSE), _thread_id(thread_id)
	{
	}
	virtual ~ManagedThreadCtrlBlock()
	{
	}
	virtual BOOL	IsWaitExit()
	{
		std::unique_lock <std::mutex> lck(_mutex);
		return _force_exit;
	}
	virtual void	SetWaitExit(const BOOL& enable)
	{
		std::unique_lock <std::mutex> lck(_mutex);
		_force_exit = enable;
	}
	virtual void	Print(LPCTSTR prix)
	{
		std::unique_lock <std::mutex> lck(_mutex);
		_tprintf(_T("%s_force_exit: %d\n"), prix, _force_exit);
		_tprintf(_T("%s_print_once: %d\n"), prix, _print_once);
		_tprintf(_T("%s_thread_id: %u\n"), prix, _thread_id);
	}

private:
	std::mutex		_mutex;

	BOOL			_force_exit;
	BOOL			_print_once;
	thread_id_t		_thread_id;
};

class UnmanagedThreadCtrlBlock : public ThreadCtrlBlock
{
public:
	UnmanagedThreadCtrlBlock(const thread_id_t& thread_id = 0) : _force_exit(FALSE), _print_once(FALSE), _thread_id(thread_id)
	{
	}
	virtual ~UnmanagedThreadCtrlBlock()
	{
	}
	virtual BOOL	IsWaitExit()
	{
		return _force_exit;
	}
	virtual void	SetWaitExit(const BOOL& enable)
	{
		_force_exit = enable;
	}
	virtual void	Print(LPCTSTR prix)
	{
		_tprintf(_T("%s_force_exit: %d\n"), prix, _force_exit);
		_tprintf(_T("%s_print_once: %d\n"), prix, _print_once);
		_tprintf(_T("%s_thread_id: %u\n"), prix, _thread_id);
	}

private:
	BOOL			_force_exit;
	BOOL			_print_once;
	thread_id_t		_thread_id;
};

struct StrCaseCmp
{
	bool operator() (const std::wstring& lhs, const std::wstring& rhs) const
	{
		return _tcsicmp(lhs.c_str(), rhs.c_str()) < 0;
	}
};

task_cls_t GetStdCls(const task_cls_t& cls);

class CTimeWheelSheduler
{
public:
	CTimeWheelSheduler();
	virtual ~CTimeWheelSheduler();

	timer_id_t	AllocNewTimer();
	BOOL	SetWorkTimer(const timer_id_t& tid, const UINT32& interval, const timer_sinkfunc_t& cb, BOOL immediate = FALSE);//immediate表示立即触发
	void	KillWorkTimer(const timer_id_t& tid);
	BOOL	ExistWorkTimer(const timer_id_t& tid);
	UINT32	Trigger(std::shared_ptr<ThreadCtrlBlock> tcb);

private:
	UINT64					_seq_counter;
	timer_id_t				_id_counter;

	struct TimerItem
	{
		UINT64				trigger_clock;
		UINT64				seq;
		timer_sinkfunc_t	cb;
		UINT32				interval;
		TimerItem(const UINT64& trigger_clock_, const UINT64& seq_, const timer_sinkfunc_t& cb_ = nullptr, const UINT32& interval_ = -1) : trigger_clock(trigger_clock_), seq(seq_), cb(cb_), interval(interval_)
		{
		}
		TimerItem() : trigger_clock(0), seq(0), cb(nullptr), interval(-1)
		{
		}
		bool operator < (const TimerItem& a) const
		{
			if (trigger_clock != a.trigger_clock)
			{
				return trigger_clock < a.trigger_clock;
			}
			return seq < a.seq;
		}
		bool operator > (const TimerItem& a) const
		{
			if (trigger_clock != a.trigger_clock)
			{
				return trigger_clock > a.trigger_clock;
			}
			return seq > a.seq;
		}
	};

	BOOL	AddTimerHelper(const timer_id_t& tid, const UINT32& interval, const timer_sinkfunc_t& cb, const UINT64& trigger_clock);
	BOOL	DelTimerHelper(const timer_id_t& tid);

	std::map<timer_id_t, TimerItem>	_timer_wheel_forward;
	std::map<TimerItem, timer_id_t>	_timer_wheel_inverte;
};

class CIdleSheduler
{
public:
	CIdleSheduler();
	virtual ~CIdleSheduler();

	timer_id_t	AllocNewIdle();
	BOOL	SetIdle(const idle_id_t& iid, const idle_sinkfunc_t& cb);
	void	KillIdle(const idle_id_t& iid);
	BOOL	ExistIdle(const idle_id_t& iid);
	UINT32	Trigger(std::shared_ptr<ThreadCtrlBlock> tcb);

private:
	UINT64					_seq_counter;
	idle_id_t				_id_counter;
	
	std::map<idle_id_t, UINT64>			_idle_seq;
	std::map<UINT64, idle_sinkfunc_t>	_idle_cb;
};

struct task_msgline_t
{
	INT64			stamp;
	task_id_t		sender_id;
	task_id_t		receiver_id;
	task_cmd_t		cmd;
	task_data_t		data;
};

class task_msgdepot_t
{
public:
	task_msgdepot_t();
	virtual ~task_msgdepot_t();
	void	SetTimeout(const UINT32& unhandle_msg_timeout);
	void	Enable(const BOOL& enable);
	BOOL	IsEnable();
	TaskErrorCode	Append(const task_msgline_t& line);
	TaskErrorCode	FetchList(const BOOL& ignore_enable, std::vector<task_msgline_t>& ar);//ignore_enable表示无视是否enable
	void	Print(LPCTSTR prix);

private:
	std::mutex		_mutex;
	BOOL			_enable;
	UINT32			_unhandle_msg_timeout;
	std::vector<task_msgline_t> _ar;
};

class CThreadLocalProxy
{
public:
	CThreadLocalProxy();
	virtual ~CThreadLocalProxy();

	task_id_t		CreateIfInvalid();
	void			DeleteIfValid();
	TaskErrorCode	RegDefaultMsgSink(const task_sinkfunc_t& sinkfunc, const task_data_t& userdata);	//没有接收器的消息进入垃圾箱
	TaskErrorCode	UnregDefaultMsgSink();
	TaskErrorCode	RegMsgSink(const task_cmd_t& cmd, const task_sinkfunc_t& sinkfunc, const task_data_t& userdata);
	TaskErrorCode	UnregMsgSink(const task_cmd_t& cmd);
	TaskErrorCode	PostMsg(const task_id_t& receiver_task_id, const task_cmd_t& cmd, task_data_t data, const task_flag_t& flags = task_flag_null);//target_id为0表示广播
	TaskErrorCode	DispatchMsg(size_t& count);
	task_id_t		GetCurrentTaskID();
	//调用Reset的为Manager/托管任务，否则为非托管任务
	void			Reset(const task_id_t&	id, std::shared_ptr<ThreadCtrlBlock> tcb);
	std::shared_ptr<ThreadCtrlBlock>	GetThreadCtrlBlock();
	std::shared_ptr<CTimeWheelSheduler>	GetTimeWheelSheduler();
	std::shared_ptr<CIdleSheduler>		GetIdleSheduler();
	
private:
	task_id_t		_id;

	struct msgsink_pair
	{
		task_sinkfunc_t		sinkfunc;
		task_data_t	userdata;

		msgsink_pair(const task_sinkfunc_t& sinkfunc_ = nullptr, const task_data_t& userdata_ = nullptr) : sinkfunc(sinkfunc_), userdata(userdata_)
		{
		}
		void Reset()
		{
			sinkfunc = nullptr;
			userdata = nullptr;
		}
	};

	std::map<task_cmd_t, msgsink_pair>		_cmd_table;
	msgsink_pair							_default_msgpair;
	std::shared_ptr<ThreadCtrlBlock>		_tcb;
	std::shared_ptr<CTimeWheelSheduler>		_tws;
	std::shared_ptr<CIdleSheduler>			_dls;
	BOOL									_managed;

	std::map<task_id_t, std::shared_ptr<task_msgdepot_t>>	_msgchannel_table;	//快捷通道,但不支持通配符
};

void	TlsProxyReset_NoLock(const task_id_t&	id, std::shared_ptr<ThreadCtrlBlock> tcb);

/////////////
