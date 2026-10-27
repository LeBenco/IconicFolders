// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Benco
#include "core.h"
#include "ui.h"
#include "resource.h"
#include <shobjidl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <wil/resource.h>
#include <wil/com.h>
#include <wil/result.h>
#include <iostream>
#include <functional>
#include <random>
#include <algorithm>

using namespace iconic;
namespace {
unsigned passed=0;
void require(bool condition,const char* detail) { if(!condition)throw std::runtime_error(detail); }
void check(const char* name,const std::function<void()>& body) { body(); ++passed; std::cout<<"PASS "<<name<<"\n"; }
std::vector<unsigned char> bytes(const std::string& s) {return {s.begin(),s.end()};}
std::string narrow(const std::vector<unsigned char>& b) {return {b.begin(),b.end()};}
std::vector<unsigned char> utf16(const std::wstring& s,bool big=false) {
    std::vector<unsigned char> b=big?std::vector<unsigned char>{254,255}:std::vector<unsigned char>{255,254};
    for(wchar_t c:s) { b.push_back(static_cast<unsigned char>(big?c>>8:c)); b.push_back(static_cast<unsigned char>(big?c:c>>8)); } return b;
}
void fails(const std::function<void()>& body,HRESULT expected=E_FAIL) {
    try {body();} catch(const wil::ResultException& e) { if(expected!=E_FAIL)require(e.GetErrorCode()==expected,"unexpected error code"); return; }
    throw std::runtime_error("operation should have failed");
}
std::vector<unsigned char> ico(unsigned char color) {
    std::vector<unsigned char> b(70,0); b[2]=1;b[4]=1;b[6]=1;b[7]=1;b[10]=1;b[12]=32;b[14]=48;b[18]=22;
    b[22]=40;b[26]=1;b[30]=2;b[34]=1;b[36]=32;b[42]=4;b[62]=color;b[63]=128;b[64]=200;b[65]=255; return b;
}
DWORD attributes(const fs::path& p) {return GetFileAttributesW(p.c_str());}
std::vector<unsigned char> shellPixels(const fs::path& p) {
    SHFILEINFOW info={}; require(SHGetFileInfoW(p.c_str(),0,&info,sizeof(info),SHGFI_ICON|SHGFI_LARGEICON)!=0,"Shell icon unavailable");wil::unique_hicon icon(info.hIcon);
    ICONINFO ii={};require(!!GetIconInfo(icon.get(),&ii),"GetIconInfo");wil::unique_hbitmap color(ii.hbmColor),mask(ii.hbmMask);
    BITMAP bitmap={};GetObjectW(color.get(),sizeof(bitmap),&bitmap);require(bitmap.bmWidth>0&&bitmap.bmHeight>0,"icon bitmap");
    BITMAPINFO bi={};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=bitmap.bmWidth;bi.bmiHeader.biHeight=-bitmap.bmHeight;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
    std::vector<unsigned char> out(static_cast<size_t>(bitmap.bmWidth)*bitmap.bmHeight*4);
    HDC dc=GetDC(nullptr);int got=GetDIBits(dc,color.get(),0,bitmap.bmHeight,out.data(),&bi,DIB_RGB_COLORS);ReleaseDC(nullptr,dc);require(got==bitmap.bmHeight,"icon pixels");return out;
}
void pump(unsigned milliseconds) {
    ULONGLONG until=GetTickCount64()+milliseconds;
    do { MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);} Sleep(10); } while(GetTickCount64()<until);
}
LRESULT CALLBACK shellNotifications(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    if(message==WM_APP+125) {
        PIDLIST_ABSOLUTE* items=nullptr; LONG event=0;
        HANDLE lock=SHChangeNotification_Lock(reinterpret_cast<HANDLE>(wParam),static_cast<DWORD>(lParam),&items,&event);
        if(lock) { if(event==SHCNE_UPDATEIMAGE&&items[1])SHHandleUpdateImage(items[1]);SHChangeNotification_Unlock(lock); }
        return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}
void cleanup(const fs::path& root) {
    if(!fs::exists(root))return;
    for(auto const& e:fs::recursive_directory_iterator(root))SetFileAttributesW(e.path().c_str(),e.is_directory()?FILE_ATTRIBUTE_DIRECTORY:FILE_ATTRIBUTE_NORMAL);
    SetFileAttributesW(root.c_str(),FILE_ATTRIBUTE_NORMAL); fs::remove_all(root);
}
}
int wmain(int argc,wchar_t** argv) {
    auto com=wil::CoInitializeEx(COINIT_APARTMENTTHREADED);
    wchar_t current[32768]; GetModuleFileNameW(nullptr,current,32768); fs::path output=fs::path(current).parent_path();
    auto root=output/(L"test-data-"+std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(root); auto clean=wil::scope_exit([&]{cleanup(root);});
    try {
        if(argc==2&&std::wstring(argv[1])==L"--registered") {
            auto first=root/L"transfert été 猫",second=root/L"second dossier";fs::create_directories(first);fs::create_directories(second);auto source=root/L"source.ico";writeFile(source,ico(50));apply(first,source,Mode::Reference,false);apply(second,source,Mode::Reference,false);
            CLSID id;THROW_IF_FAILED(CLSIDFromString(L"{75F4D684-8623-4CE1-B3B2-464EF177FDDE}",&id));wil::com_ptr<IExplorerCommand> command;THROW_IF_FAILED(CoCreateInstance(id,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&command)));
            wil::com_ptr<IEnumExplorerCommand> children;THROW_IF_FAILED(command->EnumSubCommands(&children));THROW_IF_FAILED(children->Skip(1));wil::com_ptr<IExplorerCommand> resetCommand;THROW_IF_FAILED(children->Next(1,&resetCommand,nullptr));
            wil::unique_cotaskmem_ptr<ITEMIDLIST> p1,p2;THROW_IF_FAILED(SHParseDisplayName(first.c_str(),nullptr,wil::out_param(p1),0,nullptr));THROW_IF_FAILED(SHParseDisplayName(second.c_str(),nullptr,wil::out_param(p2),0,nullptr));
            PCIDLIST_ABSOLUTE pidls[]={p1.get(),p2.get()};wil::com_ptr<IShellItemArray> selection;THROW_IF_FAILED(SHCreateShellItemArrayFromIDLists(2,pidls,&selection));EXPCMDSTATE state=ECS_HIDDEN;THROW_IF_FAILED(command->GetState(selection.get(),FALSE,&state));require(state==ECS_ENABLED,"installed menu disabled");
            wil::com_ptr<IShellFolder> parent;PCUITEMID_CHILD child=nullptr;THROW_IF_FAILED(SHBindToParent(p1.get(),IID_PPV_ARGS(&parent),&child));wil::com_ptr<IContextMenu> context;THROW_IF_FAILED(parent->GetUIObjectOf(nullptr,1,&child,IID_IContextMenu,nullptr,context.put_void()));
            HMENU menu=CreatePopupMenu();require(menu!=nullptr,"native context menu");auto destroyMenu=wil::scope_exit([&]{DestroyMenu(menu);});THROW_IF_FAILED(context->QueryContextMenu(menu,0,1,0x7fff,CMF_NORMAL));
            unsigned roots=0;for(int i=0;i<GetMenuItemCount(menu);++i) {wchar_t label[512]={};GetMenuStringW(menu,static_cast<UINT>(i),label,512,MF_BYPOSITION);std::wstring text=label;text.erase(std::remove(text.begin(),text.end(),L'&'),text.end());if(text==L"Icône du dossier"||text==L"Folder icon") {++roots;require(GetSubMenu(menu,i)!=nullptr,"native context entry has no submenu");}}
            require(roots==1,"native Shell context menu must have exactly one IconicFolders entry");std::cout<<"PASS native Shell context menu contains one IconicFolders submenu\n";
            THROW_IF_FAILED(resetCommand->Invoke(selection.get(),nullptr));
            for(unsigned retry=0;retry<100 && (!inspect(first).resource.empty()||!inspect(second).resource.empty());++retry)Sleep(50);
            require(inspect(first).resource.empty()&&inspect(second).resource.empty(),"installed command request transfer/reset failed");require(fs::exists(source),"installed command deleted original");
            std::cout<<"PASS installed COM activation and real helper invocation (two Unicode folders)\n";return 0;
        }
        // Receive Shell notifications exactly as an icon-displaying Shell client does.
        WNDCLASSW wc={};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"IconicFoldersTests.Notifications";wc.lpfnWndProc=shellNotifications;RegisterClassW(&wc);
        HWND receiver=CreateWindowExW(0,wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);require(receiver!=nullptr,"notification receiver");
        SHChangeNotifyEntry entry={nullptr,TRUE};ULONG registration=SHChangeNotifyRegister(receiver,SHCNRF_ShellLevel|SHCNRF_NewDelivery,SHCNE_UPDATEIMAGE,WM_APP+125,1,&entry);require(registration!=0,"Shell notification registration");
        auto unregister=wil::scope_exit([&]{SHChangeNotifyDeregister(registration);DestroyWindow(receiver);});
        check("INI preserves unrelated bytes, comments, blank lines, sections and CRLF",[]{
            auto original=bytes("; commentaire\r\n[Other]\r\nX=1\r\n\r\n[.ShellClassInfo]\r\n; icon note\r\n  IconFile = old.ico\r\nIconIndex=2\r\nInfoTip = hello\r\n[More]\r\nA=B");
            auto changed=Ini(original).update(L"new.ico,0");
            require(narrow(changed)=="; commentaire\r\n[Other]\r\nX=1\r\n\r\n[.ShellClassInfo]\r\nIconResource=new.ico,0\r\n; icon note\r\nInfoTip = hello\r\n[More]\r\nA=B","unrelated content changed");
            require(Ini(changed).icon()==L"new.ico,0","icon not readable");
        });
        check("INI reset preserves LF and all unrelated text",[]{
            auto out=Ini(bytes("[.shellclassinfo]\nIconResource=a,0\nIconFile=b\nIconIndex=1\nLocalizedResourceName=Name\n")).update(std::nullopt);
            require(narrow(out)=="[.shellclassinfo]\nLocalizedResourceName=Name\n","reset mismatch");
        });
        check("duplicate shell sections and keys cannot override new icon",[]{
            auto out=Ini(bytes("[.ShellClassInfo]\nICONRESOURCE=bad,0\n[Other]\nx=7\n[.ShellClassInfo]\nIconFile=old.ico\nIconResource=another,0")).update(L"good.ico,0");
            require(narrow(out)=="[.ShellClassInfo]\nIconResource=good.ico,0\n[Other]\nx=7\n[.ShellClassInfo]\n","duplicate icon left");
        });
        check("UTF16 LE/BE, UTF8 BOM, ANSI and Unicode paths",[]{
            for(bool big:{false,true}) { auto original=utf16(L"; été\n[.ShellClassInfo]\nInfoTip=é\n",big); auto updated=Ini(original).update(L"C:\\été\\猫.ico,0"); require(Ini(updated).icon()==L"C:\\été\\猫.ico,0","unicode roundtrip"); require(Ini(updated).update(std::nullopt)==original,"UTF16 unrelated bytes"); }
            auto utf8=bytes("\xef\xbb\xbf; été\n[.ShellClassInfo]\nInfoTip=猫\n"); auto updated=Ini(utf8).update(L"C:\\猫.ico,0"); require(Ini(updated).icon()==L"C:\\猫.ico,0","UTF8 roundtrip"); require(Ini(updated).update(std::nullopt)==utf8,"UTF8 changed bytes");
            auto ansi=bytes("; \xe9\n[.ShellClassInfo]\nInfoTip=abc\n"); auto a=Ini(ansi).update(L"plain.ico,0");require(Ini(a).update(std::nullopt)==ansi,"ANSI changed bytes");
            fails([&]{Ini(ansi).update(L"C:\\猫.ico,0");},HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION));
        });
        check("new desktop.ini is Unicode and append handles missing final newline",[]{
            auto created=Ini({}).update(L"new.ico,0");require(created[0]==255&&created[1]==254,"new INI not UTF16");
            require(narrow(Ini(bytes("[Other]\nx=1")).update(L"new.ico,0"))=="[Other]\nx=1\n[.ShellClassInfo]\nIconResource=new.ico,0\n","append corrupt");
            fails([]{Ini({255,254,0});},HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION));
            fails([]{Ini({0,3,0});},HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        });
        check("legacy icon reading and environment references",[]{ require(Ini(bytes("[.ShellClassInfo]\nIconFile=%SystemRoot%\\x.dll\nIconIndex=-3")).icon()==L"%SystemRoot%\\x.dll,-3","legacy reading"); });
        auto source=root/L"icônes 猫"; fs::create_directory(source); auto a=source/L"a.ico",b=source/L"b.ico"; writeFile(a,ico(32));writeFile(b,ico(240));
        check("ICO validation rejects truncated tables, invalid images and wrong extensions",[&]{
            validateIcon(a); auto corrupt=source/L"bad.ico";writeFile(corrupt,{0,0,1,0,1,0}); fails([&]{validateIcon(corrupt);});
            auto malformed=ico(1);malformed[18]=255;auto broken=source/L"broken.ico";writeFile(broken,malformed);fails([&]{validateIcon(broken);});
            auto png=source/L"fake.png";writeFile(png,ico(1));fails([&]{validateIcon(png);});
        });
        check("BOM-less ASCII INI uses Windows ANSI for accented source paths",[&]{
            auto target=root/L"ansi-native";fs::create_directory(target);auto accented=root/L"café.ico";writeFile(accented,ico(45));writeFile(target/L"desktop.ini",bytes("[.ShellClassInfo]\r\nInfoTip=keep\r\n"));apply(target,accented,Mode::Reference,false);
            wchar_t value[32768];GetPrivateProfileStringW(L".ShellClassInfo",L"IconResource",L"",value,32768,(target/L"desktop.ini").c_str());require(std::wstring(value)==fs::absolute(accented).native()+L",0","Windows cannot read written ANSI path");
        });
        check("non-native INI encodings are refused without touching files",[&]{
            unsigned n=0;for(const auto& original:{utf16(L"[.ShellClassInfo]\nInfoTip=keep\n",true),bytes("\xef\xbb\xbf[.ShellClassInfo]\nInfoTip=keep\n")}) {
                auto target=root/(L"unsupported-"+std::to_wstring(n++));fs::create_directory(target);writeFile(target/L"desktop.ini",original);fails([&]{apply(target,a,Mode::Copy,false);},UnsupportedIniEncoding);require(readFile(target/L"desktop.ini")==original&&!fs::exists(target/ManagedIcon),"unsupported encoding mutated");
            }
        });
        auto folder=root/L"Dossier été";fs::create_directory(folder);
        check("reference writes only desktop.ini, required attributes, original preserved",[&]{
            apply(folder,a,Mode::Reference,false);auto info=inspect(folder);require(!info.managed&&info.source==fs::absolute(a),"reference wrong");
            require(!fs::exists(folder/ManagedIcon),"unwanted copy");require(readFile(a)==ico(32),"source changed");
            require((attributes(folder/L"desktop.ini")&(FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM))==(FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM),"INI attrs");require(attributes(folder)&FILE_ATTRIBUTE_READONLY,"folder attrs");
        });
        check("reset reference never deletes source or other desktop.ini settings",[&]{
            reset(folder,false); require(inspect(folder).resource.empty(),"reset left icon");require(fs::exists(a),"source deleted");require(fs::exists(folder/L"desktop.ini"),"INI removed");
        });
        check("copy is relative and hidden, fixed basename, confirmation enforced",[&]{
            apply(folder,a,Mode::Copy,false);require(inspect(folder).managed,"copy not identified");require(attributes(folder/ManagedIcon)&FILE_ATTRIBUTE_HIDDEN,"copy visible");
            auto before=readFile(folder/L"desktop.ini");fails([&]{apply(folder,b,Mode::Copy,false);},HRESULT_FROM_WIN32(ERROR_FILE_EXISTS));require(readFile(folder/ManagedIcon)==ico(32)&&readFile(folder/L"desktop.ini")==before,"cancel changed files");
            apply(folder,b,Mode::Copy,true);require(readFile(folder/ManagedIcon)==ico(240),"copy not replaced");
        });
        check("copy reset cancellation and confirmed deletion",[&]{
            fails([&]{reset(folder,false);},HRESULT_FROM_WIN32(ERROR_CANCELLED));require(inspect(folder).managed&&fs::exists(folder/ManagedIcon),"cancel removed copy");
            reset(folder,true);require(!fs::exists(folder/ManagedIcon)&&inspect(folder).resource.empty(),"managed copy not removed");
        });
        check("reserved-name collision is never overwritten silently",[&]{
            writeFile(folder/ManagedIcon,bytes("unrelated file"));fails([&]{apply(folder,a,Mode::Copy,false);},HRESULT_FROM_WIN32(ERROR_FILE_EXISTS)); require(narrow(readFile(folder/ManagedIcon))=="unrelated file","collision destroyed");
            apply(folder,a,Mode::Copy,true);require(readFile(folder/ManagedIcon)==ico(32),"confirmed collision not replaced");
        });
        check("switch copy to reference requires approval and removes managed copy",[&]{
            fails([&]{apply(folder,b,Mode::Reference,false);},HRESULT_FROM_WIN32(ERROR_CANCELLED));apply(folder,b,Mode::Reference,true);require(!fs::exists(folder/ManagedIcon)&&fs::exists(b),"mode switch cleanup");
        });
        check("absolute reference with reserved basename is never deleted",[&]{
            writeFile(folder/ManagedIcon,ico(12));apply(folder,folder/ManagedIcon,Mode::Reference,true);require(!inspect(folder).managed,"absolute classified copy");reset(folder,true);require(fs::exists(folder/ManagedIcon),"original deleted");
        });
        check("unknown relative icon and unrelated reserved file survive reset",[&]{
            auto other=root/L"unknown";fs::create_directory(other);writeFile(other/L"desktop.ini",utf16(L"[.ShellClassInfo]\nIconResource=custom.ico,0\nInfoTip=keep\n"));writeFile(other/L"custom.ico",ico(1));writeFile(other/ManagedIcon,ico(2));reset(other,true);require(fs::exists(other/L"custom.ico")&&fs::exists(other/ManagedIcon),"unknown file deleted");require(Ini(readFile(other/L"desktop.ini")).update(std::nullopt)==utf16(L"[.ShellClassInfo]\nInfoTip=keep\n"),"other settings removed");
        });
        check("failure on locked INI preserves previous icon and content",[&]{
            apply(folder,a,Mode::Copy,true);auto before=readFile(folder/L"desktop.ini");
            wil::unique_handle lock(CreateFileW((folder/L"desktop.ini").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));require(!!lock,"test lock");fails([&]{apply(folder,b,Mode::Copy,true);});lock.reset();require(readFile(folder/L"desktop.ini")==before&&readFile(folder/ManagedIcon)==ico(32),"failure lost state");
        });
        check("failed deletion rolls back desktop.ini and retains copied file",[&]{
            auto before=readFile(folder/L"desktop.ini");DWORD old=attributes(folder/ManagedIcon);
            wil::unique_handle lock(CreateFileW((folder/ManagedIcon).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));require(!!lock,"test icon lock");fails([&]{reset(folder,true);});lock.reset();
            require(readFile(folder/L"desktop.ini")==before&&readFile(folder/ManagedIcon)==ico(32),"delete failure lost state");require(attributes(folder/ManagedIcon)==old,"delete failure changed attributes");
        });
        check("same file chosen in copy mode remains hidden without corruption",[&]{
            apply(folder,folder/ManagedIcon,Mode::Copy,false);require(readFile(folder/ManagedIcon)==ico(32)&&inspect(folder).managed,"self copy corrupted");
        });
        check("copied icon survives folder rename and long Unicode paths",[&]{
            auto moving=root/L"move-me";fs::create_directory(moving);apply(moving,a,Mode::Copy,false);auto moved=root/L"renamed";fs::rename(moving,moved);require(inspect(moved).managed&&readFile(moved/ManagedIcon)==ico(32),"rename lost icon");
            fs::path longFolder=root;for(int i=0;i<9;++i)longFolder/=L"long-unicode-é猫-directory-name";fs::create_directories(longFolder);apply(longFolder,b,Mode::Copy,false);require(inspect(longFolder).managed&&readFile(longFolder/ManagedIcon)==ico(240),"long path copy");reset(longFolder,true);require(!fs::exists(longFolder/ManagedIcon),"long path reset");
        });
        check("same basename in two folders refers to different local files",[&]{
            auto second=root/L"second";fs::create_directory(second);apply(second,b,Mode::Copy,false);require(readFile(second/ManagedIcon)!=readFile(folder/ManagedIcon),"cross-folder copy collision");
            auto firstPixels=shellPixels(folder),secondPixels=shellPixels(second);require(firstPixels!=secondPixels,"Shell cache mixes same basename");
            apply(folder,b,Mode::Copy,true);
            // Shell change notifications are asynchronous. Allow its message queue
            // to process the targeted update, without changing the expected pixels.
            for(unsigned retry=0;retry<20&&shellPixels(folder)==firstPixels;++retry)pump(100);
            require(shellPixels(folder)!=firstPixels,"Shell retains old image after overwrite");require(shellPixels(folder)==secondPixels,"Shell displays wrong replacement");
        });
        check("Windows argument quoting roundtrip",[]{
            std::vector<std::wstring> cases={L"",L"space path",L"quote\"",L"trailing\\",L"\\\\server\\share\\",L"é猫"};
            for(const auto& v:cases) {auto command=L"test.exe "+quoteArg(v);int count=0;auto raw=CommandLineToArgvW(command.c_str(),&count);require(raw&&count==2&&v==raw[1],"quoting failed");LocalFree(raw);}
        });
        check("native French and English resources",[]{
            SetThreadUILanguage(MAKELANGID(LANG_FRENCH,SUBLANG_FRENCH));require(tr(IDS_COPY)==L"Copie dans le dossier","French missing");
            SetThreadUILanguage(MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US));require(tr(IDS_COPY)==L"Copy into folder","English missing");
            SetThreadUILanguage(MAKELANGID(LANG_SPANISH,SUBLANG_SPANISH));require(tr(IDS_COPY)==L"Copy into folder","English fallback missing");
            SetThreadUILanguage(MAKELANGID(LANG_FRENCH,SUBLANG_FRENCH_CANADIAN));require(tr(IDS_COPY)==L"Copie dans le dossier","French regional fallback missing");
            SetThreadUILanguage(MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US));
        });
        check("native file dialog supports compact radio controls",[]{
            wil::com_ptr<IFileOpenDialog> dialog;THROW_IF_FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)));auto c=dialog.query<IFileDialogCustomize>();THROW_IF_FAILED(c->AddRadioButtonList(1));THROW_IF_FAILED(c->AddControlItem(1,2,L"Use the original icon"));THROW_IF_FAILED(c->AddControlItem(1,3,L"Copy the icon into the folder"));THROW_IF_FAILED(c->SetSelectedControlItem(1,3));DWORD selected=0;THROW_IF_FAILED(c->GetSelectedControlItem(1,&selected));require(selected==3,"radio state");
        });
        check("COM shell factory, submenu enumeration and unload lifetime",[&]{
            wil::unique_hmodule dll(LoadLibraryW((output/L"IconicFoldersShell.dll").c_str()));require(!!dll,"DLL load");
            auto create=reinterpret_cast<HRESULT(WINAPI*)(REFCLSID,REFIID,void**)>(GetProcAddress(dll.get(),"DllGetClassObject"));auto unload=reinterpret_cast<HRESULT(WINAPI*)()>(GetProcAddress(dll.get(),"DllCanUnloadNow"));require(create&&unload,"DLL exports");
            CLSID id;THROW_IF_FAILED(CLSIDFromString(L"{75F4D684-8623-4CE1-B3B2-464EF177FDDE}",&id));wil::com_ptr<IClassFactory> factory;THROW_IF_FAILED(create(id,IID_PPV_ARGS(&factory)));require(unload()==S_FALSE,"factory lifetime");
            wil::com_ptr<IExplorerCommand> rootCommand;THROW_IF_FAILED(factory->CreateInstance(nullptr,IID_PPV_ARGS(&rootCommand)));EXPCMDFLAGS flags;THROW_IF_FAILED(rootCommand->GetFlags(&flags));require(flags==ECF_HASSUBCOMMANDS,"submenu flag");
            wil::com_ptr<IEnumExplorerCommand> e;THROW_IF_FAILED(rootCommand->EnumSubCommands(&e));wil::com_ptr<IExplorerCommand> first,second;THROW_IF_FAILED(e->Next(1,&first,nullptr));THROW_IF_FAILED(e->Next(1,&second,nullptr));IExplorerCommand* extra=nullptr;require(e->Next(1,&extra,nullptr)==S_FALSE,"extra menu entry");
            first.reset();second.reset();e.reset();rootCommand.reset();factory.reset();require(unload()==S_OK,"COM leaked");
        });
        check("malformed INI fuzz corpus does not corrupt unrelated data",[]{
            std::mt19937 rng(781);for(unsigned i=0;i<5000;++i) {std::vector<unsigned char> raw;unsigned size=rng()%2048;for(unsigned j=0;j<size;++j)raw.push_back(static_cast<unsigned char>(1+rng()%255));try {Ini ini(raw);auto changed=ini.update(L"iconicfolders.ico,0");require(Ini(changed).icon()==L"iconicfolders.ico,0","fuzz roundtrip");}catch(const wil::ResultException&){} }
        });
        std::cout<<"RESULT "<<passed<<" tests passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL after "<<passed<<" tests: "<<e.what()<<"\n";return 1;}
}
