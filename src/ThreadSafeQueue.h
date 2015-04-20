#ifndef THREADSAFEQUEUE_H
#define THREADSAFEQUEUE_H

#include <list>
using namespace std;
#include "util.h"


template <typename C>
class CThreadSafeQueue: protected list<C>
{
public:
	CThreadSafeQueue(HANDLE evEmpty=NULL, stdCallback cbOnEmpty=NULL, void * cbParam=NULL)
	{
		//手动触发，必须一个个来激活
		m_ev = ::CreateEvent(NULL, FALSE, FALSE, NULL);
		InitCS(&m_Crit);

		m_evEmpty = evEmpty;
		m_onEmpty = cbOnEmpty;
		m_cbParam = cbParam;
	}
	~CThreadSafeQueue()
	{
		DestCS(&m_Crit);
		::CloseHandle(m_ev);
		m_ev = NULL;
	}

	void push_front(C& c)
	{
		CSLock lock(m_Crit);
		__super::push_front(c);
		::SetEvent(m_ev);
	}

	void push(C& c, bool high_priv=false)
	{
		CSLock lock(m_Crit);
		if(!high_priv)
			push_back(c);
		else
			__super::push_front(c);
		::SetEvent(m_ev);
	}

	bool pop(C& c)
	{
		CSLock lock(m_Crit);
		if (__super::empty()) 
			return false;
		c = front();
		pop_front();
		if(!_empty())
			::SetEvent(m_ev);	//发信号，激活下一个
		return true;
	} 

	void push_n(list<C>& li)
	{
		CSLock lock(m_Crit);
		splice(end(),li);
		::SetEvent(m_ev);
	}

	void pop_all(list<C>& li)
	{
		CSLock lock(m_Crit);
		li.clear();
		swap(li);
		::ResetEvent(m_ev);
	}

	//最后清理，不需要唤醒等待的线程
	void clear()
	{
		CSLock lock(m_Crit);
		::ResetEvent(m_ev);
		__super::clear();
	}
	bool empty()
	{
		CSLock lock(m_Crit);
		return __super::empty();
	}
	bool _empty()
	{
		bool empty = __super::empty();
		if(empty)
		{
			if(m_evEmpty)
				::SetEvent(m_evEmpty);
			if(m_onEmpty)
				m_onEmpty(m_cbParam);
		}
		return __super::empty();
	}

	template<typename T>
	void clear_invalid(const T & arg, bool (*isvalid)(const C & item, const T &))
	{
		CSLock lock(m_Crit);
		list<C>::iterator it = begin();
		for (; it != end();)
			it = isvalid(*it, arg) ? ++it : erase(it);
	}

	//获得句柄
	HANDLE GetWaitHandle() { return m_ev; }
protected:
	HANDLE m_ev;
	HANDLE m_evEmpty;
	CRITICAL_SECTION m_Crit;
	stdCallback	m_onEmpty;
	void * m_cbParam;
};

#endif
