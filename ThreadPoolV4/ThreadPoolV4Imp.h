#pragma once

#include "ThreadInnerDef.h"
#include "ThreadPoolV4.h"

//�Ѿ����ڵ��̣߳���Ҫһ���ڲ�id��Ϊ��ͨ
task_id_t		tixAllocTaskID(const task_cls_t& cls);
void			tixDeleteUnmanagedTask(const task_id_t& id);

//��Է����ߣ������Լ����Լ���
TaskErrorCode	tixPostMsg(const task_id_t& sender_id, const task_id_t& receiver_id, const task_cmd_t& cmd, task_data_t data, std::shared_ptr<task_msgdepot_t>& channel, const task_flag_t& flags = task_flag_null);
TaskErrorCode	tixFetchMsgList(const task_id_t& id, std::shared_ptr<task_msgdepot_t>& channel);

//�����߳�����
void			tixSetClsAttri(const task_cls_t& cls, const UINT16& thread_num, const UINT32& unhandle_msg_timeout);
//����ǿ��ͬ���ȴ����������̹߳ر�
TaskErrorCode	tixClearManagedTask(const task_id_t& call_id);
TaskErrorCode	tixSetTaskName(const task_id_t& id, const task_name_t& name);
TaskErrorCode	tixSetTaskAttri(const task_id_t& id, const UINT32& unhandle_msg_timeout);
TaskErrorCode	tixGetTaskName(const task_id_t& id, task_name_t& name);

//�������
TaskErrorCode	tixAddManagedTask(const task_cls_t& cls, const task_name_t& name, const task_param_t& param, const task_routinefunc_t& routine, task_id_t& id);	//��������
//�Ƿ�ȴ�Ŀ��رգ���Ҫ��ȷ��������Լ��ر��Լ���رշ��й��̣߳�������ǿ�Ƹĳ��첽																																									
TaskErrorCode	tixDelManagedTask(const task_id_t& call_id, const task_id_t& target_id);
TaskErrorCode	tixGetTaskState(const task_id_t& id, TaskWorkState& state);
TaskErrorCode	tixGetTaskByName(const task_name_t& name, task_id_t& id);

//Debug
TaskErrorCode	tixPrintMeta();
