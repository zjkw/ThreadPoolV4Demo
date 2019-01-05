#include "stdafx.h"
#include "UtilThreadPool3.h"
#include "ThreadPoolMgr3.h"
#include "Singleton.h"

//mgr�������ʱ���ü�������Ϊ�ʼ�ͻᴴ������ģ�飬��ʱ��Ҫ�������tbd
//---------------------------------------------------//
void	Util::ThreadPool3::SetThreadNum(ThreadSlotClass cls, UINT8 high)
{
	if (TRUE == CThreadPoolMgr3::IsObjectValid())
		Singleton<CThreadPoolMgr3>::Instance().SetThreadNum(cls, high);
}

UINT64	Util::ThreadPool3::AddTask(ThreadSlotClass cls, IThreadObjSink* pISink, IThreadObjWork* pIWork, void* pThreadParam)	//ע��pParam�����ڣ������new����ָ�룬��������Ϊsink��һ����Ա����������������ʱ���ͷ�
{
	if (TRUE == CThreadPoolMgr3::IsObjectValid())
		return Singleton<CThreadPoolMgr3>::Instance().AddTask(cls, pISink, pIWork, pThreadParam);
	return 0;
}

void	Util::ThreadPool3::DelTask(UINT64 id)	//Sink��Work���󲢲����ڲ�ɾ���������������ѹ�
{
	if (TRUE == CThreadPoolMgr3::IsObjectValid())
	    Singleton<CThreadPoolMgr3>::Instance().DelTask(id);
}

void		Util::ThreadPool3::DoExit()
{
	Singleton<CThreadPoolMgr3>::UnInstance();
}