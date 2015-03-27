#include <stdlib.h>
#include <time.h>
#include <vector>
#include <cassert>
using namespace std;
#include "ThreadPool.h"
#include "Util.h"

void mythread_fun(void * data)
{
	int id = *(int*)data;
	int stime = 2000 + rand()%2000;
	log("thread:%ld: i am working task:%d ,during %.3f!\n", GetCurrentThreadId(), id, stime/1000.0);
	Sleep(stime);
	log("thread:%ld: task:%d work done!\n", GetCurrentThreadId(), id);
}
void mycancel_fun(bool not_running, void * data)
{
	int id = *(int*)data;
	if(not_running) 
		logc(COLOR_GREEN,"thread:%ld: task:%d canceled!\n", GetCurrentThreadId(), id);
	else
		logc(COLOR_RED, "thread:%ld: task:%d try canceled, but is running!\n", GetCurrentThreadId(), id);
}

struct timer_param
{
	timer_param(int _id, int due, int peri): id(_id), cnt(0), due_t(due), period(peri){ };
	//string info;
	int id;
	int cnt;
	int due_t;
	int period;
};

void myTimer_fun(void * data)
{
	timer_param * p = (timer_param*)data;
	p->cnt++;
	log("thread:%ld: timer task:%d run %d time\n", GetCurrentThreadId(), p->id, p->cnt);
}
void myTimer_clear(bool not_running, void * data)
{
	timer_param * p = (timer_param*)data;
	logc(COLOR_RED, "thread:%ld: timer task:%d end\n", GetCurrentThreadId(), p->id);
	delete p;
}


int main()
{
	CMyThreadPool::RunBeforeStart();
	srand((unsigned)time(0));	
	log("start main()\n");
	CMyThreadPool* mypool = new CMyThreadPool(5, 10);
	BOOL res = mypool->Init();
	if(!res)
		log("thread pool init fail\n");
	if(0)
	{//test task
		int id[50];
		for (int i=0; i < 50; i++)
		{
			id[i] = i;
			int res = mypool->PutTask(mythread_fun, id+i, mycancel_fun);
			if(res != 0)
			{
				log("Put task fail: i=%d\n", i);
				break;
			}
			Sleep(100+rand()%200);
		}
		log("end put task, now wait\n");
		log("main sleep 5 sec\n");
		Sleep(5000);
	}
	if(1)
	{ //test timer
		timer_param * p = new timer_param(3000, 3000, 0);
		mypool->PutTask(myTimer_fun, p, myTimer_clear, p->due_t, p->period);
		p = new timer_param(3001, 4000, 0);
		mypool->PutTask(myTimer_fun, p, myTimer_clear, p->due_t, p->period);
		p = new timer_param(3002, 5000, 0);
		mypool->PutTask(myTimer_fun, p, myTimer_clear, p->due_t, p->period);

		for (int i=0; i < 5; i++)
		{
			p = new timer_param(i+100, 2000+rand()%2000, 5000+rand()%1000);
			int res = mypool->PutTask(myTimer_fun, p, myTimer_clear, p->due_t, p->period);
			assert(res == 0);
			Sleep(1000+rand()%1000);
		}
		log("main sleep 30 sec\n");
		Sleep(30000);
	}

	mypool->WaitOnQuit();
	delete mypool;
	CMyThreadPool::RunAfterEnd();
	system("pause");
	return 0;
}
