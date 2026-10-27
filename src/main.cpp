// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Benco
#include "core.h"
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

using namespace iconic;
namespace {
constexpr wchar_t Settings[]=L"Software\\IconicFolders";
constexpr DWORD Radio=200,Reference=201,Copy=202;
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
struct Preferences { std::wstring directory; Mode mode=Mode::Reference; };
Preferences loadPreferences() {
    Preferences p; DWORD mode=0,size=sizeof(mode); RegGetValueW(HKEY_CURRENT_USER,Settings,L"Copy",RRF_RT_REG_DWORD,nullptr,&mode,&size); p.mode=mode==1?Mode::Copy:Mode::Reference;
    size=0; if(RegGetValueW(HKEY_CURRENT_USER,Settings,L"LastDirectory",RRF_RT_REG_SZ,nullptr,nullptr,&size)==ERROR_SUCCESS && size<65536) {
        std::vector<wchar_t> value(size/2+1,0); if(RegGetValueW(HKEY_CURRENT_USER,Settings,L"LastDirectory",RRF_RT_REG_SZ,nullptr,value.data(),&size)==ERROR_SUCCESS)p.directory=value.data();
    } return p;
}
void savePreferences(const fs::path& source,Mode mode) {
    HKEY raw=nullptr; if(RegCreateKeyExW(HKEY_CURRENT_USER,Settings,0,nullptr,0,KEY_SET_VALUE,nullptr,&raw,nullptr)!=ERROR_SUCCESS)return;
    wil::unique_hkey key(raw); auto directory=source.parent_path().native(); DWORD copy=mode==Mode::Copy?1:0;
    RegSetValueExW(key.get(),L"Copy",0,REG_DWORD,reinterpret_cast<const BYTE*>(&copy),sizeof(copy));
    RegSetValueExW(key.get(),L"LastDirectory",0,REG_SZ,reinterpret_cast<const BYTE*>(directory.c_str()),static_cast<DWORD>((directory.size()+1)*2));
}
bool pick(const std::vector<fs::path>& folders,fs::path& source,Mode& mode) {
    auto preferences=loadPreferences(); mode=preferences.mode; std::wstring initial=preferences.directory;
    try { auto current=inspect(folders[0]); if(!current.resource.empty()) { mode=current.managed?Mode::Copy:Mode::Reference; if(!current.managed && fs::is_directory(current.source.parent_path()))initial=current.source.parent_path().native(); } } catch(...) {}
    wil::com_ptr<IFileOpenDialog> dialog; THROW_IF_FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)));
    DWORD options=0; THROW_IF_FAILED(dialog->GetOptions(&options)); THROW_IF_FAILED(dialog->SetOptions((options|FOS_FORCEFILESYSTEM|FOS_FILEMUSTEXIST|FOS_PATHMUSTEXIST|FOS_DONTADDTORECENT)&~FOS_ALLOWMULTISELECT));
    auto title=folders.size()==1?format(IDS_TITLE_ONE,folders[0].filename().native()):format(IDS_TITLE_MANY,std::to_wstring(folders.size()));
    auto filterName=tr(IDS_ICO),applyLabel=tr(IDS_APPLY); COMDLG_FILTERSPEC filter={filterName.c_str(),L"*.ico"};
    THROW_IF_FAILED(dialog->SetTitle(title.c_str())); THROW_IF_FAILED(dialog->SetFileTypes(1,&filter)); THROW_IF_FAILED(dialog->SetOkButtonLabel(applyLabel.c_str()));
    wil::com_ptr<IFileDialogCustomize> custom=dialog.query<IFileDialogCustomize>();
    auto ref=tr(IDS_REFERENCE),copy=tr(IDS_COPY);
    THROW_IF_FAILED(custom->AddRadioButtonList(Radio)); THROW_IF_FAILED(custom->AddControlItem(Radio,Reference,ref.c_str())); THROW_IF_FAILED(custom->AddControlItem(Radio,Copy,copy.c_str())); THROW_IF_FAILED(custom->SetSelectedControlItem(Radio,mode==Mode::Copy?Copy:Reference));
    if(!initial.empty()) { wil::com_ptr<IShellItem> item; if(SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(),nullptr,IID_PPV_ARGS(&item))))dialog->SetFolder(item.get()); }
    for(;;) {
        HRESULT result=dialog->Show(nullptr); if(result==HRESULT_FROM_WIN32(ERROR_CANCELLED))return false; THROW_IF_FAILED(result);
        wil::com_ptr<IShellItem> item; THROW_IF_FAILED(dialog->GetResult(&item)); wil::unique_cotaskmem_string path; THROW_IF_FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&path)); source=path.get();
        DWORD selection=Reference; THROW_IF_FAILED(custom->GetSelectedControlItem(Radio,&selection)); mode=selection==Copy?Mode::Copy:Mode::Reference;
        try { validateIcon(source); return true; } catch(...) { message(tr(IDS_INVALID_ICO),errorText(wil::ResultFromCaughtException())); }
    }
}
int run(std::vector<fs::path> folders,bool resetting) {
    std::set<std::wstring> seen; std::vector<fs::path> unique;
    for(auto& p:folders) { p=fs::absolute(p).lexically_normal(); auto s=p.native(); for(auto& c:s)c=static_cast<wchar_t>(towupper(c)); if(seen.insert(s).second)unique.push_back(p); }
    folders=std::move(unique); if(folders.empty()) { message(L"IconicFolders",tr(IDS_HELP),TD_INFORMATION_ICON); return 0; }
    fs::path source; Mode mode=Mode::Reference;
    if(!resetting&&!pick(folders,source,mode))return 0;
    std::vector<fs::path> replacements,deletions;
    for(const auto& folder:folders) try {
        auto current=inspect(folder);
        if(!resetting && needsOverwrite(folder,source,mode))replacements.push_back(folder/ManagedIcon);
        if(current.managed && fs::exists(folder/ManagedIcon) && (resetting || (mode==Mode::Reference && fs::absolute(source).lexically_normal()!=fs::absolute(folder/ManagedIcon).lexically_normal())))deletions.push_back(folder/ManagedIcon);
    } catch(...) { /* Report per-folder errors together after confirmation. */ }
    if(!confirm(IDS_CONFIRM_TITLE,IDS_CONFIRM_BODY,IDS_REPLACE,replacements)||!confirm(IDS_DELETE_TITLE,IDS_DELETE_BODY,IDS_DELETE,deletions))return 0;
    std::wstring failures; bool succeeded=false;
    for(const auto& folder:folders)try {
        auto managed=folder/ManagedIcon;
        bool approved=std::find(replacements.begin(),replacements.end(),managed)!=replacements.end()||std::find(deletions.begin(),deletions.end(),managed)!=deletions.end();
        if(resetting)reset(folder,approved);else apply(folder,source,mode,approved);succeeded=true;
    } catch(...) { failures+=folder.native()+L"\n"+errorText(wil::ResultFromCaughtException())+L"\n\n"; }
    if(succeeded&&!resetting)savePreferences(source,mode);
    if(!failures.empty()) { message(tr(IDS_ERROR),format(IDS_FAILURES,failures)); return 1; } return 0;
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    selectLanguage();
    try {
        auto com=wil::CoInitializeEx(COINIT_APARTMENTTHREADED);
        int argc=0; wil::unique_hlocal_ptr<wchar_t*[]> argv(CommandLineToArgvW(GetCommandLineW(),&argc)); THROW_LAST_ERROR_IF(!argv);
        std::vector<fs::path> folders; bool resetting=false;
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
