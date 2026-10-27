// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Benco
#include "ui.h"
namespace iconic {
void selectLanguage() {
    LANGID user=GetUserDefaultUILanguage();
    SetThreadUILanguage(PRIMARYLANGID(user)==LANG_FRENCH?MAKELANGID(LANG_FRENCH,SUBLANG_FRENCH):MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US));
}
std::wstring tr(UINT id,HMODULE module) {
    if(!module) module=GetModuleHandleW(nullptr);
    // LoadString's native ultimate fallback can pick the OS language. Explicit
    // resource lookup makes the requested English fallback deterministic, without
    // changing Explorer's thread language in the shell extension.
    LANGID language=PRIMARYLANGID(GetThreadUILanguage())==LANG_FRENCH?MAKELANGID(LANG_FRENCH,SUBLANG_FRENCH):MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US);
    HRSRC resource=FindResourceExW(module,RT_STRING,MAKEINTRESOURCEW(id/16+1),language);
    if(resource) {
        auto data=static_cast<const wchar_t*>(LockResource(LoadResource(module,resource)));
        if(data) { for(UINT i=0;i<id%16;++i)data+=1+*data; return std::wstring(data+1,*data); }
    }
    return L"IconicFolders";
}
std::wstring format(UINT id,const std::wstring& value) {
    auto s=tr(id); auto p=s.find(L"%s"); if(p!=s.npos)s.replace(p,2,value); return s;
}
}
