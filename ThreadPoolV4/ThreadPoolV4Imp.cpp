#include "stdafx.h"
#include <assert.h>
#include <map>
#include "ThreadPoolV4Imp.h"

#define DEFAULT_THREAD_NUM_BY_CLS			(2)
#define DEFAULT_UNHANDLMSG_TIMEOUT_BY_CLS	(20 * 1000)

//�����йܺͷ��й������Ӧ��kv�ṹ
struct task_static_t
{
	task_name_t		name;
};

//�й�������߳������ṹ
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
	managed_thread_t*		thread_item_ptr;	//��TTS_ACTIVEʱ����Ч���������ĸ��߳�

	managed_task_t() : thread_item_ptr(nullptr) {}
};
struct managed_pool_t
{
	UINT16	thread_num;
	UINT32	unhandle_msg_timeout;	//�ݲ�����
	BOOL	thread_init_finish;

	std::map<thread_id_t, managed_thread_t>	threads;
	std::map<task_id_t, managed_task_t>		active_tasks;
	std::map<task_id_t, managed_task_t>		wait_tasks;

	managed_pool_t() : thread_num(DEFAULT_THREAD_NUM_BY_CLS), unhandle_msg_timeout(0), thread_init_finish(FALSE) {}
};

static std::mutex													_mutex;
static std::condition_variable										_cond;
//��̬��
static BOOL															_exit_flag = FALSE;//�˳�״̬��ʶ�����ú��ʾֻ�ܼ����ܼ�
static task_id_t													_cursor = task_id_null;
static std::map<task_id_t, task_static_t>							_static_table;
//��Ϣ��
static std::map<task_id_t, std::vector<task_msgline_t>>				_msg_table;
//�й�����/�̳߳�
static std::map<std::wstring, std::shared_ptr<managed_pool_t>, StrCaseCmp>	_managed_cls_table;
static std::map<task_id_t, std::wstring>							_managed_task_index;
static std::map<thread_id_t, std::wstring>							_managed_thread_index;

///////////////��̬����������
static task_id_t		StaticAllocTaskID_InLock()
{
	return ++_cursor;
}

static void			StaticDeleteTask_InLock(const task_id_t& id)
{
	_static_table.erase(id);
}

static BOOL			StaticExistTask_InLock(const task_id_t& id)
{
	return _static_table.find(id) != _static_table.end();
}

static ThreadErrorCode StaticSetTaskName_InLock(const task_id_t& id, const task_name_t& name)
{
	if (name.empty())
	{
		return TEC_INVALID_ARG;
	}

	//���Ʊ�����һ���ַ�����������һ��buffer����Ϊ�����_tcsicmp�ȽϺ������ַ����Ƚ�
	if (_tcslen(name.c_str()) != name.size())
	{
		return TEC_INVALID_ARG;
	}

	//���Ʋ������������еĳ�ͻ
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

	//������Ϊ���֣������Ϊ��ǰid�������Զ�����ʱ��ռ��
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
		if (_tcsicmp(name.c_str(), sz))
		{
			return TEC_INVALID_ARG;
		}
	}


	std::map<task_id_t, task_static_t>::iterator it = _static_table.find(id);
	if (it == _static_table.end())
	{
		task_static_t	st;
		_static_table[id] = st;
		
		it = _static_table.find(id);
		assert(it != _static_table.end());
	}

	it->second.name = name;
	return TEC_SUCCEED;
}

static ThreadErrorCode StaticGetTaskName_InLock(const task_id_t& id, task_name_t& name)
{
	std::map<task_id_t, task_static_t>::iterator it = _static_table.find(id);
	if (it == _static_table.end())
	{
		return TEC_NOT_EXIST;
	}

	name = it->second.name;
	return TEC_SUCCEED;
}

static ThreadErrorCode StaticGetTaskByName_InLock(const task_name_t& name, task_id_t& id)
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

///////////////�й�����/�̳߳ظ�������
static void	ManagedThreadRoutine()
{
	DWORD dwThreadID = GetCurrentThreadId();

	//�ⲿȷ�����̴߳������ڣ�CLS�ṹ���Ȼ����
	std::shared_ptr<managed_pool_t>	cls_ptr = nullptr;
	managed_thread_t*	thread_item_ptr = nullptr;
	{
		std::unique_lock <std::mutex> lck(_mutex);

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

		//�ȴ���ʽ��������
		if (cls_ptr)
		{
			while (!cls_ptr->thread_init_finish)
			{
				_cond.wait(lck);
			}
		}
	}
	assert(cls_ptr);
	assert(thread_item_ptr);

	if (thread_item_ptr)
	{
		while (true)
		{
			task_id_t	first = 0;
			managed_task_t	second;

			//Ҫ��ȫ���߳��˳�����ô�����˳���
			{
				std::unique_lock <std::mutex> lck(_mutex);
				if (_exit_flag)
				{
					break;
				}

				//���������ѡ��һ������	
				std::map<task_id_t, managed_task_t>::iterator it = cls_ptr->wait_tasks.begin();
				if (it != cls_ptr->wait_tasks.end())
				{
					first = it->first;
					second = it->second;

					cls_ptr->wait_tasks.erase(it);
					second.thread_item_ptr = thread_item_ptr;
					cls_ptr->active_tasks[first] = second;
				}
				else if (cls_ptr->thread_num < cls_ptr->threads.size())
				{
					//ֱ���˳�
					if (cls_ptr)
					{
						cls_ptr->threads.erase(dwThreadID);
					}
					_managed_thread_index.erase(dwThreadID);
					return;
				}
				else 
				{
					//û���ڵȴ�ִ�е�����
					_cond.wait(lck);
				}
			}

			if (first)
			{
				//
				TlsProxyReset_NoLock(first, thread_item_ptr->thread_ctrlblock);
				second.routine(first, thread_item_ptr->thread_ctrlblock, second.param);

				std::unique_lock <std::mutex> lck(_mutex);
				cls_ptr->active_tasks.erase(first);
				_managed_task_index.erase(first);
				StaticDeleteTask_InLock(first);
				_msg_table.erase(first);
			}
		}
	}

	//�߳�Ҫ�˳��ˣ����������Դ
	{
		std::unique_lock <std::mutex> lck(_mutex);
		if (cls_ptr)
		{
			cls_ptr->threads.erase(dwThreadID);
		}
		_managed_thread_index.erase(dwThreadID);
	}
}

static BOOL CreatePoolIfNotExist_InLock(const task_cls_t& cls, const UINT16& thread_num = DEFAULT_THREAD_NUM_BY_CLS, const UINT32& unhandle_msg_timeout = DEFAULT_UNHANDLMSG_TIMEOUT_BY_CLS)
{
	//�������ݽṹ
	std::map<std::wstring, std::shared_ptr<managed_pool_t>, StrCaseCmp>::iterator it = _managed_cls_table.find(cls);
	if (it == _managed_cls_table.end())
	{
		_managed_cls_table[cls] = std::make_shared<managed_pool_t>();
		it = _managed_cls_table.find(cls);

		//�����̶߳���
		for (UINT16 i = 0; i < thread_num; i++)
		{
			managed_thread_t ti;

			std::shared_ptr<std::thread> thread_obj = std::make_shared<std::thread>(&ManagedThreadRoutine);
			thread_id_t dwThreadID = GetThreadId((HANDLE)thread_obj->native_handle());
			ti.thread_ctrlblock = std::make_shared<ManagedThreadCtrlBlock>(dwThreadID);
			thread_obj->detach();

			it->second->threads[dwThreadID] = ti;
			_managed_thread_index[dwThreadID] = cls;
		}

		it->second->thread_num = thread_num;
		it->second->unhandle_msg_timeout = unhandle_msg_timeout;

		//֪ͨ������̣߳����ܳ����Ҫ�����˳�
		it->second->thread_init_finish = TRUE;
	}
	else
	{
		for (UINT16 i = it->second->thread_num; i < thread_num; i++)
		{
			managed_thread_t ti;

			std::shared_ptr<std::thread> thread_obj = std::make_shared<std::thread>(&ManagedThreadRoutine);
			thread_id_t dwThreadID = GetThreadId((HANDLE)thread_obj->native_handle());
			ti.thread_ctrlblock = std::make_shared<ManagedThreadCtrlBlock>(dwThreadID);
			thread_obj->detach();

			it->second->threads[dwThreadID] = ti;
			_managed_thread_index[dwThreadID] = cls;
		}
		it->second->thread_num = thread_num;
		it->second->unhandle_msg_timeout = unhandle_msg_timeout;
	}

	_cond.notify_all();

	return TRUE;
}

static ThreadErrorCode PoolAddManagedTask_InLock(const task_cls_t& cls, const task_id_t& id, const task_param_t& param, const task_routinefunc_t& routine)
{
	task_cls_t cls_std = GetStdCls(cls);

	if (_exit_flag)
	{
		return TEC_EXIT_STATE;
	}

	if (!CreatePoolIfNotExist_InLock(cls_std))
	{
		return TEC_ALLOC_FAILED;
	}

	std::map<std::wstring, std::shared_ptr<managed_pool_t>, StrCaseCmp>::iterator it2 = _managed_cls_table.find(cls_std);
	if (it2 == _managed_cls_table.end())
	{
		return TEC_ALLOC_FAILED;
	}

	//����id
	managed_task_t	ti;
	ti.cls = cls_std;
	ti.param = param;
	ti.routine = routine;

	it2->second->wait_tasks[id] = ti;
	_managed_task_index[id] = cls_std;

	_cond.notify_all();

	return TEC_SUCCEED;
}


static ThreadErrorCode PoolDelManagedTask_InLock(const task_id_t& call_id, const task_id_t& target_id)
{
	//���

	//��˵���̲߳����ڣ���������Ƿ��й��߳�
	//������Ҫ����һ���̵߳���ں���Ҫɾ���Լ������
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
				_msg_table.erase(target_id);

				return TEC_SUCCEED;
			}
			else
			{
				it3 = it2->second->active_tasks.find(target_id);
				if (it3 != it2->second->active_tasks.end())
				{
					if (call_id == target_id)
					{
						assert(false);
						return TEC_MANAGED_DELETE_SELF;
					}
					assert(it3->second.thread_item_ptr);
					it3->second.thread_item_ptr->thread_ctrlblock->SetWaitExit();

					return TEC_SUCCEED;
				}
			}
		}
	}
	
	return TEC_NOT_EXIST;
}

////////////////////////////////////////////////�ӿں���////////////////////////////////////////////////////
//��Է����ߣ������Լ����Լ���
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
	//��ʱ�߳��Ѿ��˳�������ֻҪ�������ݼ���
	std::unique_lock <std::mutex> lck(_mutex);
	StaticDeleteTask_InLock(id);
	_msg_table.erase(id);
	//�й�����
}

ThreadErrorCode tixPostMsg(const task_id_t& sender_id, const task_id_t& receiver_id, const task_cmd_t& cmd, task_data_t data, const task_flag_t& flags/* = task_flag_null*/)
{
	std::unique_lock <std::mutex> lck(_mutex);

	if (_exit_flag)
	{
		return TEC_EXIT_STATE;
	}
	//�򵥼�飬���ڶ������û����ͬһ������ִ��ͬ��������ǰ����в�һ�µ���������統ִ�е�msg_ptr->PostMsgʱ�򣬿���receiver_id��ɾ����
	if (!StaticExistTask_InLock(sender_id))
	{
		return TEC_INVALID_THREADID;
	}

	std::map<task_id_t, std::vector<task_msgline_t>>::iterator it = _msg_table.find(receiver_id);
	if (it == _msg_table.end())
	{
		std::vector<task_msgline_t> v;
		_msg_table[receiver_id] = v;
		it = _msg_table.find(receiver_id);
	}
	task_msgline_t	line;
	line.stamp = GetTickCount64();
	line.sender_id = sender_id;
	line.receiver_id = receiver_id;
	line.cmd = cmd;
	line.data = data;

	it->second.push_back(line);

	return TEC_SUCCEED;
}

ThreadErrorCode tixFetchMsgList(const task_id_t& id, std::vector<task_msgline_t>& array)
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

	array.clear();

	std::map<task_id_t, std::vector<task_msgline_t>>::iterator it = _msg_table.find(id);
	if (it == _msg_table.end())
	{
		return TEC_NOT_EXIST;
	}

	array.swap(it->second);
	_msg_table.erase(id);

	return TEC_SUCCEED;
}

//////////////////
ThreadErrorCode	tixGetTaskState(const task_id_t& id, ThreadTaskState& state)
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
	state = TTS_ACTIVE;	//���й��������Ϊһֱ��TTS_ACTIVE

	std::map<task_id_t, std::wstring>::iterator it = _managed_task_index.find(id);
	if (it != _managed_task_index.end())
	{
		std::map<std::wstring, std::shared_ptr<managed_pool_t>, StrCaseCmp>::iterator it2 = _managed_cls_table.find(it->second);
		if (it2 != _managed_cls_table.end())
		{
			std::map<task_id_t, managed_task_t>::iterator it3 = it2->second->wait_tasks.find(id);
			if (it3 != it2->second->wait_tasks.end())
			{
				state = TTS_AWAIT;
			}
			else
			{
				it3 = it2->second->active_tasks.find(id);
				if (it3 != it2->second->active_tasks.end())
				{
					state = TTS_ACTIVE;
				}
			}
		}
	}

	return TEC_SUCCEED;
}

void			tixSetClsAttri(const task_cls_t& cls, const UINT16& thread_num, const UINT32& unhandle_msg_timeout)
{
	task_cls_t cls_std = GetStdCls(cls);

	std::unique_lock <std::mutex> lck(_mutex);
	if (_exit_flag)
	{
		return;
	}

	CreatePoolIfNotExist_InLock(cls_std, thread_num, unhandle_msg_timeout);
}

//����ǿ��ͬ���ȴ����������̹߳ر�
ThreadErrorCode			tixClearManagedTask(const task_id_t& call_id)
{
	//1����λ, ��Ϊ��Ҫ�ȴ�������ζ�������߳�Ҳ�ܼ�������
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
					assert(false);
					return TEC_MANAGED_DELETE_SELF;
				}
			}
		}
		for (std::map<std::wstring, std::shared_ptr<managed_pool_t>, StrCaseCmp>::iterator it = _managed_cls_table.begin(); it != _managed_cls_table.end(); it++)
		{
			for (std::map<thread_id_t, managed_thread_t>::iterator it2 = it->second->threads.begin(); it2 != it->second->threads.end(); it2++)
			{
				it2->second.thread_ctrlblock->SetWaitExit();
			}
		}
		//2��֪ͨ���й����߳�
		_cond.notify_all();
	}

	//ֱ�����ֻ���Լ�һ���߳��ڴ�
	while (true)
	{
		Sleep(10);

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

//�������
ThreadErrorCode tixSetTaskName(const task_id_t& id, const task_name_t& name)
{
	std::unique_lock <std::mutex> lck(_mutex);
	return StaticSetTaskName_InLock(id, name);
}

ThreadErrorCode tixGetTaskName(const task_id_t& id, task_name_t& name)
{
	std::unique_lock <std::mutex> lck(_mutex);
	return StaticGetTaskName_InLock(id, name);
}

ThreadErrorCode tixAddManagedTask(const task_cls_t& cls, const task_name_t& name, const task_param_t& param, const task_routinefunc_t& routine, task_id_t& id)
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

	ThreadErrorCode tec = StaticSetTaskName_InLock(id, name);

	if (TEC_SUCCEED == tec)
	{
		tec = PoolAddManagedTask_InLock(cls, id, param, routine);
	}
	if (TEC_SUCCEED != tec)
	{
		StaticDeleteTask_InLock(id);
	}
	return tec;
}
																																									
//�Ƿ�ȴ�Ŀ��رգ���Ҫ��ȷ��������Լ��ر��Լ���رշ��й��̣߳�������ǿ�Ƹĳ��첽																																									
ThreadErrorCode tixDelManagedTask(const task_id_t& call_id, const task_id_t& target_id)
{
	//ɾ�������������Ƿ�����_exit_flag����Ϊ����ɾ������
	ThreadErrorCode err = TEC_SUCCEED;

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
		while (true)
		{
			Sleep(10);

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

ThreadErrorCode tixGetTaskByName(const task_name_t& name, task_id_t& id)
{
	std::unique_lock <std::mutex> lck(_mutex);

	return StaticGetTaskByName_InLock(name, id);
}

//Debug
ThreadErrorCode tixPrintMeta(const task_id_t& id)
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
	}

	_tprintf(_T("_msg_table: \n"));
	for (std::map<task_id_t, std::vector<task_msgline_t>>::iterator it = _msg_table.begin(); it != _msg_table.end(); it++)
	{
		_tprintf(_T("	(recv)task_id: %llu\n"), it->first);
		for (std::vector<task_msgline_t>::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++)
		{
			_tprintf(_T("		sender_id: %llu\n"), it2->sender_id);
			_tprintf(_T("		receiver_id: %llu\n"), it2->receiver_id);
			_tprintf(_T("		stamp: %lld\n"), it2->stamp);
			_tprintf(_T("		cmd: %u\n"), it2->cmd);
			_tprintf(_T("		data_size: %u\n"), it2->data ? it2->data->size() : 0);
		}
	}

	_tprintf(_T("_managed_cls_table: \n"));
	for (std::map<std::wstring, std::shared_ptr<managed_pool_t>, StrCaseCmp>::iterator it = _managed_cls_table.begin(); it != _managed_cls_table.end(); it++)
	{
		_tprintf(_T("	cls_name: %s\n"), it->first.c_str());
		_tprintf(_T("	thread_num: %u\n"), it->second->thread_num);
		_tprintf(_T("	unhandle_msg_timeout: %u\n"), it->second->unhandle_msg_timeout);
		_tprintf(_T("	thread_init_finish: %d\n"), it->second->thread_init_finish);

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
			_tprintf(_T("			param_size: %u\n"), it2->second.param ? it2->second.param->size() : 0);
			_tprintf(_T("			thread_item_ptr: 0x%p\n"), it2->second.thread_item_ptr);
		}

		_tprintf(_T("		wait_tasks: \n"));
		for (std::map<task_id_t, managed_task_t>::iterator it2 = it->second->wait_tasks.begin(); it2 != it->second->wait_tasks.end(); it2++)
		{
			_tprintf(_T("			task_id: %llu\n"), it2->first);
			_tprintf(_T("			cls: %s\n"), it2->second.cls.c_str());
			_tprintf(_T("			param_size: %u\n"), it2->second.param ? it2->second.param->size() : 0);
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