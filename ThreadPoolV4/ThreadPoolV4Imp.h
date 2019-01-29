#pragma once

#include "ThreadInnerDef.h"
#include "ThreadPoolV4.h"

//已经存在的线程，需要一个内部id作为沟通
task_id_t		tixAllocTaskID(const task_cls_t& cls);
void			tixDeleteUnmanagedTask(const task_id_t& id);

//针对发送者，允许自己给自己发
TaskErrorCode	tixPostMsg(const task_id_t& sender_id, const task_id_t& receiver_id, const task_cmd_t& cmd, task_data_t data, std::shared_ptr<task_msgdepot_t>& channel, const task_flag_t& flags = task_flag_null);
TaskErrorCode	tixFetchMsgList(const task_id_t& id, std::shared_ptr<task_msgdepot_t>& channel);

//调节线程数量
void			tixSetClsAttri(const task_cls_t& cls, const UINT16& thread_num, const UINT32& unhandle_msg_timeout);
//将会强制同步等待池中所有线程关闭
TaskErrorCode	tixClearManagedTask(const task_id_t& call_id);
TaskErrorCode	tixSetTaskName(const task_id_t& id, const task_name_t& name);
TaskErrorCode	tixSetTaskAttri(const task_id_t& id, const UINT32& unhandle_msg_timeout);
TaskErrorCode	tixGetTaskName(const task_id_t& id, task_name_t& name);

//任务管理
TaskErrorCode	tixAddManagedTask(const task_cls_t& cls, const task_name_t& name, const task_param_t& param, const task_routinefunc_t& routine, task_id_t& id);	//调节任务
//是否等待目标关闭，需要明确的是如果自己关闭自己或关闭非托管线程，将会是强制改成异步																																									
TaskErrorCode	tixDelManagedTask(const task_id_t& call_id, const task_id_t& target_id);
TaskErrorCode	tixGetTaskState(const task_id_t& id, TaskWorkState& state);
TaskErrorCode	tixGetTaskByName(const task_name_t& name, task_id_t& id);

//Debug
TaskErrorCode	tixPrintMeta();
