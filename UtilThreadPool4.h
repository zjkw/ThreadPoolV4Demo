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
		//Killǰ�ȴ��̳߳�ʱʱ��
		const UINT32	THREAD_CONFIRM_WAITTIME = 20;
		const UINT32	KILLTHREAD_WAITTIME = 250;
		const UINT32	EXITTHREADCONFORM_WAITTIME = 100;

		//sys����
		const INT32		SYSCMD_CLOSE_FIN = 0x01;
		const INT32		SYSCMD_CLOSE_ACK = 0x02;

		//Fetch
		const UINT32	FETCH_W2CMSG_DELAYTIME = 10;	//�೤����ִ��һ��Fetch��
		const UINT32	FETCH_W2CMSG_NUMONCE = 10;		//���Fetchϣ��ȡ����������
		const UINT32	FETCH_W2CMSG_LOOPNUM = 10;		//ÿȡһ�����ݳ��Զ��ٴ�try_pop����Ϊ�����е�������ϵͳ������log������Ӧ�ò��ע��
		const UINT32	CHECK_C2WMSG_ONLYEXITNUM = 100;	//����100�Σ�ֱ��Exit��Ϣ

		const TCHAR		SYS_CMDNO[] = _T("SYS_T_CMDNO");
		//--------------------------------------------------//

		//״̬��
		enum ObjMachineState
		{
			OMS_NONE = 0,
			OMS_NORMAL = 1,
			OMS_WAIT = 2,	//�ȴ��ر�
			OMS_CLOSE = 3,	//ȫ�ر�
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

		//���������߳�
		class  CThreadObjSink
		{
		public:
			CThreadObjSink()	//ͨ�����ڿ���֪ͨģʽ
			{
				m_eObjMachineState = OMS_NORMAL;
			}
			virtual ~CThreadObjSink()//����Ϊvirtual
			{
				ATLTRACE(_T("~CThreadObjSink: 0x%x\n"), this);

				DoWaveHand(TRUE);
			}
			virtual void OnW2CMsgCome(ATL::CComPtr<IXData> msg)
			{
			}
			//����TRUE��ʾ
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

				//�ر�
				while (m_eObjMachineState != OMS_CLOSE)
				{
					switch (m_eObjMachineState)
					{
					case OMS_NONE:
						ATLASSERT(FALSE);											//fall  through
					case OMS_NORMAL:
						//�ر���Ϣѭ������������ڵ���Ϣ
						if (bDriving)
						{
							m_eObjMachineState = OMS_WAIT;	
						}
						else
						{
							//�������رն˷����ִ�����������Է������ȴ�����	
							CComPtr<IXData>	pIXData;
							Util::XData::CreateIXData(&pIXData);
							Util::XData::SetIXDataU32(pIXData, SYS_CMDNO, SYSCMD_CLOSE_ACK);//����SYSCMD_CLOSE_ACK
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
							Util::XData::SetIXDataU32(pIXData, SYS_CMDNO, SYSCMD_CLOSE_FIN);//����SYSCMD_CLOSE_FIN
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

							//����Ϣȥ��, ��Ϊ���ܱ���

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
							m_shTimer->DestroyWin();//���ٴ��ڣ���Ϊ������������OnTimer��������Ȼ�����Ѿ���������ô��Ϣѭ���Ѿ�û�м�ֵ�������delete���ڶ����ˣ�����ζ����OnTimer�������ٴ��ڶ���������atl�����߼������
							ATLTRACE(_T("[Wait_End Sink DoWaveHand] this: 0x%x, State: %d, bDriving: %d\n"), this, m_eObjMachineState, bDriving);
							OnWorkFinish();
						}
						break;
					case OMS_CLOSE:		//ȫ�ر�
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
			//ϣ�����ŵ���ֹ�������
			virtual BOOL	PostExitMsg()						//ʹ�����߳��յ���Ϣ�����������˳�
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
				Hwnd = m_shTimer->GetHwnd(); //Ĭ�ϻ����m_shWinMsg��Enable
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
			std::shared_ptr<concurrent_ixdata_channel>		m_queueC2W;	//���ഴ��
			std::shared_ptr<concurrent_ixdata_channel>		m_queueW2C;	//���ഴ��
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

				//tbd׷�٣������������ٶ����Ż�
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
								if (OMS_NORMAL == m_eObjMachineState)	//��������
								{
									i = FETCH_W2CMSG_LOOPNUM;	//����ѭ��
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

				//��������ûȡ�ꣿ�����´�
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
			virtual ~IThreadObjWork(){}//����Ϊvirtual
			
			virtual void Run_InWorkThread(void* pParam) = 0;	//ע����Ϊ��Ҫ���ǵ����������˳���������ʱ������Ҫ����Sleep������ܴ���m_u32WaitSleep/2
			virtual BOOL SetSinkMeta(HWND Hwnd, UINT32 MsgID, UINT32 WaitSleep, std::shared_ptr<concurrent_ixdata_channel> c2w, std::shared_ptr<concurrent_ixdata_channel> w2c) = 0;
			virtual BOOL CheckExit_InWorkThread() = 0;
			virtual BOOL DoExit_InWorkThread() = 0;
			virtual void SetTaskID(UINT64 u64TaskID) = 0;
			virtual void NotifyNewW2CMsg_InWorkThread() = 0;
			virtual BOOL CanDelete() = 0;
		};

		//���������ڹ����̣߳����죨��ʼ����������������ʼ�����������������߳�
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
			virtual ~CThreadObjWork()	//����Ϊvirtual
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
			//ִ���߳��������һ���ʱ��������������Ա���ǿ����ֹ�̣߳�����TRUE��ʾ�ⲿϣ�����˳�
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
			std::shared_ptr<concurrent_ixdata_channel>		m_queueC2W;	//����������ú�������
			std::shared_ptr<concurrent_ixdata_channel>		m_queueW2C;	//����������ú�������
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
				//�ر�
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
							//�������رն˷����ִ�����������Է������ȴ�����	
							CComPtr<IXData>	pIXData;
							Util::XData::CreateIXData(&pIXData);
							Util::XData::SetIXDataU32(pIXData, SYS_CMDNO, SYSCMD_CLOSE_ACK);//����SYSCMD_CLOSE_ACK
							PostW2CMsgHelper_InWorkThread(pIXData);

							m_eObjMachineState = OMS_CLOSE;
						}
						ATLTRACE(_T("[Normal Work DoWaveHand] this: 0x%x, State: %d, bDriving: %d\n"), this, m_eObjMachineState, bDriving);
						break;
					case OMS_WAIT:
						{
							CComPtr<IXData>	pIXData;
							Util::XData::CreateIXData(&pIXData);
							Util::XData::SetIXDataU32(pIXData, SYS_CMDNO, SYSCMD_CLOSE_FIN);//����SYSCMD_CLOSE_FIN
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
					case OMS_CLOSE:		//ȫ�ر�
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
				//tbd׷�٣������������ٶ����Ż�
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
								if (OMS_NORMAL == m_eObjMachineState)	//��������
								{
									i = try_num;	//����ѭ��
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
			TSC_CORE = 1,	//����ҵ��ģ��
			//�ļ���������
			TSC_FILETRANS_NORMAL ,  // �����ļ���
			TSC_FILETRANS_PIC,             //ͼƬ��
			TSC_FILETRANS_RECORD,   //¼���ļ���
			TSC_FILETRANS_NET_DETECTION,    //����̽���
			TSC_FILETRANS_DOWN_FILE,         //down file  max 5
			TSC_FILETRANS_UPLOAD_LOG,		 //��־�ļ��ϴ� MAX 5
			TSC_NETBROKEN_CHECK,	//������
			TSC_ALIOSSKEY,    //��ȡ����key���̲߳� �ұ��� ���ҽ���һ����Run

			TSC_HTTPS_REQUEST,	//���ڵ�����CURL���HTTPS�������߳�
			TSC_DOMAIN_DETECT,	//̽���������

			TSC_EMAILPLANE,        //�ʼ��ƻ�
			TSC_EXTRACT_PIC,        // ��ѹ�ļ�ͼƬ
			TSC_RENDER_ENGINE_RECORD,     // ��Ⱦ����---¼��
			TSC_RENDER_ENGINE_MOIVE,      // ��Ⱦ����---��Ӱ����
			TSC_RENDER_ENGINE_MISC,    // ��Ⱦ����---����
			TSC_RENDER_ENGINE_MP4PLAYBACK,  //MP4����
		};
		COMMON_API void		SetThreadNum(ThreadSlotClass cls, UINT8 high);	//Ĭ��ÿ�����ͻ���THREADNUM_DEFAULT���߳�
		COMMON_API UINT64	AddTask(ThreadSlotClass cls, IThreadObjSink* pISink, IThreadObjWork* pIWork, void* pThreadParam);	//ע��pParam�����ڣ������new����ָ�룬��������Ϊsink��һ����Ա����������������ʱ���ͷ�,����0��ʾʧ��
		COMMON_API void		DelTask(UINT64 id);
		COMMON_API void		DoExit();
	}
}
