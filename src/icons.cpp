// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Benco
#include "icons.h"
#include <wincodec.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <wil/com.h>
#include <wil/result.h>
#include <algorithm>
#include <cwctype>
#include <limits>

namespace iconic {
namespace {
unsigned u16(const unsigned char* p) { return p[0]|(p[1]<<8); }
unsigned u32(const unsigned char* p) { return p[0]|(p[1]<<8)|(p[2]<<16)|(static_cast<unsigned>(p[3])<<24); }
void word(std::vector<unsigned char>& b,unsigned v) { b.push_back(static_cast<unsigned char>(v));b.push_back(static_cast<unsigned char>(v>>8)); }
void dword(std::vector<unsigned char>& b,unsigned v) { word(b,v);word(b,v>>16); }
std::wstring extension(const fs::path& p) { auto s=p.extension().native();std::transform(s.begin(),s.end(),s.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});return s; }
void regularSource(const fs::path& p) {
    auto a=GetFileAttributesW(p.c_str());THROW_LAST_ERROR_IF(a==INVALID_FILE_ATTRIBUTES);
    THROW_WIN32_IF(ERROR_INVALID_DATA,(a&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))!=0);
}
BOOL CALLBACK foundIcon(HMODULE,PCWSTR,PWSTR,LONG_PTR parameter) { *reinterpret_cast<bool*>(parameter)=true;return FALSE; }
bool hasIcons(HMODULE module) { bool found=false;EnumResourceNamesW(module,RT_GROUP_ICON,foundIcon,reinterpret_cast<LONG_PTR>(&found));return found; }
wil::unique_hmodule resourceModule(const fs::path& p) {
    regularSource(p);
    auto module=wil::unique_hmodule(LoadLibraryExW(p.c_str(),nullptr,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE));
    THROW_LAST_ERROR_IF(!module);
    // Modern Windows moves neutral icon resources to SystemResources/*.mun.
    if(!hasIcons(module.get())) {
        wchar_t system[32768]={};THROW_LAST_ERROR_IF(!GetSystemDirectoryW(system,32768));
        if(CompareStringOrdinal(p.parent_path().c_str(),-1,system,-1,TRUE)==CSTR_EQUAL) {
            auto companion=fs::path(system).parent_path()/L"SystemResources"/(p.filename().native()+L".mun");
            if(fs::is_regular_file(companion)) {
                auto other=wil::unique_hmodule(LoadLibraryExW(companion.c_str(),nullptr,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE));
                if(other&&hasIcons(other.get()))module=std::move(other);
            }
        }
    }
    return module;
}
struct ResourceName { std::wstring text;WORD id=0;bool integer=false; };
struct Names { std::vector<ResourceName> values; HRESULT failure=S_OK; };
BOOL CALLBACK collect(HMODULE,PCWSTR,PWSTR name,LONG_PTR parameter) noexcept {
    auto& names=*reinterpret_cast<Names*>(parameter);
    try { ResourceName value;if(IS_INTRESOURCE(name)){value.integer=true;value.id=static_cast<WORD>(reinterpret_cast<ULONG_PTR>(name));}else value.text=name;names.values.push_back(std::move(value));return TRUE; }
    catch(...) { names.failure=wil::ResultFromCaughtException();return FALSE; }
}
Names namesIn(HMODULE module) {
    Names names;EnumResourceNamesW(module,RT_GROUP_ICON,collect,reinterpret_cast<LONG_PTR>(&names));THROW_IF_FAILED(names.failure);return names;
}
std::vector<unsigned char> resourceBytes(HMODULE module,PCWSTR type,PCWSTR name,size_t limit=64*1024*1024) {
    auto resource=FindResourceW(module,name,type);THROW_LAST_ERROR_IF(!resource);
    DWORD size=SizeofResource(module,resource);THROW_WIN32_IF(ERROR_INVALID_DATA,!size||size>limit);
    auto handle=LoadResource(module,resource);THROW_LAST_ERROR_IF(!handle);
    auto p=static_cast<const unsigned char*>(LockResource(handle));THROW_LAST_ERROR_IF(!p);
    return {p,p+size};
}
std::vector<unsigned char> resourceIco(HMODULE module,int index) {
    auto names=namesIn(module);PCWSTR name=nullptr;std::wstring text;
    if(index<0) { THROW_WIN32_IF(ERROR_INVALID_DATA,index==std::numeric_limits<int>::min()||-index>65535);name=MAKEINTRESOURCEW(-index); }
    else {
        THROW_WIN32_IF(ERROR_INVALID_DATA,static_cast<size_t>(index)>=names.values.size());const auto& entry=names.values[index];text=entry.text;
        name=entry.integer?MAKEINTRESOURCEW(entry.id):text.c_str();
    }
    auto group=resourceBytes(module,RT_GROUP_ICON,name);
    THROW_WIN32_IF(ERROR_INVALID_DATA,group.size()<6||u16(group.data())||u16(group.data()+2)!=1);
    unsigned count=u16(group.data()+4);THROW_WIN32_IF(ERROR_INVALID_DATA,!count||count>(group.size()-6)/14);
    std::vector<std::vector<unsigned char>> frames;unsigned offset=6+16*count;
    for(unsigned i=0;i<count;++i){auto frame=resourceBytes(module,RT_ICON,MAKEINTRESOURCEW(u16(group.data()+6+i*14+12)),64*1024*1024-offset);offset+=static_cast<unsigned>(frame.size());frames.push_back(std::move(frame));}
    std::vector<unsigned char> bytes;word(bytes,0);word(bytes,1);word(bytes,count);offset=6+16*count;
    for(unsigned i=0;i<count;++i) {
        auto p=group.data()+6+i*14;bytes.insert(bytes.end(),p,p+8);dword(bytes,static_cast<unsigned>(frames[i].size()));dword(bytes,offset);
        THROW_WIN32_IF(ERROR_INVALID_DATA,frames[i].size()>64*1024*1024-offset);offset+=static_cast<unsigned>(frames[i].size());
    }
    for(const auto& frame:frames)bytes.insert(bytes.end(),frame.begin(),frame.end());return bytes;
}
std::vector<unsigned char> png(IWICImagingFactory* factory,IWICBitmapSource* source,UINT size) {
    UINT width=0,height=0;THROW_IF_FAILED(source->GetSize(&width,&height));
    double scale=std::min(static_cast<double>(size)/width,static_cast<double>(size)/height);
    UINT w=std::max(1u,static_cast<UINT>(width*scale+.5)),h=std::max(1u,static_cast<UINT>(height*scale+.5));
    wil::com_ptr<IWICFormatConverter> premultiplied;THROW_IF_FAILED(factory->CreateFormatConverter(&premultiplied));
    THROW_IF_FAILED(premultiplied->Initialize(source,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    wil::com_ptr<IWICBitmapScaler> scaler;THROW_IF_FAILED(factory->CreateBitmapScaler(&scaler));
    THROW_IF_FAILED(scaler->Initialize(premultiplied.get(),w,h,WICBitmapInterpolationModeFant));
    wil::com_ptr<IWICFormatConverter> straight;THROW_IF_FAILED(factory->CreateFormatConverter(&straight));
    THROW_IF_FAILED(straight->Initialize(scaler.get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    std::vector<unsigned char> scaledPixels(w*h*4),canvas(size*size*4,0);THROW_IF_FAILED(straight->CopyPixels(nullptr,w*4,static_cast<UINT>(scaledPixels.size()),scaledPixels.data()));
    UINT x=(size-w)/2,y=(size-h)/2;
    for(UINT row=0;row<h;++row)std::copy_n(scaledPixels.data()+row*w*4,w*4,canvas.data()+((y+row)*size+x)*4);
    wil::com_ptr<IWICBitmap> bitmap;THROW_IF_FAILED(factory->CreateBitmapFromMemory(size,size,GUID_WICPixelFormat32bppBGRA,size*4,static_cast<UINT>(canvas.size()),canvas.data(),&bitmap));
    wil::com_ptr<IStream> stream;THROW_IF_FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream));
    wil::com_ptr<IWICBitmapEncoder> encoder;THROW_IF_FAILED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));THROW_IF_FAILED(encoder->Initialize(stream.get(),WICBitmapEncoderNoCache));
    wil::com_ptr<IWICBitmapFrameEncode> frame;THROW_IF_FAILED(encoder->CreateNewFrame(&frame,nullptr));THROW_IF_FAILED(frame->Initialize(nullptr));THROW_IF_FAILED(frame->SetSize(size,size));
    auto format=GUID_WICPixelFormat32bppBGRA;THROW_IF_FAILED(frame->SetPixelFormat(&format));THROW_IF_FAILED(frame->WriteSource(bitmap.get(),nullptr));THROW_IF_FAILED(frame->Commit());THROW_IF_FAILED(encoder->Commit());
    STATSTG stat={};THROW_IF_FAILED(stream->Stat(&stat,STATFLAG_NONAME));THROW_WIN32_IF(ERROR_INVALID_DATA,stat.cbSize.QuadPart>64*1024*1024);
    LARGE_INTEGER start={};THROW_IF_FAILED(stream->Seek(start,STREAM_SEEK_SET,nullptr));
    std::vector<unsigned char> result(stat.cbSize.LowPart);ULONG read=0;THROW_IF_FAILED(stream->Read(result.data(),static_cast<ULONG>(result.size()),&read));THROW_WIN32_IF(ERROR_READ_FAULT,read!=result.size());return result;
}
IconData imageData(const fs::path& source) {
    IconData data;data.source=fs::absolute(source);data.image=true;
    auto bytes=readFile(source,64*1024*1024);wil::com_ptr<IStream> stream;stream.attach(SHCreateMemStream(bytes.data(),static_cast<UINT>(bytes.size())));THROW_IF_NULL_ALLOC(stream);
    wil::com_ptr<IWICImagingFactory> factory;THROW_IF_FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    wil::com_ptr<IWICBitmapDecoder> decoder;THROW_IF_FAILED(factory->CreateDecoderFromStream(stream.get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder));
    wil::com_ptr<IWICBitmapFrameDecode> frame;THROW_IF_FAILED(decoder->GetFrame(0,&frame));THROW_IF_FAILED(frame->GetSize(&data.width,&data.height));
    THROW_WIN32_IF(ERROR_INVALID_DATA,!data.width||!data.height||data.width>32768||data.height>32768||static_cast<UINT64>(data.width)*data.height>100000000);
    const UINT sizes[]={16,24,32,48,64,128,256};std::vector<std::vector<unsigned char>> frames;
    for(auto size:sizes)frames.push_back(png(factory.get(),frame.get(),size));
    word(data.bytes,0);word(data.bytes,1);word(data.bytes,7);unsigned offset=6+7*16;
    for(unsigned i=0;i<7;++i) {
        data.bytes.push_back(static_cast<unsigned char>(sizes[i]));data.bytes.push_back(static_cast<unsigned char>(sizes[i]));word(data.bytes,0);word(data.bytes,1);word(data.bytes,32);
        dword(data.bytes,static_cast<unsigned>(frames[i].size()));dword(data.bytes,offset);offset+=static_cast<unsigned>(frames[i].size());
    }
    for(const auto& f:frames)data.bytes.insert(data.bytes.end(),f.begin(),f.end());return data;
}
}
bool imageFile(const fs::path& p) { auto e=extension(p);return e==L".png"||e==L".jpg"||e==L".jpeg"||e==L".bmp"; }
bool iconFile(const fs::path& p) { return extension(p)==L".ico"; }
std::vector<int> resourceIcons(const fs::path& path) {
    auto module=resourceModule(fs::absolute(path));auto names=namesIn(module.get());std::vector<int> result;
    for(size_t i=0;i<names.values.size();++i) {
        const auto& n=names.values[i];int id=n.integer?n.id:0;
        result.push_back(id>0?-id:static_cast<int>(i));
    }return result;
}
IconData loadIconData(const fs::path& path,int index) {
    regularSource(path);IconData data;data.source=fs::absolute(path);data.index=index;
    if(imageFile(path))data=imageData(path);
    else if(iconFile(path)) { THROW_WIN32_IF(ERROR_INVALID_DATA,index!=0);data.bytes=readFile(path,64*1024*1024); }
    else { data.copyAllowed=false;auto module=resourceModule(data.source);data.bytes=resourceIco(module.get(),index); }
    validateIconBytes(data.bytes);
    if(!data.image)for(unsigned i=0;i<u16(data.bytes.data()+4);++i) {
        auto p=data.bytes.data()+6+16*i;UINT w=p[0]?p[0]:256,h=p[1]?p[1]:256;
        if(w*h>data.width*data.height) {data.width=w;data.height=h;}
    }
    return data;
}
IconData folderIconData(const fs::path& folder) {
    auto current=inspect(folder);if(!current.resource.empty())return loadIconData(current.source,current.index);
    SHSTOCKICONINFO stock={sizeof(stock)};THROW_IF_FAILED(SHGetStockIconInfo(SIID_FOLDER,SHGSI_ICONLOCATION,&stock));return loadIconData(stock.szPath,stock.iIcon);
}
wil::unique_hicon previewIcon(const IconData& data,int size) {
    auto& b=data.bytes;THROW_WIN32_IF(ERROR_INVALID_DATA,b.size()<22);
    unsigned count=u16(b.data()+4),best=0;UINT distance=std::numeric_limits<UINT>::max();
    for(unsigned i=0;i<count;++i) { auto p=b.data()+6+16*i;UINT w=p[0]?p[0]:256;UINT d=w>=static_cast<UINT>(size)?w-size:10000+size-w;if(d<distance){distance=d;best=i;} }
    auto p=b.data()+6+16*best;auto offset=u32(p+12),length=u32(p+8);THROW_WIN32_IF(ERROR_INVALID_DATA,offset>b.size()||length>b.size()-offset);
    wil::unique_hicon icon(CreateIconFromResourceEx(const_cast<PBYTE>(b.data()+offset),length,TRUE,0x30000,size,size,LR_DEFAULTCOLOR));THROW_LAST_ERROR_IF(!icon);return icon;
}
}
