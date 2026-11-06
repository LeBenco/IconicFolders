// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Benco
#include "shell-safety.h"
#include "core.h"
#include <sddl.h>
#include <wil/resource.h>
#include <wil/result.h>
#include <vector>

namespace iconic {
namespace {
constexpr PCWSTR SafetyKey=L"Software\\IconicFolders\\Safety";
LONG watcherStarted=0;
ULONGLONG timestamp() noexcept {FILETIME time={};GetSystemTimeAsFileTime(&time);ULARGE_INTEGER value={};value.LowPart=time.dwLowDateTime;value.HighPart=time.dwHighDateTime;return value.QuadPart;}
class StateLock {
    wil::unique_handle mutex_;
public:
    StateLock() {
        HANDLE raw=nullptr;THROW_IF_WIN32_BOOL_FALSE(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&raw));wil::unique_handle token(raw);
        DWORD size=0;GetTokenInformation(token.get(),TokenUser,nullptr,0,&size);THROW_WIN32_IF(ERROR_INVALID_DATA,!size);
        std::vector<BYTE> data(size);THROW_IF_WIN32_BOOL_FALSE(GetTokenInformation(token.get(),TokenUser,data.data(),size,&size));
        PWSTR rawSid=nullptr;THROW_IF_WIN32_BOOL_FALSE(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&rawSid));wil::unique_hlocal_string sid(rawSid);
        auto name=std::wstring(L"Global\\IconicFolders.Safety.")+sid.get();
        mutex_.reset(CreateMutexW(nullptr,FALSE,name.c_str()));THROW_LAST_ERROR_IF(!mutex_);
        DWORD wait=WaitForSingleObject(mutex_.get(),2000);THROW_WIN32_IF(ERROR_BUSY,wait!=WAIT_OBJECT_0&&wait!=WAIT_ABANDONED);
    }
    ~StateLock(){ReleaseMutex(mutex_.get());}
    StateLock(const StateLock&)=delete;
    StateLock& operator=(const StateLock&)=delete;
};
wil::unique_hkey openState() {
    HKEY raw=nullptr;THROW_IF_WIN32_ERROR(RegCreateKeyExW(HKEY_CURRENT_USER,SafetyKey,0,nullptr,0,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&raw,nullptr));return wil::unique_hkey(raw);
}
DWORD readDword(HKEY key,PCWSTR name) noexcept {DWORD value=0,size=sizeof(value);if(RegGetValueW(key,nullptr,name,RRF_RT_REG_DWORD,nullptr,&value,&size)!=ERROR_SUCCESS)return 0;return value;}
ShellFailureState readState(HKEY key) noexcept {
    ShellFailureState state;state.failures=readDword(key,L"Failures");state.disabled=readDword(key,L"Disabled")!=0;state.generation=readDword(key,L"Generation");
    DWORD size=sizeof(state.lastFailure);if(RegGetValueW(key,nullptr,L"LastFailure",RRF_RT_REG_QWORD,nullptr,&state.lastFailure,&size)!=ERROR_SUCCESS)state.lastFailure=0;return state;
}
void writeState(HKEY key,const ShellFailureState& state) {
    DWORD disabled=state.disabled?1:0;
    THROW_IF_WIN32_ERROR(RegSetValueExW(key,L"Failures",0,REG_DWORD,reinterpret_cast<const BYTE*>(&state.failures),sizeof(state.failures)));
    THROW_IF_WIN32_ERROR(RegSetValueExW(key,L"LastFailure",0,REG_QWORD,reinterpret_cast<const BYTE*>(&state.lastFailure),sizeof(state.lastFailure)));
    THROW_IF_WIN32_ERROR(RegSetValueExW(key,L"Generation",0,REG_DWORD,reinterpret_cast<const BYTE*>(&state.generation),sizeof(state.generation)));
    THROW_IF_WIN32_ERROR(RegSetValueExW(key,L"Disabled",0,REG_DWORD,reinterpret_cast<const BYTE*>(&disabled),sizeof(disabled)));
}
bool inExplorer() {
    wchar_t image[32768]={},windows[32768]={};DWORD length=GetModuleFileNameW(nullptr,image,32768);
    if(!length||length>=32768||!GetWindowsDirectoryW(windows,32768))return false;
    std::wstring expected=std::wstring(windows)+L"\\explorer.exe";
    return CompareStringOrdinal(image,-1,expected.c_str(),-1,TRUE)==CSTR_EQUAL;
}
void startWatcher(HMODULE module) {
    {StateLock lock;auto key=openState();auto state=readState(key.get());THROW_WIN32_IF(ERROR_CANCELLED,state.disabled);writeState(key.get(),state);}
    wchar_t path[32768]={};DWORD length=GetModuleFileNameW(module,path,32768);THROW_LAST_ERROR_IF(!length||length>=32768);
    fs::path executable=fs::path(path).parent_path()/L"IconicFolders.exe";
    wil::unique_handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,TRUE,GetCurrentProcessId()));THROW_LAST_ERROR_IF(!process);
    // An inherited real process handle remains queryable if Explorer exits
    // before this short-lived helper starts. Inherit this handle only.
    SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);THROW_WIN32_IF(ERROR_INVALID_DATA,!bytes);
    std::vector<BYTE> storage(bytes);auto attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    THROW_IF_WIN32_BOOL_FALSE(InitializeProcThreadAttributeList(attributes,1,0,&bytes));auto cleanup=wil::scope_exit([&]{DeleteProcThreadAttributeList(attributes);});
    HANDLE inherited=process.get();THROW_IF_WIN32_BOOL_FALSE(UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,&inherited,sizeof(inherited),nullptr,nullptr));
    std::wstring command=quoteArg(executable.native())+L" --watch-explorer "+std::to_wstring(reinterpret_cast<ULONG_PTR>(inherited));
    STARTUPINFOEXW startup={};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attributes;PROCESS_INFORMATION child={};
    THROW_IF_WIN32_BOOL_FALSE(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW,nullptr,executable.parent_path().c_str(),&startup.StartupInfo,&child));
    CloseHandle(child.hThread);CloseHandle(child.hProcess);
}
}

bool abnormalShellExit(DWORD code) noexcept {
    // Task Manager/TerminateProcess often use 0, 1 or -1. These are not crashes.
    return (code&0x80000000U)!=0&&code!=0xFFFFFFFFU&&code!=0xC000013AU;
}
bool recordShellFailure(ShellFailureState& state,ULONGLONG now,DWORD generation) noexcept {
    if(state.disabled||state.generation!=generation)return false;
    if(now<state.lastFailure||now-state.lastFailure>ShellFailureWindow)state.failures=0;
    if(state.failures<ShellFailureLimit)++state.failures;
    state.lastFailure=now;
    if(state.failures>=ShellFailureLimit){state.disabled=true;return true;}return false;
}
bool shellSafetyDisabled() noexcept {
    HKEY raw=nullptr;if(RegOpenKeyExW(HKEY_CURRENT_USER,SafetyKey,0,KEY_QUERY_VALUE,&raw)!=ERROR_SUCCESS)return false;wil::unique_hkey key(raw);return readDword(key.get(),L"Disabled")!=0;
}
HRESULT resetShellSafety() noexcept {
    try {StateLock lock;auto key=openState();auto state=readState(key.get());++state.generation;state.failures=0;state.lastFailure=0;state.disabled=false;writeState(key.get(),state);return S_OK;} CATCH_RETURN();
}
bool allowShellActivation(HMODULE module) noexcept {
    try {
        if(shellSafetyDisabled())return false;
        if(!inExplorer())return true;
        if(InterlockedCompareExchange(&watcherStarted,1,0)==0) {
            try {startWatcher(module);}catch(...){InterlockedExchange(&watcherStarted,0);throw;}
        }
        return true;
    }catch(...){return false;} // Fail closed, without showing a dialog in Explorer.
}
HRESULT watchExplorer(HANDLE inheritedProcess,bool& newlyDisabled) noexcept {
    newlyDisabled=false;
    try {
        wil::unique_handle process(inheritedProcess);THROW_WIN32_IF(ERROR_INVALID_HANDLE,!process||process.get()==INVALID_HANDLE_VALUE);
        DWORD target=GetProcessId(process.get());THROW_LAST_ERROR_IF(!target);
        // Deduplicate even if COM unloads/reloads our DLL during these 10 seconds.
        auto name=L"Local\\IconicFolders.Watch."+std::to_wstring(target);
        wil::unique_handle instance(CreateMutexW(nullptr,FALSE,name.c_str()));DWORD error=GetLastError();THROW_LAST_ERROR_IF(!instance);if(error==ERROR_ALREADY_EXISTS)return S_OK;
        DWORD generation=0;ULONGLONG started=timestamp();
        {StateLock lock;auto key=openState();auto state=readState(key.get());if(state.disabled)return S_OK;generation=state.generation;}
        DWORD wait=WaitForSingleObject(process.get(),ShellHealthMilliseconds);
        if(wait==WAIT_TIMEOUT) {
            StateLock lock;auto key=openState();auto state=readState(key.get());
            // A healthy concurrent Explorer must not erase newer crash reports.
            if(!state.disabled&&state.generation==generation&&state.lastFailure<=started){state.failures=0;state.lastFailure=0;writeState(key.get(),state);}return S_OK;
        }
        THROW_LAST_ERROR_IF(wait==WAIT_FAILED);THROW_WIN32_IF(ERROR_INVALID_DATA,wait!=WAIT_OBJECT_0);
        DWORD exit=0;THROW_IF_WIN32_BOOL_FALSE(GetExitCodeProcess(process.get(),&exit));if(!abnormalShellExit(exit))return S_OK;
        StateLock lock;auto key=openState();auto state=readState(key.get());newlyDisabled=recordShellFailure(state,timestamp(),generation);writeState(key.get(),state);return S_OK;
    } CATCH_RETURN();
}
}
