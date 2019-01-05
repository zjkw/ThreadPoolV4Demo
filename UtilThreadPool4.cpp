#include "stdafx.h"
#include "UtilThreadPool3.h"
#include "ThreadPoolMgr3.h"
#include "Singleton.h"

//mgr对象操作时候不用加锁，因为最开始就会创建网络模块，当时需要后续添加tbd
//---------------------------------------------------//
void	Util::ThreadPool3::SetThreadNum(ThreadSlotClass cls, UINT8 high)
{
	if (TRUE == CThreadPoolMgr3::IsObjectValid())
		Singleton<CThreadPoolMgr3>::Instance().SetThreadNum(cls, high);
}

UINT64	Util::ThreadPool3::AddTask(ThreadSlotClass cls, IThreadObjSink* pISink, IThreadObjWork* pIWork, void* pThreadParam)	//注意pParam生命期，如果是new出来指针，建议是作为sink的一个成员变量，比如在析构时候释放
{
	if (TRUE == CThreadPoolMgr3::IsObjectValid())
		return Singleton<CThreadPoolMgr3>::Instance().AddTask(cls, pISink, pIWork, pThreadParam);
	return 0;
}

void	Util::ThreadPool3::DelTask(UINT64 id)	//Sink和Work对象并不会内部删除，仅仅与对象池脱钩
{
	if (TRUE == CThreadPoolMgr3::IsObjectValid())
	    Singleton<CThreadPoolMgr3>::Instance().DelTask(id);
}

void		Util::ThreadPool3::DoExit()
{
	Singleton<CThreadPoolMgr3>::UnInstance();
}