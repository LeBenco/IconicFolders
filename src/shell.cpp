// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Benco
#include "core.h"
#include "ui.h"
#include "resource.h"
#include <shobjidl.h>
#include <shlwapi.h>
#include <wrl.h>
#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result.h>
#include <atomic>
#include <algorithm>

using namespace Microsoft::WRL;
using namespace iconic;
// {75F4D684-8623-4CE1-B3B2-464EF177FDDE}
const CLSID CLSID_IconicFolders={0x75f4d684,0x8623,0x4ce1,{0xb3,0xb2,0x46,0x4e,0xf1,0x77,0xfd,0xde}};
HMODULE moduleHandle=nullptr;
std::atomic<long> objects=0;
struct Counted { Counted(){++objects;} virtual ~Counted(){--objects;} };
class Command;
class Enumerator final:public RuntimeClass<RuntimeClassFlags<ClassicCom>,IEnumExplorerCommand>,public Counted {
    ULONG position_=0;
public:
    explicit Enumerator(ULONG position=0):position_(position){}
    IFACEMETHODIMP Next(ULONG count,IExplorerCommand** commands,ULONG* fetched) noexcept override;
    IFACEMETHODIMP Skip(ULONG count) noexcept override { ULONG available=2-position_; position_+=std::min(count,available); return count<=available?S_OK:S_FALSE; }
    IFACEMETHODIMP Reset() noexcept override {position_=0;return S_OK;}
    IFACEMETHODIMP Clone(IEnumExplorerCommand** copy) noexcept override { if(!copy)return E_POINTER; auto e=Make<Enumerator>(position_); return e?e.CopyTo(copy):E_OUTOFMEMORY; }
};
class Command final:public RuntimeClass<RuntimeClassFlags<ClassicCom>,IExplorerCommand>,public Counted {
    UINT kind_=0;
public:
    explicit Command(UINT kind=0):kind_(kind){}
    IFACEMETHODIMP GetTitle(IShellItemArray*,LPWSTR* title) noexcept override {
        try { return SHStrDupW(tr(kind_==0?IDS_MENU:kind_==1?IDS_CHOOSE:IDS_RESET,moduleHandle).c_str(),title); } CATCH_RETURN();
    }
    IFACEMETHODIMP GetIcon(IShellItemArray*,LPWSTR* icon) noexcept override {
        if(!icon)return E_POINTER; *icon=nullptr;
        try { wchar_t path[32768]; DWORD n=GetModuleFileNameW(moduleHandle,path,32768); RETURN_LAST_ERROR_IF(!n||n>=32768); return SHStrDupW((std::wstring(path)+L",-1").c_str(),icon); } CATCH_RETURN();
    }
    IFACEMETHODIMP GetToolTip(IShellItemArray*,LPWSTR* tip) noexcept override { if(!tip)return E_POINTER; *tip=nullptr; return E_NOTIMPL; }
    IFACEMETHODIMP GetCanonicalName(GUID* name) noexcept override { if(!name)return E_POINTER; *name=CLSID_IconicFolders; name->Data1+=kind_; return S_OK; }
    IFACEMETHODIMP GetState(IShellItemArray* items,BOOL,EXPCMDSTATE* state) noexcept override {
        if(!state)return E_POINTER; *state=ECS_HIDDEN; if(!items)return S_OK;
        SFGAOF attrs=0; HRESULT hr=items->GetAttributes(SIATTRIBFLAGS_AND,SFGAO_FOLDER|SFGAO_FILESYSTEM,&attrs);
        if(SUCCEEDED(hr) && (attrs&(SFGAO_FOLDER|SFGAO_FILESYSTEM))==(SFGAO_FOLDER|SFGAO_FILESYSTEM))*state=ECS_ENABLED;
        return S_OK;
    }
    IFACEMETHODIMP Invoke(IShellItemArray* items,IBindCtx*) noexcept override {
        try {
            if(!items||kind_==0)return E_INVALIDARG;
            DWORD count=0; RETURN_IF_FAILED(items->GetCount(&count)); if(!count)return E_INVALIDARG;
            std::wstring request=kind_==1?L"choose\n":L"reset\n";
            for(DWORD i=0;i<count;++i) {
                wil::com_ptr<IShellItem> item; RETURN_IF_FAILED(items->GetItemAt(i,&item));
                wil::unique_cotaskmem_string path; RETURN_IF_FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&path));
                std::wstring p=path.get(); RETURN_HR_IF(E_INVALIDARG,p.find_first_of(L"\r\n")!=p.npos); request+=p+L"\n";
            }
            wchar_t tmp[32768],file[32768],dll[32768]; DWORD n=GetTempPathW(32768,tmp); RETURN_LAST_ERROR_IF(!n||n>=32768);
            RETURN_LAST_ERROR_IF(!GetTempFileNameW(tmp,L"icf",0,file));
            auto cleanup=wil::scope_exit([&]{DeleteFileW(file);});
            // GetTempFileName reserved the name: replace only this newly created empty file.
            wil::unique_handle h(CreateFileW(file,GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_TEMPORARY,nullptr)); RETURN_LAST_ERROR_IF(!h);
            DWORD wrote=0; DWORD size=static_cast<DWORD>(request.size()*sizeof(wchar_t)); RETURN_LAST_ERROR_IF(!WriteFile(h.get(),request.data(),size,&wrote,nullptr)); RETURN_HR_IF(E_FAIL,wrote!=size); h.reset();
            n=GetModuleFileNameW(moduleHandle,dll,32768); RETURN_LAST_ERROR_IF(!n||n>=32768);
            fs::path exe=fs::path(dll).parent_path()/L"IconicFolders.exe";
            std::wstring command=quoteArg(exe.native())+L" --request "+quoteArg(file);
            STARTUPINFOW startup={sizeof(startup)}; PROCESS_INFORMATION process={};
            RETURN_LAST_ERROR_IF(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,exe.parent_path().c_str(),&startup,&process));
            CloseHandle(process.hThread); CloseHandle(process.hProcess); cleanup.release(); return S_OK;
        } CATCH_RETURN();
    }
    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* flags) noexcept override { if(!flags)return E_POINTER; *flags=kind_==0?ECF_HASSUBCOMMANDS:ECF_DEFAULT; return S_OK; }
    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** commands) noexcept override { if(!commands)return E_POINTER; *commands=nullptr; if(kind_)return E_NOTIMPL; auto e=Make<Enumerator>(); return e?e.CopyTo(commands):E_OUTOFMEMORY; }
};
HRESULT Enumerator::Next(ULONG count,IExplorerCommand** commands,ULONG* fetched) noexcept {
    if(!commands||(!fetched&&count!=1))return E_POINTER; if(fetched)*fetched=0; ULONG actual=0;
    for(;actual<count&&position_<2;++actual,++position_) {
        auto command=Make<Command>(position_+1); if(!command)return E_OUTOFMEMORY;
        command.CopyTo(&commands[actual]); if(fetched)++*fetched;
    } return actual==count?S_OK:S_FALSE;
}
class Factory final:public RuntimeClass<RuntimeClassFlags<ClassicCom>,IClassFactory>,public Counted {
public:
    IFACEMETHODIMP CreateInstance(IUnknown* outer,REFIID iid,void** result) noexcept override { if(!result)return E_POINTER; *result=nullptr; if(outer)return CLASS_E_NOAGGREGATION; auto command=Make<Command>(); return command?command->QueryInterface(iid,result):E_OUTOFMEMORY; }
    IFACEMETHODIMP LockServer(BOOL lock) noexcept override { if(lock)++objects;else --objects;return S_OK; }
};
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid,REFIID iid,void** result) {
    if(!result)return E_POINTER; *result=nullptr; if(clsid!=CLSID_IconicFolders)return CLASS_E_CLASSNOTAVAILABLE;
    auto factory=Make<Factory>(); return factory?factory->QueryInterface(iid,result):E_OUTOFMEMORY;
}
extern "C" HRESULT __stdcall DllCanUnloadNow() { return objects==0?S_OK:S_FALSE; }
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) { if(reason==DLL_PROCESS_ATTACH) {moduleHandle=instance;DisableThreadLibraryCalls(instance);} return TRUE; }
