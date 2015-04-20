#ifndef _THREAD_POOL_H
#define _THREAD_POOL_H
#pragma once
#include<windows.h>
#include<string>
using namespace std;

#include "util.h"
#include "ThreadSafeQueue.h"

/*
 *	目标： 
 *	1.简化接口，屏蔽win底层的数据结构
 *	2.跨windows版本实现
 *	3.实现定时器功能
 *	4.实现分组功能（预留接口）
 *
 */

typedef struct TPTask* PTPTask;
struct TimerTask;

class CMyThreadPool
{
public:
	CMyThreadPool(int ThreadMinimum=1, int ThreadMaximum=20);
	~CMyThreadPool(void);

	static void RunBeforeStart();
	static void RunAfterEnd();

	BOOL Init();
	//可直接调用，退出
	void WaitOnQuit()
	{
		if(Closed())
		{
			log("WaitOnQuit::closed, return\n");
			return;
		}
		bool need_wait = m_nThread > 0 ? true : false;
		Close();
		if(need_wait)
		{
			log("WaitOnQuit::need wait threads quits\n");
			WaitForSingleObject(m_evQuit, INFINITE);		
			log("WaitOnQuit::all threads quits\n");
		}
	}
	HANDLE GetWaitHandle() const {return m_evQuit;}
	void SetThreadMaximum(int newMaxThreadNum);

private:
	bool Closed() const {return m_state == pool_closed;}
	bool Running() const {return m_state == pool_running || m_state == pool_opening;}
	void Close();
	bool Empty();

public:
	/*
	 *	@param callback: 执行的函数指针
	 *	@param user_data:	用户数据参数，回调函数处理(用户负责分配和销毁)
	 *	@param cbClear: 任务被清理时候的调用, 是用来销毁参数user_data；为NULL时，如被撤销，系统默认delete user_data
	 *
	 *	@param duetime:	定时任务，表示从现在开始多久执行，单位是毫秒（没必要那么精细）(默认0为马上执行)
	 *	@param period:	定时任务，>0 表示这是一个循环执行任务，单位是毫秒。如果为默认0，表示这个任务只执行一次
	 *	@high_priv:		高优先级任务，会排在任务队列的前端(适合重做的任务)
	 *	@param group:	任务分组，暂时占位，以后会根据组来分配执行线程数(也就是说这个组的任务只能占有多少个线程)(可以利用
	 *		线程的私有队列来进行映射达到这个功能: 一个组 1:M 个线程)
	 *	@param task_key: 任务的key，用来在某个线程上排队
	 */
	int PutTask(SimpleCallback callback, PVOID user_data, ClearCallback cbClear = NULL,
					int duetime=0, int period=0, bool high_priv=false, 
					const char* group=NULL, const char * task_key=NULL);

	//可以用来做cbClear 的参数
	static void DoNothing(bool not_running, void* pt) { };

//group

private:
	//队列为空时候的回调
	static void CALLBACK TimerAPCProc(LPVOID lpArg, DWORD dwTimerLowValue, DWORD dwTimerHighValue);
	static void     __stdcall CALLBACK _TP_QueueEmpty(PVOID pool){ };
	static unsigned __stdcall CALLBACK _TP_WorkCallBack(PVOID pool);
	static void TimerTaskHandler(PVOID data);
	static void TimerCallback(PVOID data);
	static void LoadTimeTask(TimerTask *ptt, SimpleCallback cb, bool high_priv=false);

public:
	void erase_timer(TimerTask* ptt)
	{
		CSLock lock(m_lock);
		m_TimerTasks.remove(ptt);
	}

private:
	void Run(HANDLE hThread);

	PTPTask GetTask();
	void ReleaseTask(PTPTask pt);

	BOOL CreateThread();	//创建线程
	void CloseThread();		//关闭线程 及 对应的handle

private:
	enum PoolState { pool_closed, pool_closing, pool_opening, pool_running };

	//MemPool<TPTask> m_mPool;
	CRITICAL_SECTION m_lock;
	int m_minThread, m_maxThread, m_turn;

	volatile LONG m_nThread, m_nTask;
	volatile LONG m_state;	//PoolState

	CThreadSafeQueue<TPTask*> m_readyQueue;
	list<TPTask*> m_running;
	list<TimerTask*> m_TimerTasks;
	CRITICAL_SECTION m_lock_running;

	HANDLE m_evQuit;
	volatile BOOL m_alreadyMarkCancel;
};

#endif
