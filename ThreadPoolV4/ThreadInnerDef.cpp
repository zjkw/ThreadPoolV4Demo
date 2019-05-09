#include "stdafx.h"
#include <atlbase.h>
#include "ThreadInnerDef.h"

task_cls_t GetStdCls(const task_cls_t& cls)
{
	if (cls.empty())
	{
		return task_cls_default;
	}
	else
	{
		return cls;
	}
}


CTimeWheelSheduler::CTimeWheelSheduler()
	: _seq_counter(0), _id_counter(0)
{
}

CTimeWheelSheduler::~CTimeWheelSheduler()
{
}

UINT32	CTimeWheelSheduler::Trigger(std::shared_ptr<ThreadCtrlBlock> tcb)
{
	UINT32	do_count = 0;

	UINT64 now = GetTickCount64();

	//	Util::Log::Info(_T("CTimeWheelSheduler"), _T("Trigger Thread: 0x%x, now: %llu, _seq_counter: %llu"), this, now, _seq_counter);
	//	for (std::map<TimerItem, timer_id_t>::iterator it = _timer_wheel_inverte.begin(); it != _timer_wheel_inverte.end(); it++)
	//	{
	//		Util::Log::Info(_T("CTimeWheelSheduler"), _T("Trigger Thread: 0x%x, trigger_clock: %llu, seq: %llu, interval: %u, timer_id_t: %llu"), this, it->first.trigger_clock, it->first.seq, it->first.interval, it->second);
	//	}

	// 此刻最大的id
	TimerItem	cursor_id;//默认为zero，无需再初始化
	UINT64		old_seq = ++_seq_counter;	//为了避免回调插入导致的循环：tick -> cb -> map::insert
											// 取出满足条件最小的
	while (!tcb->IsWaitExit())	//避免由于ti.cb导致的退出
	{
		std::map<TimerItem, timer_id_t>::iterator it = _timer_wheel_inverte.upper_bound(cursor_id);
		if (it == _timer_wheel_inverte.end())
		{
			//		Util::Log::Info(_T("CTimeWheelSheduler"), _T("Trigger Thread: 0x%x, cursor_id: %llu"), this, cursor_id.trigger_clock);
			break;
		}

		//符合条件的有两种：已经执行过且插队的；未执行过的
		//其中前者需要检查，当seq不符合的，则跳过而不是跳出，给其他人机会；后者即可跳出

		if (it->first.trigger_clock > now)
		{
			break;
		}

		cursor_id = it->first;
		if (it->first.seq < old_seq)
		{
			TimerItem	ti = it->first;
			timer_id_t	tid = it->second;

			ATLASSERT(ti.interval);
			AddTimerHelper(tid, ti.interval, ti.cb, it->first.trigger_clock + ti.interval);

			do_count++;
			ti.cb(tid);
		}
	}

	return do_count;
}

timer_id_t	CTimeWheelSheduler::AllocNewTimer()
{
	return ++_id_counter;
}

BOOL	CTimeWheelSheduler::SetWorkTimer(const timer_id_t& tid, const UINT32& interval, const timer_sinkfunc_t& cb, BOOL immediate/* = FALSE*/)//immediate表示立即触发
{
	if (!tid || !interval)
	{
		ATLASSERT(FALSE);
		return FALSE;
	}

	UINT64	now = GetTickCount64();
	if (!immediate)
	{
		now += interval;
	}
	return AddTimerHelper(tid, interval, cb, now);
}

void	CTimeWheelSheduler::KillWorkTimer(const timer_id_t& tid)
{
	DelTimerHelper(tid);
}

BOOL	CTimeWheelSheduler::ExistWorkTimer(const timer_id_t& tid)
{
	return _timer_wheel_forward.find(tid) != _timer_wheel_forward.end();
}

BOOL	CTimeWheelSheduler::AddTimerHelper(const timer_id_t& tid, const UINT32& interval, const timer_sinkfunc_t& cb, const UINT64& trigger_clock)
{
	DelTimerHelper(tid);

	ATLASSERT(_timer_wheel_forward.find(tid) == _timer_wheel_forward.end());

	TimerItem	ti(trigger_clock, ++_seq_counter, cb, interval);

	_timer_wheel_forward[tid] = ti;
	_timer_wheel_inverte[ti] = tid;

	return TRUE;
}

BOOL	CTimeWheelSheduler::DelTimerHelper(const timer_id_t& tid)
{
	std::map<timer_id_t, TimerItem>::iterator it = _timer_wheel_forward.find(tid);
	if (it != _timer_wheel_forward.end())
	{
		ATLASSERT(_timer_wheel_inverte.find(it->second) != _timer_wheel_inverte.end());
		_timer_wheel_inverte.erase(it->second);
		_timer_wheel_forward.erase(it);
	}

	return TRUE;
}

CIdleSheduler::CIdleSheduler()
	: _seq_counter(0), _id_counter(0)
{
}

CIdleSheduler::~CIdleSheduler()
{
}

timer_id_t	CIdleSheduler::AllocNewIdle()
{
	return ++_id_counter;
}

BOOL	CIdleSheduler::SetIdle(const idle_id_t& iid, const idle_sinkfunc_t& cb)
{
	if (!iid || !cb)
	{
		ATLASSERT(FALSE);
		return FALSE;
	}

	KillIdle(iid);

	_idle_seq[iid] = ++_seq_counter;
	_idle_cb[_seq_counter] = cb;

	return TRUE;
}

void	CIdleSheduler::KillIdle(const idle_id_t& iid)
{
	std::map<idle_id_t, UINT64>::iterator it = _idle_seq.find(iid);
	if (it == _idle_seq.end())
	{
		return;
	}
	_idle_cb.erase(it->second);
	_idle_seq.erase(it);
}

BOOL	CIdleSheduler::ExistIdle(const idle_id_t& iid)
{
	return _idle_seq.find(iid) != _idle_seq.end();
}

UINT32	CIdleSheduler::Trigger(std::shared_ptr<ThreadCtrlBlock> tcb)
{
	UINT32	do_count = 0;
	
	//设置门槛
	UINT64	last_max_seq = _seq_counter;

	UINT64	cursor = 0;
	while (!tcb->IsWaitExit())
	{
		std::map<UINT64, idle_sinkfunc_t>::iterator it = _idle_cb.upper_bound(cursor);
		if (it == _idle_cb.end())
		{
			break;
		}
		if (it->first > last_max_seq)
		{
			break;
		}
		cursor = it->first;

		do_count++;
		it->second(cursor);
	}

	return do_count;
}

task_msgdepot_t::task_msgdepot_t()
	: _enable(TRUE), _unhandle_msg_timeout(DEFAULT_UNHANDLMSG_TIMEOUT_BY_CLS)
{
}

task_msgdepot_t::~task_msgdepot_t()
{
}

void	task_msgdepot_t::SetTimeout(const UINT32& unhandle_msg_timeout)
{
	_unhandle_msg_timeout = unhandle_msg_timeout;
}

void	task_msgdepot_t::Enable(const BOOL& enable)
{
	std::unique_lock <std::mutex> lck(_mutex);
	_enable = enable;
}

BOOL	task_msgdepot_t::IsEnable()
{
	std::unique_lock <std::mutex> lck(_mutex);
	return _enable;
}

TaskErrorCode	task_msgdepot_t::Append(const task_msgline_t& line)
{
	std::unique_lock <std::mutex> lck(_mutex);
	if (!_enable)
	{
		return TEC_NO_RECEIVER;
	}
	_ar.push_back(line);
	return TEC_SUCCEED;
}

TaskErrorCode	task_msgdepot_t::FetchList(const BOOL& ignore_enable, std::vector<task_msgline_t>& ar)
{
	//tbd
	std::unique_lock <std::mutex> lck(_mutex);
	if (!_enable && !ignore_enable)
	{
		return TEC_NO_RECEIVER;
	}
	ar.clear();
	ar.swap(_ar);
	return TEC_SUCCEED;
}

void task_msgdepot_t::Print(LPCTSTR prix)
{
	std::unique_lock <std::mutex> lck(_mutex);
	for (std::vector<task_msgline_t>::iterator it2 = _ar.begin(); it2 != _ar.end(); it2++)
	{
		_tprintf(_T("%ssender_id: %llu\n"), prix, it2->sender_id);
		_tprintf(_T("%sreceiver_id: %llu\n"), prix, it2->receiver_id);
		_tprintf(_T("%sstamp: %lld\n"), prix, it2->stamp);
		_tprintf(_T("%scmd: %u\n"), prix, it2->cmd);
//		_tprintf(_T("%sdata_size: %u\n"), prix, it2->data ? it2->data->size() : 0);
	}
}
