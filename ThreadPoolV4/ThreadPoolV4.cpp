#include "stdafx.h"
#include <atlbase.h>
#include <map>
#include "ThreadInnerDef.h"
#include "ThreadPoolV4Imp.h"
#include "ThreadPoolV4.h"

CThreadLocalProxy::CThreadLocalProxy() : _id(task_id_null), _rubbish_sink(nullptr), _managed(FALSE)
{
}

CThreadLocalProxy::~CThreadLocalProxy()
{
	DeleteIfValid();
}

task_id_t	CThreadLocalProxy::CreateIfInvalid()
{
	if (_id == task_id_null)
	{
		_id = tixAllocTaskID(task_cls_default);
		if (_id != task_id_null)
		{
			TCHAR name[64];
			_sntprintf_s(name, _countof(name), _T("%lld"), _id);
			TaskErrorCode tec = tixSetTaskName(_id, name);
			ATLASSERT(tec == TEC_SUCCEED);
		}
	}
	if (!_tcb)
	{
		_tcb = std::make_shared<UnmanagedThreadCtrlBlock>(GetCurrentThreadId());
	}
	if (!_tws)
	{
		_tws = std::make_shared<CTimeWheelSheduler>();
	}
	if (!_dls)
	{
		_dls = std::make_shared<CIdleSheduler>();
	}

	return _id;
}

void	CThreadLocalProxy::DeleteIfValid()
{
	if (_id != task_id_null)
	{
		tixDeleteUnmanagedTask(_id);
	}
}

TaskErrorCode CThreadLocalProxy::RegRubbishMsgSink(const task_sinkfunc_t& sinkfunc)	//没有接收器的消息进入垃圾箱
{
	//发送者的TLS可能还没初始化，应该先建立网络
	task_id_t id = CreateIfInvalid();
	if (id == task_id_null)
	{
		return TEC_ALLOC_FAILED;
	}
	_rubbish_sink = sinkfunc;
	return TEC_SUCCEED;
}

TaskErrorCode CThreadLocalProxy::UnregRubbishMsgSink()
{
	_rubbish_sink = nullptr;
	return TEC_SUCCEED;
}

TaskErrorCode CThreadLocalProxy::RegMsgSink(const task_cmd_t& cmd, const task_sinkfunc_t& sinkfunc)
{
	//发送者的TLS可能还没初始化，应该先建立网络
	task_id_t id = CreateIfInvalid();
	if (id == task_id_null)
	{
		return TEC_ALLOC_FAILED;
	}
	_cmd_table[cmd] = sinkfunc;
	return TEC_SUCCEED;
}

TaskErrorCode CThreadLocalProxy::UnregMsgSink(const task_cmd_t& cmd)
{
	_cmd_table.erase(cmd);
	return TEC_SUCCEED;
}

TaskErrorCode CThreadLocalProxy::DispatchMsg(size_t& count)
{
	//发送者的TLS可能还没初始化，应该先建立网络
	task_id_t id = CreateIfInvalid();
	if (id == task_id_null)
	{
		return TEC_ALLOC_FAILED;
	}
	ATLASSERT(_tcb);

	std::vector<task_msgline_t> ar;
	TaskErrorCode err = TEC_FAILED;

	std::map<task_id_t, std::shared_ptr<task_msgdepot_t>>::iterator it = _msgchannel_table.find(id);	//快捷通道
	if (it != _msgchannel_table.end())
	{
		err = it->second->FetchList(TRUE, ar);
	}
	else
	{
		std::shared_ptr<task_msgdepot_t>	depot = nullptr;
		err = tixFetchMsgList(id, depot);
		if (err == TEC_SUCCEED)
		{
			ATLASSERT(depot);
			_msgchannel_table[id] = depot;
			err = depot->FetchList(TRUE, ar);
		}
	}

	if (err != TEC_SUCCEED)
	{
		return err;
	}
	count = 0;
	for (std::vector<task_msgline_t>::iterator it2 = ar.begin(); it2 != ar.end(); it2++)
	{
		if (_tcb)
		{
			if (_tcb->IsWaitExit())
			{
				break;
			}
		}
		count++;

		std::map<task_cmd_t, task_sinkfunc_t>::iterator it3 = _cmd_table.find(it2->cmd);
		if (it3 != _cmd_table.end())
		{
			it3->second(it2->sender_id, it2->cmd, it2->data);
		}
		else
		{
			if (_rubbish_sink)
			{
				_rubbish_sink(it2->sender_id, it2->cmd, it2->data);
			}
		}
	}
	return err;
}

task_id_t CThreadLocalProxy::GetCurrentTaskID()
{
	return CreateIfInvalid();
}

void	CThreadLocalProxy::Reset(const task_id_t& id, std::shared_ptr<ThreadCtrlBlock> tcb)
{
	_id = id;
	_cmd_table.clear();
	_rubbish_sink = nullptr;
	_tcb = tcb;
	_managed = TRUE;

	_tws = std::make_shared<CTimeWheelSheduler>();
	_dls = std::make_shared<CIdleSheduler>();

	task_name_t name;
	TaskErrorCode tec = tixGetTaskName(_id, name);
	if (tec == TEC_SUCCEED)
	{
		ATLASSERT(!name.empty());
		if (name.empty())
		{
			TCHAR sz[64];
			_sntprintf_s(sz, _countof(sz), _T("%lld"), _id);
			tec = tixSetTaskName(_id, sz);
			ATLASSERT(tec == TEC_SUCCEED);
		}
	}
	else if (tec == TEC_NOT_EXIST)
	{
		TCHAR sz[64];
		_sntprintf_s(sz, _countof(sz), _T("%lld"), _id);
		tec = tixSetTaskName(_id, sz);
		ATLASSERT(tec == TEC_SUCCEED);
	}
	else
	{
		ATLASSERT(FALSE);
	}
}

std::shared_ptr<ThreadCtrlBlock>	CThreadLocalProxy::GetThreadCtrlBlock()
{
	return _tcb;
}

std::shared_ptr<CTimeWheelSheduler>	CThreadLocalProxy::GetTimeWheelSheduler()
{
	return _tws;
}

std::shared_ptr<CIdleSheduler>		CThreadLocalProxy::GetIdleSheduler()
{
	return _dls;
}

TaskErrorCode CThreadLocalProxy::PostMsg(const task_id_t& receiver_task_id, const task_cmd_t& cmd, task_data_t data, const task_flag_t& flags/* = task_flag_null*/)//target_id为0表示广播
{
	//参数检查

	//发送者的TLS可能还没初始化，应该先建立网络
	task_id_t id = CreateIfInvalid();
	if (id == task_id_null)
	{
		return TEC_ALLOC_FAILED;
	}

	//现在是否有channel登记
	if (IsSingleTaskID(receiver_task_id))
	{
		std::map<task_id_t, std::shared_ptr<task_msgdepot_t>>::iterator it = _msgchannel_table.find(receiver_task_id);	//快捷通道
		if (it != _msgchannel_table.end())
		{
			task_msgline_t	line;
			line.stamp = GetTickCount64();
			line.sender_id = id;
			line.receiver_id = receiver_task_id;
			line.cmd = cmd;
			line.data = data;

			return it->second->Append(line);
		}
	}

	//执行真正发送操作	
	std::shared_ptr<task_msgdepot_t> channel = nullptr;
	TaskErrorCode tec = tixPostMsg(id, receiver_task_id, cmd, data, channel, flags);
	if (tec == TEC_SUCCEED && IsSingleTaskID(receiver_task_id))
	{
		ATLASSERT(_msgchannel_table.find(receiver_task_id) == _msgchannel_table.end());
		ATLASSERT(channel);
		_msgchannel_table[receiver_task_id] = channel;
	}
	return tec;
}

thread_local CThreadLocalProxy	_tls_proxy;

//只有当前线程会使用到，所有无需加锁

////////////////////////
//针对发送者，允许自己给自己发
TaskErrorCode ThreadPoolV4::PostMsg(const task_id_t& receiver_task_id, const task_cmd_t& cmd, task_data_t data, const task_flag_t& flags/* = task_flag_null*/, const task_echofunc_t& echofunc/* = nullptr*/)//target_id为0表示广播
{
	//参数检查

	//发送者的TLS可能还没初始化，应该先建立网络
	//执行真正发送操作
	TaskErrorCode tec = _tls_proxy.PostMsg(receiver_task_id, cmd, data, flags);
	if (echofunc)
	{
		echofunc(_tls_proxy.GetCurrentTaskID(), cmd, data, tec);
	}
	return tec;
}

TaskErrorCode ThreadPoolV4::PostMsg(const task_name_t& receiver_task_name, const task_cmd_t& cmd, task_data_t data, const task_flag_t& flags/* = task_flag_null*/, const task_echofunc_t& echofunc/* = nullptr*/)//target_id为0表示广播
{
	//参数检查

	//发送者的TLS可能还没初始化，应该先建立网络
	task_id_t receiver_task_id = task_id_null;
	TaskErrorCode tec = GetTaskByName(receiver_task_name, receiver_task_id);
	if (tec == TEC_SUCCEED)
	{
		//执行真正发送操作
		tec = _tls_proxy.PostMsg(receiver_task_id, cmd, data, flags);
		if (echofunc)
		{
			echofunc(_tls_proxy.GetCurrentTaskID(), cmd, data, tec);
		}
	}

	return tec;
}
//针对接收者
TaskErrorCode ThreadPoolV4::RegRubbishMsgSink(const task_sinkfunc_t& sinkfunc)	//没有接收器的消息进入垃圾箱
{
	return _tls_proxy.RegRubbishMsgSink(sinkfunc);
}

TaskErrorCode ThreadPoolV4::UnregRubbishMsgSink()
{
	return _tls_proxy.UnregRubbishMsgSink();
}

TaskErrorCode ThreadPoolV4::RegMsgSink(const task_cmd_t& cmd, const task_sinkfunc_t& sinkfunc)
{
	return _tls_proxy.RegMsgSink(cmd, sinkfunc);
}

TaskErrorCode ThreadPoolV4::UnregMsgSink(const task_cmd_t& cmd)
{
	return _tls_proxy.UnregMsgSink(cmd);
}

TaskErrorCode ThreadPoolV4::DispatchMsg(size_t& count)
{
	return 	_tls_proxy.DispatchMsg(count);
}

//线程池
void			ThreadPoolV4::SetManagedClsAttri(const task_cls_t& cls, const UINT16& thread_num, const UINT32& unhandle_msg_timeout)//调节线程数量
{
	tixSetClsAttri(cls, thread_num, unhandle_msg_timeout);
}

TaskErrorCode ThreadPoolV4::ClearManagedTask()//将会强制同步等待池中所有线程关闭
{
	task_id_t id = _tls_proxy.CreateIfInvalid();
	if (id == task_id_null)
	{
		return TEC_ALLOC_FAILED;
	}
	return tixClearManagedTask(id);
}

task_id_t ThreadPoolV4::GetCurrentTaskID()
{
	return 	_tls_proxy.GetCurrentTaskID();
}

void	TlsProxyReset_NoLock(const task_id_t&	id, std::shared_ptr<ThreadCtrlBlock> tcb)
{
	//非锁环境下随便调
	_tls_proxy.Reset(id, tcb);
}

TaskErrorCode ThreadPoolV4::SetCurrentName(const task_name_t& name)//因为托管线程会在AddManagedTask提供名字能力，这里也顺便给非托管线程一个机会：增加别名以便查询
{
	task_id_t id = _tls_proxy.CreateIfInvalid();
	if (id == task_id_null)
	{
		return TEC_ALLOC_FAILED;
	}
	return 	tixSetTaskName(id, name);
}

TaskErrorCode ThreadPoolV4::RunBaseLoop()
{
	task_id_t id = _tls_proxy.CreateIfInvalid();
	if (id == task_id_null)
	{
		return TEC_ALLOC_FAILED;
	}
	std::shared_ptr<ThreadCtrlBlock>	tcb = _tls_proxy.GetThreadCtrlBlock();
	std::shared_ptr<CTimeWheelSheduler>	tws = _tls_proxy.GetTimeWheelSheduler();
	std::shared_ptr<CIdleSheduler>		dls = _tls_proxy.GetIdleSheduler();
	
	while (!tcb->IsWaitExit())
	{
		BOOL has_business = FALSE;

		size_t count = 0;
		if (DispatchMsg(count) == TEC_SUCCEED)
		{
			if (count)
			{
				has_business = TRUE;
			}
		}
		
		if (!has_business)
		{
			UINT32 timer_count = tws->Trigger(tcb);
			if (!timer_count)
			{
				UINT32 idle_count = dls->Trigger(tcb);
				if (!idle_count)
				{
					Sleep(10);
				}
			}
		}
	}

	return TEC_SUCCEED;
}

TaskErrorCode ThreadPoolV4::RunWinLoop()
{
	task_id_t id = _tls_proxy.CreateIfInvalid();
	if (id == task_id_null)
	{
		return TEC_ALLOC_FAILED;
	}
	std::shared_ptr<ThreadCtrlBlock>	tcb = _tls_proxy.GetThreadCtrlBlock();
	std::shared_ptr<CTimeWheelSheduler>	tws = _tls_proxy.GetTimeWheelSheduler();
	std::shared_ptr<CIdleSheduler>		dls = _tls_proxy.GetIdleSheduler();

	while (!tcb->IsWaitExit())
	{
		BOOL has_business = FALSE;

		MSG msg;
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
			{
				break;
			}

			TranslateMessage(&msg);
			DispatchMessage(&msg);

			has_business = TRUE;

			//			Util::Log::Info(_T("CThreadWorkBase"), _T("RunLoop(%u), Msg: %u"), tid, msg.message);
		}

		size_t count = 0;
		if (DispatchMsg(count) == TEC_SUCCEED)
		{
			has_business = TRUE;
		}
		
		if (!has_business)
		{
			UINT32 timer_count = tws->Trigger(tcb);
			if (!timer_count)
			{
				UINT32 idle_count = dls->Trigger(tcb);
				if (!idle_count)
				{
					Sleep(10);
				}
			}
		}
	}

	return TEC_SUCCEED;
}

TaskErrorCode ThreadPoolV4::SetExitLoop()
{
	task_id_t id = _tls_proxy.CreateIfInvalid();
	if (id == task_id_null)
	{
		return TEC_ALLOC_FAILED;
	}
	std::shared_ptr<ThreadCtrlBlock>	tcb = _tls_proxy.GetThreadCtrlBlock();

	tcb->SetWaitExit();

	return TEC_SUCCEED;
}

TaskErrorCode ThreadPoolV4::IsExitLoop(BOOL& enable)
{
	task_id_t id = _tls_proxy.CreateIfInvalid();
	if (id == task_id_null)
	{
		return TEC_ALLOC_FAILED;
	}
	std::shared_ptr<ThreadCtrlBlock>	tcb = _tls_proxy.GetThreadCtrlBlock();

	enable = tcb->IsWaitExit();

	return TEC_SUCCEED;
}

//任务管理
TaskErrorCode ThreadPoolV4::AddManagedTask(const task_cls_t& cls, const task_name_t& name, const task_param_t& param, const task_routinefunc_t& routine, task_id_t& id)	//调节任务
{
	return 	tixAddManagedTask(cls, name, param, routine, id);
}

TaskErrorCode ThreadPoolV4::DelManagedTask(const task_id_t& id)//是否等待目标关闭，需要明确的是如果自己关闭自己或关闭非托管线程，将会是强制改成异步
{
	//参数检查

	//发送者的TLS可能还没初始化，应该先建立网络
	task_id_t call_id = _tls_proxy.CreateIfInvalid();
	if (call_id == task_id_null)
	{
		return TEC_ALLOC_FAILED;
	}

	return 	tixDelManagedTask(call_id, id);
}

TaskErrorCode	ThreadPoolV4::GetTaskState(const task_id_t& id, TaskWorkState& state)
{
	return 	tixGetTaskState(id, state);
}

TaskErrorCode ThreadPoolV4::GetTaskByName(const task_name_t& name, task_id_t& id)
{
	return 	tixGetTaskByName(name, id);
}

//---------定时器和IDLE----------
TaskErrorCode	ThreadPoolV4::AllocTimer(timer_id_t& tid)
{
	task_id_t call_id = _tls_proxy.CreateIfInvalid();
	if (call_id == task_id_null)
	{
		return TEC_ALLOC_FAILED;
	}

	std::shared_ptr<CTimeWheelSheduler>	tws = _tls_proxy.GetTimeWheelSheduler();
	tid = tws->AllocNewTimer();

	return TEC_SUCCEED;
}

TaskErrorCode	ThreadPoolV4::StartTimer(const timer_id_t& tid, const UINT32& millisecond, const timer_sinkfunc_t& cb, BOOL immediate/* = FALSE*/)
{
	task_id_t call_id = _tls_proxy.CreateIfInvalid();
	if (call_id == task_id_null)
	{
		return TEC_ALLOC_FAILED;
	}

	std::shared_ptr<CTimeWheelSheduler>	tws = _tls_proxy.GetTimeWheelSheduler();
	BOOL ret = tws->SetWorkTimer(tid, millisecond, cb, immediate);

	return ret ? TEC_SUCCEED : TEC_FAILED;
}

TaskErrorCode	ThreadPoolV4::StopTimer(const timer_id_t& tid)
{
	task_id_t call_id = _tls_proxy.CreateIfInvalid();
	if (call_id == task_id_null)
	{
		return TEC_ALLOC_FAILED;
	}

	std::shared_ptr<CTimeWheelSheduler>	tws = _tls_proxy.GetTimeWheelSheduler();
	tws->KillWorkTimer(tid);

	return TEC_SUCCEED;
}

TaskErrorCode	ThreadPoolV4::ExistTimer(const timer_id_t& tid, BOOL& exist)
{
	task_id_t call_id = _tls_proxy.CreateIfInvalid();
	if (call_id == task_id_null)
	{
		return TEC_ALLOC_FAILED;
	}

	std::shared_ptr<CTimeWheelSheduler>	tws = _tls_proxy.GetTimeWheelSheduler();
	exist = tws->ExistWorkTimer(tid);

	return TEC_SUCCEED;
}

TaskErrorCode	ThreadPoolV4::AllocIdle(idle_id_t& iid)
{
	task_id_t call_id = _tls_proxy.CreateIfInvalid();
	if (call_id == task_id_null)
	{
		return TEC_ALLOC_FAILED;
	}

	std::shared_ptr<CIdleSheduler>	dls = _tls_proxy.GetIdleSheduler();
	iid = dls->AllocNewIdle();

	return TEC_SUCCEED;
}

TaskErrorCode	ThreadPoolV4::StartIdle(const idle_id_t& iid, const idle_sinkfunc_t& cb)
{
	task_id_t call_id = _tls_proxy.CreateIfInvalid();
	if (call_id == task_id_null)
	{
		return TEC_ALLOC_FAILED;
	}

	std::shared_ptr<CIdleSheduler>	dls = _tls_proxy.GetIdleSheduler();
	BOOL ret = dls->SetIdle(iid, cb);

	return ret ? TEC_SUCCEED : TEC_FAILED;
}

TaskErrorCode	ThreadPoolV4::StopIdle(const idle_id_t& iid)
{
	task_id_t call_id = _tls_proxy.CreateIfInvalid();
	if (call_id == task_id_null)
	{
		return TEC_ALLOC_FAILED;
	}

	std::shared_ptr<CIdleSheduler>	dls = _tls_proxy.GetIdleSheduler();
	dls->KillIdle(iid);

	return TEC_SUCCEED;
}

TaskErrorCode	ThreadPoolV4::ExistIdle(const idle_id_t& iid, BOOL& exist)
{
	task_id_t call_id = _tls_proxy.CreateIfInvalid();
	if (call_id == task_id_null)
	{
		return TEC_ALLOC_FAILED;
	}

	std::shared_ptr<CIdleSheduler>	dls = _tls_proxy.GetIdleSheduler();
	exist = dls->ExistIdle(iid);

	return TEC_SUCCEED;
}

///////////////////////////////////////////////////////////////
ThreadPoolV4::CTaskTimerHelper::CTaskTimerHelper()
	: _timer_id(0), _cb(nullptr)
{
	_belongs_task_id = GetCurrentTaskID();
}

ThreadPoolV4::CTaskTimerHelper::~CTaskTimerHelper()
{
	ATLASSERT(_belongs_task_id == GetCurrentTaskID());

	Stop();
}

BOOL ThreadPoolV4::CTaskTimerHelper::Start(UINT32 millisecond, BOOL immediate/* = FALSE*/)
{
	ATLASSERT(_belongs_task_id == GetCurrentTaskID());

	if (!_timer_id)
	{
		timer_id_t tid = 0;
		TaskErrorCode	tec = AllocTimer(tid);
		if (tec != TEC_SUCCEED)
		{
			return FALSE;
		}
		_timer_id = tid;
	}

	TaskErrorCode	tec = StartTimer(_timer_id, millisecond, std::bind(&CTaskTimerHelper::OnTimer, this, std::placeholders::_1), immediate);
	if (tec != TEC_SUCCEED)
	{
		return FALSE;
	}

	return TRUE;
}

void ThreadPoolV4::CTaskTimerHelper::Stop()
{
	ATLASSERT(_belongs_task_id == GetCurrentTaskID());

	if (!_timer_id)
	{
		TaskErrorCode	tec = StopTimer(_timer_id);
		if (tec != TEC_SUCCEED)
		{
			return;
		}

		_timer_id = 0;
	}
}

BOOL ThreadPoolV4::CTaskTimerHelper::IsActive()
{
	ATLASSERT(_belongs_task_id == GetCurrentTaskID());

	BOOL exist = FALSE;
	if (!_timer_id)
	{
		TaskErrorCode	tec = ExistTimer(_timer_id, exist);
		if (tec != TEC_SUCCEED)
		{
			return FALSE;
		}
	}

	return exist;
}

void ThreadPoolV4::CTaskTimerHelper::SetCallBack(const timer_sinkfunc_t& cb)
{
	ATLASSERT(_belongs_task_id == GetCurrentTaskID());

	_cb = cb;
}

void ThreadPoolV4::CTaskTimerHelper::OnTimer(const timer_id_t& tid)
{
	ATLASSERT(_belongs_task_id == GetCurrentTaskID());

	ATLASSERT(_cb);
	if (_cb)
	{
		_cb(tid);
	}
}

ThreadPoolV4::CTaskIdleHelper::CTaskIdleHelper()
	: _idle_id(0), _cb(nullptr)
{
	_belongs_task_id = GetCurrentTaskID();
}

ThreadPoolV4::CTaskIdleHelper::~CTaskIdleHelper()
{
	ATLASSERT(_belongs_task_id == GetCurrentTaskID());

	Stop();
}

BOOL ThreadPoolV4::CTaskIdleHelper::Start()
{
	ATLASSERT(_belongs_task_id == GetCurrentTaskID());

	if (!_idle_id)
	{
		idle_id_t iid = 0;
		TaskErrorCode	tec = AllocIdle(iid);
		if (tec != TEC_SUCCEED)
		{
			return FALSE;
		}
		_idle_id = iid;
	}

	TaskErrorCode	tec = StartIdle(_idle_id, std::bind(&CTaskIdleHelper::OnIdle, this, std::placeholders::_1));
	if (tec != TEC_SUCCEED)
	{
		return FALSE;
	}

	return TRUE;
}

void ThreadPoolV4::CTaskIdleHelper::Stop()
{
	ATLASSERT(_belongs_task_id == GetCurrentTaskID());

	if (!_idle_id)
	{
		TaskErrorCode	tec = StopIdle(_idle_id);
		if (tec != TEC_SUCCEED)
		{
			return;
		}

		_idle_id = 0;
	}
}

BOOL ThreadPoolV4::CTaskIdleHelper::IsActive()
{
	ATLASSERT(_belongs_task_id == GetCurrentTaskID());

	BOOL exist = FALSE;
	if (!_idle_id)
	{
		TaskErrorCode	tec = ExistIdle(_idle_id, exist);
		if (tec != TEC_SUCCEED)
		{
			return FALSE;
		}
	}

	return exist;
}

void ThreadPoolV4::CTaskIdleHelper::SetCallBack(const idle_sinkfunc_t& cb)
{
	ATLASSERT(_belongs_task_id == GetCurrentTaskID());

	_cb = cb;
}


void	ThreadPoolV4::CTaskIdleHelper::OnIdle(const idle_id_t& iid)
{
	ATLASSERT(_belongs_task_id == GetCurrentTaskID());

	ATLASSERT(_cb);
	if (_cb)
	{
		_cb(iid);
	}
}

//---------Debug----------
TaskErrorCode ThreadPoolV4::PrintMeta()
{
	return tixPrintMeta();
}
