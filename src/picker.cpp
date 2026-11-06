// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Benco
#include "picker.h"
#include "file-selector.h"
#include "ui.h"
#include "resource.h"
#include <shlobj.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <wil/com.h>
#include <wil/result.h>
#include <array>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace iconic {
namespace {
constexpr wchar_t Settings[]=L"Software\\IconicFolders";
enum : UINT { Tabs=200,Sources,AddSource,Grid,Reference,Copy,Note,Preview,Resolution,Status };
constexpr UINT SelectorChanged=WM_APP+10,Loaded=WM_APP+11,CreateSelectors=WM_APP+12;
std::wstring registryText(PCWSTR name){
    DWORD size=0;if(RegGetValueW(HKEY_CURRENT_USER,Settings,name,RRF_RT_REG_SZ,nullptr,nullptr,&size)!=ERROR_SUCCESS||size>65536)return {};
    std::vector<wchar_t> data(size/sizeof(wchar_t)+1,0);return RegGetValueW(HKEY_CURRENT_USER,Settings,name,RRF_RT_REG_SZ,nullptr,data.data(),&size)==ERROR_SUCCESS?data.data():L"";
}
wil::unique_hkey settingsKey(){HKEY key=nullptr;THROW_IF_WIN32_ERROR(RegCreateKeyExW(HKEY_CURRENT_USER,Settings,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr));return wil::unique_hkey(key);}
void saveText(HKEY key,PCWSTR name,const std::wstring& value){THROW_IF_WIN32_ERROR(RegSetValueExW(key,name,0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),static_cast<DWORD>((value.size()+1)*sizeof(wchar_t))));}
std::vector<fs::path> storedSources(){
    DWORD size=0;std::vector<fs::path> result;if(RegGetValueW(HKEY_CURRENT_USER,Settings,L"Sources",RRF_RT_REG_MULTI_SZ,nullptr,nullptr,&size)!=ERROR_SUCCESS||size>1024*1024)return result;
    std::vector<wchar_t> data(size/sizeof(wchar_t)+2,0);if(RegGetValueW(HKEY_CURRENT_USER,Settings,L"Sources",RRF_RT_REG_MULTI_SZ,nullptr,data.data(),&size)==ERROR_SUCCESS)
        for(size_t p=0;p<data.size()&&data[p];){size_t end=p;while(end<data.size()&&data[end])++end;result.emplace_back(std::wstring(data.data()+p,end-p));p=end+1;}return result;
}
bool samePath(const fs::path& a,const fs::path& b){return CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE)==CSTR_EQUAL;}
void showError(HWND owner,HRESULT hr){
    wchar_t* raw=nullptr;FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,hr,0,reinterpret_cast<wchar_t*>(&raw),0,nullptr);auto free=wil::scope_exit([&]{LocalFree(raw);});auto title=tr(IDS_SOURCE_ERROR);
    auto text=raw?std::wstring(raw):L"HRESULT "+std::to_wstring(static_cast<unsigned long>(hr));TaskDialog(owner,nullptr,L"IconicFolders",title.c_str(),text.c_str(),TDCBF_OK_BUTTON,TD_ERROR_ICON,nullptr);
}
struct Source{fs::path path;std::wstring name;bool builtin=false;};
struct Candidate{fs::path path;int index=0;bool folder=false;unsigned generation=0;std::optional<IconData> data;std::wstring status;};
struct Completion{int tab;unsigned generation;std::optional<IconData> data;HRESULT error=S_OK;};
struct Geometry{DWORD version=1;WINDOWPLACEMENT placement={sizeof(WINDOWPLACEMENT)};};
struct Picker {
    HWND window=nullptr,catalog=nullptr;std::array<HWND,Status+1> controls={};RECT pageBounds={};
    wil::unique_hfont font,trashFont;wil::unique_hicon preview;wil::unique_himagelist thumbnails;
    std::array<std::unique_ptr<IFileSelector>,2> selectors;std::array<Candidate,3> candidates;std::vector<Source> sources;std::vector<int> indices;
    std::array<bool,2> retiring={},validatedSelector={};std::array<fs::path,2> browsingDirectories;bool selectorsInitialized=false;
    IconMode mode;int active=0,catalogSource=-1,baseX=6,baseY=13;size_t nextThumbnail=0;UINT dpi=96;unsigned serial=0;
    bool accepted=false,closing=false,updating=false,applyWhenLoaded=false,sized=false,geometryRestored=false;
    std::wstring title;IconData result;std::optional<IconInfo> initialIcon;fs::path initialDirectory;HRESULT initialization=S_OK;
    std::mutex mutex;std::condition_variable condition;std::array<std::optional<Candidate>,3> pending;std::vector<Completion> completed;bool stopping=false;std::thread worker;
    explicit Picker(const std::vector<fs::path>& folders){
        title=folders.size()==1?format(IDS_TITLE_ONE,folders[0].filename().native()):format(IDS_TITLE_MANY,std::to_wstring(folders.size()));DWORD copy=0,size=sizeof(copy);RegGetValueW(HKEY_CURRENT_USER,Settings,L"Copy",RRF_RT_REG_DWORD,nullptr,&copy,&size);mode.preferred=copy==1?Mode::Copy:Mode::Reference;
        initialDirectory=registryText(L"LastDirectory");try{auto current=inspect(folders[0]);if(!current.resource.empty()){initialIcon=current;mode.preferred=current.managed?Mode::Copy:Mode::Reference;}}catch(...){}
        std::error_code ec;if(initialDirectory.empty()||!fs::is_directory(initialDirectory,ec))initialDirectory=folders[0];
        if(initialIcon&&(iconFile(initialIcon->source)||imageFile(initialIcon->source))&&fs::is_directory(initialIcon->source.parent_path(),ec))initialDirectory=initialIcon->source.parent_path();
        browsingDirectories.fill(initialDirectory);
        wchar_t system[32768]={};THROW_LAST_ERROR_IF(!GetSystemDirectoryW(system,32768));const wchar_t* names[]={L"imageres.dll",L"shell32.dll",L"DDORes.dll",L"moricons.dll"};
        for(int i=0;i<4;++i){auto path=fs::path(system)/names[i];if(fs::is_regular_file(path,ec))sources.push_back({path,tr(IDS_COLLECTION_WINDOWS+i),true});}
        for(const auto& path:storedSources())if(std::none_of(sources.begin(),sources.end(),[&](const Source& source){return samePath(source.path,path);}))sources.push_back({path,path.filename().native(),false});
    }
    ~Picker(){
        closing=true;for(auto& selector:selectors)if(selector)selector->requestStop();
        {std::lock_guard lock(mutex);stopping=true;for(auto& job:pending)job.reset();}condition.notify_one();if(worker.joinable())worker.join();
        for(auto& selector:selectors)selector.reset();if(window)DestroyWindow(window);
    }
    int px(int value)const{return MulDiv(value,static_cast<int>(dpi),96);}int dx(int dlu)const{return MulDiv(dlu,baseX,4);}int dy(int dlu)const{return MulDiv(dlu,baseY,8);}
    HWND control(UINT id)const{return controls[id];}
    bool stopped()const{return std::all_of(selectors.begin(),selectors.end(),[](const auto& selector){return !selector||selector->stopped();});}
    HWND add(UINT id,PCWSTR type,PCWSTR text,DWORD style,DWORD ex=0,HWND owner=nullptr){
        HWND item=CreateWindowExW(ex,type,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,owner?owner:window,reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);THROW_LAST_ERROR_IF(!item);if(id<controls.size())controls[id]=item;SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(font.get()),TRUE);return item;
    }
    void fontForDpi(){
        NONCLIENTMETRICSW metrics={sizeof(metrics)};THROW_IF_WIN32_BOOL_FALSE(SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0,dpi));font.reset(CreateFontIndirectW(&metrics.lfMessageFont));THROW_LAST_ERROR_IF(!font);
        HDC dc=GetDC(window);auto old=SelectObject(dc,font.get());TEXTMETRICW text={};SIZE alphabet={};GetTextMetricsW(dc,&text);GetTextExtentPoint32W(dc,L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",52,&alphabet);SelectObject(dc,old);ReleaseDC(window,dc);baseX=(alphabet.cx/26+1)/2;baseY=text.tmHeight;
        auto glyph=metrics.lfMessageFont;glyph.lfHeight=-px(16);lstrcpyW(glyph.lfFaceName,L"Segoe MDL2 Assets");trashFont.reset(CreateFontIndirectW(&glyph));THROW_LAST_ERROR_IF(!trashFont);
        for(auto item:controls)if(item)SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(font.get()),TRUE);
    }
    void startWorker(){
        worker=std::thread([this]{HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);auto uninit=wil::scope_exit([&]{if(SUCCEEDED(com))CoUninitialize();});
            for(;;){Candidate job;int tab=-1;{std::unique_lock lock(mutex);condition.wait(lock,[&]{return stopping||std::any_of(pending.begin(),pending.end(),[](const auto& value){return value.has_value();});});if(stopping)return;for(int i=0;i<3;++i)if(pending[i]){tab=i;job=std::move(*pending[i]);pending[i].reset();break;}}
                Completion completion={tab,job.generation,std::nullopt};try{THROW_IF_FAILED(com);completion.data=job.folder?folderIconData(job.path):loadIconData(job.path,job.index);}catch(...){completion.error=wil::ResultFromCaughtException();}
                {std::lock_guard lock(mutex);if(stopping)return;completed.push_back(std::move(completion));}PostMessageW(window,Loaded,0,0);
            }
        });
    }
    void select(int tab,const fs::path& path,int index=0,bool folder=false){
        auto& current=candidates[tab];if(current.path==path&&current.index==index&&current.folder==folder&&(current.data||current.status==tr(IDS_LOADING)))return;
        current={path,index,folder,++serial,std::nullopt,path.empty()?L"":tr(IDS_LOADING)};if(active==tab)applyWhenLoaded=false;
        {std::lock_guard lock(mutex);if(path.empty())pending[tab].reset();else pending[tab]=current;}condition.notify_one();if(active==tab)refresh();
    }
    void receive(){
        std::vector<Completion> results;{std::lock_guard lock(mutex);results.swap(completed);}for(auto& completion:results){auto& current=candidates[completion.tab];if(current.generation!=completion.generation)continue;current.data=std::move(completion.data);current.status=SUCCEEDED(completion.error)?L"":tr(IDS_INVALID_SOURCE);if(completion.tab==active){refresh();if(applyWhenLoaded&&current.data)accept();else if(!current.data){applyWhenLoaded=false;showActiveSelector();}}}
    }
    bool imageSelected()const{const auto& current=candidates[active];return current.data?current.data->image:!current.folder&&imageFile(current.path);}
    bool copyAllowed()const{const auto& current=candidates[active];if(current.data)return current.data->copyAllowed;if(current.path.empty())return true;return !current.folder&&(imageFile(current.path)||iconFile(current.path));}
    void refresh(){
        const auto& current=candidates[active];bool valid=current.data.has_value(),image=imageSelected(),copy=copyAllowed();auto effective=mode.effective(image,copy);
        SendMessageW(control(Reference),BM_SETCHECK,effective==Mode::Reference?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(control(Copy),BM_SETCHECK,effective==Mode::Copy?BST_CHECKED:BST_UNCHECKED,0);EnableWindow(control(Reference),!image);EnableWindow(control(Copy),copy);
        auto note=image?tr(IDS_IMAGE_COPY):!copy?tr(IDS_RESOURCE_REFERENCE):L"";SetWindowTextW(control(Note),note.c_str());SetWindowTextW(control(Status),current.status.c_str());EnableWindow(GetDlgItem(window,IDOK),valid);
        ShowWindow(control(Note),current.status.empty()?SW_SHOW:SW_HIDE);ShowWindow(control(Status),current.status.empty()?SW_HIDE:SW_SHOW);
        RECT previewBounds={};GetClientRect(control(Preview),&previewBounds);int previewSize=std::min(previewBounds.right,previewBounds.bottom);
        wil::unique_hicon icon;if(valid&&previewSize>0)icon=previewIcon(*current.data,previewSize);SendMessageW(control(Preview),STM_SETICON,reinterpret_cast<WPARAM>(icon.get()),0);preview=std::move(icon);
        auto label=valid?tr(image?IDS_RESOLUTION:IDS_MAX_RESOLUTION)+L" "+std::to_wstring(current.data->width)+L" × "+std::to_wstring(current.data->height):L"";SetWindowTextW(control(Resolution),label.c_str());
    }
    void saveGeometry()noexcept{try{Geometry value;THROW_IF_WIN32_BOOL_FALSE(GetWindowPlacement(window,&value.placement));value.placement.flags=0;value.placement.showCmd=IsZoomed(window)?SW_SHOWMAXIMIZED:SW_SHOWNORMAL;auto key=settingsKey();RegSetValueExW(key.get(),L"WindowPlacement",0,REG_BINARY,reinterpret_cast<const BYTE*>(&value),sizeof(value));}catch(...){}}
    bool restoreGeometry(){
        Geometry value;DWORD size=sizeof(value);if(RegGetValueW(HKEY_CURRENT_USER,Settings,L"WindowPlacement",RRF_RT_REG_BINARY,nullptr,&value,&size)!=ERROR_SUCCESS||size!=sizeof(value)||value.version!=1||value.placement.length!=sizeof(WINDOWPLACEMENT))return false;
        auto& rect=value.placement.rcNormalPosition;if(rect.right-rect.left<px(500)||rect.bottom-rect.top<px(350)||rect.right-rect.left>32768||rect.bottom-rect.top>32768)return false;
        value.placement.flags=0;if(value.placement.showCmd!=SW_SHOWMAXIMIZED)value.placement.showCmd=SW_SHOWNORMAL;return !!SetWindowPlacement(window,&value.placement);
    }
    void close(){if(closing)return;saveGeometry();closing=true;for(auto& selector:selectors)if(selector)selector->requestStop();}
    void accept(){if(candidates[active].data){result=*candidates[active].data;accepted=true;close();}else if(!candidates[active].path.empty()&&candidates[active].status==tr(IDS_LOADING))applyWhenLoaded=true;}
    void initialize();void initializeSelectors();void showActiveSelector();void layout();void switchTab();void selectorEvents(int);void sizeFromNative();
    void fillSources();void saveSources();void openSource();void removeSource(int);void loadCollection(int);void fillThumbnails();void selectResource();
    INT_PTR notify(NMHDR*);INT_PTR message(UINT,WPARAM,LPARAM);
    static INT_PTR CALLBACK pageProc(HWND hwnd,UINT msg,WPARAM w,LPARAM l){
        if(msg==WM_INITDIALOG){SetWindowLongPtrW(hwnd,DWLP_USER,l);EnableThemeDialogTexture(hwnd,ETDT_ENABLETAB);return TRUE;}auto self=reinterpret_cast<Picker*>(GetWindowLongPtrW(hwnd,DWLP_USER));
        try{if(self&&msg==WM_NOTIFY){auto notificationResult=self->notify(reinterpret_cast<NMHDR*>(l));SetWindowLongPtrW(hwnd,DWLP_MSGRESULT,notificationResult);return TRUE;}if(self&&msg==WM_COMMAND){SendMessageW(self->window,msg,w,l);return TRUE;}}catch(...){showError(self?self->window:hwnd,wil::ResultFromCaughtException());return TRUE;}return FALSE;
    }
    static INT_PTR CALLBACK procedure(HWND hwnd,UINT msg,WPARAM w,LPARAM l){
        auto self=reinterpret_cast<Picker*>(GetWindowLongPtrW(hwnd,DWLP_USER));if(msg==WM_INITDIALOG){self=reinterpret_cast<Picker*>(l);self->window=hwnd;SetWindowLongPtrW(hwnd,DWLP_USER,l);}if(!self)return FALSE;
        try{return self->message(msg,w,l);}catch(...){if(msg==WM_INITDIALOG){self->initialization=wil::ResultFromCaughtException();self->close();}else showError(hwnd,wil::ResultFromCaughtException());return TRUE;}
    }
};
void Picker::initialize(){
    dpi=GetDpiForWindow(window);fontForDpi();SetWindowTextW(window,title.c_str());auto icon=LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(1));SendMessageW(window,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(icon));SendMessageW(window,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(icon));const DWORD rounded=2;DwmSetWindowAttribute(window,33,&rounded,sizeof(rounded));
    add(Tabs,WC_TABCONTROLW,L"",WS_TABSTOP|WS_CLIPSIBLINGS);for(int i=0;i<3;++i){auto label=tr(IDS_TAB_IMAGE+i);TCITEMW item={};item.mask=TCIF_TEXT;item.pszText=label.data();TabCtrl_InsertItem(control(Tabs),i,&item);}
    struct Template{DLGTEMPLATE dialog;WORD menu=0,klass=0,title=0;} page={};page.dialog.style=WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS|DS_CONTROL;page.dialog.dwExtendedStyle=WS_EX_CONTROLPARENT;catalog=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),&page.dialog,window,pageProc,reinterpret_cast<LPARAM>(this));THROW_LAST_ERROR_IF(!catalog);
    add(Sources,WC_LISTVIEWW,tr(IDS_SOURCES).c_str(),WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|LVS_NOCOLUMNHEADER,WS_EX_CLIENTEDGE,catalog);LVCOLUMNW column={};column.mask=LVCF_WIDTH;column.cx=px(214);ListView_InsertColumn(control(Sources),0,&column);
    add(AddSource,WC_BUTTONW,tr(IDS_ADD_SOURCE).c_str(),WS_TABSTOP|BS_PUSHBUTTON,0,catalog);add(Grid,WC_LISTVIEWW,tr(IDS_TAB_CATALOG).c_str(),WS_TABSTOP|LVS_ICON|LVS_SINGLESEL|LVS_SHOWSELALWAYS|LVS_AUTOARRANGE|LVS_SHAREIMAGELISTS,WS_EX_CLIENTEDGE,catalog);
    ListView_SetExtendedListViewStyle(control(Sources),LVS_EX_DOUBLEBUFFER|LVS_EX_FULLROWSELECT|LVS_EX_INFOTIP);ListView_SetExtendedListViewStyle(control(Grid),LVS_EX_DOUBLEBUFFER|LVS_EX_INFOTIP);for(UINT id:{Sources,Grid})SetWindowTheme(control(id),L"Explorer",nullptr);ListView_SetIconSpacing(control(Grid),px(76),px(82));
    add(Reference,WC_BUTTONW,tr(IDS_REFERENCE).c_str(),WS_GROUP|WS_TABSTOP|BS_AUTORADIOBUTTON);add(Copy,WC_BUTTONW,tr(IDS_COPY).c_str(),WS_TABSTOP|BS_AUTORADIOBUTTON);add(Note,WC_STATICW,L"",SS_LEFT);add(Preview,WC_STATICW,L"",SS_ICON|SS_REALSIZECONTROL|SS_CENTERIMAGE);add(Resolution,WC_STATICW,L"",SS_RIGHT);add(Status,WC_STATICW,L"",SS_LEFT|SS_ENDELLIPSIS);
    add(IDCANCEL,WC_BUTTONW,tr(IDS_CANCEL).c_str(),WS_TABSTOP|BS_PUSHBUTTON);add(IDOK,WC_BUTTONW,tr(IDS_APPLY).c_str(),WS_TABSTOP|BS_DEFPUSHBUTTON);startWorker();layout();fillSources();
    if(initialIcon&&!iconFile(initialIcon->source)&&!imageFile(initialIcon->source)){
        auto current=*initialIcon;auto found=std::find_if(sources.begin(),sources.end(),[&](const Source& source){return samePath(source.path,current.source);});if(found==sources.end()){sources.push_back({current.source,current.source.filename().native(),false});fillSources();}
        TabCtrl_SetCurSel(control(Tabs),2);active=2;int row=static_cast<int>(std::find_if(sources.begin(),sources.end(),[&](const Source& source){return samePath(source.path,current.source);})-sources.begin());ListView_SetItemState(control(Sources),row,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);auto at=std::find(indices.begin(),indices.end(),current.index);if(at!=indices.end()){int selected=static_cast<int>(at-indices.begin());ListView_SetItemState(control(Grid),selected,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);ListView_EnsureVisible(control(Grid),selected,FALSE);}
    }
    switchTab();
}
void Picker::initializeSelectors(){
    selectorsInitialized=true;showActiveSelector();
}
void Picker::showActiveSelector(){
    if(closing||!selectorsInitialized)return;
    // Reparented Shell dialogs share the owner's input queue. Never run two
    // modal Shell loops against that queue, including during a tab transition.
    for(int i=0;i<2;++i)if(retiring[i]&&selectors[i]&&!selectors[i]->stopped()){EnableWindow(control(AddSource),FALSE);return;}
    for(int i=0;i<2;++i)if(retiring[i]){selectors[i].reset();retiring[i]=false;}
    EnableWindow(control(AddSource),TRUE);
    if(active>=2)return;
    if(selectors[active]&&validatedSelector[active]&&selectors[active]->stopped()&&!applyWhenLoaded)selectors[active].reset();
    if(!selectors[active]){
        SelectorOptions options;options.owner=window;options.notification=SelectorChanged;options.tag=active;options.kind=active?SelectorKind::Directory:SelectorKind::ImageOrIcon;options.initialFolder=browsingDirectories[active];options.bounds=pageBounds;
        options.initialSelection=candidates[active].path;
        if(options.initialSelection.empty()&&active==0&&initialIcon&&(iconFile(initialIcon->source)||imageFile(initialIcon->source)))options.initialSelection=initialIcon->source;
        validatedSelector[active]=false;selectors[active]=createNativeFileSelector(options);
    }
    selectors[active]->show(true);
}
void Picker::layout(){
    if(!control(Tabs))return;RECT client={};GetClientRect(window,&client);int w=client.right,h=client.bottom;auto place=[](HWND item,int x,int y,int width,int height){SetWindowPos(item,nullptr,x,y,std::max(0,width),std::max(0,height),SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS);};
    int mx=dx(7),my=dy(7),gap=dy(4),radio=dy(10),label=dy(8),button=dy(14);
    int groupHeight=std::max(2*radio+gap,px(32));
    int buttonY=h-my-button,optionsBottom=buttonY-gap,optionsTop=optionsBottom-groupHeight;
    int tabBottom=optionsTop-gap;place(control(Tabs),mx,my,w-2*mx,tabBottom-my);
    GetClientRect(control(Tabs),&pageBounds);TabCtrl_AdjustRect(control(Tabs),FALSE,&pageBounds);MapWindowPoints(control(Tabs),window,reinterpret_cast<POINT*>(&pageBounds),2);
    for(auto& selector:selectors)if(selector)selector->setBounds(pageBounds,dpi);int pw=pageBounds.right-pageBounds.left,ph=pageBounds.bottom-pageBounds.top;
    place(catalog,pageBounds.left,pageBounds.top,pw,ph);int x=dx(7),y=dy(7),sw=dx(130),height=ph-2*y;place(control(Sources),x,y,sw,height-button-gap);place(control(AddSource),x,ph-y-button,sw,button);place(control(Grid),x+sw+dx(7),y,pw-2*x-sw-dx(7),height);RECT list={};GetClientRect(control(Sources),&list);ListView_SetColumnWidth(control(Sources),0,list.right);
    int radioX=pageBounds.left+dx(7),radioWidth=w-radioX-mx-dx(160);place(control(Reference),radioX,optionsTop,radioWidth,radio);place(control(Copy),radioX,optionsTop+radio+gap,radioWidth,radio);
    int buttonsX=w-mx-2*dx(50)-dx(4),messageX=radioX+dx(10);for(UINT id:{Note,Status})place(control(id),messageX,buttonY+(button-label)/2,buttonsX-gap-messageX,label);
    place(control(Preview),w-mx-groupHeight,optionsTop,groupHeight,groupHeight);place(control(Resolution),w-mx-dx(160),optionsTop+(groupHeight-label)/2,dx(160)-groupHeight-dx(4),label);
    place(GetDlgItem(window,IDCANCEL),w-mx-dx(50)-dx(4)-dx(50),buttonY,dx(50),button);place(GetDlgItem(window,IDOK),w-mx-dx(50),buttonY,dx(50),button);
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
}
void Picker::sizeFromNative(){
    if(sized||geometryRestored||!selectors[0])return;auto preferred=selectors[0]->preferredSize(dpi);if(!preferred.cx||!preferred.cy)return;sized=true;
    RECT client={},page={};GetClientRect(window,&client);GetClientRect(selectors[0]->window(),&page);RECT desired={0,0,client.right+preferred.cx-page.right,client.bottom+preferred.cy-page.bottom};THROW_IF_WIN32_BOOL_FALSE(AdjustWindowRectExForDpi(&desired,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_EXSTYLE)),dpi));
    MONITORINFO monitor={sizeof(monitor)};THROW_IF_WIN32_BOOL_FALSE(GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor));auto& area=monitor.rcWork;int width=std::min(desired.right-desired.left,area.right-area.left),height=std::min(desired.bottom-desired.top,area.bottom-area.top);SetWindowPos(window,nullptr,area.left+(area.right-area.left-width)/2,area.top+(area.bottom-area.top-height)/2,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
}
void Picker::switchTab(){
    active=TabCtrl_GetCurSel(control(Tabs));applyWhenLoaded=false;for(int i=0;i<2;++i)if(selectors[i]&&i!=active&&!retiring[i]){retiring[i]=true;selectors[i]->requestStop();}
    if(active==2){SetWindowPos(catalog,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);if(catalogSource<0&&!sources.empty())ListView_SetItemState(control(Sources),0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);}else ShowWindow(catalog,SW_HIDE);showActiveSelector();refresh();
}
void Picker::selectorEvents(int tab){
    if(tab<0||tab>1||!selectors[tab])return;auto events=selectors[tab]->takeEvents();
    for(const auto& event:events)if(event.type==SelectorEvent::Type::Navigation&&!event.folder.empty())browsingDirectories[tab]=event.folder;
    if(closing)return;if(retiring[tab]){showActiveSelector();return;}
    for(const auto& event:events){switch(event.type){
    case SelectorEvent::Type::Ready:sizeFromNative();layout();break;
    case SelectorEvent::Type::Selection:if(event.selection)select(tab,event.selection->path,0,event.selection->directory);else select(tab,{});break;
    case SelectorEvent::Type::Activate:if(active==tab&&event.selection){validatedSelector[tab]=true;select(tab,event.selection->path,0,event.selection->directory);accept();}break;
    case SelectorEvent::Type::Cancel:close();break;
    case SelectorEvent::Type::Failed:select(tab,{});candidates[tab].status=tr(IDS_EMBED_UNAVAILABLE);if(active==tab)refresh();break;
    case SelectorEvent::Type::Stopped:showActiveSelector();break;
    default:break;
    }}
}
void Picker::fillSources(){
    updating=true;ListView_DeleteAllItems(control(Sources));for(size_t i=0;i<sources.size();++i){LVITEMW item={};item.mask=LVIF_TEXT;item.iItem=static_cast<int>(i);item.pszText=sources[i].name.data();ListView_InsertItem(control(Sources),&item);}RECT client={};GetClientRect(control(Sources),&client);ListView_SetColumnWidth(control(Sources),0,client.right);updating=false;
}
void Picker::saveSources(){
    std::vector<wchar_t> text;for(const auto& source:sources)if(!source.builtin){auto path=source.path.native();text.insert(text.end(),path.begin(),path.end());text.push_back(0);}text.push_back(0);if(text.size()==1)text.push_back(0);auto key=settingsKey();THROW_IF_WIN32_ERROR(RegSetValueExW(key.get(),L"Sources",0,REG_MULTI_SZ,reinterpret_cast<const BYTE*>(text.data()),static_cast<DWORD>(text.size()*sizeof(wchar_t))));
}
void Picker::openSource(){
    applyWhenLoaded=false;
    wil::com_ptr<IFileOpenDialog> dialog;THROW_IF_FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)));DWORD options=0;THROW_IF_FAILED(dialog->GetOptions(&options));THROW_IF_FAILED(dialog->SetOptions((options|FOS_FORCEFILESYSTEM|FOS_FILEMUSTEXIST|FOS_PATHMUSTEXIST|FOS_DONTADDTORECENT)&~FOS_ALLOWMULTISELECT));auto label=tr(IDS_ADD_SOURCE);THROW_IF_FAILED(dialog->SetTitle(label.c_str()));label=tr(IDS_APPLICATION_FILES);COMDLG_FILTERSPEC filter={label.c_str(),L"*.exe;*.dll"};THROW_IF_FAILED(dialog->SetFileTypes(1,&filter));
    HRESULT hr=dialog->Show(window);if(hr==HRESULT_FROM_WIN32(ERROR_CANCELLED))return;THROW_IF_FAILED(hr);wil::com_ptr<IShellItem> item;THROW_IF_FAILED(dialog->GetResult(&item));wil::unique_cotaskmem_string path;THROW_IF_FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&path));fs::path file=path.get();auto extension=file.extension().native();THROW_WIN32_IF(ERROR_INVALID_DATA,CompareStringOrdinal(extension.c_str(),-1,L".exe",-1,TRUE)!=CSTR_EQUAL&&CompareStringOrdinal(extension.c_str(),-1,L".dll",-1,TRUE)!=CSTR_EQUAL);THROW_WIN32_IF(ERROR_RESOURCE_TYPE_NOT_FOUND,resourceIcons(file).empty());
    auto found=std::find_if(sources.begin(),sources.end(),[&](const Source& source){return samePath(source.path,file);});int row=static_cast<int>(found-sources.begin());if(found==sources.end()){sources.push_back({file,file.filename().native(),false});saveSources();fillSources();}catalogSource=-1;ListView_SetItemState(control(Sources),-1,0,LVIS_SELECTED);ListView_SetItemState(control(Sources),row,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);ListView_EnsureVisible(control(Sources),row,FALSE);
}
void Picker::removeSource(int row){
    if(row<0||static_cast<size_t>(row)>=sources.size()||sources[row].builtin)return;KillTimer(window,1);catalogSource=-1;select(2,{});sources.erase(sources.begin()+row);saveSources();fillSources();if(!sources.empty())ListView_SetItemState(control(Sources),std::min(row,static_cast<int>(sources.size())-1),LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
}
void Picker::loadCollection(int row){
    if(updating||row==catalogSource||row<0||static_cast<size_t>(row)>=sources.size())return;KillTimer(window,1);catalogSource=row;select(2,{});indices.clear();updating=true;ListView_DeleteAllItems(control(Grid));ListView_SetImageList(control(Grid),nullptr,LVSIL_NORMAL);thumbnails.reset(ImageList_Create(px(48),px(48),ILC_COLOR32|ILC_MASK,16,32));THROW_LAST_ERROR_IF(!thumbnails);ListView_SetImageList(control(Grid),thumbnails.get(),LVSIL_NORMAL);updating=false;
    try{indices=resourceIcons(sources[row].path);}catch(...){candidates[2].status=tr(IDS_INVALID_SOURCE);if(active==2)refresh();return;}if(indices.empty()){candidates[2].status=tr(IDS_NO_ICONS);if(active==2)refresh();return;}
    SendMessageW(control(Grid),WM_SETREDRAW,FALSE,0);for(size_t i=0;i<indices.size();++i){auto label=std::to_wstring(i+1);LVITEMW item={};item.mask=LVIF_TEXT|LVIF_IMAGE;item.iItem=static_cast<int>(i);item.iImage=I_IMAGENONE;item.pszText=label.data();ListView_InsertItem(control(Grid),&item);}SendMessageW(control(Grid),WM_SETREDRAW,TRUE,0);InvalidateRect(control(Grid),nullptr,TRUE);nextThumbnail=0;SetTimer(window,1,15,nullptr);
}
void Picker::fillThumbnails(){
    if(catalogSource<0){KillTimer(window,1);return;}for(int n=0;n<8&&nextThumbnail<indices.size();++n,++nextThumbnail)try{auto data=loadIconData(sources[catalogSource].path,indices[nextThumbnail]);auto icon=previewIcon(data,px(48));int image=ImageList_AddIcon(thumbnails.get(),icon.get());LVITEMW item={};item.mask=LVIF_IMAGE;item.iItem=static_cast<int>(nextThumbnail);item.iImage=image;ListView_SetItem(control(Grid),&item);}catch(...){}if(nextThumbnail==indices.size())KillTimer(window,1);
}
void Picker::selectResource(){
    int selected=ListView_GetNextItem(control(Grid),-1,LVNI_SELECTED);if(selected<0||catalogSource<0||static_cast<size_t>(selected)>=indices.size()){select(2,{});return;}select(2,sources[catalogSource].path,indices[selected]);
}
INT_PTR Picker::notify(NMHDR* header){
    if(header->idFrom==Tabs&&header->code==TCN_SELCHANGE){switchTab();return TRUE;}
    if(header->idFrom==Sources){
        if(header->code==NM_CUSTOMDRAW){auto draw=reinterpret_cast<NMLVCUSTOMDRAW*>(header);if(draw->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;if(draw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT)return CDRF_NOTIFYPOSTPAINT;if(draw->nmcd.dwDrawStage==CDDS_ITEMPOSTPAINT&&draw->nmcd.dwItemSpec<sources.size()&&!sources[draw->nmcd.dwItemSpec].builtin){RECT row={},client={};ListView_GetItemRect(control(Sources),static_cast<int>(draw->nmcd.dwItemSpec),&row,LVIR_BOUNDS);GetClientRect(control(Sources),&client);row.right=client.right-dx(4);row.left=row.right-px(20);int saved=SaveDC(draw->nmcd.hdc);SelectObject(draw->nmcd.hdc,trashFont.get());SetBkMode(draw->nmcd.hdc,TRANSPARENT);SetTextColor(draw->nmcd.hdc,GetSysColor(COLOR_GRAYTEXT));DrawTextW(draw->nmcd.hdc,L"\xE74D",1,&row,DT_CENTER|DT_VCENTER|DT_SINGLELINE);RestoreDC(draw->nmcd.hdc,saved);}return CDRF_DODEFAULT;}
        if(header->code==LVN_ITEMCHANGED&&!updating){auto change=reinterpret_cast<NMLISTVIEW*>(header);if((change->uNewState&LVIS_SELECTED)&&!(change->uOldState&LVIS_SELECTED))loadCollection(change->iItem);}
        else if(header->code==NM_CLICK){auto click=reinterpret_cast<NMITEMACTIVATE*>(header);RECT client={};GetClientRect(control(Sources),&client);if(click->ptAction.x>=client.right-dx(4)-px(20))removeSource(click->iItem);}
        else if(header->code==LVN_KEYDOWN&&reinterpret_cast<NMLVKEYDOWN*>(header)->wVKey==VK_DELETE)removeSource(ListView_GetNextItem(control(Sources),-1,LVNI_SELECTED));
        else if(header->code==LVN_GETINFOTIPW){auto tip=reinterpret_cast<NMLVGETINFOTIPW*>(header);if(tip->iItem>=0&&static_cast<size_t>(tip->iItem)<sources.size()){auto text=sources[tip->iItem].path.native();if(!sources[tip->iItem].builtin)text+=L"\n"+tr(IDS_REMOVE_HINT);lstrcpynW(tip->pszText,text.c_str(),tip->cchTextMax);}}
    }
    if(header->idFrom==Grid&&!updating){if(header->code==LVN_ITEMCHANGED){auto change=reinterpret_cast<NMLISTVIEW*>(header);if((change->uChanged&LVIF_STATE)&&((change->uNewState^change->uOldState)&LVIS_SELECTED))selectResource();}else if(header->code==NM_DBLCLK||header->code==NM_RETURN){selectResource();accept();}}
    return FALSE;
}
INT_PTR Picker::message(UINT msg,WPARAM w,LPARAM l){
    if(closing&&msg!=SelectorChanged&&msg!=WM_CLOSE&&msg!=WM_DESTROY)return FALSE;
    switch(msg){
    case WM_INITDIALOG:initialize();return TRUE;
    case CreateSelectors:initializeSelectors();return TRUE;
    case WM_SIZE:layout();return TRUE;
    case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={px(640),px(520)};return TRUE;
    case WM_DPICHANGED:{dpi=HIWORD(w);fontForDpi();auto rect=reinterpret_cast<RECT*>(l);SetWindowPos(window,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);layout();refresh();ListView_SetIconSpacing(control(Grid),px(76),px(82));if(catalogSource>=0){int row=catalogSource,selected=ListView_GetNextItem(control(Grid),-1,LVNI_SELECTED);catalogSource=-1;loadCollection(row);if(selected>=0&&static_cast<size_t>(selected)<indices.size())ListView_SetItemState(control(Grid),selected,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);}return TRUE;}
    case SelectorChanged:selectorEvents(static_cast<int>(w));return TRUE;
    case Loaded:receive();return TRUE;
    case WM_TIMER:if(w==1)fillThumbnails();return TRUE;
    case WM_NOTIFY:{auto notificationResult=notify(reinterpret_cast<NMHDR*>(l));SetWindowLongPtrW(window,DWLP_MSGRESULT,notificationResult);return TRUE;}
    case WM_CLOSE:close();return TRUE;
    case WM_COMMAND:switch(LOWORD(w)){
        case IDCANCEL:close();return TRUE;case IDOK:if(active<2&&selectors[active])selectors[active]->activateSelection();else accept();return TRUE;
        case Reference:case Copy:mode.choose(LOWORD(w)==Copy?Mode::Copy:Mode::Reference,imageSelected(),copyAllowed());refresh();return TRUE;
        case AddSource:openSource();return TRUE;
    }
    }return FALSE;
}
}
void rememberIconChoice(const fs::path& source,Mode preferred){try{auto key=settingsKey();DWORD copy=preferred==Mode::Copy?1:0;THROW_IF_WIN32_ERROR(RegSetValueExW(key.get(),L"Copy",0,REG_DWORD,reinterpret_cast<const BYTE*>(&copy),sizeof(copy)));if(iconFile(source)||imageFile(source))saveText(key.get(),L"LastDirectory",source.parent_path().native());}catch(...){}}
bool pickIcon(const std::vector<fs::path>& folders,IconData& result,Mode& effective,Mode& preferred){
    INITCOMMONCONTROLSEX common={sizeof(common),ICC_STANDARD_CLASSES|ICC_TAB_CLASSES|ICC_LISTVIEW_CLASSES};THROW_IF_WIN32_BOOL_FALSE(InitCommonControlsEx(&common));Picker picker(folders);
    struct Template{DLGTEMPLATE dialog;WORD menu=0,klass=0,title=0;} layout={};layout.dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|DS_MODALFRAME|WS_CLIPCHILDREN;layout.dialog.dwExtendedStyle=WS_EX_CONTROLPARENT|WS_EX_APPWINDOW;layout.dialog.cx=540;layout.dialog.cy=380;
    THROW_LAST_ERROR_IF(!CreateDialogIndirectParamW(GetModuleHandleW(nullptr),&layout.dialog,nullptr,Picker::procedure,reinterpret_cast<LPARAM>(&picker)));THROW_IF_FAILED(picker.initialization);picker.geometryRestored=picker.restoreGeometry();ShowWindow(picker.window,IsZoomed(picker.window)?SW_SHOWMAXIMIZED:SW_SHOWNORMAL);ShowWindow(picker.window,SW_SHOW);SetForegroundWindow(picker.window);
    PostMessageW(picker.window,CreateSelectors,0,0);
    MSG message={};while((!picker.closing||!picker.stopped())&&GetMessageW(&message,nullptr,0,0)>0)if(!IsDialogMessageW(picker.window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
    if(!picker.accepted)return false;result=std::move(picker.result);preferred=picker.mode.preferred;effective=picker.mode.effective(result.image,result.copyAllowed);return true;
}
}
