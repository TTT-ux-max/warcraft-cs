#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace NativeArchive {
inline std::vector<uint8_t> Read(const std::string& path,DWORD maximum=8*1024*1024) {
    // Read the active map first through Storm, retaining custom imports without packaging game assets.
    auto storm=GetModuleHandleA("Storm.dll");if(!storm)return {};
    auto open=reinterpret_cast<BOOL(WINAPI*)(const char*,HANDLE*)>(GetProcAddress(storm,MAKEINTRESOURCEA(267)));
    auto size=reinterpret_cast<DWORD(WINAPI*)(HANDLE,DWORD*)>(GetProcAddress(storm,MAKEINTRESOURCEA(265)));
    auto read=reinterpret_cast<BOOL(WINAPI*)(HANDLE,void*,DWORD,DWORD*,LONG)>(GetProcAddress(storm,MAKEINTRESOURCEA(269)));
    auto close=reinterpret_cast<BOOL(WINAPI*)(HANDLE)>(GetProcAddress(storm,MAKEINTRESOURCEA(253)));
    HANDLE file=nullptr;if(!open || !size || !read || !close || !open(path.c_str(),&file))return {};
    DWORD length=size(file,nullptr),got=0;std::vector<uint8_t> bytes;
    if(length && length<=maximum) {bytes.resize(length);if(!read(file,bytes.data(),length,&got,0) || got!=length)bytes.clear();}
    close(file);return bytes;
}
}
