// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Benco
#include "setup.h"
#include <restartmanager.h>
#include <wil/resource.h>
#include <wil/result.h>
#include <string>

namespace iconic {
HRESULT restartExplorer(bool probeOnly,RestartTrace trace,bool forceAfterTimeout) noexcept {
    try {
        // The installer runs per user. Never launch an elevated desktop shell.
        HANDLE raw=nullptr;THROW_IF_WIN32_BOOL_FALSE(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&raw));wil::unique_handle token(raw);TOKEN_ELEVATION elevation={};DWORD bytes=0;
        THROW_IF_WIN32_BOOL_FALSE(GetTokenInformation(token.get(),TokenElevation,&elevation,sizeof(elevation),&bytes));THROW_WIN32_IF(ERROR_ACCESS_DENIED,elevation.TokenIsElevated!=0);
        HWND desktop=GetShellWindow();THROW_WIN32_IF(ERROR_NOT_FOUND,!desktop);DWORD pid=0;GetWindowThreadProcessId(desktop,&pid);
        if(trace)trace(L"Explorer PID",pid);
        wil::unique_handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid));THROW_LAST_ERROR_IF(!process);
        wchar_t path[32768]={},windows[32768]={};DWORD length=32768;THROW_IF_WIN32_BOOL_FALSE(QueryFullProcessImageNameW(process.get(),0,path,&length));THROW_LAST_ERROR_IF(!GetWindowsDirectoryW(windows,32768));auto expected=std::wstring(windows)+L"\\explorer.exe";
        THROW_WIN32_IF(ERROR_NOT_SUPPORTED,CompareStringOrdinal(path,-1,expected.c_str(),-1,TRUE)!=CSTR_EQUAL);
        RM_UNIQUE_PROCESS shell={};shell.dwProcessId=pid;FILETIME exit={},kernel={},user={};THROW_IF_WIN32_BOOL_FALSE(GetProcessTimes(process.get(),&shell.ProcessStartTime,&exit,&kernel,&user));
        DWORD session=0;wchar_t key[CCH_RM_SESSION_KEY+1]={};THROW_IF_WIN32_ERROR(RmStartSession(&session,0,key));auto end=wil::scope_exit([&]{RmEndSession(session);});
        THROW_IF_WIN32_ERROR(RmRegisterResources(session,0,nullptr,1,&shell,0,nullptr));UINT needed=0,count=1;DWORD reasons=0;RM_PROCESS_INFO info={};THROW_IF_WIN32_ERROR(RmGetList(session,&needed,&count,&info,&reasons));
        THROW_WIN32_IF(ERROR_FAIL_RESTART,count!=1||info.Process.dwProcessId!=pid||reasons||!info.bRestartable);
        if(trace)trace(L"RmGetList restartable",info.bRestartable);
        if(probeOnly)return S_OK;
        // This is an explicit, optional user action after the installer warns
        // to finish file operations. Restart Manager first requests shutdown,
        // then terminates an unresponsive Explorer after its standard timeout.
        // Do not restart a shell whose old process is still running.
        if(trace)trace(L"RmShutdown begin",0);
        DWORD shutdown=RmShutdown(session,RmShutdownOnlyRegistered|(forceAfterTimeout?RmForceShutdown:0),nullptr);
        if(trace)trace(L"RmShutdown result",shutdown);
        DWORD stopped=WaitForSingleObject(process.get(),10000);
        if(trace)trace(L"Original Explorer termination wait",stopped);
        if(trace)trace(L"RmRestart begin",0);
        DWORD restart=RmRestart(session,0,nullptr); // Required even on partial shutdown.
        if(trace)trace(L"RmRestart result",restart);
        DWORD originalExit=0;
        if(trace&&GetExitCodeProcess(process.get(),&originalExit))trace(L"Original Explorer exit code",originalExit);
        // RmRestart and Winlogon own shell recovery. Starting a second Explorer
        // here after a partial shutdown can cause a CoreUIComponents crash loop.
        THROW_IF_WIN32_ERROR(shutdown);
        THROW_WIN32_IF(ERROR_FAIL_SHUTDOWN,stopped!=WAIT_OBJECT_0);
        for(int i=0;i<200&&!GetShellWindow();++i)Sleep(50);
        if(trace)trace(L"Desktop recovered",GetShellWindow()?1:0);
        THROW_WIN32_IF(ERROR_FAIL_RESTART,!GetShellWindow());(void)restart;return S_OK;
    } CATCH_RETURN();
}
}
