#pragma once

#include <map>
#include "ThreadPoolV4.h"

class ManagedThreadCtrlBlock : public ThreadCtrlBlock
{
public:
	ManagedThreadCtrlBlock(const thread_id_t& thread_id = 0) : ThreadCtrlBlock(thread_id)
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
};

class UnmanagedThreadCtrlBlock : public ThreadCtrlBlock
{
public:
	UnmanagedThreadCtrlBlock(const thread_id_t& thread_id = 0) : ThreadCtrlBlock(thread_id)
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
};

struct StrCaseCmp
{
	bool operator() (const std::wstring& lhs, const std::wstring& rhs) const
	{
		return _tcsicmp(lhs.c_str(), rhs.c_str()) < 0;
	}
};

task_cls_t GetStdCls(const task_cls_t& cls);

class CThreadLocalProxy
{
public:
	CThreadLocalProxy();
	virtual ~CThreadLocalProxy();

	task_id_t	CreateIfInvalid();
	void	DeleteIfValid();
	ThreadErrorCode RegRubbishMsgSink(const task_sinkfunc_t& sinkfunc);	//没有接收器的消息进入垃圾箱
	ThreadErrorCode UnregRubbishMsgSink();
	ThreadErrorCode RegMsgSink(const task_cmd_t& cmd, const task_sinkfunc_t& sinkfunc);
	ThreadErrorCode UnregMsgSink(const task_cmd_t& cmd);
	ThreadErrorCode DispatchMsg(size_t& count);
	task_id_t		GetCurrentTaskID();
	//调用Reset的为Manager/托管任务，否则为非托管任务
	void			Reset(const task_id_t&	id, std::shared_ptr<ThreadCtrlBlock> tcb);
	std::shared_ptr<ThreadCtrlBlock>	GetThreadCtrlBlock();

private:
	task_id_t		_id;

	std::map<task_cmd_t, task_sinkfunc_t>	_cmd_table;
	task_sinkfunc_t							_rubbish_sink;
	std::shared_ptr<ThreadCtrlBlock>		_tcb;
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