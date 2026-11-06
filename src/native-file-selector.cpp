// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Benco
#include "file-selector.h"
#include "ui.h"
#include "resource.h"
#include <shobjidl.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result.h>
#include <atomic>
#include <mutex>
#include <algorithm>

namespace iconic {
namespace {
const UINT LayoutReady=RegisterWindowMessageW(L"IconicFolders.FileSelector.LayoutReady.1");
const UINT StopDialog=RegisterWindowMessageW(L"IconicFolders.FileSelector.Stop.1");
const UINT StartDialog=RegisterWindowMessageW(L"IconicFolders.FileSelector.Start.1");
const UINT AttachDialog=RegisterWindowMessageW(L"IconicFolders.FileSelector.Attach.1");
constexpr int Browse=501;
class NativeSelector;
thread_local NativeSelector* filteringSelector=nullptr;
struct DialogEvents final:IFileDialogEvents {
    std::atomic<ULONG> refs=1;NativeSelector& selector;
    explicit DialogEvents(NativeSelector& value):selector(value){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;if(id!=IID_IUnknown&&id!=IID_IFileDialogEvents)return E_NOINTERFACE;*out=static_cast<IFileDialogEvents*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override{ULONG count=--refs;if(!count)delete this;return count;}
    HRESULT STDMETHODCALLTYPE OnFileOk(IFileDialog*) override;
    HRESULT STDMETHODCALLTYPE OnFolderChanging(IFileDialog*,IShellItem*) override;
    HRESULT STDMETHODCALLTYPE OnFolderChange(IFileDialog*) override;
    HRESULT STDMETHODCALLTYPE OnSelectionChange(IFileDialog*) override;
    HRESULT STDMETHODCALLTYPE OnShareViolation(IFileDialog*,IShellItem*,FDE_SHAREVIOLATION_RESPONSE* response) override{*response=FDESVR_DEFAULT;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnTypeChange(IFileDialog*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnOverwrite(IFileDialog*,IShellItem*,FDE_OVERWRITE_RESPONSE* response) override{*response=FDEOR_DEFAULT;return S_OK;}
};
class NativeSelector final:public IFileSelector {
public:
    explicit NativeSelector(const SelectorOptions& value,bool allowEmbedding):options(value),lastFolder(value.initialFolder){
        struct Template{DLGTEMPLATE dialog;WORD menu=0,klass=0,title=0;} page={};
        page.dialog.style=WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS|DS_CONTROL;page.dialog.dwExtendedStyle=WS_EX_CONTROLPARENT;
        view=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),&page.dialog,options.owner,pageProc,reinterpret_cast<LPARAM>(this));THROW_LAST_ERROR_IF(!view);
        try {setBounds(options.bounds,GetDpiForWindow(options.owner));if(allowEmbedding){THROW_IF_WIN32_BOOL_FALSE(PostMessageW(view,StartDialog,0,0));}else{failed=true;finished=true;emit({SelectorEvent::Type::Failed,std::nullopt,{},HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)});}}catch(...){DestroyWindow(view);view=nullptr;throw;}
    }
    ~NativeSelector() override {
        notifying=false;requestStop();
        // A deferred Show may still be pending. Run it to completion before
        // destroying the selector's state or its window.
        while(!finished.load()){
            MsgWaitForMultipleObjectsEx(0,nullptr,50,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
            MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT)continue;TranslateMessage(&msg);DispatchMessageW(&msg);}
        }
        if(view)DestroyWindow(view);
    }
    HWND window()const noexcept override{return view;}
    void setBounds(const RECT& rect,UINT dpi) override {
        SetWindowPos(view,nullptr,rect.left,rect.top,std::max(0L,rect.right-rect.left),std::max(0L,rect.bottom-rect.top),SWP_NOZORDER|SWP_NOACTIVATE);
        HWND child=embedded.load();if(child){int crop=MulDiv(trim.load(),static_cast<int>(dpi),static_cast<int>(nativeDpi));SetWindowPos(child,nullptr,0,0,rect.right-rect.left,rect.bottom-rect.top+crop,SWP_NOZORDER|SWP_NOACTIVATE|SWP_ASYNCWINDOWPOS|SWP_NOCOPYBITS);}
        if(failed.load())layoutFallback();
    }
    void show(bool visible) override {
        if(visible)SetWindowPos(view,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);else ShowWindow(view,SW_HIDE);
    }
    void activateSelection()override{if(stopping.load())return;HWND child=embedded.load();if(child)PostMessageW(child,WM_COMMAND,IDOK,0);else if(standaloneSelection)emit({SelectorEvent::Type::Activate,standaloneSelection});}
    SIZE preferredSize(UINT dpi)const noexcept override {
        if(!ready.load())return {};
        return {MulDiv(natural.cx,static_cast<int>(dpi),static_cast<int>(nativeDpi)),MulDiv(natural.cy-trim.load(),static_cast<int>(dpi),static_cast<int>(nativeDpi))};
    }
    std::vector<SelectorEvent> takeEvents() override {
        std::vector<SelectorEvent> result;{std::lock_guard lock(mutex);result.swap(events);}
        for(const auto& event:result){if(event.type==SelectorEvent::Type::Navigation&&!event.folder.empty())lastFolder=event.folder;if(event.type==SelectorEvent::Type::Failed)layoutFallback();}
        return result;
    }
    void requestStop()noexcept override{if(finished.load()||stopping.exchange(true))return;HWND child=embedded.load();if(!child)child=pendingNative;if(child)PostMessageW(child,StopDialog,0,0);else{HWND target=dispatcher.load();if(target)PostMessageW(target,StopDialog,0,0);}}
    bool stopped()const noexcept override{return finished.load();}
    void emit(SelectorEvent event){
        {std::lock_guard lock(mutex);events.push_back(std::move(event));}
        if(notifying.load())PostMessageW(options.owner,options.notification,options.tag,0);
    }
    bool selection(IFileDialog* dialog,bool activate=false){
        wil::com_ptr<IShellItem> item;
        HRESULT hr=activate?dialog->GetResult(&item):dialog->GetCurrentSelection(&item);
        std::optional<FileSelection> selected;if(SUCCEEDED(hr)&&item)selected=selectionOf(item.get());
        // Native folder pickers allow selecting the current location as well.
        if(!selected&&options.kind==SelectorKind::Directory){item.reset();if(SUCCEEDED(dialog->GetFolder(&item))&&item)selected=selectionOf(item.get());}
        if(selected!=lastSelection){lastSelection=selected;emit({SelectorEvent::Type::Selection,selected});}
        if(activate&&selected)emit({SelectorEvent::Type::Activate,selected});
        return selected.has_value();
    }
    HRESULT commit(IFileDialog* dialog){if(!selection(dialog,true))return S_FALSE;committed=true;detach();return S_OK;}
    void clearSelection(){lastSelection.reset();emit({SelectorEvent::Type::Selection});}
    void abortDialog(IFileDialog* dialog,HRESULT error)noexcept{
        abortHosting=true;try{if(!failed.exchange(true))emit({SelectorEvent::Type::Failed,std::nullopt,{},error});}catch(...){}detach();dialog->Close(error);if(pendingNative&&IsWindow(pendingNative))EndDialog(pendingNative,IDCANCEL);
    }
    void folderChanged(IFileDialog* dialog){
        prepareAttachment(dialog);wil::com_ptr<IShellItem> folder;wil::unique_cotaskmem_string path;
        if(SUCCEEDED(dialog->GetFolder(&folder))&&folder&&SUCCEEDED(folder->GetDisplayName(SIGDN_FILESYSPATH,&path)))emit({SelectorEvent::Type::Navigation,std::nullopt,path.get()});
        if(firstFolder&&!options.initialSelection.empty()){
            firstFolder=false;FileSelection selected={options.initialSelection,options.kind==SelectorKind::Directory};lastSelection=selected;emit({SelectorEvent::Type::Selection,selected});
        }else{firstFolder=false;selection(dialog);}
    }
private:
    SelectorOptions options;HWND view=nullptr,fallbackText=nullptr,fallbackButton=nullptr;
    wil::unique_hfont fallbackFont;UINT fallbackDpi=0;
    std::filesystem::path lastFolder;std::optional<FileSelection> standaloneSelection;std::atomic<HWND> embedded=nullptr;
    std::atomic<HWND> dispatcher=nullptr;
    HWND originalParent=nullptr,originalOwner=nullptr;LONG_PTR originalStyle=0,originalExtended=0;
    HWND pendingNative=nullptr;
    SIZE natural={};UINT nativeDpi=96;int bottomMargin=0;std::atomic<int> trim=0;
    std::atomic<bool> ready=false,finished=false,stopping=false,failed=false,notifying=true;
    std::mutex mutex;std::vector<SelectorEvent> events;
    bool firstFolder=true,abortHosting=false,committed=false;std::optional<FileSelection> lastSelection;
    IFileDialog* liveDialog=nullptr; // Borrowed, used only on its owning STA.
    static LRESULT CALLBACK filter(int code,WPARAM w,LPARAM l){
        auto self=filteringSelector;auto message=reinterpret_cast<MSG*>(l);
        if(code==MSGF_DIALOGBOX&&self&&!self->stopping.load()&&message->message>=WM_KEYFIRST&&message->message<=WM_KEYLAST&&(message->hwnd==self->options.owner||IsChild(self->options.owner,message->hwnd))&&IsDialogMessageW(self->options.owner,message))return 1;
        return CallNextHookEx(nullptr,code,w,l);
    }
    void detach()noexcept{
        HWND child=embedded.exchange(nullptr);if(!child)return;
        // Restore the native modal window before asking IFileDialog to close.
        // Its modal loop must finish with its original top-level hierarchy.
        ShowWindow(child,SW_HIDE);SetParent(child,originalParent);
        SetWindowLongPtrW(child,GWL_STYLE,originalStyle&~WS_VISIBLE);SetWindowLongPtrW(child,GWL_EXSTYLE,originalExtended);
        SetWindowLongPtrW(child,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(originalOwner));
        SetWindowPos(child,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    }
    void stopDialog()noexcept{
        HWND child=pendingNative;detach();if(liveDialog)liveDialog->Close(HRESULT_FROM_WIN32(ERROR_CANCELLED));
        // Close marks the COM dialog closed, but its Win32 modal manager can
        // remain waiting after hosting. Finish that same modal dialog on its
        // owning UI thread, after restoring its original window hierarchy.
        if(child&&IsWindow(child))EndDialog(child,committed?IDOK:IDCANCEL);
    }
    static LRESULT CALLBACK dispatchProc(HWND window,UINT message,WPARAM w,LPARAM l){
        auto self=reinterpret_cast<NativeSelector*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<NativeSelector*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(message==StopDialog){if(self)self->stopDialog();return 0;}
        return DefWindowProcW(window,message,w,l);
    }
    std::optional<FileSelection> selectionOf(IShellItem* item)const{
        wil::unique_cotaskmem_string path;SFGAOF flags=0;
        if(FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))||FAILED(item->GetAttributes(SFGAO_FOLDER,&flags)))return std::nullopt;
        bool folder=(flags&SFGAO_FOLDER)!=0;if(folder!=(options.kind==SelectorKind::Directory))return std::nullopt;
        return FileSelection{path.get(),folder};
    }
    void configure(IFileOpenDialog* dialog,bool standalone){
        DWORD flags=0;THROW_IF_FAILED(dialog->GetOptions(&flags));flags|=FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_DONTADDTORECENT|FOS_NOTESTFILECREATE;
        flags&=~FOS_ALLOWMULTISELECT;flags|=options.kind==SelectorKind::Directory?FOS_PICKFOLDERS:FOS_FILEMUSTEXIST;THROW_IF_FAILED(dialog->SetOptions(flags));
        if(options.kind==SelectorKind::ImageOrIcon){auto label=tr(IDS_IMAGE_FILES);COMDLG_FILTERSPEC filter={label.c_str(),L"*.ico;*.png;*.jpg;*.jpeg;*.bmp"};THROW_IF_FAILED(dialog->SetFileTypes(1,&filter));}
        GUID identity={};THROW_IF_FAILED(CoCreateGuid(&identity));THROW_IF_FAILED(dialog->SetClientGuid(identity));
        auto directory=standalone?lastFolder:options.initialFolder;
        if(!directory.empty()){wil::com_ptr<IShellItem> item;if(SUCCEEDED(SHCreateItemFromParsingName(directory.c_str(),nullptr,IID_PPV_ARGS(&item))))dialog->SetFolder(item.get());}
        if(!options.initialSelection.empty()&&(options.kind==SelectorKind::ImageOrIcon||options.initialSelection!=directory))dialog->SetFileName(options.initialSelection.filename().c_str());
    }
    void run()noexcept{
        HRESULT error=S_OK;
        try {
            SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(options.owner));selectLanguage();auto com=wil::CoInitializeEx(COINIT_APARTMENTTHREADED);
            // All hosted windows belong to the owner's UI thread. Cross-thread
            // parenting attaches input queues and can stall native modal loops.
            // A message-only dispatcher exists before Show and does not depend
            // on the native dialog's HWND or its reparented window hierarchy.
            static std::once_flag registered;std::call_once(registered,[]{WNDCLASSW type={};type.hInstance=GetModuleHandleW(nullptr);type.lpfnWndProc=dispatchProc;type.lpszClassName=L"IconicFolders.SelectorDispatcher";THROW_LAST_ERROR_IF(!RegisterClassW(&type));});
            HWND target=CreateWindowExW(0,L"IconicFolders.SelectorDispatcher",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),this);THROW_LAST_ERROR_IF(!target);
            dispatcher=target;auto destroy=wil::scope_exit([&]{dispatcher=nullptr;DestroyWindow(target);});
            wil::com_ptr<IFileOpenDialog> dialog;THROW_IF_FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)));configure(dialog.get(),false);
            wil::com_ptr<DialogEvents> sink;sink.attach(new DialogEvents(*this));DWORD cookie=0;THROW_IF_FAILED(dialog->Advise(sink.get(),&cookie));auto unadvise=wil::scope_exit([&]{dialog->Unadvise(cookie);});
            liveDialog=dialog.get();auto clear=wil::scope_exit([&]{liveDialog=nullptr;});
            // The native modal loop must also route Tab/Enter/Escape through
            // the host dialog, including controls outside its embedded page.
            auto previous=filteringSelector;HHOOK hook=SetWindowsHookExW(WH_MSGFILTER,filter,nullptr,GetCurrentThreadId());THROW_LAST_ERROR_IF(!hook);filteringSelector=this;auto unhook=wil::scope_exit([&]{UnhookWindowsHookEx(hook);filteringSelector=previous;});
            if(!stopping.load()){error=dialog->Show(nullptr);if(error==HRESULT_FROM_WIN32(ERROR_CANCELLED))error=S_OK;}
        }catch(...){error=wil::ResultFromCaughtException();}
        embedded=nullptr;
        try{if(!stopping.load()&&!committed&&!failed.exchange(true))emit({SelectorEvent::Type::Failed,std::nullopt,{},FAILED(error)?error:E_UNEXPECTED});emit({SelectorEvent::Type::Stopped});}catch(...){}
        finished=true;if(notifying.load())PostMessageW(options.owner,options.notification,options.tag,0);
    }
    static BOOL CALLBACK hideControls(HWND child,LPARAM){
        wchar_t name[64]={};GetClassNameW(child,name,64);auto style=GetWindowLongPtrW(child,GWL_STYLE);int id=GetDlgCtrlID(child);
        if((lstrcmpiW(name,L"ScrollBar")==0&&(style&(SBS_SIZEGRIP|SBS_SIZEBOX)))||(lstrcmpiW(name,L"Button")==0&&(id==IDOK||id==IDCANCEL)))ShowWindow(child,SW_HIDE);return TRUE;
    }
    static int fieldsBottom(HWND window){
        struct Fields{HWND window;int bottom=0;} fields={window};
        EnumChildWindows(window,[](HWND child,LPARAM parameter)->BOOL{auto& result=*reinterpret_cast<Fields*>(parameter);wchar_t name[64]={};GetClassNameW(child,name,64);
            if((GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE)&&(lstrcmpiW(name,L"ComboBox")==0||lstrcmpiW(name,L"ComboBoxEx32")==0||lstrcmpiW(name,L"Edit")==0)){
                RECT rect={};GetWindowRect(child,&rect);MapWindowPoints(HWND_DESKTOP,result.window,reinterpret_cast<POINT*>(&rect),2);result.bottom=std::max(result.bottom,static_cast<int>(rect.bottom));
            }return TRUE;
        },reinterpret_cast<LPARAM>(&fields));return fields.bottom;
    }
    void prepareAttachment(IFileDialog* dialog){
        if(embedded.load()||pendingNative)return;wil::com_ptr<IOleWindow> ole;THROW_IF_FAILED(dialog->QueryInterface(IID_PPV_ARGS(&ole)));HWND child=nullptr;THROW_IF_FAILED(ole->GetWindow(&child));
        pendingNative=child;THROW_IF_WIN32_BOOL_FALSE(SetWindowSubclass(child,subclass,1,reinterpret_cast<DWORD_PTR>(this)));PostMessageW(child,AttachDialog,0,0);
    }
    void attach(HWND child){
        RECT rect={};GetClientRect(child,&rect);natural={rect.right,rect.bottom};nativeDpi=GetDpiForWindow(child);
        originalParent=GetAncestor(child,GA_PARENT);originalOwner=GetWindow(child,GW_OWNER);originalStyle=GetWindowLongPtrW(child,GWL_STYLE);originalExtended=GetWindowLongPtrW(child,GWL_EXSTYLE);embedded=child;
        HDC dc=GetDC(child);auto font=reinterpret_cast<HFONT>(SendMessageW(child,WM_GETFONT,0,0));auto previous=SelectObject(dc,font?font:GetStockObject(DEFAULT_GUI_FONT));TEXTMETRICW text={};GetTextMetricsW(dc,&text);SelectObject(dc,previous);ReleaseDC(child,dc);bottomMargin=MulDiv(7,text.tmHeight,8);
        // Experimental bridge: keep every dependency on the native dialog's
        // window tree here. This is not IFileDialog's documented hosting API.
        auto style=GetWindowLongPtrW(child,GWL_STYLE);style&=~(WS_POPUP|WS_CAPTION|WS_THICKFRAME|WS_SYSMENU|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|DS_MODALFRAME);SetWindowLongPtrW(child,GWL_STYLE,style|WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS);
        auto ex=GetWindowLongPtrW(child,GWL_EXSTYLE);SetWindowLongPtrW(child,GWL_EXSTYLE,(ex&~(WS_EX_DLGMODALFRAME|WS_EX_WINDOWEDGE|WS_EX_APPWINDOW))|WS_EX_CONTROLPARENT);
        SetLastError(ERROR_SUCCESS);auto parent=SetParent(child,view);THROW_LAST_ERROR_IF(!parent&&GetLastError()!=ERROR_SUCCESS);EnableThemeDialogTexture(child,ETDT_ENABLETAB);
        GetClientRect(view,&rect);SetWindowPos(child,nullptr,0,0,rect.right,rect.bottom,SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);EnumChildWindows(child,hideControls,0);embedded=child;
        PostMessageW(child,LayoutReady,0,0);if(stopping.load())PostMessageW(dispatcher.load(),StopDialog,0,0);
    }
    static LRESULT CALLBACK subclass(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
        auto self=reinterpret_cast<NativeSelector*>(data);
        try {
            if(message==WM_SHOWWINDOW&&w)PostMessageW(window,AttachDialog,0,0);
            if(message==AttachDialog){if(!self->embedded.load()&&!self->stopping.load()&&!self->committed&&!self->abortHosting&&IsWindowVisible(window))self->attach(window);return 0;}
            if(message==StopDialog){self->stopDialog();return 0;}
            if((message==WM_CLOSE||(message==WM_COMMAND&&LOWORD(w)==IDCANCEL))&&!self->stopping.load()&&!self->abortHosting){self->emit({SelectorEvent::Type::Cancel});return 0;}
            if(message==LayoutReady){RECT rect={};GetClientRect(window,&rect);int bottom=fieldsBottom(window),currentDpi=static_cast<int>(GetDpiForWindow(window));if(bottom>rect.bottom/2){int margin=MulDiv(self->bottomMargin,currentDpi,static_cast<int>(self->nativeDpi));self->trim=MulDiv(std::max(0,static_cast<int>(rect.bottom)-bottom-margin),static_cast<int>(self->nativeDpi),currentDpi);}self->ready=true;self->emit({SelectorEvent::Type::Ready});return 0;}
            if(message==WM_NCDESTROY)RemoveWindowSubclass(window,subclass,1);
            LRESULT result=DefSubclassProc(window,message,w,l);if(message==WM_SIZE||message==WM_WINDOWPOSCHANGED)EnumChildWindows(window,hideControls,0);return result;
        }catch(...){if(message==AttachDialog&&self->liveDialog)self->abortDialog(self->liveDialog,wil::ResultFromCaughtException());return 0;}
    }
    void layoutFallback(){
        if(!fallbackText){auto text=tr(IDS_EMBED_UNAVAILABLE),label=tr(IDS_BROWSE);fallbackText=CreateWindowExW(0,WC_STATICW,text.c_str(),WS_CHILD|WS_VISIBLE|SS_CENTER,0,0,0,0,view,nullptr,GetModuleHandleW(nullptr),nullptr);fallbackButton=CreateWindowExW(0,WC_BUTTONW,label.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,0,0,view,reinterpret_cast<HMENU>(static_cast<INT_PTR>(Browse)),GetModuleHandleW(nullptr),nullptr);}
        UINT dpi=GetDpiForWindow(view);if(dpi!=fallbackDpi){NONCLIENTMETRICSW metrics={sizeof(metrics)};SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0,dpi);fallbackFont.reset(CreateFontIndirectW(&metrics.lfMessageFont));fallbackDpi=dpi;SendMessageW(fallbackText,WM_SETFONT,reinterpret_cast<WPARAM>(fallbackFont.get()),TRUE);SendMessageW(fallbackButton,WM_SETFONT,reinterpret_cast<WPARAM>(fallbackFont.get()),TRUE);}
        RECT rect={};GetClientRect(view,&rect);int scale=static_cast<int>(GetDpiForWindow(view)),width=std::min(static_cast<int>(rect.right),MulDiv(340,scale,96)),height=MulDiv(50,scale,96);SetWindowPos(fallbackText,nullptr,(rect.right-width)/2,std::max(0L,rect.bottom/2-height),width,height,SWP_NOZORDER|SWP_NOACTIVATE);SetWindowPos(fallbackButton,nullptr,(rect.right-MulDiv(160,scale,96))/2,rect.bottom/2,MulDiv(160,scale,96),MulDiv(26,scale,96),SWP_NOZORDER|SWP_NOACTIVATE);
    }
    void browseStandalone(){
        wil::com_ptr<IFileOpenDialog> dialog;THROW_IF_FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)));configure(dialog.get(),true);HRESULT hr=dialog->Show(options.owner);if(hr==HRESULT_FROM_WIN32(ERROR_CANCELLED))return;THROW_IF_FAILED(hr);
        wil::com_ptr<IShellItem> item;THROW_IF_FAILED(dialog->GetResult(&item));auto selected=selectionOf(item.get());if(selected){standaloneSelection=selected;lastFolder=selected->directory?selected->path:selected->path.parent_path();emit({SelectorEvent::Type::Navigation,std::nullopt,lastFolder});emit({SelectorEvent::Type::Selection,selected});}
    }
    static INT_PTR CALLBACK pageProc(HWND window,UINT message,WPARAM w,LPARAM l){
        if(message==WM_INITDIALOG){SetWindowLongPtrW(window,DWLP_USER,l);EnableThemeDialogTexture(window,ETDT_ENABLETAB);return TRUE;}
        auto self=reinterpret_cast<NativeSelector*>(GetWindowLongPtrW(window,DWLP_USER));if(!self)return FALSE;
        if(message==StartDialog){self->run();return TRUE;}
        if(message==WM_COMMAND&&LOWORD(w)==Browse){try{self->browseStandalone();}catch(...){self->emit({SelectorEvent::Type::Failed,std::nullopt,{},wil::ResultFromCaughtException()});}return TRUE;}return FALSE;
    }
};
HRESULT DialogEvents::OnFileOk(IFileDialog* dialog){try{return selector.commit(dialog);}CATCH_RETURN();}
HRESULT DialogEvents::OnFolderChanging(IFileDialog*,IShellItem*){try{selector.clearSelection();return S_OK;}CATCH_RETURN();}
HRESULT DialogEvents::OnFolderChange(IFileDialog* dialog){try{selector.folderChanged(dialog);return S_OK;}catch(...){auto error=wil::ResultFromCaughtException();selector.abortDialog(dialog,error);return error;}}
HRESULT DialogEvents::OnSelectionChange(IFileDialog* dialog){try{selector.selection(dialog);return S_OK;}CATCH_RETURN();}
}
std::unique_ptr<IFileSelector> createNativeFileSelector(const SelectorOptions& options,bool allowEmbedding){return std::make_unique<NativeSelector>(options,allowEmbedding);}
}
