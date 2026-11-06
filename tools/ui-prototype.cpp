// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Benco
// Visual experiment only. No icon conversion, desktop.ini changes or settings.
// Reparenting IFileDialog is not a documented hosting contract. This executable
// exists to evaluate its appearance and behavior before choosing an approach.
#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <commctrl.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result.h>
#include <atomic>
#include <thread>
#include <array>
#include <string>
#include <algorithm>

namespace {
constexpr UINT Ready=WM_APP+1,Finished=WM_APP+2,NativeLayoutReady=WM_APP+3;
constexpr int Tabs=100,Reference=101,Copy=102,Add=103;
std::wstring debugLog;
void trace(const std::string& text){if(debugLog.empty())return;wil::unique_hfile file(CreateFileW(debugLog.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));if(file){DWORD n=0;auto line=text+"\r\n";WriteFile(file.get(),line.data(),static_cast<DWORD>(line.size()),&n,nullptr);}}
struct NativePanel;
struct Events final:IFileDialogEvents {
    std::atomic<ULONG> references=1;NativePanel& panel;
    explicit Events(NativePanel& p):panel(p){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** value) override {
        *value=nullptr;if(id!=IID_IUnknown&&id!=IID_IFileDialogEvents)return E_NOINTERFACE;*value=static_cast<IFileDialogEvents*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++references;}
    ULONG STDMETHODCALLTYPE Release() override{auto count=--references;if(!count)delete this;return count;}
    HRESULT STDMETHODCALLTYPE OnFileOk(IFileDialog*) override{return S_FALSE;}
    HRESULT STDMETHODCALLTYPE OnFolderChanging(IFileDialog*,IShellItem*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnFolderChange(IFileDialog*) override;
    HRESULT STDMETHODCALLTYPE OnSelectionChange(IFileDialog*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnShareViolation(IFileDialog*,IShellItem*,FDE_SHAREVIOLATION_RESPONSE* response) override{*response=FDESVR_DEFAULT;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnTypeChange(IFileDialog*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnOverwrite(IFileDialog*,IShellItem*,FDE_OVERWRITE_RESPONSE* response) override{*response=FDEOR_DEFAULT;return S_OK;}
};
struct NativePanel {
    HWND parent=nullptr,host=nullptr;std::atomic<HWND> dialog=nullptr;
    int naturalWidth=0,naturalHeight=0,bottomMargin=0;std::atomic<int> trimBottom=0;std::atomic<bool> ready=false;UINT originalDpi=96;
    std::atomic<bool> stopping=false,finished=false;std::thread worker;bool folders=false;std::wstring initial;
    static BOOL CALLBACK hideButtons(HWND child,LPARAM) {
        wchar_t type[64]={};GetClassNameW(child,type,64);int id=GetDlgCtrlID(child);
        bool grip=lstrcmpiW(type,L"ScrollBar")==0&&(GetWindowLongPtrW(child,GWL_STYLE)&(SBS_SIZEGRIP|SBS_SIZEBOX));
        if(grip||(lstrcmpiW(type,L"Button")==0&&(id==IDOK||id==IDCANCEL)))ShowWindow(child,SW_HIDE);return TRUE;
    }
    static int fieldsBottom(HWND window){
        struct Fields{HWND window;int bottom=0;} fields={window};
        EnumChildWindows(window,[](HWND child,LPARAM data)->BOOL{
            auto& f=*reinterpret_cast<Fields*>(data);wchar_t type[64]={};GetClassNameW(child,type,64);
            if((GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE)&&(lstrcmpiW(type,L"ComboBox")==0||lstrcmpiW(type,L"ComboBoxEx32")==0||lstrcmpiW(type,L"Edit")==0)){
                RECT rect={};GetWindowRect(child,&rect);MapWindowPoints(HWND_DESKTOP,f.window,reinterpret_cast<POINT*>(&rect),2);f.bottom=std::max(f.bottom,static_cast<int>(rect.bottom));
            }return TRUE;
        },reinterpret_cast<LPARAM>(&fields));return fields.bottom;
    }
    static LRESULT CALLBACK subclass(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
        auto self=reinterpret_cast<NativePanel*>(data);
        if((message==WM_CLOSE||(message==WM_COMMAND&&(LOWORD(w)==IDCANCEL||LOWORD(w)==IDOK)))&&!self->stopping)return 0;
        if(message==NativeLayoutReady){
            RECT client={};GetClientRect(window,&client);int bottom=fieldsBottom(window);
            if(bottom>client.bottom/2)self->trimBottom=std::max(0,static_cast<int>(client.bottom)-bottom-self->bottomMargin);
            trace("stable footer "+std::to_string(bottom)+" of "+std::to_string(client.bottom)+", trim="+std::to_string(self->trimBottom.load()));
            self->ready=true;PostMessageW(self->parent,Ready,0,0);return 0;
        }
        if(message==WM_NCDESTROY)RemoveWindowSubclass(window,subclass,1);
        LRESULT result=DefSubclassProc(window,message,w,l);
        if(message==WM_WINDOWPOSCHANGED||message==WM_SIZE)EnumChildWindows(window,hideButtons,0);
        return result;
    }
    void attach(IFileDialog* source) {
        if(dialog.load())return;
        trace(folders?"folder attach":"file attach");
        wil::com_ptr<IOleWindow> ole;THROW_IF_FAILED(source->QueryInterface(IID_PPV_ARGS(&ole)));HWND hwnd=nullptr;THROW_IF_FAILED(ole->GetWindow(&hwnd));
        RECT natural={};GetClientRect(hwnd,&natural);naturalWidth=natural.right;naturalHeight=natural.bottom;originalDpi=GetDpiForWindow(hwnd);
        HDC dc=GetDC(hwnd);auto font=reinterpret_cast<HFONT>(SendMessageW(hwnd,WM_GETFONT,0,0));auto old=SelectObject(dc,font?font:GetStockObject(DEFAULT_GUI_FONT));TEXTMETRICW text={};GetTextMetricsW(dc,&text);SelectObject(dc,old);ReleaseDC(hwnd,dc);bottomMargin=MulDiv(7,text.tmHeight,8);
        // This belongs to the experimental hosting bridge. Measure standard
        // fields after the native dialog completes its initial layout, with no
        // private control IDs or fixed offsets. Otherwise retain its full area.
        trace("natural "+std::to_string(naturalWidth)+"x"+std::to_string(naturalHeight));
        SetWindowSubclass(hwnd,subclass,1,reinterpret_cast<DWORD_PTR>(this));
        auto style=GetWindowLongPtrW(hwnd,GWL_STYLE);style&=~(WS_POPUP|WS_CAPTION|WS_THICKFRAME|WS_SYSMENU|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|DS_MODALFRAME);style|=WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS;
        SetWindowLongPtrW(hwnd,GWL_STYLE,style);auto ex=GetWindowLongPtrW(hwnd,GWL_EXSTYLE);SetWindowLongPtrW(hwnd,GWL_EXSTYLE,(ex&~(WS_EX_DLGMODALFRAME|WS_EX_WINDOWEDGE|WS_EX_APPWINDOW))|WS_EX_CONTROLPARENT);
        SetLastError(ERROR_SUCCESS);auto previous=SetParent(hwnd,host);THROW_LAST_ERROR_IF(!previous&&GetLastError()!=ERROR_SUCCESS);
        EnableThemeDialogTexture(hwnd,ETDT_ENABLETAB);
        trace("parent and theme set");
        RECT rect={};GetClientRect(host,&rect);SetWindowPos(hwnd,nullptr,0,0,rect.right,rect.bottom+trimBottom,SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
        EnumChildWindows(hwnd,hideButtons,0);dialog.store(hwnd);PostMessageW(hwnd,NativeLayoutReady,0,0);
        trace("attached, waiting for native layout");
        if(stopping)PostMessageW(hwnd,WM_CLOSE,0,0);
    }
    void start() {
        worker=std::thread([this]{
            trace("worker started");
            HRESULT error=S_OK;
            try {
                auto com=wil::CoInitializeEx(COINIT_APARTMENTTHREADED);wil::com_ptr<IFileOpenDialog> picker;THROW_IF_FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&picker)));
                trace("file dialog created");
                DWORD options=0;THROW_IF_FAILED(picker->GetOptions(&options));options|=FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_DONTADDTORECENT|FOS_NOTESTFILECREATE;
                if(folders)options|=FOS_PICKFOLDERS;else options|=FOS_FILEMUSTEXIST;
                THROW_IF_FAILED(picker->SetOptions(options));
                if(!folders){COMDLG_FILTERSPEC filter={L"Images et icônes (*.ico; *.png; *.jpg; *.jpeg; *.bmp)",L"*.ico;*.png;*.jpg;*.jpeg;*.bmp"};THROW_IF_FAILED(picker->SetFileTypes(1,&filter));}
                // Fresh prototype identity: measure the native initial geometry,
                // without erasing history or reusing an embedded window's size.
                GUID identity={};THROW_IF_FAILED(CoCreateGuid(&identity));THROW_IF_FAILED(picker->SetClientGuid(identity));wil::com_ptr<IShellItem> item;if(SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(),nullptr,IID_PPV_ARGS(&item))))picker->SetFolder(item.get());
                wil::com_ptr<Events> events;events.attach(new Events(*this));DWORD cookie=0;THROW_IF_FAILED(picker->Advise(events.get(),&cookie));auto unadvise=wil::scope_exit([&]{picker->Unadvise(cookie);});
                trace("show file dialog");error=picker->Show(nullptr);trace("file dialog returned "+std::to_string(error));if(error==HRESULT_FROM_WIN32(ERROR_CANCELLED))error=S_OK;
            }catch(...){error=wil::ResultFromCaughtException();trace("worker error "+std::to_string(error));}
            dialog.store(nullptr);finished.store(true);PostMessageW(parent,Finished,static_cast<WPARAM>(error),0);
        });
    }
    void close(){stopping=true;auto hwnd=dialog.load();if(hwnd)PostMessageW(hwnd,WM_CLOSE,0,0);}
};
HRESULT Events::OnFolderChange(IFileDialog* source){try{panel.attach(source);return S_OK;}CATCH_RETURN();}

struct Prototype {
    HWND window=nullptr,tabs=nullptr,catalog=nullptr,sources=nullptr,grid=nullptr,add=nullptr,reference=nullptr,copy=nullptr,note=nullptr,preview=nullptr,resolution=nullptr,cancel=nullptr,apply=nullptr,status=nullptr;
    wil::unique_hfont font,trashFont;wil::unique_hicon folderIcon;wil::unique_himagelist images;
    std::array<NativePanel,2> native;UINT dpi=96;int active=0,baseX=6,baseY=13;bool closing=false,checked=false,sized=false;std::wstring initial,verificationLog;
    int px(int n)const{return MulDiv(n,static_cast<int>(dpi),96);}
    int dx(int dlu)const{return MulDiv(dlu,baseX,4);}
    int dy(int dlu)const{return MulDiv(dlu,baseY,8);}
    void updateFont(){
        NONCLIENTMETRICSW metrics={sizeof(metrics)};THROW_IF_WIN32_BOOL_FALSE(SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0,dpi));font.reset(CreateFontIndirectW(&metrics.lfMessageFont));THROW_LAST_ERROR_IF(!font);
        HDC dc=GetDC(window);auto old=SelectObject(dc,font.get());TEXTMETRICW text={};SIZE alphabet={};GetTextMetricsW(dc,&text);GetTextExtentPoint32W(dc,L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",52,&alphabet);SelectObject(dc,old);ReleaseDC(window,dc);
        // The same font-dependent base units used by MapDialogRect.
        baseX=(alphabet.cx/26+1)/2;baseY=text.tmHeight;
        LOGFONTW glyph=metrics.lfMessageFont;glyph.lfHeight=-px(16);lstrcpyW(glyph.lfFaceName,L"Segoe MDL2 Assets");trashFont.reset(CreateFontIndirectW(&glyph));THROW_LAST_ERROR_IF(!trashFont);
        for(HWND h:{tabs,sources,grid,add,reference,copy,note,resolution,cancel,apply,status})if(h)SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font.get()),TRUE);
    }
    HWND create(PCWSTR type,PCWSTR text,DWORD style,int id=0,HWND owner=nullptr,DWORD ex=0){
        HWND h=CreateWindowExW(ex,type,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,owner?owner:window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);THROW_LAST_ERROR_IF(!h);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font.get()),TRUE);return h;
    }
    HWND createPage(){
        struct Template{DLGTEMPLATE dialog;WORD menu=0,klass=0,title=0;} page={};page.dialog.style=WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_CLIPSIBLINGS|DS_CONTROL;page.dialog.dwExtendedStyle=WS_EX_CONTROLPARENT;
        HWND hwnd=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),&page.dialog,window,pageProcedure,reinterpret_cast<LPARAM>(this));THROW_LAST_ERROR_IF(!hwnd);return hwnd;
    }
    static INT_PTR CALLBACK pageProcedure(HWND hwnd,UINT message,WPARAM,LPARAM l){
        if(message==WM_INITDIALOG){SetWindowLongPtrW(hwnd,DWLP_USER,l);EnableThemeDialogTexture(hwnd,ETDT_ENABLETAB);return TRUE;}
        auto self=reinterpret_cast<Prototype*>(GetWindowLongPtrW(hwnd,DWLP_USER));
        if(self&&message==WM_NOTIFY){auto header=reinterpret_cast<NMHDR*>(l);if(header->hwndFrom==self->sources&&header->code==NM_CUSTOMDRAW){
            auto draw=reinterpret_cast<NMLVCUSTOMDRAW*>(l);LRESULT result=CDRF_DODEFAULT;
            if(draw->nmcd.dwDrawStage==CDDS_PREPAINT)result=CDRF_NOTIFYITEMDRAW;
            else if(draw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT)result=CDRF_NOTIFYPOSTPAINT;
            else if(draw->nmcd.dwDrawStage==CDDS_ITEMPOSTPAINT&&draw->nmcd.dwItemSpec==4){
                RECT row={},client={};ListView_GetItemRect(self->sources,4,&row,LVIR_BOUNDS);GetClientRect(self->sources,&client);row.right=client.right-self->dx(4);row.left=row.right-self->px(20);
                int saved=SaveDC(draw->nmcd.hdc);SelectObject(draw->nmcd.hdc,self->trashFont.get());SetBkMode(draw->nmcd.hdc,TRANSPARENT);SetTextColor(draw->nmcd.hdc,GetSysColor(COLOR_GRAYTEXT));DrawTextW(draw->nmcd.hdc,L"\xE74D",1,&row,DT_CENTER|DT_VCENTER|DT_SINGLELINE);RestoreDC(draw->nmcd.hdc,saved);
            }
            SetWindowLongPtrW(hwnd,DWLP_MSGRESULT,result);return TRUE;
        }}return FALSE;
    }
    void initialize() {
        dpi=GetDpiForWindow(window);updateFont();
        auto appIcon=LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(1));SendMessageW(window,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(appIcon));SendMessageW(window,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(appIcon));const DWORD rounded=2;DwmSetWindowAttribute(window,33,&rounded,sizeof(rounded));
        tabs=create(WC_TABCONTROLW,L"",WS_TABSTOP|WS_CLIPSIBLINGS,Tabs);
        const wchar_t* names[]={L"Image ou icône",L"Autre répertoire",L"Catalogue d’icônes"};for(int i=0;i<3;++i){TCITEMW item={};item.mask=TCIF_TEXT;item.pszText=const_cast<PWSTR>(names[i]);TabCtrl_InsertItem(tabs,i,&item);}
        for(int i=0;i<2;++i){native[i].parent=window;native[i].folders=i==1;native[i].initial=initial;native[i].host=createPage();}
        catalog=createPage();
        sources=create(WC_LISTVIEWW,L"Sources",LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|LVS_NOCOLUMNHEADER|WS_TABSTOP,0,catalog,WS_EX_CLIENTEDGE);
        LVCOLUMNW column={};column.mask=LVCF_WIDTH;column.cx=px(214);ListView_InsertColumn(sources,0,&column);
        const wchar_t* collections[]={L"Windows",L"Explorateur",L"Appareils",L"Classiques",L"Application ajoutée.dll"};
        for(int i=0;i<5;++i){LVITEMW item={};item.mask=LVIF_TEXT;item.iItem=i;item.pszText=const_cast<PWSTR>(collections[i]);ListView_InsertItem(sources,&item);}ListView_SetItemState(sources,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        add=create(WC_BUTTONW,L"Ajouter une EXE/DLL…",BS_PUSHBUTTON|WS_TABSTOP,Add,catalog);EnableWindow(add,FALSE);
        grid=create(WC_LISTVIEWW,L"Icônes",LVS_ICON|LVS_SINGLESEL|LVS_SHOWSELALWAYS|LVS_AUTOARRANGE|WS_TABSTOP,0,catalog,WS_EX_CLIENTEDGE);
        for(HWND h:{sources,grid}){SetWindowTheme(h,L"Explorer",nullptr);ListView_SetExtendedListViewStyle(h,LVS_EX_DOUBLEBUFFER|LVS_EX_FULLROWSELECT);}
        images.reset(ImageList_Create(px(48),px(48),ILC_COLOR32|ILC_MASK,80,16));ListView_SetImageList(grid,images.get(),LVSIL_NORMAL);ListView_SetIconSpacing(grid,px(76),px(82));
        wchar_t system[32768]={};GetSystemDirectoryW(system,32768);std::wstring source=std::wstring(system)+L"\\imageres.dll";
        for(int i=0;i<80;++i){HICON raw=nullptr;if(ExtractIconExW(source.c_str(),i,&raw,nullptr,1)&&raw){wil::unique_hicon icon(raw);int image=ImageList_AddIcon(images.get(),raw);auto text=std::to_wstring(i+1);LVITEMW item={};item.mask=LVIF_TEXT|LVIF_IMAGE;item.iItem=i;item.iImage=image;item.pszText=text.data();ListView_InsertItem(grid,&item);}}
        reference=create(WC_BUTTONW,L"Utiliser l’icône d’origine",BS_AUTORADIOBUTTON|WS_TABSTOP|WS_GROUP,Reference);copy=create(WC_BUTTONW,L"Copier l’icône dans le dossier",BS_AUTORADIOBUTTON|WS_TABSTOP,Copy);SendMessageW(reference,BM_SETCHECK,BST_CHECKED,0);
        note=create(WC_STATICW,L"Les images doivent être converties en icones et stockées dans le répertoire",SS_LEFT);ShowWindow(note,SW_HIDE);
        preview=create(WC_STATICW,L"",SS_ICON|SS_REALSIZECONTROL|SS_CENTERIMAGE);SHSTOCKICONINFO stock={sizeof(stock)};if(SUCCEEDED(SHGetStockIconInfo(SIID_FOLDER,SHGSI_ICON|SHGSI_LARGEICON,&stock))){folderIcon.reset(stock.hIcon);SendMessageW(preview,STM_SETICON,reinterpret_cast<WPARAM>(folderIcon.get()),0);}
        resolution=create(WC_STATICW,L"Résolution max : 256 × 256",SS_RIGHT);cancel=create(WC_BUTTONW,L"Fermer le prototype",BS_PUSHBUTTON|WS_TABSTOP,IDCANCEL);apply=create(WC_BUTTONW,L"Appliquer",BS_DEFPUSHBUTTON|WS_TABSTOP,IDOK);EnableWindow(apply,FALSE);
        status=create(WC_STATICW,L"",SS_LEFT);ShowWindow(status,SW_HIDE);
        layout();switchTab();for(auto& panel:native)panel.start();
    }
    void layout() {
        if(!tabs)return;RECT client={};GetClientRect(window,&client);int w=client.right,h=client.bottom;
        auto move=[](HWND item,int x,int y,int width,int height){SetWindowPos(item,nullptr,x,y,std::max(0,width),std::max(0,height),SWP_NOACTIVATE|SWP_NOZORDER);};
        // Windows dialog guidelines: 7-DLU margins, 4-DLU related spacing,
        // 10-DLU radio height, 14-DLU button height. Never reserve the hidden note.
        int mx=dx(7),my=dy(7),gap=dy(4),buttonHeight=dy(14),radioHeight=dy(10),labelHeight=dy(8);
        int buttonY=h-my-buttonHeight,optionsBottom=buttonY-gap,optionsTop=optionsBottom-labelHeight-dy(3)-px(48),tabBottom=optionsTop-my;
        move(tabs,mx,my,w-2*mx,tabBottom-my);
        RECT page={};GetClientRect(tabs,&page);TabCtrl_AdjustRect(tabs,FALSE,&page);MapWindowPoints(tabs,window,reinterpret_cast<POINT*>(&page),2);
        int pageWidth=page.right-page.left,pageHeight=page.bottom-page.top;
        for(auto& panel:native){move(panel.host,page.left,page.top,pageWidth,pageHeight);HWND hwnd=panel.dialog.load();if(hwnd)SetWindowPos(hwnd,nullptr,0,0,pageWidth,pageHeight+MulDiv(panel.trimBottom,static_cast<int>(dpi),static_cast<int>(panel.originalDpi)),SWP_NOACTIVATE|SWP_NOZORDER|SWP_ASYNCWINDOWPOS);}
        move(catalog,page.left,page.top,pageWidth,pageHeight);
        int sourceWidth=dx(130),innerX=dx(7),innerY=dy(7),catalogHeight=pageHeight-2*innerY;
        move(sources,innerX,innerY,sourceWidth,catalogHeight-buttonHeight-gap);move(add,innerX,pageHeight-innerY-buttonHeight,sourceWidth,buttonHeight);
        move(grid,innerX+sourceWidth+dx(7),innerY,pageWidth-2*innerX-sourceWidth-dx(7),catalogHeight);RECT list={};GetClientRect(sources,&list);ListView_SetColumnWidth(sources,0,list.right);
        int radioX=page.left+innerX,radioWidth=w-radioX-mx-dx(170);
        move(reference,radioX,optionsTop,radioWidth,radioHeight);move(copy,radioX,optionsTop+radioHeight+gap,radioWidth,radioHeight);move(note,radioX+dx(10),optionsTop+2*(radioHeight+gap),radioWidth-dx(10),2*labelHeight);
        move(preview,w-mx-px(48),optionsTop,px(48),px(48));move(resolution,w-mx-dx(160),optionsBottom-labelHeight,dx(160),labelHeight);
        int applyWidth=dx(50),cancelWidth=dx(95),buttonGap=dx(4);
        move(cancel,w-mx-applyWidth-buttonGap-cancelWidth,buttonY,cancelWidth,buttonHeight);move(apply,w-mx-applyWidth,buttonY,applyWidth,buttonHeight);
    }
    void sizeFromNative(){
        if(sized||!native[0].ready.load())return;sized=true;
        trace("sizeFromNative");
        // Production will restore saved window placement first. The native
        // initial client size is the first-launch default only.
        RECT client={},page={};GetClientRect(window,&client);GetClientRect(native[0].host,&page);
        RECT desired={0,0,client.right+native[0].naturalWidth-page.right,client.bottom+native[0].naturalHeight-native[0].trimBottom-page.bottom};
        THROW_IF_WIN32_BOOL_FALSE(AdjustWindowRectExForDpi(&desired,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_EXSTYLE)),dpi));
        MONITORINFO monitor={sizeof(monitor)};THROW_IF_WIN32_BOOL_FALSE(GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor));auto& work=monitor.rcWork;
        int width=std::min(desired.right-desired.left,work.right-work.left),height=std::min(desired.bottom-desired.top,work.bottom-work.top);
        SetWindowPos(window,nullptr,work.left+(work.right-work.left-width)/2,work.top+(work.bottom-work.top-height)/2,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
        trace("native size applied "+std::to_string(width)+"x"+std::to_string(height));
    }
    void switchTab(){
        active=TabCtrl_GetCurSel(tabs);for(int i=0;i<2;++i)ShowWindow(native[i].host,active==i?SW_SHOW:SW_HIDE);ShowWindow(catalog,active==2?SW_SHOW:SW_HIDE);
        // Pages and tab control are siblings. The full-size tab body otherwise
        // covers the page even though all its controls exist and are "visible".
        HWND page=active<2?native[active].host:catalog;
        SetWindowPos(page,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
    }
    bool pageIsInFront()const{
        HWND page=active<2?native[active].host:catalog;RECT rect={};GetClientRect(page,&rect);if(rect.right<=0||rect.bottom<=0)return false;
        POINT point={rect.right/2,rect.bottom/2};MapWindowPoints(page,window,&point,1);
        if(ChildWindowFromPointEx(window,point,CWP_SKIPINVISIBLE)!=page)return false;
        if(active<2){POINT inside={rect.right/2,rect.bottom/2};HWND child=native[active].dialog.load();return child&&IsWindowVisible(child)&&ChildWindowFromPointEx(page,inside,CWP_SKIPINVISIBLE)==child;}
        return IsWindowVisible(grid)&&IsWindowVisible(sources);
    }
    void verifyLayout(){
        std::string log;bool passed=true;
        auto record=[&](bool ok,const std::string& detail){log+=(ok?"PASS ":"FAIL ")+detail+"\r\n";passed=passed&&ok;};
        HWND embedded=native[0].dialog.load();RECT embeddedSize={};GetClientRect(embedded,&embeddedSize);
        record(embeddedSize.right==native[0].naturalWidth&&embeddedSize.bottom==native[0].naturalHeight,"native file dialog default size "+std::to_string(native[0].naturalWidth)+" x "+std::to_string(native[0].naturalHeight)+", embedded "+std::to_string(embeddedSize.right)+" x "+std::to_string(embeddedSize.bottom));
        record(Header_GetItemCount(ListView_GetHeader(sources))==1,"source list has one column");
        for(int i=0;i<3;++i){
            TabCtrl_SetCurSel(tabs,i);switchTab();bool visible=pageIsInFront();
            log+=(visible?"PASS ":"FAIL ")+std::string("page ")+std::to_string(i+1)+" is in front of tab body\r\n";passed=passed&&visible;
            // Negative control reproduces the V2 regression, then restores the page.
            SetWindowPos(tabs,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            bool detects=!pageIsInFront();log+=(detects?"PASS ":"FAIL ")+std::string("covered page detected\r\n");passed=passed&&detects;switchTab();
            if(i<2){
                RECT host={};GetClientRect(native[i].host,&host);int fields=NativePanel::fieldsBottom(native[i].dialog.load()),gap=host.bottom-fields;
                record(fields>0&&abs(gap-native[i].bottomMargin)<=2,"native footer gap "+std::to_string(gap)+", expected "+std::to_string(native[i].bottomMargin)+", removed "+std::to_string(native[i].trimBottom));
                bool grip=false;EnumChildWindows(native[i].dialog.load(),[](HWND child,LPARAM data)->BOOL{wchar_t name[64]={};GetClassNameW(child,name,64);auto style=GetWindowLongPtrW(child,GWL_STYLE);if(lstrcmpiW(name,L"ScrollBar")==0&&(style&WS_VISIBLE)&&(style&(SBS_SIZEGRIP|SBS_SIZEBOX)))*reinterpret_cast<bool*>(data)=true;return TRUE;},reinterpret_cast<LPARAM>(&grip));record(!grip,"native size grip hidden");
            }
        }
        log+=passed?"RESULT PASS\r\n":"RESULT FAIL\r\n";
        wil::unique_hfile file(CreateFileW(verificationLog.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));THROW_LAST_ERROR_IF(!file);DWORD written=0;THROW_IF_WIN32_BOOL_FALSE(WriteFile(file.get(),log.data(),static_cast<DWORD>(log.size()),&written,nullptr));close();
    }
    void close(){if(closing)return;closing=true;ShowWindow(window,SW_HIDE);for(auto& panel:native)panel.close();}
    bool finished()const{return closing&&std::all_of(native.begin(),native.end(),[](const NativePanel& p){return p.finished.load();});}
    static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l){
        auto self=reinterpret_cast<Prototype*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));if(message==WM_NCCREATE){self=static_cast<Prototype*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(!self)return DefWindowProcW(hwnd,message,w,l);
        try {
            switch(message){
            case WM_CREATE:self->initialize();return 0;
            case WM_SIZE:self->layout();return 0;
            case Ready:trace("main Ready");self->sizeFromNative();self->layout();self->switchTab();if(!self->verificationLog.empty()&&!self->checked&&self->native[0].ready.load()&&self->native[1].ready.load()){self->checked=true;SetTimer(hwnd,1,500,nullptr);trace("verification scheduled");}return 0;
            case WM_TIMER:if(w==1){KillTimer(hwnd,1);self->verifyLayout();}return 0;
            case Finished:if(FAILED(static_cast<HRESULT>(w)))MessageBoxW(hwnd,L"La boîte native n’a pas pu être hébergée sur ce poste.",L"IconicFolders — prototype",MB_OK|MB_ICONERROR);return 0;
            case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={self->px(640),self->px(480)};return 0;
            case WM_DPICHANGED:{self->dpi=HIWORD(w);self->updateFont();auto rect=reinterpret_cast<RECT*>(l);SetWindowPos(hwnd,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);self->layout();return 0;}
            case WM_NOTIFY:if(reinterpret_cast<NMHDR*>(l)->idFrom==Tabs&&reinterpret_cast<NMHDR*>(l)->code==TCN_SELCHANGE)self->switchTab();return 0;
            case WM_COMMAND:if(LOWORD(w)==IDCANCEL)self->close();return 0;
            case WM_CLOSE:self->close();return 0;
            }
        }catch(...){MessageBoxW(hwnd,L"Impossible de créer le prototype.",L"IconicFolders",MB_OK|MB_ICONERROR);self->close();}
        return DefWindowProcW(hwnd,message,w,l);
    }
};
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int){
    try {
        auto com=wil::CoInitializeEx(COINIT_APARTMENTTHREADED);INITCOMMONCONTROLSEX common={sizeof(common),ICC_STANDARD_CLASSES|ICC_TAB_CLASSES|ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&common);
        Prototype prototype;int argc=0;wil::unique_hlocal_ptr<wchar_t*[]> argv(CommandLineToArgvW(GetCommandLineW(),&argc));
        if(argc>1)prototype.initial=argv[1];else{wil::unique_cotaskmem_string path;THROW_IF_FAILED(SHGetKnownFolderPath(FOLDERID_Pictures,0,nullptr,&path));prototype.initial=path.get();}
        if(argc==4&&std::wstring(argv[2])==L"--verify-layout"){prototype.verificationLog=argv[3];debugLog=prototype.verificationLog+L".startup";}
        WNDCLASSW klass={};klass.lpfnWndProc=Prototype::procedure;klass.hInstance=instance;klass.lpszClassName=L"IconicFolders.UIPrototype";klass.hbrBackground=GetSysColorBrush(COLOR_BTNFACE);klass.hCursor=LoadCursorW(nullptr,IDC_ARROW);THROW_LAST_ERROR_IF(!RegisterClassW(&klass));
        HWND hwnd=CreateWindowExW(WS_EX_CONTROLPARENT,klass.lpszClassName,L"IconicFolders — prototype d’intégration native v4",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1060,800,nullptr,nullptr,instance,&prototype);THROW_LAST_ERROR_IF(!hwnd);ShowWindow(hwnd,SW_SHOWNORMAL);ShowWindow(hwnd,SW_SHOW);UpdateWindow(hwnd);
        MSG msg={};while(!prototype.finished()&&GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(hwnd,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
        for(auto& panel:prototype.native){panel.close();if(panel.worker.joinable())panel.worker.join();}DestroyWindow(hwnd);return 0;
    }catch(...){MessageBoxW(nullptr,L"Impossible de lancer le prototype.",L"IconicFolders",MB_OK|MB_ICONERROR);return 1;}
}
