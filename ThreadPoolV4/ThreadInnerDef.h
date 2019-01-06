#pragma once

#include <map>
#include "ThreadPoolV4.h"

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
	virtual void	SetWaitExit() = 0;
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
	virtual void	SetWaitExit()
	{
		std::unique_lock <std::mutex> lck(_mutex);
		_force_exit = TRUE;
	}
	virtual void	Print(LPCTSTR prix)
	{
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
	virtual void	SetWaitExit()
	{
		_force_exit = TRUE;
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

class CThreadLocalProxy
{
public:
	CThreadLocalProxy();
	virtual ~CThreadLocalProxy();

	task_id_t	CreateIfInvalid();
	void	DeleteIfValid();
	TaskErrorCode RegRubbishMsgSink(const task_sinkfunc_t& sinkfunc);	//没有接收器的消息进入垃圾箱
	TaskErrorCode UnregRubbishMsgSink();
	TaskErrorCode RegMsgSink(const task_cmd_t& cmd, const task_sinkfunc_t& sinkfunc);
	TaskErrorCode UnregMsgSink(const task_cmd_t& cmd);
	TaskErrorCode DispatchMsg(size_t& count);
	task_id_t		GetCurrentTaskID();
	//调用Reset的为Manager/托管任务，否则为非托管任务
	void			Reset(const task_id_t&	id, std::shared_ptr<ThreadCtrlBlock> tcb);
	std::shared_ptr<ThreadCtrlBlock>	GetThreadCtrlBlock();
	std::shared_ptr<CTimeWheelSheduler>	GetTimeWheelSheduler();
	std::shared_ptr<CIdleSheduler>		GetIdleSheduler();
	
private:
	task_id_t		_id;

	std::map<task_cmd_t, task_sinkfunc_t>	_cmd_table;
	task_sinkfunc_t							_rubbish_sink;
	std::shared_ptr<ThreadCtrlBlock>		_tcb;
	std::shared_ptr<CTimeWheelSheduler>		_tws;
	std::shared_ptr<CIdleSheduler>			_dls;
	BOOL									_managed;
};

void	TlsProxyReset_NoLock(const task_id_t&	id, std::shared_ptr<ThreadCtrlBlock> tcb);

struct task_msgline_t
{
	INT64			stamp;
	task_id_t		sender_id;
	task_id_t		receiver_id;
	task_cmd_t		cmd;
	task_data_t		data;
};

/////////////
