#include "stdafx.h"
#include <atlbase.h>
#include <map>
#include "ThreadPoolV4Imp.h"

//包括托管和非托管任务对应的kv结构
struct task_static_t
{
	task_name_t		name;
	std::shared_ptr<task_msgdepot_t>	msgdepot;
};

//托管任务和线程描述结构
struct managed_thread_t
{
	//		std::shared_ptr<std::thread>		thread_obj;
	std::shared_ptr<ThreadCtrlBlock>	thread_ctrlblock;
};
struct managed_task_t
{
	task_cls_t		cls;
	task_param_t	param;
	task_routinefunc_t routine;
	managed_thread_t*		thread_item_ptr;	//当TWS_ACTIVE时候有效，标明是哪个线程

	managed_task_t() : thread_item_ptr(nullptr) {}
};
struct managed_pool_t
{
	UINT16	thread_limit_max_num;
	UINT32	unhandle_msg_timeout;	//暂不处理

	std::map<thread_id_t, managed_thread_t>	threads;
	std::map<task_id_t, managed_task_t>		active_tasks;
	std::map<task_id_t, managed_task_t>		wait_tasks;

	managed_pool_t() : thread_limit_max_num(DEFAULT_THREAD_NUM_BY_CLS), unhandle_msg_timeout(0) {}
};

static std::mutex															_mutex;
static std::condition_variable												_cond;
//静态表(含消息)
static BOOL																	_exit_flag = FALSE;//退出状态标识，设置后表示只能减不能加
static task_id_t															_cursor = task_id_null;
static std::map<task_id_t, task_static_t>									_static_table;
//托管任务/线程池
static std::map<std::wstring, std::shared_ptr<managed_pool_t>, StrCaseCmp>	_managed_cls_table;
static std::map<task_id_t, std::wstring>									_managed_task_index;
static std::map<thread_id_t, std::wstring>									_managed_thread_index;
void		tixDebugViewLocalVariables()
{
	int k = 0;
	k++;
}

///////////////静态表辅助管理
static task_id_t		StaticAllocTaskID_InLock()
{
	return ++_cursor;
}

static void			StaticDeleteTask_InLock(const task_id_t& id)
{
	std::map<task_id_t, task_static_t>::iterator it = _static_table.find(id);
	if (it != _static_table.end())
	{
		//msgdepot可能被多出引用
		if (it->second.msgdepot)
		{
			it->second.msgdepot->Enable(FALSE);
		}

		_static_table.erase(it);
	}	
}

static BOOL			StaticExistTask_InLock(const task_id_t& id)
{
	return _static_table.find(id) != _static_table.end();
}

//Name策略：输入name为空则忽略此参数，但无论如何都会检查本地name，如果为空则填写默认值
static TaskErrorCode StaticUpdateTask_InLock(const task_id_t& id, const task_name_t& name = _T(""), const UINT32& unhandle_msg_timeout = 0)
{
	std::map<task_id_t, task_static_t>::iterator it = _static_table.find(id);
	if (it == _static_table.end())
	{
		task_static_t	st;
		st.msgdepot = std::make_shared<task_msgdepot_t>();
		if (!st.msgdepot)
		{
			return TEC_ALLOC_FAILED;
		}
		
		_static_table[id] = st;
		it = _static_table.find(id);
		ATLASSERT(it != _static_table.end());
	}
	
	if (!name.empty())
	{
		it->second.name = name;
	}
	if (it->second.name.empty())
	{
		TCHAR sz[64];
		_sntprintf_s(sz, _countof(sz), _T("%lld"), id);
		it->second.name = sz;
	}
	
	if (unhandle_msg_timeout)
	{
		it->second.msgdepot->SetTimeout(unhandle_msg_timeout);
	}

	return TEC_SUCCEED;
}

//空的name将会填写一个默认的名字
static TaskErrorCode StaticSetTaskName_InLock(const task_id_t& id, const task_name_t& name)
{
	//名称必须是一个字符串，而不是一个buffer，因为后面的_tcsicmp比较函数是字符串比较
	if (_tcslen(name.c_str()) != name.size())
	{
		return TEC_INVALID_ARG;
	}

	//名称不能与其他已有的冲突
	for (std::map<task_id_t, task_static_t>::iterator it = _static_table.begin(); it != _static_table.end(); it++)
	{
		if (id != it->first)
		{
			if (!_tcsicmp(name.c_str(), it->second.name.c_str()))
			{
				return TEC_NAME_EXIST;
			}
		}
	}

	//名称若为数字，则必须为当前id，避免自动赋名时候占坑
	BOOL	exist_non_digit = FALSE;
	for (task_name_t::const_iterator it = name.cbegin(); it != name.cend(); it++)
	{
		if (*it < _T('0') || *it > _T('9'))
		{
			exist_non_digit = TRUE;
			break;
		}
	}
	if (!exist_non_digit)
	{
		TCHAR sz[64];
		_sntprintf_s(sz, _countof(sz), _T("%lld"), id);
		if (name.empty())
		{
			return StaticUpdateTask_InLock(id, sz);
		}
		else if (_tcsicmp(name.c_str(), sz))
		{
			return TEC_INVALID_ARG;
		}
	}
	
	return StaticUpdateTask_InLock(id, name);
}

static TaskErrorCode StaticGetTaskName_InLock(const task_id_t& id, task_name_t& name)
{
	std::map<task_id_t, task_static_t>::iterator it = _static_table.find(id);
	if (it == _static_table.end())
	{
		return TEC_NOT_EXIST;
	}

	name = it->second.name;
	return TEC_SUCCEED;
}

static TaskErrorCode StaticGetTaskByName_InLock(const task_name_t& name, task_id_t& id)
{
	for (std::map<task_id_t, task_static_t>::iterator it = _static_table.begin(); it != _static_table.end(); it++)
	{
		if (!_tcsicmp(name.c_str(), it->second.name.c_str()))
		{
			id = it->first;
			return TEC_SUCCEED;
		}
	}

	return TEC_NOT_EXIST;
}

///////////////托管任务/线程池辅助管理
static void  ManagedThreadRoutine()
{
	DWORD dwThreadID = GetCurrentThreadId();

	//外部确保：线程存续期内，CLS结构体必然存在
	std::shared_ptr<managed_pool_t>	cls_ptr = nullptr;
	managed_thread_t*	thread_item_ptr = nullptr;
	while(!thread_item_ptr)
	{
		std::unique_lock <std::mutex> lck(_mutex);
		if (_exit_flag)
		{
			break;
		}

		std::map<thread_id_t, std::wstring>::iterator it = _managed_thread_index.find(dwThreadID);
		if (it != _managed_thread_index.end())
		{
			std::map<std::wstring, std::shared_ptr<managed_pool_t>, StrCaseCmp>::iterator it2 = _managed_cls_table.find(it->second);
			if (it2 != _managed_cls_table.end())
			{
				cls_ptr = it2->second;

				std::map<thread_id_t, managed_thread_t>::iterator it3 = cls_ptr->threads.find(dwThreadID);
				if (it3 != cls_ptr->threads.end())
				{
					thread_item_ptr = &it3->second;
				}
			}
		}

		if (!thread_item_ptr)
		{
			_cond.wait(lck);
		}
	}

	if (thread_item_ptr)
	{
		while (true)
		{
			task_id_t	first = 0;
			managed_task_t	second;

			//要求全部线程退出，那么还是退出吧
			{
				std::unique_lock <std::mutex> lck(_mutex);
				if (_exit_flag)
				{
					break;
				}

				//任务先添加，因为线程池的线程退出也依赖任务数，如果任务后面添加可能导致线程数全部退出，任务无线程执行
				size_t task_num = cls_ptr->wait_tasks.size() + cls_ptr->active_tasks.size();
				size_t ask_thread_num = min(task_num, cls_ptr->thread_limit_max_num);

				//从任务队列选择一个运行	
				std::map<task_id_t, managed_task_t>::iterator it = cls_ptr->wait_tasks.begin();
				if (it != cls_ptr->wait_tasks.end())
				{
					first = it->first;
					second = it->second;

					cls_ptr->wait_tasks.erase(it);
					second.thread_item_ptr = thread_item_ptr;
					cls_ptr->active_tasks[first] = second;
				}
				//只要min(任务数，线程数上限阈值) < 当前实际线程数
				else if (ask_thread_num < cls_ptr->threads.size())
				{
					//直接退出
					if (cls_ptr)
					{
						cls_ptr->threads.erase(dwThreadID);
					}
					_managed_thread_index.erase(dwThreadID);
					return;
				}
				else 
				{
					//没有在等待执行的任务
					_cond.wait(lck);
				}	

				if (first)
				{
					StaticUpdateTask_InLock(first, _T(""), cls_ptr->unhandle_msg_timeout);
				}
			}

			if (first)
			{
				//
				TlsProxyReset_NoLock(first, thread_item_ptr->thread_ctrlblock);
				second.routine(first, second.param);

				std::unique_lock <std::mutex> lck(_mutex);
				cls_ptr->active_tasks.erase(first);
				_managed_task_index.erase(first);
				StaticDeleteTask_InLock(first);
			}
		}
	}

	//线程要退出了，清理相关资源
	{
		std::unique_lock <std::mutex> lck(_mutex);
		if (cls_ptr)
		{
			cls_ptr->threads.erase(dwThreadID);
		}
		_managed_thread_index.erase(dwThreadID);
	}

	return;
}

//task_num表示任务数，当为0表示忽略此参数
static BOOL CreatePoolIfNotExist_InLock(std::unique_lock<std::mutex>& lck, const task_cls_t& cls, const size_t& task_num, const UINT16& thread_limit_max_num = DEFAULT_THREAD_NUM_BY_CLS, const UINT32& unhandle_msg_timeout = DEFAULT_UNHANDLMSG_TIMEOUT_BY_CLS)
{
	size_t real_thread_num = min(task_num, thread_limit_max_num);

	//预备创建线程
	size_t exist_thread_num = 0;
	std::map<std::wstring, std::shared_ptr<managed_pool_t>, StrCaseCmp>::iterator it = _managed_cls_table.find(cls);
	if (it != _managed_cls_table.end())
	{
		exist_thread_num = it->second->threads.size();
	}

	//手工解锁："一个我们托管线程 X 退出时候"持有了"操作系统内部线程管理数据结构"锁，而当此时_beginthreadex/其他线程创建函数所在的线程 Y 此刻就会等待此锁，
	//而 X 退出时候会进入CThreadLocalProxy析构，这个析构会通过本库的全局锁_mutex清理相关数据，但此_mutex被已被 Y 拥有，导致 X 和 Y 死锁
	lck.unlock();

	std::vector<std::shared_ptr<std::thread>>	thread_list;
	for (size_t i = exist_thread_num; i < real_thread_num; i++)
	{
		std::shared_ptr<std::thread> thread_obj = std::make_shared<std::thread>(&ManagedThreadRoutine);
		
		thread_list.push_back(thread_obj);
	}

	lck.lock();

	//关联数据结构
	it = _managed_cls_table.find(cls);
	if (it == _managed_cls_table.end())
	{
		_managed_cls_table[cls] = std::make_shared<managed_pool_t>();
		it = _managed_cls_table.find(cls);
		ATLASSERT(it != _managed_cls_table.end());
	}

	for (std::vector<std::shared_ptr<std::thread>>::iterator it2 = thread_list.begin(); it2 != thread_list.end(); it2++)
	{
		managed_thread_t ti;
				
		thread_id_t dwThreadID = GetThreadId((HANDLE)(*it2)->native_handle());
		ti.thread_ctrlblock = std::make_shared<ManagedThreadCtrlBlock>(dwThreadID);
		(*it2)->detach();

		it->second->threads[dwThreadID] = ti;
		_managed_thread_index[dwThreadID] = cls;
	}
	it->second->thread_limit_max_num = thread_limit_max_num;
	it->second->unhandle_msg_timeout = unhandle_msg_timeout;

	_cond.notify_all();

	return TRUE;
}

static TaskErrorCode PoolAddManagedTask_InLock(std::unique_lock<std::mutex>& lck, const task_cls_t& cls, const task_id_t& id, const task_param_t& param, const task_routinefunc_t& routine)
{
	task_cls_t cls_std = GetStdCls(cls);

	if (_exit_flag)
	{
		return TEC_EXIT_STATE;
	}

	size_t task_num = 1;
	UINT16 thread_limit_max_num = DEFAULT_THREAD_NUM_BY_CLS;
	UINT32 unhandle_msg_timeout = DEFAULT_UNHANDLMSG_TIMEOUT_BY_CLS;

	std::map<std::wstring, std::shared_ptr<managed_pool_t>, StrCaseCmp>::iterator it2 = _managed_cls_table.find(cls_std);
	if (it2 != _managed_cls_table.end())
	{
		std::shared_ptr<managed_pool_t> cls_thread_pool = it2->second;
		task_num += cls_thread_pool->active_tasks.size() + cls_thread_pool->wait_tasks.size();
		thread_limit_max_num = cls_thread_pool->thread_limit_max_num;
		unhandle_msg_timeout = cls_thread_pool->unhandle_msg_timeout;
	}
	else
	{
		_managed_cls_table[cls_std] = std::make_shared<managed_pool_t>();
	}
	
	//托管
	managed_task_t	ti;
	ti.cls = cls_std;
	ti.param = param;
	ti.routine = routine;

	it2 = _managed_cls_table.find(cls_std);
	ATLASSERT(it2 != _managed_cls_table.end());
	it2->second->wait_tasks[id] = ti;
	_managed_task_index[id] = cls_std;

	//任务先添加，因为线程池的线程退出也依赖任务数，如果任务后面添加可能导致线程数全部退出，任务无线程执行
	if (!CreatePoolIfNotExist_InLock(lck, cls_std, task_num, thread_limit_max_num, unhandle_msg_timeout))
	{
		return TEC_ALLOC_FAILED;
	}
	_cond.notify_all();

	return TEC_SUCCEED;
}


static TaskErrorCode PoolDelManagedTask_InLock(const task_id_t& call_id, const task_id_t& target_id)
{
	//检查

	//这说明线程不存在，或者这个是非托管线程
	//另外需要考虑一个线程的入口函数要删除自己的情况
	std::map<task_id_t, std::wstring>::iterator it = _managed_task_index.find(target_id);
	if (it != _managed_task_index.end())
	{
		std::map<std::wstring, std::shared_ptr<managed_pool_t>, StrCaseCmp>::iterator it2 = _managed_cls_table.find(it->second);
		if (it2 != _managed_cls_table.end())
		{
			std::map<task_id_t, managed_task_t>::iterator it3 = it2->second->wait_tasks.find(target_id);
			if (it3 != it2->second->wait_tasks.end())
			{
				it2->second->wait_tasks.erase(target_id);
				_managed_task_index.erase(target_id);
				StaticDeleteTask_InLock(target_id);

				return TEC_SUCCEED;
			}
			else
			{
				it3 = it2->second->active_tasks.find(target_id);
				if (it3 != it2->second->active_tasks.end())
				{
					if (call_id == target_id)
					{
						ATLASSERT(FALSE);
						return TEC_MANAGED_DELETE_SELF;
					}
					ATLASSERT(it3->second.thread_item_ptr);
					it3->second.thread_item_ptr->thread_ctrlblock->SetWaitExit(TRUE);

					return TEC_SUCCEED;
				}
			}
		}
	}
	
	return TEC_NOT_EXIST;
}

////////////////////////////////////////////////接口函数////////////////////////////////////////////////////
//针对发送者，允许自己给自己发
task_id_t	tixAllocTaskID(const task_cls_t& cls)
{
	std::unique_lock <std::mutex> lck(_mutex);
	if (_exit_flag)
	{
		return task_id_null;
	}

	return StaticAllocTaskID_InLock();
}

void	tixDeleteUnmanagedTask(const task_id_t& id)
{
	//此时线程已经退出，所以只要管理数据即可
	std::unique_lock <std::mutex> lck(_mutex);
	StaticDeleteTask_InLock(id);
	//托管任务
}

//
TaskErrorCode tixPostMsg(const task_id_t& sender_id, const task_id_t& receiver_id, const task_cmd_t& cmd, task_data_t data, std::shared_ptr<task_msgdepot_t>& channel, const task_flag_t& flags/* = task_flag_null*/)
{
	std::unique_lock <std::mutex> lck(_mutex);
	if (_exit_flag)
	{
		return TEC_EXIT_STATE;
	}

	//检查发送者，无需检查接收者是否存在，tbd超时
	if (!StaticExistTask_InLock(sender_id))
	{
		return TEC_INVALID_THREADID;
	}

	std::vector<task_id_t>	receiver_list;
	if (receiver_id == task_id_null)
	{
	}
	else if (receiver_id == task_id_self)
	{
		receiver_list.push_back(sender_id);
	}
	else if (receiver_id == task_id_broadcast_allothers)
	{
		for (std::map<task_id_t, task_static_t>::iterator it = _static_table.begin(); it != _static_table.end(); it++)
		{
			if (it->first != sender_id)
			{
				receiver_list.push_back(it->first);
			}
		}
	}
	else if (receiver_id == task_id_broadcast_sameclsothers)
	{
		//注意: 只有托管任务才有类别
		std::map<task_id_t, std::wstring>::iterator it = _managed_task_index.find(sender_id);
		if (it != _managed_task_index.end())
		{
			std::map<std::wstring, std::shared_ptr<managed_pool_t>, StrCaseCmp>::iterator it2 = _managed_cls_table.find(it->second);
			if (it2 != _managed_cls_table.end())
			{
				for (std::map<task_id_t, managed_task_t>::iterator it3 = it2->second->active_tasks.begin(); it3 != it2->second->active_tasks.end(); it3++)
				{
					if (it3->first != sender_id)
					{
						receiver_list.push_back(it3->first);
					}
				}
				for (std::map<task_id_t, managed_task_t>::iterator it3 = it2->second->wait_tasks.begin(); it3 != it2->second->wait_tasks.end(); it3++)
				{
					if (it3->first != sender_id)
					{
						receiver_list.push_back(it3->first);
					}
				}
			}
		}
	}
	else
	{
		receiver_list.push_back(receiver_id);
	}

	INT64	tick = GetTickCount64();
	for (std::vector<task_id_t>::iterator it = receiver_list.begin(); it != receiver_list.end(); it++)
	{
		TCHAR sz[64];
		_sntprintf_s(sz, _countof(sz), _T("%lld"), *it);
		StaticUpdateTask_InLock(*it, sz);

		std::map<task_id_t, task_static_t>::iterator it2 = _static_table.find(*it);
		if (it2 == _static_table.end())
		{
			return TEC_ALLOC_FAILED;
		}

		task_msgline_t	line;
		line.stamp = tick;
		line.sender_id = sender_id;
		line.receiver_id = *it;
		line.cmd = cmd;
		line.data = data;

		it2->second.msgdepot->Append(line);
	}
	if (IsSingleTaskID(receiver_id))
	{
		std::map<task_id_t, task_static_t>::iterator it2 = _static_table.find(receiver_id);
		ATLASSERT(it2 != _static_table.end());
		channel = it2->second.msgdepot;
	}
	
	return TEC_SUCCEED;
}

TaskErrorCode tixFetchMsgList(const task_id_t& id, std::shared_ptr<task_msgdepot_t>& channel)
{
	std::unique_lock <std::mutex> lck(_mutex);

	if (_exit_flag)
	{
		return TEC_EXIT_STATE;
	}

	std::map<task_id_t, task_static_t>::iterator it2 = _static_table.find(id);
	if (it2 == _static_table.end())
	{
		return TEC_NOT_EXIST;
	}	

	channel = it2->second.msgdepot;
	return TEC_SUCCEED;
}

//////////////////
TaskErrorCode	tixGetTaskState(const task_id_t& id, TaskWorkState& state)
{
	std::unique_lock <std::mutex> lck(_mutex);
	if (_exit_flag)
	{
		return TEC_EXIT_STATE;
	}

	if (!StaticExistTask_InLock(id))
	{
		return TEC_INVALID_THREADID;
	}
	state = TWS_ACTIVE;	//非托管任务会认为一直是TWS_ACTIVE

	std::map<task_id_t, std::wstring>::iterator it = _managed_task_index.find(id);
	if (it != _managed_task_index.end())
	{
		std::map<std::wstring, std::shared_ptr<managed_pool_t>, StrCaseCmp>::iterator it2 = _managed_cls_table.find(it->second);
		if (it2 != _managed_cls_table.end())
		{
			std::map<task_id_t, managed_task_t>::iterator it3 = it2->second->wait_tasks.find(id);
			if (it3 != it2->second->wait_tasks.end())
			{
				state = TWS_AWAIT;
			}
			else
			{
				it3 = it2->second->active_tasks.find(id);
				if (it3 != it2->second->active_tasks.end())
				{
					state = TWS_ACTIVE;
				}
			}
		}
	}

	return TEC_SUCCEED;
}

void			tixSetClsAttri(const task_cls_t& cls, const UINT16& thread_limit_max_num, const UINT32& unhandle_msg_timeout)
{
	task_cls_t cls_std = GetStdCls(cls);

	std::unique_lock <std::mutex> lck(_mutex);
	if (_exit_flag)
	{
		return;
	}

	//tbd unhandle_msg_timeout

	CreatePoolIfNotExist_InLock(lck, cls_std, 0, thread_limit_max_num, unhandle_msg_timeout);
}

//将会强制同步等待池中所有线程关闭
TaskErrorCode			tixClearManagedTask(const task_id_t& call_id, const task_userloop_t& user_loop_func)
{
	//1，置位, 因为需要等待，这意味着其他线程也能继续访问
	{
		std::unique_lock <std::mutex> lck(_mutex);
		if (_exit_flag)
		{
			return TEC_EXIT_STATE;
		}
		_exit_flag = TRUE;
		for (std::map<std::wstring, std::shared_ptr<managed_pool_t>, StrCaseCmp>::iterator it = _managed_cls_table.begin(); it != _managed_cls_table.end(); it++)
		{
			for (std::map<task_id_t, managed_task_t>::iterator it2 = it->second->active_tasks.begin(); it2 != it->second->active_tasks.end(); it2++)
			{
				if (call_id == it2->first)
				{
					ATLASSERT(FALSE);
					return TEC_MANAGED_DELETE_SELF;
				}
			}
		}
		for (std::map<std::wstring, std::shared_ptr<managed_pool_t>, StrCaseCmp>::iterator it = _managed_cls_table.begin(); it != _managed_cls_table.end(); it++)
		{
			for (std::map<thread_id_t, managed_thread_t>::iterator it2 = it->second->threads.begin(); it2 != it->second->threads.end(); it2++)
			{
				it2->second.thread_ctrlblock->SetWaitExit(TRUE);
			}
		}
		//2，通知所有挂起线程
		_cond.notify_all();
	}

	//所有托管线程退出
	BOOL loop_again = TRUE;
	while (true)
	{
		//可能需要窗口消息交互，一个例子是：
		//父线窗口有父窗口，子线程有其子窗口，当父线程删除子线程的时候，父线程在此等待子线程退出，但子线程窗口删除过程需要和父窗口交互，
		//所以会一直等待父线程的父窗口响应，如果父窗口是"硬核"等待子窗口，那么就等于死锁了
		//这提示我们：关键在于父窗口/线程需要做到在等待过程中响应子线程的必要消息，故父线程在等待中要有消息循环放开口子
		if (user_loop_func && loop_again)
		{
			loop_again = user_loop_func();
			if (!loop_again)
			{
				SetExitLoop(TRUE);
			}
		}
		else
		{
			Sleep(1);
		}

		std::unique_lock <std::mutex> lck(_mutex);
		if (_managed_thread_index.empty())
		{
			break;
		}
	}
	
	_managed_task_index.clear();
	_managed_cls_table.clear();

	return TEC_SUCCEED;
}

//任务管理
TaskErrorCode tixSetTaskName(const task_id_t& id, const task_name_t& name)
{
	std::unique_lock <std::mutex> lck(_mutex);
	return StaticSetTaskName_InLock(id, name);
}

TaskErrorCode	tixSetTaskAttri(const task_id_t& id, const UINT32& unhandle_msg_timeout)
{
	std::unique_lock <std::mutex> lck(_mutex);
	return StaticUpdateTask_InLock(id, _T(""), unhandle_msg_timeout);
}

TaskErrorCode tixGetTaskName(const task_id_t& id, task_name_t& name)
{
	std::unique_lock <std::mutex> lck(_mutex);
	return StaticGetTaskName_InLock(id, name);
}

TaskErrorCode tixAddManagedTask(const task_cls_t& cls, const task_name_t& name, const task_param_t& param, const task_routinefunc_t& routine, task_id_t& id)
{
	std::unique_lock <std::mutex> lck(_mutex);
	if (_exit_flag)
	{
		return TEC_EXIT_STATE;
	}

	id = StaticAllocTaskID_InLock();
	if (task_id_null == id)
	{
		return TEC_ALLOC_FAILED;
	}

	TaskErrorCode tec = StaticSetTaskName_InLock(id, name);

	if (TEC_SUCCEED == tec)
	{
		tec = PoolAddManagedTask_InLock(lck, cls, id, param, routine);
	}
	if (TEC_SUCCEED != tec)
	{
		StaticDeleteTask_InLock(id);
	}

	return tec;
}
																																									
//是否等待目标关闭，需要明确的是如果自己关闭自己或关闭非托管线程，将会是强制改成异步																																									
TaskErrorCode tixDelManagedTask(const task_id_t& call_id, const task_id_t& target_id, const task_userloop_t& user_loop_func)
{
	TaskErrorCode err = TEC_SUCCEED;

	{
		std::unique_lock <std::mutex> lck(_mutex);

		if (!StaticExistTask_InLock(target_id))
		{
			return TEC_INVALID_THREADID;
		}
	
		err = PoolDelManagedTask_InLock(call_id, target_id);
	}

	if(err == TEC_SUCCEED)
	{
		BOOL loop_again = TRUE;
		while (true)
		{			
			//可能需要窗口消息交互，一个例子是父线窗口是父窗口，子线程是子窗口，当通知子线程退出时候，
			//父线程在此等待，但子线程窗口删除过程需要和父线程交互，导致子线程无法DestroyWindow，一直
			//等待父线程响应，而父线程又在等待子线程退出...
			if (user_loop_func && loop_again)
			{
				loop_again = user_loop_func();
				if (!loop_again)
				{
					SetExitLoop(TRUE);
				}
			}
			else
			{
				Sleep(1);
			}			

			std::unique_lock <std::mutex> lck(_mutex);
			if (_managed_task_index.find(target_id) == _managed_task_index.end())
			{
				break;
			}
		}

		tixDeleteUnmanagedTask(target_id);
	}	

	return err;
}

TaskErrorCode tixGetTaskByName(const task_name_t& name, task_id_t& id)
{
	std::unique_lock <std::mutex> lck(_mutex);

	return StaticGetTaskByName_InLock(name, id);
}

//Debug
TaskErrorCode tixPrintMeta()
{
	std::unique_lock <std::mutex> lck(_mutex);

	_tprintf(_T("-------------PrintMeta------------\n"));
	_tprintf(_T("_exit_flag: %d\n"), _exit_flag);
	_tprintf(_T("_cursor: %llu\n"), _cursor);

	_tprintf(_T("_static_table: \n"));
	for (std::map<task_id_t, task_static_t>::iterator it = _static_table.begin(); it != _static_table.end(); it++)
	{
		_tprintf(_T("	task_id: %llu\n"), it->first);
		_tprintf(_T("	name: %s\n"), it->second.name.c_str());
		_tprintf(_T("	(recv)task_id: %llu\n"), it->first);
		it->second.msgdepot->Print(_T("		"));
	}

	_tprintf(_T("_managed_cls_table: \n"));
	for (std::map<std::wstring, std::shared_ptr<managed_pool_t>, StrCaseCmp>::iterator it = _managed_cls_table.begin(); it != _managed_cls_table.end(); it++)
	{
		_tprintf(_T("	cls_name: %s\n"), it->first.c_str());
		_tprintf(_T("	thread_limit_max_num: %u\n"), it->second->thread_limit_max_num);
		_tprintf(_T("	unhandle_msg_timeout: %u\n"), it->second->unhandle_msg_timeout);

		_tprintf(_T("		threads: \n"));
		for (std::map<thread_id_t, managed_thread_t>::iterator it2 = it->second->threads.begin(); it2 != it->second->threads.end(); it2++)
		{
			_tprintf(_T("			thread_id: %u\n"), it2->first);
			_tprintf(_T("			thread_item_ptr: 0x%p\n"), &it2->second);
			_tprintf(_T("			thread_ctrlblock: \n"));
			it2->second.thread_ctrlblock->Print(_T("				"));
		}

		_tprintf(_T("		active_tasks: \n"));
		for (std::map<task_id_t, managed_task_t>::iterator it2 = it->second->active_tasks.begin(); it2 != it->second->active_tasks.end(); it2++)
		{
			_tprintf(_T("			task_id: %llu\n"), it2->first);
			_tprintf(_T("			cls_name: %s\n"), it2->second.cls.c_str());
			if (it2->second.param)
			{
				it2->second.param->Print(_T("			"));
			}
			_tprintf(_T("			thread_item_ptr: 0x%p\n"), it2->second.thread_item_ptr);
		}

		_tprintf(_T("		wait_tasks: \n"));
		for (std::map<task_id_t, managed_task_t>::iterator it2 = it->second->wait_tasks.begin(); it2 != it->second->wait_tasks.end(); it2++)
		{
			_tprintf(_T("			task_id: %llu\n"), it2->first);
			_tprintf(_T("			cls: %s\n"), it2->second.cls.c_str());
			if (it2->second.param)
			{
				it2->second.param->Print(_T("			"));
			}
			_tprintf(_T("			thread_item_ptr: 0x%p\n"), it2->second.thread_item_ptr);
		}
	}

	_tprintf(_T("_managed_task_index: \n"));
	for (std::map<task_id_t, std::wstring>::iterator it = _managed_task_index.begin(); it != _managed_task_index.end(); it++)
	{
		_tprintf(_T("	task_id: %llu\n"), it->first);
		_tprintf(_T("	cls_name: %s\n"), it->second.c_str());
	}

	_tprintf(_T("_managed_thread_index: \n"));
	for (std::map<thread_id_t, std::wstring>::iterator it = _managed_thread_index.begin(); it != _managed_thread_index.end(); it++)
	{
		_tprintf(_T("	thread_id: %u\n"), it->first);
		_tprintf(_T("	cls_name: %s\n"), it->second.c_str());
	}

	return TEC_SUCCEED;
}