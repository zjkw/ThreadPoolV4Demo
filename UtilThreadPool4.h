#pragma once

#include <basetsd.h>
#include <memory>

#include <queue>
#include <mutex>

#include "BaseDefine.h"
#include "UtilXData.h"

namespace Util
{
	namespace ThreadPool4
	{
		//Kill前等待线程超时时间
		const UINT32	THREAD_CONFIRM_WAITTIME = 20;
		const UINT32	KILLTHREAD_WAITTIME = 250;
		const UINT32	EXITTHREADCONFORM_WAITTIME = 100;

		//sys命令
		const INT32		SYSCMD_CLOSE_FIN = 0x01;
		const INT32		SYSCMD_CLOSE_ACK = 0x02;

		//Fetch
		const UINT32	FETCH_W2CMSG_DELAYTIME = 10;	//多长毫秒执行一次Fetch？
		const UINT32	FETCH_W2CMSG_NUMONCE = 10;		//这次Fetch希望取出多少数据
		const UINT32	FETCH_W2CMSG_LOOPNUM = 10;		//每取一次数据尝试多少次try_pop？因为数据有但可能是系统本身如log而不是应用层关注的
		const UINT32	CHECK_C2WMSG_ONLYEXITNUM = 100;	//最多查100次，直到Exit消息

		const TCHAR		SYS_CMDNO[] = _T("SYS_T_CMDNO");
		//--------------------------------------------------//

		//状态机
		enum ObjMachineState
		{
			OMS_NONE = 0,
			OMS_NORMAL = 1,
			OMS_WAIT = 2,	//等待关闭
			OMS_CLOSE = 3,	//全关闭
		};

		template<typename T>
		class CThreadSafeQueue
		{
		public:
			CThreadSafeQueue()
			{
			}
			virtual ~CThreadSafeQueue()
			{
				ATLTRACE(_T("[~CThreadSafeQueue] this: 0x%x\n"), this);
			}
			size_t	size()
			{
				std::unique_lock <std::mutex> lck(_lock);
				return _queue.size();
			}
			bool	empty()
			{
				std::unique_lock <std::mutex> lck(_lock);
				return _queue.empty();
			}
			void	push(T t)
			{
				std::unique_lock <std::mutex> lck(_lock);
				_queue.push(t);
			}
			BOOL	try_pop(T& t)
			{
				std::unique_lock <std::mutex> lck(_lock);
				if (!_queue.empty())
				{
					t = _queue.front();
					_queue.pop();
					return TRUE;
				}
				return FALSE;
			}

		private:
			std::mutex							_lock;
			std::queue<T>						_queue;
		};

		typedef std::shared_ptr<CThreadSafeQueue<ATL::CComPtr<IXData>>>		thread_ixdata_channel_ptr;

		//工作于主线程
		class  CThreadObjSink
		{
		public:
			CThreadObjSink()	//通过窗口快速通知模式
			{
				m_eObjMachineState = OMS_NORMAL;
			}
			virtual ~CThreadObjSink()//必须为virtual
			{
				ATLTRACE(_T("~CThreadObjSink: 0x%x\n"), this);

				DoWaveHand(TRUE);
			}
			virtual void OnW2CMsgCome(ATL::CComPtr<IXData> msg)
			{
			}
			//返回TRUE表示
			virtual BOOL OnWinPeekMsg()
			{
				return TRUE;
			}
			UINT64	GetTaskID()
			{ 
				return m_u64TaskID; 
			}
			BOOL CanDelete()
			{
				return m_eObjMachineState == OMS_CLOSE;
			}

		protected:
			void	SetTaskID(UINT64 u64TaskID)
			{
				m_u64TaskID = u64TaskID;
			}
			void	SetFetchParam(UINT32 u32TimerInterval, UINT32 u32FetchNumOnce)
			{
				m_u32TimerInterval = u32TimerInterval;
				m_u32FetchNumOnce = u32FetchNumOnce;
				ATLASSERT(m_shTimer);
				m_shTimer->Start(_T("FetchDelayTimer"), m_u32TimerInterval);
			}
			void	DoWaveHand(BOOL bDriving)
			{
				ATLTRACE(_T("[Enter Sink DoWaveHand] this: 0x%x, State: %d, bDriving: %d, TID: %u\n"), this, m_eObjMachineState, bDriving, GetCurrentThreadId());

				//关闭
				while (m_eObjMachineState != OMS_CLOSE)
				{
					switch (m_eObjMachineState)
					{
					case OMS_NONE:
						ATLASSERT(FALSE);											//fall  through
					case OMS_NORMAL:
						//关闭消息循环里面这个窗口的消息
						if (bDriving)
						{
							m_eObjMachineState = OMS_WAIT;	
						}
						else
						{
							//向主动关闭端发起回执，这样结束对方阻塞等待过程	
							CComPtr<IXData>	pIXData;
							Util::XData::CreateIXData(&pIXData);
							Util::XData::SetIXDataU32(pIXData, SYS_CMDNO, SYSCMD_CLOSE_ACK);//发送SYSCMD_CLOSE_ACK
							PostC2WMsgHelper(pIXData);

							m_eObjMachineState = OMS_CLOSE;
							OnWorkFinish();
						}
						ATLTRACE(_T("[Normal Sink DoWaveHand] this: 0x%x, State: %d, bDriving: %d\n"), this, m_eObjMachineState, bDriving);
						break;
					case OMS_WAIT:	
						{							
							CComPtr<IXData>	pIXData;
							Util::XData::CreateIXData(&pIXData);
							Util::XData::SetIXDataU32(pIXData, SYS_CMDNO, SYSCMD_CLOSE_FIN);//发送SYSCMD_CLOSE_FIN
							PostC2WMsgHelper(pIXData);	

							ATLTRACE(_T("[Wait_Send_Fin Sink DoWaveHand] this: 0x%x, State: %d, bDriving: %d, TID: %u\n"), this, m_eObjMachineState, bDriving, GetCurrentThreadId());

							while (TRUE)
							{
								CComPtr<IXData> msg;
								if (GetW2CMsgHelper(msg))
								{
									UINT32	cmdno = 0;
									Util::XData::GetIXDataU32(msg, SYS_CMDNO, &cmdno);
									if (cmdno == SYSCMD_CLOSE_ACK)
									{
										break;
									}
								}
								else
								{
									Sleep(THREAD_CONFIRM_WAITTIME);
								}
							}

							//将消息去掉, 因为可能崩溃

							if (m_shTimer)
							{
								m_shTimer->UnhookEvent(evtOnThreadTimer, this, &CThreadObjSink::OnFetchDelayTimer);
								m_shTimer->UnhookEvent(evtOnAssociateMsg, this, &CThreadObjSink::OnNewMsg);
#if 0
								MSG msg;
								HWND hWnd = m_shTimer->GetHwnd(FALSE);
								while (hWnd)
								{
									if (PeekMessage(&msg, hWnd, 0, 0, PM_REMOVE))
									{
										if (msg.message == WM_QUIT)
										{
											break;
										}
									}
									else
									{
										break;
									}
								}
#endif								
							}
													
							m_eObjMachineState = OMS_CLOSE;
							m_shTimer->DestroyWin();//销毁窗口，因为本函数可能是OnTimer触发，既然故事已经结束，那么消息循环已经没有价值，但如果delete窗口对象了，这意味着在OnTimer里面销毁窗口对象本身，按照atl窗口逻辑会崩溃
							ATLTRACE(_T("[Wait_End Sink DoWaveHand] this: 0x%x, State: %d, bDriving: %d\n"), this, m_eObjMachineState, bDriving);
							OnWorkFinish();
						}
						break;
					case OMS_CLOSE:		//全关闭
						break;
					}
				}

				m_queueC2W = nullptr;
				m_queueW2C = nullptr;

				if (m_shTimer)
				{
					m_shTimer->UnhookEvent(evtOnThreadTimer, this, &CThreadObjSink::OnFetchDelayTimer);
					m_shTimer->UnhookEvent(evtOnAssociateMsg, this, &CThreadObjSink::OnNewMsg);
				}
			}
			//希望文雅的终止的情况下
			virtual BOOL	PostExitMsg()						//使工作线程收到消息后酌情主动退出
			{
				ATLTRACE(_T("[PostExitMsg Sink] this: 0x%x, State\n"), this, m_eObjMachineState);
				DoWaveHand(TRUE);
				return TRUE;
			}

			BOOL	PostC2WMsg(CComPtr<IXData>	pIXData)
			{
				if (m_eObjMachineState != OMS_NORMAL)
				{
					return FALSE;
				}
				return PostC2WMsgHelper(pIXData);
			}
			BOOL	GetW2CMsg(CComPtr<IXData>& pIXData)
			{
				if (m_eObjMachineState != OMS_NORMAL)
				{
					return FALSE;
				}
				return GetW2CMsgHelper(pIXData);
			}
			virtual BOOL GetSinkMeta(HWND& Hwnd, UINT32& MsgID, UINT32& WaitSleep, std::shared_ptr<concurrent_ixdata_channel>& c2w, std::shared_ptr<concurrent_ixdata_channel>& w2c)
			{
				ATLASSERT(m_shTimer);
				Hwnd = m_shTimer->GetHwnd(); //默认会调用m_shWinMsg的Enable
				MsgID = m_shTimer->GetMsgID();

				c2w = m_queueC2W;
				w2c = m_queueW2C;
				WaitSleep = KILLTHREAD_WAITTIME;
				
				return TRUE;
			}
			virtual void OnFetchMsg()
			{
				ATLASSERT(m_eObjMachineState == OMS_NORMAL);
				for (size_t i = 0; i < m_u32FetchNumOnce && m_eObjMachineState == OMS_NORMAL; i++)
				{
					CComPtr<IXData> msg;
					if (GetW2CMsgHelper(msg))
					{
						UINT32	cmdno = 0;
						Util::XData::GetIXDataU32(msg, SYS_CMDNO, &cmdno);
						if(!cmdno)
						{
							OnW2CMsgCome(msg);
						}
		//				ATLTRACE(_T("OnFetchMsg\n"));
					}
					else
					{
						break;
					}
				}
			}

		private:
			DECLARE_EVENT_RECEIVER2(COMMON_API, CThreadObjSink)
			std::shared_ptr<concurrent_ixdata_channel>		m_queueC2W;	//本类创建
			std::shared_ptr<concurrent_ixdata_channel>		m_queueW2C;	//本类创建
			std::shared_ptr<XThreadTimerSinkHelper>			m_shTimer;
			UINT32											m_u32TimerInterval;
			UINT32											m_u32FetchNumOnce;
			UINT64											m_u64TaskID;
			ObjMachineState									m_eObjMachineState;

			BOOL	PostC2WMsgHelper(CComPtr<IXData> pIXData)
			{
				if (!m_queueC2W)
				{
					return FALSE;
				}

				m_queueC2W->push(pIXData);
				return TRUE;
			}
			BOOL	GetW2CMsgHelper(CComPtr<IXData>& pIXData)
			{
				if (!m_queueW2C)
				{
					return FALSE;
				}

				//tbd追踪，保持消费者速度最优化
				for (UINT32 i = 0; i < FETCH_W2CMSG_LOOPNUM; i++)
				{
					if (m_queueW2C->try_pop(pIXData))
					{
						UINT32	cmdno = 0;
						Util::XData::GetIXDataU32(pIXData, SYS_CMDNO, &cmdno);

						if(cmdno)
						{
							switch (cmdno)
							{
							case SYSCMD_CLOSE_FIN:
								if (OMS_NORMAL == m_eObjMachineState)	//避免重入
								{
									i = FETCH_W2CMSG_LOOPNUM;	//跳出循环
									DoWaveHand(FALSE);
								}
								break;
							default:
								return TRUE;
							}

						}
						else
						{
							return TRUE;
						}
					}
					else
					{
						break;
					}
				}

				return FALSE;
			}
			void OnNewMsg(WPARAM wParam, LPARAM lParam)
			{
				ATLASSERT(m_shTimer);
				if (!m_shTimer->IsActive())
				{
					m_shTimer->Start(_T("FetchDelayTimer"), m_u32TimerInterval);
				}
			}
			void OnFetchDelayTimer()
			{
				ATLASSERT(m_shTimer);
				OnFetchMsg();

				//还有数据没取完？继续下次
				if (!m_queueW2C || m_queueW2C->empty())
				{
					m_shTimer->Stop(_T("FetchDelayTimer"));
				}
			}
		};

		//--------------------------------------------------//
		class  IThreadObjWork
		{
		public:
			IThreadObjWork(){  }
			virtual ~IThreadObjWork(){}//必须为virtual
			
			virtual void Run_InWorkThread(void* pParam) = 0;	//注意因为需要考虑到尽量主动退出，所以在时间上需要留意Sleep间隔不能大于m_u32WaitSleep/2
			virtual BOOL SetSinkMeta(HWND Hwnd, UINT32 MsgID, UINT32 WaitSleep, std::shared_ptr<concurrent_ixdata_channel> c2w, std::shared_ptr<concurrent_ixdata_channel> w2c) = 0;
			virtual BOOL CheckExit_InWorkThread() = 0;
			virtual BOOL DoExit_InWorkThread() = 0;
			virtual void SetTaskID(UINT64 u64TaskID) = 0;
			virtual void NotifyNewW2CMsg_InWorkThread() = 0;
			virtual BOOL CanDelete() = 0;
		};

		//基本工作于工作线程，构造（初始化）和析构（反初始化）函数工作于主线程
		class  CThreadObjWork : public IThreadObjWork
		{
		public:
			CThreadObjWork()
			{
				m_queueC2W = NULL;
				m_queueW2C = NULL;
				m_hHostWnd = NULL;
				m_u32HostMsgID = 0;
				m_u32WaitSleep = KILLTHREAD_WAITTIME;
				m_u64TaskID = 0;
				m_eObjMachineState = OMS_NORMAL;
			}
			virtual ~CThreadObjWork()	//必须为virtual
			{
				DoWaveHand(TRUE);

				m_queueC2W = NULL;
				m_queueW2C = NULL;
				m_hHostWnd = NULL;
				m_u32HostMsgID = 0;
				m_u32WaitSleep = KILLTHREAD_WAITTIME;
				m_u64TaskID = 0;
			}
			UINT64	GetTaskID(){ return m_u64TaskID; }

		protected:
			//执行线程最好在在一个额定时间检查这个函数，以避免强制终止线程，返回TRUE表示外部希望其退出
			BOOL	CheckExit_InWorkThread()
			{
				return m_eObjMachineState == OMS_CLOSE;
			}
			BOOL	DoExit_InWorkThread()
			{
				DoWaveHand(TRUE);

				return TRUE;
			}
			void	SetTaskID(UINT64 u64TaskID)
			{ 
				m_u64TaskID = u64TaskID; 
			}
			void	NotifyNewW2CMsg_InWorkThread()
			{
				//ATLTRACE(_T("[NotifyNewW2CMsg_InWorkThread Work] this: 0x%x, State: %d, m_u32HostMsgID: %u, m_hHostWnd: 0x%x\n"), this, m_eObjMachineState, m_u32HostMsgID, m_hHostWnd);
				if (m_eObjMachineState == OMS_NORMAL && m_hHostWnd && m_u32HostMsgID)
				{
					::PostMessage(m_hHostWnd, m_u32HostMsgID, 0L, 0L);
				}
			}
			BOOL	GetC2WMsg_InWorkThread(CComPtr<IXData>&	pIXData)
			{
				if (m_eObjMachineState != OMS_NORMAL)
				{
					return FALSE;
				}
			
				if(!GetC2WMsgHelper_InWorkThread(pIXData))
				{
					return FALSE;
				}
				UINT32	cmdno = 0;
				Util::XData::GetIXDataU32(pIXData, SYS_CMDNO, &cmdno);
				
				return 0 == cmdno;
			}
			UINT32 GetC2WMsgCount()
			{
				if (m_eObjMachineState != OMS_NORMAL)
				{
					return 0;
				}
				return m_queueC2W->size();
			}
			BOOL	PostW2CMsg_InWorkThread(CComPtr<IXData>	pIXData)
			{
				if (m_eObjMachineState != OMS_NORMAL)
				{
					return FALSE;
				}
				return PostW2CMsgHelper_InWorkThread(pIXData);
			}
			BOOL CanDelete()
			{
				return m_eObjMachineState == OMS_CLOSE;
			}

			UINT32	m_u32WaitSleep;
		private:
			std::shared_ptr<concurrent_ixdata_channel>		m_queueC2W;	//本类仅仅引用和生命期
			std::shared_ptr<concurrent_ixdata_channel>		m_queueW2C;	//本类仅仅引用和生命期
			HWND	m_hHostWnd;
			UINT32	m_u32HostMsgID;
			UINT64	m_u64TaskID;
			ObjMachineState									m_eObjMachineState;
			
			virtual BOOL SetSinkMeta(HWND Hwnd, UINT32 MsgID, UINT32 WaitSleep, std::shared_ptr<concurrent_ixdata_channel> pC2W, std::shared_ptr<concurrent_ixdata_channel> pW2C)
			{
				m_u32HostMsgID = MsgID;
				m_queueC2W = pC2W;
				m_queueW2C = pW2C;
				m_hHostWnd = Hwnd;
				m_u32WaitSleep = WaitSleep;

				return TRUE;
			}
			void	DoWaveHand(BOOL bDriving)
			{
				ATLTRACE(_T("[Enter Work DoWaveHand] this: 0x%x, State: %d, bDriving: %d\n"), this, m_eObjMachineState, bDriving);
				//关闭
				while (m_eObjMachineState != OMS_CLOSE)
				{
					switch (m_eObjMachineState)
					{
					case OMS_NONE:
						ATLASSERT(FALSE);											//fall  through
					case OMS_NORMAL:
						if (bDriving)
						{
							m_eObjMachineState = OMS_WAIT;
						}
						else
						{
							//向主动关闭端发起回执，这样结束对方阻塞等待过程	
							CComPtr<IXData>	pIXData;
							Util::XData::CreateIXData(&pIXData);
							Util::XData::SetIXDataU32(pIXData, SYS_CMDNO, SYSCMD_CLOSE_ACK);//发送SYSCMD_CLOSE_ACK
							PostW2CMsgHelper_InWorkThread(pIXData);

							m_eObjMachineState = OMS_CLOSE;
						}
						ATLTRACE(_T("[Normal Work DoWaveHand] this: 0x%x, State: %d, bDriving: %d\n"), this, m_eObjMachineState, bDriving);
						break;
					case OMS_WAIT:
						{
							CComPtr<IXData>	pIXData;
							Util::XData::CreateIXData(&pIXData);
							Util::XData::SetIXDataU32(pIXData, SYS_CMDNO, SYSCMD_CLOSE_FIN);//发送SYSCMD_CLOSE_FIN
							PostW2CMsgHelper_InWorkThread(pIXData);

							ATLTRACE(_T("[Wait_Send_Fin Work DoWaveHand] this: 0x%x, State: %d, bDriving: %d\n"), this, m_eObjMachineState, bDriving);

							while (TRUE)
							{
								CComPtr<IXData> msg;
								if (GetC2WMsgHelper_InWorkThread(msg))
								{
									UINT32	cmdno = 0;
									Util::XData::GetIXDataU32(msg, SYS_CMDNO, &cmdno);
									if (cmdno == SYSCMD_CLOSE_ACK)
									{
										break;
									}
								}
								else
								{
									Sleep(THREAD_CONFIRM_WAITTIME);
								}
							}

							m_eObjMachineState = OMS_CLOSE;

							ATLTRACE(_T("[Wait_End Work DoWaveHand] this: 0x%x, State: %d, bDriving: %d\n"), this, m_eObjMachineState, bDriving);
						}
						break;
					case OMS_CLOSE:		//全关闭
						break;
					}
				}

				m_queueC2W = nullptr;
				m_queueW2C = nullptr;
			}
			BOOL	PostW2CMsgHelper_InWorkThread(CComPtr<IXData> pIXData)
			{
				m_queueW2C->push(pIXData);
				return TRUE;
			}
			BOOL	GetC2WMsgHelper_InWorkThread(CComPtr<IXData>& pIXData)
			{
				//tbd追踪，保持消费者速度最优化
				const size_t try_num = 5;
				for (size_t i = 0; i < try_num; i++)
				{
					if (m_queueC2W->try_pop(pIXData))
					{
						UINT32	cmdno = 0;
						Util::XData::GetIXDataU32(pIXData, SYS_CMDNO, &cmdno);
						if (cmdno)
						{
							switch (cmdno)
							{
							case SYSCMD_CLOSE_FIN:
								if (OMS_NORMAL == m_eObjMachineState)	//避免重入
								{
									i = try_num;	//跳出循环
									DoWaveHand(FALSE);
								}
								break;
							default:
								return TRUE;
							}
						}
						else
						{
							return TRUE;
						}
					}
				}

				return FALSE;
			}
		};

		//---------------------------------------------------//
		enum ThreadSlotClass
		{
			TSC_NONE = 0,
			TSC_CORE = 1,	//核心业务模块
			//文件传输类型
			TSC_FILETRANS_NORMAL ,  // 正常文件槽
			TSC_FILETRANS_PIC,             //图片槽
			TSC_FILETRANS_RECORD,   //录音文件槽
			TSC_FILETRANS_NET_DETECTION,    //网络探测的
			TSC_FILETRANS_DOWN_FILE,         //down file  max 5
			TSC_FILETRANS_UPLOAD_LOG,		 //日志文件上传 MAX 5
			TSC_NETBROKEN_CHECK,	//网络监测
			TSC_ALIOSSKEY,    //获取阿里key的线程槽 且必须 有且仅有一个在Run

			TSC_HTTPS_REQUEST,	//基于第三方CURL库的HTTPS请求处理线程
			TSC_DOMAIN_DETECT,	//探测解析域名

			TSC_EMAILPLANE,        //邮件计划
			TSC_EXTRACT_PIC,        // 解压文件图片
			TSC_RENDER_ENGINE_RECORD,     // 渲染引擎---录制
			TSC_RENDER_ENGINE_MOIVE,      // 渲染引擎---电影播放
			TSC_RENDER_ENGINE_MISC,    // 渲染引擎---杂项
			TSC_RENDER_ENGINE_MP4PLAYBACK,  //MP4播放
		};
		COMMON_API void		SetThreadNum(ThreadSlotClass cls, UINT8 high);	//默认每个类型会有THREADNUM_DEFAULT个线程
		COMMON_API UINT64	AddTask(ThreadSlotClass cls, IThreadObjSink* pISink, IThreadObjWork* pIWork, void* pThreadParam);	//注意pParam生命期，如果是new出来指针，建议是作为sink的一个成员变量，比如在析构时候释放,返回0表示失败
		COMMON_API void		DelTask(UINT64 id);
		COMMON_API void		DoExit();
	}
}
