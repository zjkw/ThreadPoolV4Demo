#include "stdafx.h"
#include <assert.h>
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
			ThreadErrorCode tec = tixSetTaskName(_id, name);
			assert(tec == TEC_SUCCEED);
		}
	}
	if (!_tcb)
	{
		_tcb = std::make_shared<UnmanagedThreadCtrlBlock>(GetCurrentThreadId());
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

ThreadErrorCode CThreadLocalProxy::RegRubbishMsgSink(const task_sinkfunc_t& sinkfunc)	//没有接收器的消息进入垃圾箱
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

ThreadErrorCode CThreadLocalProxy::UnregRubbishMsgSink()
{
	_rubbish_sink = nullptr;
	return TEC_SUCCEED;
}

ThreadErrorCode CThreadLocalProxy::RegMsgSink(const task_cmd_t& cmd, const task_sinkfunc_t& sinkfunc)
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

ThreadErrorCode CThreadLocalProxy::UnregMsgSink(const task_cmd_t& cmd)
{
	_cmd_table.erase(cmd);
	return TEC_SUCCEED;
}

ThreadErrorCode CThreadLocalProxy::DispatchMsg(size_t& count)
{
	//发送者的TLS可能还没初始化，应该先建立网络
	task_id_t id = CreateIfInvalid();
	if (id == task_id_null)
	{
		return TEC_ALLOC_FAILED;
	}
	assert(_tcb);

	std::vector<task_msgline_t> ar;
	ThreadErrorCode err = tixFetchMsgList(id, ar);
	if (err != TEC_SUCCEED)
	{
		return err;
	}
	count = 0;
	for (std::vector<task_msgline_t>::iterator it = ar.begin(); it != ar.end(); it++)
	{
		if (_tcb)
		{
			if (_tcb->IsWaitExit())
			{
				break;
			}
		}
		count++;

		std::map<task_cmd_t, task_sinkfunc_t>::iterator it2 = _cmd_table.find(it->cmd);
		if (it2 != _cmd_table.end())
		{
			it2->second(it->sender_id, it->cmd, it->data);
		}
		else
		{
			if (_rubbish_sink)
			{
				_rubbish_sink(it->sender_id, it->cmd, it->data);
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

	task_name_t name;
	ThreadErrorCode tec = tixGetTaskName(_id, name);
	if (tec == TEC_SUCCEED)
	{
		assert(!name.empty());
		if (name.empty())
		{
			TCHAR sz[64];
			_sntprintf_s(sz, _countof(sz), _T("%lld"), _id);
			tec = tixSetTaskName(_id, sz);
			assert(tec == TEC_SUCCEED);
		}
	}
	else if (tec == TEC_NOT_EXIST)
	{
		TCHAR sz[64];
		_sntprintf_s(sz, _countof(sz), _T("%lld"), _id);
		tec = tixSetTaskName(_id, sz);
		assert(tec == TEC_SUCCEED);
	}
	else
	{
		assert(false);
	}
}

std::shared_ptr<ThreadCtrlBlock>	CThreadLocalProxy::GetThreadCtrlBlock()
{
	return _tcb;
}

thread_local CThreadLocalProxy	_tls_proxy;

//只有当前线程会使用到，所有无需加锁

////////////////////////
//针对发送者，允许自己给自己发
ThreadErrorCode ThreadPoolV4::PostMsg(const task_id_t& target_id, const task_cmd_t& cmd, task_data_t data, const task_flag_t& flags/* = task_flag_null*/, const task_echofunc_t& echofunc/* = nullptr*/)//target_id为0表示广播
{
	//参数检查

	//发送者的TLS可能还没初始化，应该先建立网络
	task_id_t id = _tls_proxy.CreateIfInvalid();
	if (id == task_id_null)
	{
		return TEC_ALLOC_FAILED;
	}

	//执行真正发送操作
	ThreadErrorCode tec = tixPostMsg(id, target_id, cmd, data, flags);
	if (echofunc)
	{
		echofunc(target_id, cmd, data, tec);
	}
	return tec;
}

ThreadErrorCode ThreadPoolV4::PostMsg(const task_name_t& target_name, const task_cmd_t& cmd, task_data_t data, const task_flag_t& flags/* = task_flag_null*/, const task_echofunc_t& echofunc/* = nullptr*/)//target_id为0表示广播
{
	//参数检查

	//发送者的TLS可能还没初始化，应该先建立网络
	task_id_t id = _tls_proxy.CreateIfInvalid();
	if (id == task_id_null)
	{
		return TEC_ALLOC_FAILED;
	}

	task_id_t target_id = task_id_null;
	ThreadErrorCode tec = GetTaskByName(target_name, target_id);
	if (tec == TEC_SUCCEED)
	{
		//执行真正发送操作
		tec = tixPostMsg(id, target_id, cmd, data, flags);
		if (echofunc)
		{
			echofunc(target_id, cmd, data, tec);
		}
	}

	return tec;
}
//针对接收者
ThreadErrorCode ThreadPoolV4::RegRubbishMsgSink(const task_sinkfunc_t& sinkfunc)	//没有接收器的消息进入垃圾箱
{
	return _tls_proxy.RegRubbishMsgSink(sinkfunc);
}

ThreadErrorCode ThreadPoolV4::UnregRubbishMsgSink()
{
	return _tls_proxy.UnregRubbishMsgSink();
}

ThreadErrorCode ThreadPoolV4::RegMsgSink(const task_cmd_t& cmd, const task_sinkfunc_t& sinkfunc)
{
	return _tls_proxy.RegMsgSink(cmd, sinkfunc);
}

ThreadErrorCode ThreadPoolV4::UnregMsgSink(const task_cmd_t& cmd)
{
	return _tls_proxy.UnregMsgSink(cmd);
}

ThreadErrorCode ThreadPoolV4::DispatchMsg(size_t& count)
{
	return 	_tls_proxy.DispatchMsg(count);
}

//线程池
void			ThreadPoolV4::SetClsAttri(const task_cls_t& cls, const UINT16& thread_num, const UINT32& unhandle_msg_timeout)//调节线程数量
{
	tixSetClsAttri(cls, thread_num, unhandle_msg_timeout);
}

ThreadErrorCode ThreadPoolV4::ClearManagedTask()//将会强制同步等待池中所有线程关闭
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

ThreadErrorCode ThreadPoolV4::SetCurrentName(const task_name_t& name)//因为托管线程会在AddManagedTask提供名字能力，这里也顺便给非托管线程一个机会：增加别名以便查询
{
	task_id_t id = _tls_proxy.CreateIfInvalid();
	if (id == task_id_null)
	{
		return TEC_ALLOC_FAILED;
	}
	return 	tixSetTaskName(id, name);
}

ThreadErrorCode ThreadPoolV4::RunBaseLoop()
{
	task_id_t id = _tls_proxy.CreateIfInvalid();
	if (id == task_id_null)
	{
		return TEC_ALLOC_FAILED;
	}
	std::shared_ptr<ThreadCtrlBlock>	tcb = _tls_proxy.GetThreadCtrlBlock();

	while (!tcb->IsWaitExit())
	{
		size_t count = 0;
		if (DispatchMsg(count) == TEC_SUCCEED)
		{
			if (count)
			{
				Sleep(10);
			}
		}
		else
		{
			Sleep(10);
		}
	}

	return TEC_SUCCEED;
}

ThreadErrorCode ThreadPoolV4::RunWinLoop()
{
	task_id_t id = _tls_proxy.CreateIfInvalid();
	if (id == task_id_null)
	{
		return TEC_ALLOC_FAILED;
	}
	std::shared_ptr<ThreadCtrlBlock>	tcb = _tls_proxy.GetThreadCtrlBlock();

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
		
		if(!has_business)
		{
			Sleep(10);
		}
	}

	return TEC_SUCCEED;
}

ThreadErrorCode ThreadPoolV4::ExitLoop()
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

//任务管理
ThreadErrorCode ThreadPoolV4::AddManagedTask(const task_cls_t& cls, const task_name_t& name, const task_param_t& param, const task_routinefunc_t& routine, task_id_t& id)	//调节任务
{
	return 	tixAddManagedTask(cls, name, param, routine, id);
}
ThreadErrorCode ThreadPoolV4::DelManagedTask(const task_id_t& id)//是否等待目标关闭，需要明确的是如果自己关闭自己或关闭非托管线程，将会是强制改成异步
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

ThreadErrorCode	ThreadPoolV4::GetTaskState(const task_id_t& id, ThreadTaskState& state)
{
	return 	tixGetTaskState(id, state);
}

ThreadErrorCode ThreadPoolV4::GetTaskByName(const task_name_t& name, task_id_t& id)
{
	return 	tixGetTaskByName(name, id);
}

//Debug
ThreadErrorCode ThreadPoolV4::PrintMeta(const task_id_t& id/* = task_id_self*/)
{
	if (id == task_id_self)
	{
		task_id_t id_self = _tls_proxy.CreateIfInvalid();
		if (id_self == task_id_null)
		{
			return TEC_ALLOC_FAILED;
		}
		return tixPrintMeta(id_self);
	}
	else
	{
		return tixPrintMeta(id);
	}
}
