#ifndef _UTIL_H
#define _UTIL_H

#pragma once
#pragma warning (disable:4793)

#include<stdio.h>
#include<windows.h>

#define COLOR_RED	FOREGROUND_RED
#define COLOR_GREEN FOREGROUND_GREEN
#define COLOR_BLUE  FOREGROUND_BLUE
#define COLOR_YELLOW	(FOREGROUND_RED | FOREGROUND_GREEN)
#define COLOR_MAGENTA	(FOREGROUND_RED | FOREGROUND_BLUE)
#define COLOR_CYAN		(FOREGROUND_GREEN | FOREGROUND_BLUE)
#define COLOR_WHITE		(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)
#define DEFAULT_COLOR COLOR_WHITE

typedef void (* SimpleCallback)(void* user_data);
typedef void (__stdcall * stdCallback)(void* data);

//任务被清除的时候的回调
//@param not_running: true, 表示未运行已被撤销； false表示任务已经在运行，这时候需要做撤销（改变user_data的相关变量）
typedef void (* ClearCallback)(bool not_running, void* user_data);


extern CRITICAL_SECTION g_cs_log;

//用cout打印
#define USE_COUT

#ifdef USE_COUT
#include<iostream>
#endif

//设置控制台输出颜色
inline BOOL SetConsoleColor(WORD wAttributes)
{
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hConsole == INVALID_HANDLE_VALUE)
		return FALSE;
	return SetConsoleTextAttribute(hConsole, wAttributes);
}

inline size_t now_s(char * buf, size_t len)
{
	SYSTEMTIME sys;
	GetLocalTime(&sys);
	return sprintf_s(buf, len, "%d-%d-%d %d:%d:%d.%d\t",
					 sys.wYear,sys.wMonth,sys.wDay,sys.wHour,sys.wMinute,sys.wSecond,sys.wMilliseconds);
}

inline char * now()
{
	SYSTEMTIME sys;
	GetLocalTime(&sys);
	static char TS[255] = "";
	sprintf_s(TS, 255, "%d-%d-%d %d:%d:%d.%d\t",
		sys.wYear,sys.wMonth,sys.wDay,sys.wHour,sys.wMinute,sys.wSecond,sys.wMilliseconds);
	return &TS[0];
}

#ifdef _DEBUG
inline void logc(WORD color, char *pszFormat, ...)
{
	va_list   pArgList;
	va_start(pArgList, pszFormat);
	EnterCriticalSection(&g_cs_log);
	if(color != 0 && color != COLOR_WHITE)
		SetConsoleColor(color);
	char buf[1024];
	size_t n = now_s(buf, 1024);
	vsnprintf_s(buf+n, 1024-n, _TRUNCATE, pszFormat, pArgList);
#ifdef USE_COUT
	std::cout << buf;
#else
	fprintf(stdout, buf);
#endif
	if(color != 0 && color != COLOR_WHITE)
		SetConsoleColor(COLOR_WHITE);
	LeaveCriticalSection(&g_cs_log);
	va_end(pArgList);
}

inline void log(char *pszFormat, ...)
{
	va_list   pArgList;
	va_start(pArgList, pszFormat);
	EnterCriticalSection(&g_cs_log);
	char buf[1024];
	size_t n = now_s(buf, 1024);
	vsnprintf_s(buf+n, 1024-n, _TRUNCATE, pszFormat, pArgList);
#ifdef USE_COUT
	std::cout << buf;
#else
	fprintf(stdout, buf);
#endif
	LeaveCriticalSection(&g_cs_log);
	va_end(pArgList);
}
#else

#define log(...)
#define logc(...)

#endif

void GetProcessNum(int & logicNum, int & procCore);


//临界区操作api

inline void InitCS(CRITICAL_SECTION * cs){
	if(!::InitializeCriticalSectionAndSpinCount(cs, 4000))
		::InitializeCriticalSection(cs);
}

inline void DestCS(CRITICAL_SECTION * cs){
	::DeleteCriticalSection(cs);
}

class CSLock
{
public:
	CSLock(
		 _Inout_ CRITICAL_SECTION& cs,
		 _In_ bool bInitialLock = true,
		 _In_ bool bInitialcs = false);	//初始化，但是析构不释放资源
	~CSLock() throw();
	void Lock();
	void Unlock() throw();
private:
	CRITICAL_SECTION& m_cs;
	bool m_bLocked;
	// Private to avoid accidental use
	CSLock(_In_ const CSLock&) throw();
	CSLock& operator=(_In_ const CSLock&) throw();
};

inline CSLock::CSLock(
			  _Inout_ CRITICAL_SECTION& cs,
			  _In_ bool bInitialLock,
			  _In_ bool bInitialcs):
	m_cs(cs), m_bLocked(false)
{
	if(bInitialcs)
		InitCS(&m_cs);
	if(bInitialLock)
		Lock();
}
inline CSLock::~CSLock() throw()
{
	if(m_bLocked)
		Unlock();
}
inline void CSLock::Lock()
{
	::EnterCriticalSection(&m_cs);
	m_bLocked = true;
}
inline void CSLock::Unlock() throw()
{
	::LeaveCriticalSection(&m_cs);
	m_bLocked = false;
}

#endif
