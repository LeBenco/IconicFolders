// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Benco
#include "core.h"
#include "picker.h"
#include "setup.h"
#include "shell-safety.h"
#include "ui.h"
#include "resource.h"
#include <shobjidl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result.h>
#include <set>
#include <algorithm>
#include <thread>
#include <atomic>

using namespace iconic;
namespace {
std::wstring errorText(HRESULT hr) {
    if(hr==UnsupportedIniEncoding)return tr(IDS_UNSUPPORTED_ENCODING);
    if(hr==HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION))return tr(IDS_ENCODING);
    if(hr==HRESULT_FROM_WIN32(ERROR_BUSY))return tr(IDS_BUSY);
    wchar_t* text=nullptr; DWORD n=FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,hr,0,reinterpret_cast<wchar_t*>(&text),0,nullptr);
    std::wstring result=n?std::wstring(text,n):L"HRESULT "+std::to_wstring(static_cast<unsigned long>(hr)); LocalFree(text); return result;
}
void message(const std::wstring& title,const std::wstring& text,PCWSTR icon=TD_ERROR_ICON) {
    TaskDialog(nullptr,nullptr,L"IconicFolders",title.c_str(),text.c_str(),TDCBF_OK_BUTTON,icon,nullptr);
}
bool confirm(UINT title,UINT body,UINT button,const std::vector<fs::path>& paths) {
    if(paths.empty())return true;
    std::wstring list; for(const auto& p:paths)list+=p.native()+L"\n";
    auto head=tr(title),content=format(body,list),label=tr(button);
    TASKDIALOG_BUTTON buttons[]={{IDYES,label.c_str()}};
    TASKDIALOGCONFIG config={sizeof(config)}; config.pszWindowTitle=L"IconicFolders"; config.pszMainInstruction=head.c_str(); config.pszContent=content.c_str(); config.pszMainIcon=TD_WARNING_ICON; config.dwCommonButtons=TDCBF_CANCEL_BUTTON; config.cButtons=1; config.pButtons=buttons; config.nDefaultButton=IDCANCEL; config.dwFlags=TDF_SIZE_TO_CONTENT|TDF_ALLOW_DIALOG_CANCELLATION;
    int answer=0; THROW_IF_FAILED(TaskDialogIndirect(&config,&answer,nullptr,nullptr)); return answer==IDYES;
}
void safetyMessage() {
    auto title=tr(IDS_SAFETY_TITLE),body=tr(IDS_SAFETY_BODY),enable=tr(IDS_SAFETY_ENABLE);
    TASKDIALOG_BUTTON button={IDYES,enable.c_str()};TASKDIALOGCONFIG dialog={sizeof(dialog)};
    dialog.pszWindowTitle=L"IconicFolders";dialog.pszMainInstruction=title.c_str();dialog.pszContent=body.c_str();dialog.pszMainIcon=TD_WARNING_ICON;
    dialog.dwCommonButtons=TDCBF_CLOSE_BUTTON;dialog.cButtons=1;dialog.pButtons=&button;dialog.nDefaultButton=IDCLOSE;dialog.dwFlags=TDF_SIZE_TO_CONTENT|TDF_ALLOW_DIALOG_CANCELLATION;
    int answer=IDCLOSE;THROW_IF_FAILED(TaskDialogIndirect(&dialog,&answer,nullptr,nullptr));
    if(answer==IDYES)THROW_IF_FAILED(resetShellSafety());
}
HRESULT restartWithProgress(){
    struct Progress {
        std::thread worker;std::atomic<bool> done=false;HRESULT result=E_UNEXPECTED;
        static HRESULT CALLBACK callback(HWND window,UINT event,WPARAM,LPARAM,LONG_PTR data) noexcept {
            auto self=reinterpret_cast<Progress*>(data);
            if(event==TDN_CREATED){
                SendMessageW(window,TDM_SET_PROGRESS_BAR_MARQUEE,TRUE,0);
                SendMessageW(window,TDM_ENABLE_BUTTON,IDCLOSE,FALSE);
                try {self->worker=std::thread([self,window]{self->result=restartExplorer();self->done=true;PostMessageW(window,TDM_ENABLE_BUTTON,IDCLOSE,TRUE);PostMessageW(window,TDM_CLICK_BUTTON,IDCLOSE,0);});}
                catch(...){self->result=wil::ResultFromCaughtException();self->done=true;PostMessageW(window,TDM_ENABLE_BUTTON,IDCLOSE,TRUE);PostMessageW(window,TDM_CLICK_BUTTON,IDCLOSE,0);}
            }
            if(event==TDN_BUTTON_CLICKED&&!self->done)return S_FALSE;
            return S_OK;
        }
    } progress;
    auto title=tr(IDS_RESTARTING),body=tr(IDS_RESTART_WAIT);
    TASKDIALOGCONFIG dialog={sizeof(dialog)};dialog.pszWindowTitle=L"IconicFolders";dialog.pszMainInstruction=title.c_str();dialog.pszContent=body.c_str();
    dialog.dwFlags=TDF_SHOW_MARQUEE_PROGRESS_BAR|TDF_SIZE_TO_CONTENT;dialog.dwCommonButtons=TDCBF_CLOSE_BUTTON;dialog.pfCallback=Progress::callback;dialog.lpCallbackData=reinterpret_cast<LONG_PTR>(&progress);
    HRESULT shown=TaskDialogIndirect(&dialog,nullptr,nullptr,nullptr);if(progress.worker.joinable())progress.worker.join();return FAILED(shown)?shown:progress.result;
}
int run(std::vector<fs::path> folders,bool resetting) {
    std::set<std::wstring> seen; std::vector<fs::path> unique;
    for(auto& p:folders) { p=fs::absolute(p).lexically_normal(); auto s=p.native(); for(auto& c:s)c=static_cast<wchar_t>(towupper(c)); if(seen.insert(s).second)unique.push_back(p); }
    folders=std::move(unique); if(folders.empty()) { message(L"IconicFolders",tr(IDS_HELP),TD_INFORMATION_ICON); return 0; }
    IconData data; Mode mode=Mode::Reference,preferred=Mode::Reference;
    if(!resetting&&!pickIcon(folders,data,mode,preferred))return 0;
    const auto& source=data.source;
    std::vector<fs::path> replacements,deletions;
    std::set<fs::path> approvals;
    for(const auto& folder:folders) try {
        auto current=inspect(folder);
        if(!resetting && needsOverwrite(folder,data,mode)){replacements.push_back(current.managed?current.source:folder/managedIconName(data.bytes));approvals.insert(folder);}
        if(current.managed && fs::exists(current.source) && (resetting || (mode==Mode::Reference && fs::absolute(source).lexically_normal()!=fs::absolute(current.source).lexically_normal()))){deletions.push_back(current.source);approvals.insert(folder);}
    } catch(...) { /* Report per-folder errors together after confirmation. */ }
    if(!confirm(IDS_CONFIRM_TITLE,IDS_CONFIRM_BODY,IDS_REPLACE,replacements)||!confirm(IDS_DELETE_TITLE,IDS_DELETE_BODY,IDS_DELETE,deletions))return 0;
    std::wstring failures; bool succeeded=false;
    for(const auto& folder:folders)try {
        bool approved=approvals.contains(folder);
        if(resetting)reset(folder,approved);else apply(folder,data,mode,approved);succeeded=true;
    } catch(...) { failures+=folder.native()+L"\n"+errorText(wil::ResultFromCaughtException())+L"\n\n"; }
    if(succeeded&&!resetting)rememberIconChoice(source,preferred);
    if(!failures.empty()) { message(tr(IDS_ERROR),format(IDS_FAILURES,failures)); return 1; } return 0;
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    selectLanguage();
    try {
        auto com=wil::CoInitializeEx(COINIT_APARTMENTTHREADED);
        int argc=0; wil::unique_hlocal_ptr<wchar_t*[]> argv(CommandLineToArgvW(GetCommandLineW(),&argc)); THROW_LAST_ERROR_IF(!argv);
        std::vector<fs::path> folders; bool resetting=false;
        if(argc==3&&std::wstring(argv[1])==L"--watch-explorer") {
            wchar_t* end=nullptr;ULONGLONG value=wcstoull(argv[2],&end,10);THROW_WIN32_IF(ERROR_INVALID_PARAMETER,!value||!end||*end||value==~0ULL);
            bool disabled=false;HRESULT watched=watchExplorer(reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(value)),disabled);if(FAILED(watched))return 1;
            if(disabled)safetyMessage();return 0;
        }
        if(argc==1&&shellSafetyDisabled()){safetyMessage();return 0;}
        if(argc==2&&(std::wstring(argv[1])==L"--restart-explorer"||std::wstring(argv[1])==L"--probe-explorer-restart")){
            bool probe=std::wstring(argv[1])==L"--probe-explorer-restart";HRESULT hr=probe?restartExplorer(true):restartWithProgress();
            if(FAILED(hr)&&!probe)message(L"IconicFolders",tr(IDS_RESTART_FAILED),TD_INFORMATION_ICON);
            return SUCCEEDED(hr)?0:1;
        }
        if(argc==3 && std::wstring(argv[1])==L"--request") {
            fs::path requestPath=argv[2]; auto bytes=readFile(requestPath,4*1024*1024);
            auto remove=wil::scope_exit([&]{DeleteFileW(requestPath.c_str());});
            THROW_WIN32_IF(ERROR_INVALID_DATA,bytes.size()%2 || bytes.empty());
            std::wstring request(bytes.size()/2,0); memcpy(request.data(),bytes.data(),bytes.size());
            size_t end=request.find(L'\n'); THROW_WIN32_IF(ERROR_INVALID_DATA,end==request.npos);
            auto action=request.substr(0,end); THROW_WIN32_IF(ERROR_INVALID_DATA,action!=L"choose"&&action!=L"reset"); resetting=action==L"reset";
            for(size_t p=end+1;p<request.size();) { end=request.find(L'\n',p); THROW_WIN32_IF(ERROR_INVALID_DATA,end==request.npos||end==p); folders.emplace_back(request.substr(p,end-p)); p=end+1; }
            remove.reset();
            return run(std::move(folders),resetting);
        }
        for(int i=1;i<argc;++i) { std::wstring arg=argv[i]; if(i==1 && arg==L"--reset")resetting=true; else if(i==1 && arg==L"--choose"){} else folders.emplace_back(arg); }
        return run(std::move(folders),resetting);
    } catch(...) { message(tr(IDS_ERROR),errorText(wil::ResultFromCaughtException())); return 1; }
}
