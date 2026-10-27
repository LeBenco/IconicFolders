// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Benco
#include "core.h"
#include <shellapi.h>
#include <shlobj.h>
#include <wil/resource.h>
#include <wil/com.h>
#include <wil/result.h>
#include <algorithm>
#include <cwctype>
#include <cstring>
#include <limits>

namespace iconic {
namespace {
std::wstring trim(std::wstring s) {
    auto first = s.find_first_not_of(L" \t\r");
    if (first == s.npos) return {};
    return s.substr(first, s.find_last_not_of(L" \t\r") - first + 1);
}
bool equal(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}
struct Line { std::wstring body, end; bool shell = false, section = false; std::wstring key; size_t value = 0; };
std::vector<Line> lines(const std::wstring& text) {
    std::vector<Line> out; bool shell = false;
    for (size_t p = 0; p < text.size();) {
        size_t e = text.find_first_of(L"\r\n", p); if (e == text.npos) e = text.size();
        Line line; line.body = text.substr(p, e-p);
        size_t next = e;
        if (next < text.size()) { ++next; if (text[e] == L'\r' && next < text.size() && text[next] == L'\n') ++next; }
        line.end = text.substr(e, next-e); p = next;
        auto t = trim(line.body);
        if (!t.empty() && t.front() == L'[' && t.back() == L']') { line.section = true; shell = equal(trim(t.substr(1,t.size()-2)), L".ShellClassInfo"); }
        line.shell = shell;
        size_t eq = line.body.find(L'=');
        if (shell && !line.section && !t.empty() && t[0] != L';' && t[0] != L'#' && eq != t.npos) {
            line.key = trim(line.body.substr(0,eq)); line.value = eq+1;
            while (line.value < line.body.size() && (line.body[line.value] == L' ' || line.body[line.value] == L'\t')) ++line.value;
        }
        out.push_back(std::move(line));
    }
    return out;
}
bool iconKey(const std::wstring& s) { return equal(s,L"IconResource") || equal(s,L"IconFile") || equal(s,L"IconIndex"); }
DWORD attrs(const fs::path& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) { DWORD e = GetLastError(); if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND) THROW_WIN32(e); }
    return a;
}
void regular(const fs::path& path, bool directory = false) {
    DWORD a = attrs(path); THROW_WIN32_IF(ERROR_FILE_NOT_FOUND, a == INVALID_FILE_ATTRIBUTES);
    THROW_WIN32_IF(ERROR_REPARSE_TAG_INVALID, (a & FILE_ATTRIBUTE_REPARSE_POINT)!=0);
    THROW_WIN32_IF(ERROR_DIRECTORY, !!(a & FILE_ATTRIBUTE_DIRECTORY) != directory);
}
fs::path fullPath(const fs::path& p) { return fs::absolute(p).lexically_normal(); }
bool same(const fs::path& a, const fs::path& b) { return equal(fullPath(a).native(), fullPath(b).native()); }
fs::path tempPath(const fs::path& folder) {
    GUID id; THROW_IF_FAILED(CoCreateGuid(&id)); wchar_t name[64]; StringFromGUID2(id,name,64);
    return folder / (std::wstring(L".iconicfolders-") + name + L".tmp");
}
void replace(const fs::path& target, const std::vector<unsigned char>& bytes, DWORD attributes) {
    auto tmp = tempPath(target.parent_path());
    auto cleanup = wil::scope_exit([&] { DeleteFileW(tmp.c_str()); });
    writeFile(tmp, bytes);
    DWORD old = attrs(target);
    if (old != INVALID_FILE_ATTRIBUTES) {
        regular(target);
        auto backup=tempPath(target.parent_path());
        auto backupCleanup=wil::scope_exit([&]{DeleteFileW(backup.c_str());});
        DWORD writable=old & ~(FILE_ATTRIBUTE_READONLY|FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM);
        THROW_IF_WIN32_BOOL_FALSE(SetFileAttributesW(target.c_str(),writable?writable:FILE_ATTRIBUTE_NORMAL));
        for(unsigned attempt=0;;++attempt) {
            if(ReplaceFileW(target.c_str(),tmp.c_str(),backup.c_str(),0,nullptr,nullptr))break;
            DWORD error=GetLastError();
            bool transient=error==ERROR_SHARING_VIOLATION||error==ERROR_LOCK_VIOLATION||error==ERROR_UNABLE_TO_REMOVE_REPLACED;
            if(!transient||attempt==6) {
                // With a backup name the documented 1176/1177 failure states can
                // be recovered without losing the original file.
                if(attrs(target)==INVALID_FILE_ATTRIBUTES && attrs(backup)!=INVALID_FILE_ATTRIBUTES) {
                    if(!MoveFileExW(backup.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH))backupCleanup.release();
                }
                SetFileAttributesW(target.c_str(),old); THROW_WIN32_MSG(error,"ReplaceFile: %ls",target.c_str());
            }
            Sleep(50*(attempt+1));
        }
    } else { THROW_IF_WIN32_BOOL_FALSE(MoveFileExW(tmp.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH)); }
    THROW_IF_WIN32_BOOL_FALSE(SetFileAttributesW(target.c_str(),attributes & ~FILE_ATTRIBUTE_DIRECTORY));
}
struct Snapshot { bool exists; DWORD attributes; std::vector<unsigned char> bytes; };
Snapshot snapshot(const fs::path& p) {
    DWORD a = attrs(p); if (a == INVALID_FILE_ATTRIBUTES) return {false, FILE_ATTRIBUTE_NORMAL,{}};
    regular(p); return {true,a,readFile(p,64*1024*1024)};
}
wil::unique_handle protectIni(const fs::path& p) {
    wil::unique_handle handle(CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr));
    if(!handle) { DWORD error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)THROW_WIN32(error); }
    return handle;
}
void unchanged(const fs::path& p,const Snapshot& old) {
    DWORD current=attrs(p);
    THROW_WIN32_IF(ERROR_RETRY,old.exists!=(current!=INVALID_FILE_ATTRIBUTES));
    if(old.exists) {regular(p);THROW_WIN32_IF(ERROR_RETRY,readFile(p)!=old.bytes);}
}
void restore(const fs::path& p, const Snapshot& s) {
    if (s.exists) replace(p,s.bytes,s.attributes);
    else if (attrs(p) != INVALID_FILE_ATTRIBUTES) { SetFileAttributesW(p.c_str(),FILE_ATTRIBUTE_NORMAL); THROW_IF_WIN32_BOOL_FALSE(DeleteFileW(p.c_str())); }
}
// Serialize our own processes per folder. Other writers are excluded by the INI handle.
wil::unique_handle lockFolder(const fs::path& folder) {
    auto s = fullPath(folder).native(); for (auto& c:s) c = static_cast<wchar_t>(towupper(c));
    unsigned long long hash=14695981039346656037ull; for (wchar_t c:s) { hash ^= c; hash *= 1099511628211ull; }
    auto name = L"Local\\IconicFolders." + std::to_wstring(hash);
    wil::unique_handle h(CreateMutexW(nullptr,FALSE,name.c_str())); THROW_LAST_ERROR_IF(!h);
    DWORD w=WaitForSingleObject(h.get(),0); THROW_WIN32_IF(ERROR_BUSY,w!=WAIT_OBJECT_0 && w!=WAIT_ABANDONED);
    return h;
}
IconInfo infoFrom(const fs::path& folder, const Ini& ini) {
    IconInfo info; info.resource=ini.icon(); auto value=trim(info.resource);
    if (value.empty()) return info;
    size_t comma=value.rfind(L','); if (comma!=value.npos) value=trim(value.substr(0,comma));
    if (value.size()>1 && value.front()==L'"' && value.back()==L'"') value=value.substr(1,value.size()-2);
    info.managed=equal(value,ManagedIcon) && trim(info.resource.substr(info.resource.rfind(L',')+1))==L"0";
    wchar_t expanded[32768]; DWORD n=ExpandEnvironmentStringsW(value.c_str(),expanded,32768);
    THROW_WIN32_IF(ERROR_FILENAME_EXCED_RANGE,!n || n>32768);
    info.source=fs::path(expanded); if (info.source.is_relative()) info.source=folder/info.source;
    return info;
}
struct CachedIcon {
    std::wstring file; int resource=0,system=-1; UINT flags=0;
    explicit CachedIcon(const fs::path& folder) {
        wil::unique_cotaskmem_ptr<ITEMIDLIST> pidl;
        if(FAILED(SHParseDisplayName(folder.c_str(),nullptr,wil::out_param(pidl),0,nullptr)))return;
        wil::com_ptr<IShellFolder> parent; PCUITEMID_CHILD child=nullptr;
        if(FAILED(SHBindToParent(pidl.get(),IID_PPV_ARGS(&parent),&child)))return;
        wil::com_ptr<IExtractIconW> extract;
        if(FAILED(parent->GetUIObjectOf(nullptr,1,&child,IID_IExtractIconW,nullptr,extract.put_void())))return;
        wchar_t location[32768]={};
        if(FAILED(extract->GetIconLocation(0,location,32768,&resource,&flags)))return;
        SHFILEINFOW info={};
        if(!SHGetFileInfoW(reinterpret_cast<PCWSTR>(pidl.get()),0,&info,sizeof(info),SHGFI_PIDL|SHGFI_SYSICONINDEX))return;
        file=location;system=info.iIcon;
    }
    void refresh() const { if(system>=0)SHUpdateImageW(file.c_str(),resource,flags,system); }
};
}

Ini::Ini(const std::vector<unsigned char>& bytes) : encoding_(Encoding::Utf16LE) {
    if (bytes.empty()) return;
    size_t start=0;
    if (bytes.size()>=2 && ((bytes[0]==255&&bytes[1]==254)||(bytes[0]==254&&bytes[1]==255))) {
        encoding_=bytes[0]==255?Encoding::Utf16LE:Encoding::Utf16BE;
        THROW_WIN32_IF(ERROR_NO_UNICODE_TRANSLATION,bytes.size()%2!=0);
        for(size_t i=2;i<bytes.size();i+=2) text_.push_back(static_cast<wchar_t>(encoding_==Encoding::Utf16LE?bytes[i]|(bytes[i+1]<<8):(bytes[i]<<8)|bytes[i+1]));
    } else {
        bool bom=bytes.size()>=3&&bytes[0]==239&&bytes[1]==187&&bytes[2]==191; start=bom?3:0;
        // BOM-less INI files use the Windows ANSI code page, even when their
        // ASCII bytes also happen to be valid UTF-8.
        encoding_=bom?Encoding::Utf8Bom:Encoding::Ansi;
        for(size_t i=start;i<bytes.size();++i) text_.push_back(bytes[i]);
    }
    THROW_WIN32_IF(ERROR_INVALID_DATA,text_.find(L'\0')!=text_.npos);
}
void Ini::requireShellEncoding() const {
    THROW_HR_IF(UnsupportedIniEncoding,encoding_==Encoding::Utf8Bom||encoding_==Encoding::Utf16BE);
}
std::wstring Ini::encodeValue(const std::wstring& s) const {
    if (encoding_==Encoding::Utf16LE||encoding_==Encoding::Utf16BE) return s;
    UINT cp=encoding_==Encoding::Ansi?CP_ACP:CP_UTF8; BOOL used=FALSE;
    int n=WideCharToMultiByte(cp,cp==CP_ACP?WC_NO_BEST_FIT_CHARS:WC_ERR_INVALID_CHARS,s.c_str(),static_cast<int>(s.size()),nullptr,0,nullptr,cp==CP_ACP?&used:nullptr);
    THROW_WIN32_IF(ERROR_NO_UNICODE_TRANSLATION,!n || used);
    std::string bytes(n,0); WideCharToMultiByte(cp,cp==CP_ACP?WC_NO_BEST_FIT_CHARS:WC_ERR_INVALID_CHARS,s.c_str(),static_cast<int>(s.size()),bytes.data(),n,nullptr,cp==CP_ACP?&used:nullptr);
    THROW_WIN32_IF(ERROR_NO_UNICODE_TRANSLATION,used); std::wstring raw; for(unsigned char c:bytes)raw.push_back(c); return raw;
}
std::wstring Ini::decodeValue(const std::wstring& s) const {
    if (encoding_==Encoding::Utf16LE||encoding_==Encoding::Utf16BE||s.empty()) return s;
    std::string bytes; for(wchar_t c:s) bytes.push_back(static_cast<char>(c));
    UINT cp=encoding_==Encoding::Ansi?CP_ACP:CP_UTF8;
    int n=MultiByteToWideChar(cp,0,bytes.data(),static_cast<int>(bytes.size()),nullptr,0); THROW_LAST_ERROR_IF(!n);
    std::wstring out(n,0); MultiByteToWideChar(cp,0,bytes.data(),static_cast<int>(bytes.size()),out.data(),n); return out;
}
std::wstring Ini::icon() const {
    std::wstring legacy,index=L"0";
    for(const auto& line:lines(text_)) {
        if(equal(line.key,L"IconResource")) return decodeValue(trim(line.body.substr(line.value)));
        if(equal(line.key,L"IconFile")) legacy=decodeValue(trim(line.body.substr(line.value)));
        if(equal(line.key,L"IconIndex")) index=decodeValue(trim(line.body.substr(line.value)));
    }
    return legacy.empty()?L"":legacy+L","+index;
}
std::vector<unsigned char> Ini::update(const std::optional<std::wstring>& resource) const {
    auto all=lines(text_); std::wstring eol=L"\r\n";
    for(const auto& l:all) if(!l.end.empty()) { eol=l.end; break; }
    std::wstring out; bool written=false,seenSection=false;
    for(const auto& l:all) {
        if(l.section && l.shell && !seenSection) {
            seenSection=true;
            out+=l.body+l.end;
            if(resource) { if(l.end.empty()) out+=eol; out+=L"IconResource="+encodeValue(*resource)+eol; written=true; }
        } else if(l.shell && iconKey(l.key)) {
            // All duplicate and legacy icon entries are removed; unrelated bytes are untouched.
        } else out+=l.body+l.end;
    }
    if(resource&&!written) { if(!out.empty()&&out.back()!=L'\r'&&out.back()!=L'\n') out+=eol; out+=L"[.ShellClassInfo]"+eol+L"IconResource="+encodeValue(*resource)+eol; }
    std::vector<unsigned char> bytes;
    if(encoding_==Encoding::Utf16LE) bytes={255,254}; else if(encoding_==Encoding::Utf16BE) bytes={254,255}; else if(encoding_==Encoding::Utf8Bom) bytes={239,187,191};
    for(wchar_t c:out) { if(encoding_==Encoding::Utf16LE) { bytes.push_back(static_cast<unsigned char>(c)); bytes.push_back(static_cast<unsigned char>(c>>8)); } else if(encoding_==Encoding::Utf16BE) { bytes.push_back(static_cast<unsigned char>(c>>8)); bytes.push_back(static_cast<unsigned char>(c)); } else bytes.push_back(static_cast<unsigned char>(c)); }
    return bytes;
}
std::vector<unsigned char> readFile(const fs::path& p,size_t limit) {
    wil::unique_handle h(CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr)); THROW_LAST_ERROR_IF(!h);
    LARGE_INTEGER size; THROW_IF_WIN32_BOOL_FALSE(GetFileSizeEx(h.get(),&size)); THROW_WIN32_IF(ERROR_FILE_TOO_LARGE,size.QuadPart<0||static_cast<unsigned long long>(size.QuadPart)>limit);
    std::vector<unsigned char> bytes(static_cast<size_t>(size.QuadPart)); DWORD got=0;
    if(!bytes.empty()) { THROW_IF_WIN32_BOOL_FALSE(ReadFile(h.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr)); THROW_WIN32_IF(ERROR_READ_FAULT,got!=bytes.size()); } return bytes;
}
void writeFile(const fs::path& p,const std::vector<unsigned char>& bytes) {
    wil::unique_handle h(CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr)); THROW_LAST_ERROR_IF(!h); DWORD wrote=0;
    if(!bytes.empty()) { THROW_IF_WIN32_BOOL_FALSE(WriteFile(h.get(),bytes.data(),static_cast<DWORD>(bytes.size()),&wrote,nullptr)); THROW_WIN32_IF(ERROR_WRITE_FAULT,wrote!=bytes.size()); }
    THROW_IF_WIN32_BOOL_FALSE(FlushFileBuffers(h.get()));
}
void validateIcon(const fs::path& p) {
    regular(p); THROW_WIN32_IF(ERROR_INVALID_DATA,!equal(p.extension().native(),L".ico")); auto b=readFile(p,64*1024*1024);
    auto u16=[&](size_t i)->unsigned { return b[i]|(b[i+1]<<8); };
    auto u32=[&](size_t i)->unsigned { return b[i]|(b[i+1]<<8)|(b[i+2]<<16)|(static_cast<unsigned>(b[i+3])<<24); };
    THROW_WIN32_IF(ERROR_INVALID_DATA,b.size()<6 || u16(0)!=0 || u16(2)!=1 || !u16(4));
    unsigned count=u16(4); THROW_WIN32_IF(ERROR_INVALID_DATA,count>(b.size()-6)/16);
    for(unsigned i=0;i<count;++i) { size_t p0=6+i*16; size_t len=u32(p0+8),offset=u32(p0+12);
        THROW_WIN32_IF(ERROR_INVALID_DATA,!len || offset<6+count*16 || offset>b.size() || len>b.size()-offset);
        wil::unique_hicon icon(CreateIconFromResourceEx(b.data()+offset,static_cast<DWORD>(len),TRUE,0x30000,b[p0]?b[p0]:256,b[p0+1]?b[p0+1]:256,LR_DEFAULTCOLOR)); THROW_WIN32_IF(ERROR_INVALID_DATA,!icon);
    }
}
IconInfo inspect(const fs::path& folder) { auto p=folder/L"desktop.ini"; if(attrs(p)==INVALID_FILE_ATTRIBUTES)return {}; regular(p); return infoFrom(folder,Ini(readFile(p))); }
bool needsOverwrite(const fs::path& folder,const fs::path& source,Mode mode) { return mode==Mode::Copy && attrs(folder/ManagedIcon)!=INVALID_FILE_ATTRIBUTES && !same(source,folder/ManagedIcon); }
void apply(const fs::path& input,const fs::path& source,Mode mode,bool overwriteApproved) {
    auto folder=fullPath(input); regular(folder,true); validateIcon(source);
    auto mutex=lockFolder(folder); auto release=wil::scope_exit([&]{ReleaseMutex(mutex.get());});
    auto iniPath=folder/L"desktop.ini",iconPath=folder/ManagedIcon;
    auto iniLock=protectIni(iniPath);
    Snapshot oldIni=snapshot(iniPath); Ini ini(oldIni.bytes);ini.requireShellEncoding(); auto old=infoFrom(folder,ini);
    bool copy=mode==Mode::Copy && !same(source,iconPath);
    THROW_WIN32_IF(ERROR_FILE_EXISTS,copy && attrs(iconPath)!=INVALID_FILE_ATTRIBUTES && !overwriteApproved);
    // An absolute reference to the reserved filename remains a reference, never an owned copy.
    auto value=mode==Mode::Copy?std::wstring(ManagedIcon):fullPath(source).native();
    THROW_WIN32_IF(ERROR_INVALID_NAME,value.find_first_of(L"\r\n")!=value.npos);
    auto updated=ini.update(value+L",0");
    bool removeOld=mode==Mode::Reference && old.managed && !same(source,iconPath) && attrs(iconPath)!=INVALID_FILE_ATTRIBUTES;
    THROW_WIN32_IF(ERROR_CANCELLED,removeOld&&!overwriteApproved);
    Snapshot oldIcon=(copy||removeOld||mode==Mode::Copy)?snapshot(iconPath):Snapshot{false,FILE_ATTRIBUTE_NORMAL,{}}; DWORD folderAttrs=attrs(folder);
    std::optional<CachedIcon> cache;
    if((copy&&oldIcon.exists)||removeOld)cache.emplace(folder);
    bool changedIcon=false,changedIni=false;
    try {
        if(copy) { replace(iconPath,readFile(source,64*1024*1024),FILE_ATTRIBUTE_HIDDEN); changedIcon=true; }
        else if(mode==Mode::Copy) { THROW_IF_WIN32_BOOL_FALSE(SetFileAttributesW(iconPath.c_str(),oldIcon.attributes|FILE_ATTRIBUTE_HIDDEN)); changedIcon=true; }
        unchanged(iniPath,oldIni);
        replace(iniPath,updated,(oldIni.exists?oldIni.attributes:0)|FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM); changedIni=true;
        THROW_IF_WIN32_BOOL_FALSE(SetFileAttributesW(folder.c_str(),folderAttrs|FILE_ATTRIBUTE_READONLY));
        if(removeOld) { regular(iconPath); THROW_IF_WIN32_BOOL_FALSE(SetFileAttributesW(iconPath.c_str(),oldIcon.attributes&~FILE_ATTRIBUTE_READONLY)); changedIcon=true; THROW_IF_WIN32_BOOL_FALSE(DeleteFileW(iconPath.c_str())); }
    } catch (...) {
        auto failure=std::current_exception();
        if(changedIni) restore(iniPath,oldIni);
        if(changedIcon) restore(iconPath,oldIcon);
        SetFileAttributesW(folder.c_str(),folderAttrs); std::rethrow_exception(failure);
    }
    if(cache)cache->refresh(); notify(folder);
}
void reset(const fs::path& input,bool deleteApproved) {
    auto folder=fullPath(input); regular(folder,true); auto mutex=lockFolder(folder); auto release=wil::scope_exit([&]{ReleaseMutex(mutex.get());});
    auto iniPath=folder/L"desktop.ini";auto iniLock=protectIni(iniPath); auto old=snapshot(iniPath); if(!old.exists)return;
    Ini ini(old.bytes);ini.requireShellEncoding(); auto info=infoFrom(folder,ini); if(info.resource.empty())return;
    auto iconPath=folder/ManagedIcon; bool remove=info.managed&&attrs(iconPath)!=INVALID_FILE_ATTRIBUTES;
    THROW_WIN32_IF(ERROR_CANCELLED,remove&&!deleteApproved); Snapshot icon=remove?snapshot(iconPath):Snapshot{false,FILE_ATTRIBUTE_NORMAL,{}};
    std::optional<CachedIcon> cache;if(remove)cache.emplace(folder);
    unchanged(iniPath,old);replace(iniPath,ini.update(std::nullopt),old.attributes|FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM);
    if(remove) {
        try {
            regular(iconPath); THROW_IF_WIN32_BOOL_FALSE(SetFileAttributesW(iconPath.c_str(),icon.attributes&~FILE_ATTRIBUTE_READONLY));
            THROW_IF_WIN32_BOOL_FALSE(DeleteFileW(iconPath.c_str()));
        } catch(...) {
            auto failure=std::current_exception();SetFileAttributesW(iconPath.c_str(),icon.attributes);restore(iniPath,old);std::rethrow_exception(failure);
        }
    }
    if(cache)cache->refresh(); notify(folder);
}
void notify(const fs::path& folder) {
    wil::unique_cotaskmem_ptr<ITEMIDLIST> item,parent;
    if(SUCCEEDED(SHParseDisplayName(folder.c_str(),nullptr,wil::out_param(item),0,nullptr))) {
        SHChangeNotify(SHCNE_ATTRIBUTES|SHCNE_UPDATEITEM,SHCNF_IDLIST|SHCNF_FLUSH,item.get(),nullptr);
    }
    if(SUCCEEDED(SHParseDisplayName(folder.parent_path().c_str(),nullptr,wil::out_param(parent),0,nullptr))) {
        SHChangeNotify(SHCNE_UPDATEDIR,SHCNF_IDLIST|SHCNF_FLUSH,parent.get(),nullptr);
    }
}
std::wstring quoteArg(const std::wstring& arg) {
    std::wstring out=L"\""; size_t slashes=0;
    for(wchar_t c:arg) { if(c==L'\\')++slashes; else { out.append(slashes*(c==L'"'?2:1),L'\\'); slashes=0; if(c==L'"')out+=L'\\'; out+=c; } }
    out.append(slashes*2,L'\\'); return out+L'"';
}
}
