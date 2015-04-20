#include <assert.h>
#include <process.h>	//create thread
#include "ThreadPool.h"
#include "Util.h"

//线程最大空闲寿命10分钟
#define THREAD_MAX_SPARE_LIFE 10*60*1000

#ifdef _DEBUG
volatile LONG taskid = 0;
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif


struct _guard {int placeholer;} _stopthread;

typedef struct TPTask
{
	TPTask(SimpleCallback cb, PVOID _userdata, ClearCallback cbClear=NULL):
		callback(cb), user_data(_userdata), cb_clear(cbClear)
	{
#ifdef _DEBUG
		id = ::InterlockedIncrement(&taskid) - 1;
		log("task:%d created! \n", id);
#endif
	}

	SimpleCallback callback;
	ClearCallback cb_clear;
	PVOID user_data; 
#ifdef _DEBUG
	int id;
#endif
}*PTPTask;

typedef struct TimerTask: public TPTask
{
	TimerTask(SimpleCallback cb, PVOID _userdata, ClearCallback cbClear, int duetime, int peri):
		TPTask(cb, _userdata, cbClear), pool(NULL), timer(NULL), due(duetime), period(peri), state(0) { };
	//表示几个阶段， 定时中；	定时后；	执行中；	关闭；
	enum {TP_OPEN=0, TP_SLEEP, TP_AFT_SLEEP, TP_RUN, TP_CLOSED};
	bool set_state(long st_now, long st_want){
		long st = ::InterlockedCompareExchange(&state, st_want, st_now);
		if(st_now != st) return false;
		return true;
	}
	void complete()
	{
		pool->erase_timer(this);	//拿锁了, 保证同步
		if(timer)
		{
			CancelWaitableTimer(timer);
			CloseHandle(timer);
			timer = NULL;
		}
		logc(COLOR_RED, "timer task:%d complete! \n", id);
		destory();
	}
	void destory()
	{
		assert(timer == NULL);
		if(cb_clear)
			(*cb_clear)(true, user_data);
		else
			delete user_data;
		logc(COLOR_RED, "timer task:%d destory! \n", id);
		delete this;
	}

	CMyThreadPool * pool;
	HANDLE timer;
	int due; 
	int period;
	volatile long state;
}TimerTask;


//统一销毁, 所以这里不销毁ptt
//但是这个ptt还在全局的list中保留着(只有完成了 或者 最后摧毁才去掉)
void clear_timertask(bool not_running, void* pt) 
{ 
	TimerTask *ptt = (TimerTask *)pt;
	if(ptt->cb_clear)
	{
		(*ptt->cb_clear)(not_running, ptt->user_data);	//这里可以让用户程序提前返回
		ptt->cb_clear = CMyThreadPool::DoNothing;
	}
};


//*
CMyThreadPool::CMyThreadPool(int ThreadMinimum, int ThreadMaximum):
	m_minThread(ThreadMinimum), m_maxThread(ThreadMaximum), m_turn(0), m_nThread(0), m_nTask(0),
	m_state(pool_closed), m_readyQueue(NULL, CMyThreadPool::_TP_QueueEmpty, this),
	m_alreadyMarkCancel(FALSE)
{
	m_evQuit = ::CreateEvent(NULL, FALSE, FALSE, NULL);
	InitCS(&m_lock);
	InitCS(&m_lock_running);
}

CMyThreadPool::~CMyThreadPool(void)
{
	log("in CMyThreadPool::~CMyThreadPool\n");
	WaitOnQuit();

	log("in CMyThreadPool::~CMyThreadPool, clear timer tasks\n");
	list<TimerTask*>::iterator it = m_TimerTasks.begin();
	for(;it != m_TimerTasks.end(); ++it)
		(*it)->destory();

	DestCS(&m_lock_running);
	DestCS(&m_lock);
	::CloseHandle(m_evQuit);
	assert(m_nThread==0);
	assert(m_readyQueue.empty());
	assert(m_running.empty());
}

void CMyThreadPool::RunBeforeStart()
{
	InitializeCriticalSection(&g_cs_log);
}
void CMyThreadPool::RunAfterEnd()
{
	DeleteCriticalSection(&g_cs_log);
}

BOOL CMyThreadPool::Init()
{
	int logicCpu, nCpu;
	GetProcessNum(logicCpu, nCpu);
	if(m_minThread < logicCpu*2)
		m_minThread = logicCpu*2;
	if(m_maxThread < m_minThread + logicCpu*2)
		m_maxThread = m_minThread + logicCpu*2;

	m_state = pool_opening;
	for(int i=0; i < m_minThread; i++)
		if(!CreateThread()) 
			goto init_cleanup;
	m_state = pool_running;
	return TRUE;

init_cleanup:
	m_state = pool_closing;
	for(int nThread = m_nThread; nThread > 0; --nThread)
		CloseThread();
	m_state = pool_closed;
	return FALSE;
}

typedef struct __tag1
{
	CMyThreadPool * pool;
	HANDLE hdThread;
}__tag1;

BOOL CMyThreadPool::CreateThread()
{
	__tag1* t = (__tag1*)malloc(sizeof(__tag1));
	t->pool = this;
	HANDLE hd = (HANDLE)_beginthreadex(NULL, 0, CMyThreadPool::_TP_WorkCallBack, t, CREATE_SUSPENDED, NULL);
	t->hdThread = hd; 
	if(hd == 0)
	{
		log("CreateThread failed. LastError: %u\n", GetLastError());  
		free(t);
		return FALSE;
	}
	::InterlockedIncrement(&m_nThread);
	::ResumeThread(hd);
	return TRUE;
}

unsigned __stdcall CMyThreadPool::_TP_WorkCallBack(PVOID tag)
{
	__tag1 * t = (__tag1*)tag;
	CMyThreadPool * tp = t->pool;
	HANDLE hThread = t->hdThread;
	free(t);
	tp->Run(hThread);
	return 0;
}

void CMyThreadPool::CloseThread()
{
	GetTask();
	TPTask* ptask = new TPTask(NULL, &_stopthread);
	m_readyQueue.push(ptask);
}

void CMyThreadPool::SetThreadMaximum(int newMaxThreadNum)
{
	int logicCpu, nCpu;
	GetProcessNum(logicCpu, nCpu);
	m_maxThread = newMaxThreadNum;
	if(m_maxThread < m_minThread + logicCpu*2)
		m_maxThread = m_minThread + logicCpu*2;
}

PTPTask CMyThreadPool::GetTask()
{
	long taskn = ::InterlockedIncrement(&m_nTask);
	if(!Closed() && 
	   m_nThread < m_maxThread && 
	   (taskn > (m_turn + 2.718) * m_nThread))
	{
		m_turn++;
		CreateThread();
	}
	return NULL;
}
void CMyThreadPool::ReleaseTask(PTPTask pt)
{
	//pt->~TPTask();
	long taskn = ::InterlockedDecrement(&m_nTask);
	if(taskn == 0)
		m_turn = 0;
	delete pt;
}

//为什么要单独拿出来丢到队列？因为set timer只是影响执行的线程,
//所以必须是线程池内的线程来set timer， 其超时后导致一个执行APC
void CMyThreadPool::TimerTaskHandler(void * param)
{
	TimerTask* ptt = (TimerTask*)param;
	//设置task run
	ptt->set_state(TimerTask::TP_OPEN, TimerTask::TP_SLEEP);
	if(ptt->state == TimerTask::TP_CLOSED)
		return;
	int due = ptt->due ? ptt->due : ptt->period;
	if(due == 0)
	{//(一次性)任务完成了
		ptt->complete();
		return;
	}
	HANDLE timer = ptt->timer;
	if(!timer)//手动复原, one time
		ptt->timer = timer = CreateWaitableTimer(NULL, TRUE, NULL);

	LARGE_INTEGER liDueTime;
	liDueTime.QuadPart = -10*1000LL;	//100ns为单位
	liDueTime.QuadPart *= due;
	ptt->due = 0;
	//设置异步定时器
	if(!SetWaitableTimer(timer, &liDueTime, 0, CMyThreadPool::TimerAPCProc, ptt, 0))
	{
		log("SetWaitableTimer failed (%d)\n", GetLastError());
		ptt->complete();
	}
	//log("set timer:%d\n", ptt->id);
	return;
}

//定时器的APC回调，也就是时间到了，它的唯一作用就是把回调任务放入队列
//同时也表明这个线程没有退出，那么pool肯定没有摧毁（它要等待所有线程退出）
VOID CALLBACK CMyThreadPool::TimerAPCProc(
   LPVOID lpArg,               // Data value
   DWORD dwTimerLowValue,      // Timer low value
   DWORD dwTimerHighValue )    // Timer high value

{
	// Formal parameters not used in this example.
	UNREFERENCED_PARAMETER(dwTimerLowValue);
	UNREFERENCED_PARAMETER(dwTimerHighValue);

	TimerTask	*ptt = (TimerTask*)lpArg;
	log("in TimerAPCProc, id:%d\n", ptt->id);
	ptt->set_state(TimerTask::TP_SLEEP, TimerTask::TP_AFT_SLEEP);
	
	LoadTimeTask(ptt, CMyThreadPool::TimerCallback, true);
}

//APC装载了这个回调
void CMyThreadPool::TimerCallback(void * param)
{
	TimerTask	*ptt = (TimerTask*)param;
	if(ptt->set_state(TimerTask::TP_AFT_SLEEP, TimerTask::TP_RUN))
	{
		(*ptt->callback)(ptt->user_data);	//user callback
		ptt->set_state(TimerTask::TP_RUN, TimerTask::TP_OPEN);
	}
	if(ptt->state == TimerTask::TP_CLOSED) 
		return;

	//准备重新重做
	LoadTimeTask(ptt, TimerTaskHandler);
}

void CMyThreadPool::LoadTimeTask(TimerTask *ptt, SimpleCallback cb, bool high_priv/*=false*/)
{
	CMyThreadPool *pool = ptt->pool;
	PTPTask ptask = new TPTask(cb, ptt, clear_timertask);
	pool->GetTask(); 
	pool->m_readyQueue.push(ptask, high_priv);
}

//外部线程
int CMyThreadPool::PutTask(SimpleCallback callback, PVOID user_data, ClearCallback cbClear,
				int duetime, int period, bool high_priv, const char * group, const char * task_key)
{
	CSLock lock(m_lock);
	if(Closed()) return -1;
	PTPTask ptask = NULL;

	if(duetime == 0 && period == 0)
	{//普通任务
		ptask = GetTask();
		if(NULL == ptask)
		{
			ptask = new TPTask(callback, user_data, cbClear);
			if(NULL == ptask)
			{
				log("new task fail\n");
				return -2;
			}
		}
		else
			new(ptask) TPTask(callback, user_data, cbClear);
		m_readyQueue.push(ptask, high_priv);
		return 0;
	}

	TimerTask * pt_task = new TimerTask(callback, user_data, cbClear, duetime, period);
	pt_task->pool = this;
	logc(COLOR_GREEN, "new timer task create, duetime:%d, period:%d\n", duetime, period);

	m_TimerTasks.push_back(pt_task);
	//丢入线程池里，让线程池线程去激活timer
	LoadTimeTask(pt_task, TimerTaskHandler, true);
	return 0;
}

//TODO:支持线程的私有队列
void CMyThreadPool::Run(HANDLE hThread)
{
	HANDLE ev = m_readyQueue.GetWaitHandle();
	PTPTask ptask;
	while(!Closed() || !Empty())
	{
		bool get = m_readyQueue.pop(ptask);
		if(!get || ptask == NULL)
		{
			DWORD res = WaitForSingleObjectEx(ev, THREAD_MAX_SPARE_LIFE, TRUE);
			if(res == WAIT_IO_COMPLETION)
			{ //APC, wait again
				res = WaitForSingleObjectEx(ev, THREAD_MAX_SPARE_LIFE, TRUE);	//保证定时任务不会只在一个线程里跑
			}
			if(res == WAIT_TIMEOUT)
			{//end spare thread
				::InterlockedIncrement(&m_nThread);
				if(::InterlockedDecrement(&m_nThread) > m_minThread)
				{//ok
					goto quit;
				}
			}
			continue;
		}

		if(ptask->callback == NULL && 
		   ptask->user_data == &_stopthread)
		{
			ReleaseTask(ptask);
			::InterlockedIncrement(&m_nThread);
			if(1 == ::InterlockedDecrement(&m_nThread))//最后一个走正常流程
			{
				logc(COLOR_YELLOW, "meet quit guard, but is last one, do next\n");
				CSLock lock(m_lock);	//等一下吧，不然有状态不一致问题要解决
				assert(Closed());
				continue;	//没有break，是因为要清空队列
			}
			else
			{//退出
				logc(COLOR_YELLOW, "meet quit guard, quit\n");
				goto quit;
			}
		}

		//退出，取消了排队任务
		if(m_alreadyMarkCancel) //canceled
		{
			log("already canceled task\n");
canceled:
			if(ptask->cb_clear)
				(*ptask->cb_clear)(true, ptask->user_data);
			else
				delete ptask->user_data;
			ReleaseTask(ptask);
			continue;
		}

		list<TPTask*>::iterator pos;
		{
			CSLock rlock(m_lock_running);
			if(m_alreadyMarkCancel) //canceled
			{
				log("double check fail: cancel this task\n");
				goto canceled;
			}
			//放入running链
			pos = m_running.insert(m_running.end(), ptask);
		}

		try{
			assert(ptask->callback);
			//log("thread:%p run task\n", hThread);
			//执行回调
			(*ptask->callback)(ptask->user_data);
		}
		catch(...)
		{
			log("catch except from thread\n");
		}
		{
			CSLock rlock(m_lock_running);
			m_running.erase(pos);
		}
		ReleaseTask(ptask);
	}
	logc(COLOR_RED, "thread:%p ran out all task and pool is closed\n", hThread);
quit:
	//CSLock lock(m_lock);
	logc(COLOR_YELLOW, "thread:%p , thread id:%ld, quit\n", hThread, GetCurrentThreadId());
	::CloseHandle(hThread);
	if(0 == ::InterlockedDecrement(&m_nThread))
	{
		log("notify evQuit\n");
		::SetEvent(m_evQuit);
	}
}

bool CMyThreadPool::Empty()
{
	return m_readyQueue.empty();
}

void CMyThreadPool::Close()
{ 
	CSLock lock(m_lock);
	m_state = pool_closing; 
	m_alreadyMarkCancel = TRUE;
	{
		CSLock rlock(m_lock_running);
		list<TPTask*>::iterator it = m_running.begin();
		for(;it != m_running.end(); ++it)
		{
			log("notify running task quit\n");
			TPTask* pt = *it;
			if(pt->cb_clear)
				(*pt->cb_clear)(false, pt->user_data);
		}
	}

	list<TimerTask*>::iterator it2 = m_TimerTasks.begin();
	for(;it2 != m_TimerTasks.end(); ++it2)
	{
		TimerTask* ptt = *it2;
		log("in close, end timer task:%d\n", ptt->id);

		if(ptt->timer)
		{
			CancelWaitableTimer(ptt->timer); //会取消APC
			CloseHandle(ptt->timer);
			ptt->timer = NULL;
		}

		//这只关闭
		::InterlockedExchange(&ptt->state, TimerTask::TP_CLOSED);
	}

	log("create %d quit task\n",m_nThread);
	for(int nThread = m_nThread; nThread > 0; --nThread)
		CloseThread();
	//XXX 还是放这里
	m_state = pool_closed; 
}

