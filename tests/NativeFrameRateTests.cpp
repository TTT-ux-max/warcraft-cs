#include "../src/platform/NativeFrameRate.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

namespace wc3 {void Log(const char*,...) {} void LogError(const char*,...) {}}
int main() {
    // Synthetic executable memory reproduces only the guarded operand; no game file/process is accessed.
    auto memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x630000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    assert(memory);
    const unsigned char signature[]={0x8B,0x46,0x44,0xA8,0x04,0x74,0x14,0x83,0xE0,0xFB};
    auto target=memory+0x62D7F2;
    std::memcpy(target,signature,sizeof(signature));
    NativeFrameRate adapter;assert(adapter.Install(reinterpret_cast<uintptr_t>(memory)));
    adapter.Configure(144);assert(target[9]==0xFF);
    adapter.Configure(0);assert(!std::memcmp(target,signature,sizeof(signature)));
    adapter.Configure(120);assert(target[9]==0xFF);
    LARGE_INTEGER frequency,start,end;QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&start);
    for (int i=0;i<31;++i) adapter.Pace();
    QueryPerformanceCounter(&end);
    double elapsed=double(end.QuadPart-start.QuadPart)/frequency.QuadPart;
    assert(elapsed>=.249 && elapsed<1.5);
    // External changes must never be overwritten on reload, and unknown hosts stay unmodified.
    target[8]=0xE1;adapter.Configure(0);assert(target[8]==0xE1 && target[9]==0xFF);
    NativeFrameRate unknown;assert(!unknown.Install(reinterpret_cast<uintptr_t>(memory)));
    unknown.Configure(144);unknown.Pace();assert(target[8]==0xE1);
    VirtualFree(memory,0,MEM_RELEASE);
    printf("PASS native frame-rate operand, legacy restore, reload, foreign-patch refusal and real 120 FPS pacing (%.3fs).\n",elapsed);
}
