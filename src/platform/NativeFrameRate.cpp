#include "NativeFrameRate.hpp"
#include "WarcraftApi.hpp"
#include <algorithm>
#include <cstring>

namespace {
// Render completion clears demand bit 4 at [this+44h]. Keeping that bit set permits more
// frames without changing game clocks or simulation timers. The address is resolved per
// build through wc3::Rva(); the instruction itself is stable across builds but not
// byte-identical, so only the register encoding and the short-jump displacement are
// treated as variable.
constexpr uintptr_t DemandInstruction=0x62D7F2;  // 1.26a RVA; resolved per build below
constexpr unsigned char signature[]={0x8B,0x46,0x44,0xA8,0x04,0x74,0x14,0x83,0xE0,0xFB};
// mov eax,[reg+44h] / test al,4 / jz short / and eax,-5 / mov [reg+44h],eax
//   index 1: mod/rm byte. 1.26a encodes esi (0x46), 1.27a encodes edi (0x47).
//   index 6: jz displacement. 1.26a jumps +14h, 1.27a jumps +11h.
// Every opcode, the field offset 44h, the tested bit and the cleared mask stay exact;
// a stale or unrelated site cannot satisfy the masked comparison.
bool Variable(size_t index) { return index==1 || index==6; }
bool MatchesPrologue(const unsigned char* bytes) {
    for (size_t index=0;index<sizeof(signature)-1;++index) {
        if (!Variable(index) && bytes[index]!=signature[index]) return false;
    }
    // `mov eax,[r32+disp8]` is mod=01, reg=eax(000), rm=<base register>. Only the rm
    // field may differ between builds, so the mod and reg bits must both stay put.
    if ((bytes[1]&0xF8)!=0x40) return false;
    // A short conditional jump never carries the sign bit of a longer displacement here.
    if (bytes[6]>0x7F) return false;
    return true;
}
}
NativeFrameRate::~NativeFrameRate() { if (timer_) CloseHandle(timer_); }
bool NativeFrameRate::Install(uintptr_t gameBase) {
    unsigned char bytes[sizeof(signature)];SIZE_T got=0;
    // The render-demand byte lives at a different RVA in every build, so an unresolved
    // build skips the patch and keeps the native limiter.
    uintptr_t demandRva=wc3::Rva(DemandInstruction);
    if (!demandRva) { wc3::Log("Frame-rate address unavailable on this build; native limiter retained"); return false; }
    auto address=reinterpret_cast<unsigned char*>(gameBase+demandRva);
    if (!ReadProcessMemory(GetCurrentProcess(),address,bytes,sizeof(bytes),&got) || got!=sizeof(bytes) ||
        !MatchesPrologue(bytes) || bytes[9]!=0xFB) {
        wc3::Log("Frame-rate signature mismatch; native limiter retained");
        return false;
    }
    instruction_=address;
    QueryPerformanceFrequency(&frequency_);
    // Windows 10/11 high-resolution waits avoid imposing a global timer-resolution change.
    timer_=CreateWaitableTimerExW(nullptr,nullptr,0x2,TIMER_ALL_ACCESS);
    bool highResolution=timer_!=nullptr;
    if (!timer_) timer_=CreateWaitableTimerW(nullptr,FALSE,nullptr);
    wc3::Log("Frame-rate adapter ready build=%s rva=0x%X highResolutionTimer=%d",
        wc3::ActiveBuildName(),unsigned(demandRva),highResolution);
    return true;
}
bool NativeFrameRate::WriteDemand(bool unlocked) {
    unsigned char bytes[sizeof(signature)];SIZE_T got=0;
    if (!ReadProcessMemory(GetCurrentProcess(),instruction_,bytes,sizeof(bytes),&got) || got!=sizeof(bytes) ||
        !MatchesPrologue(bytes) || (bytes[9]!=0xFB && bytes[9]!=0xFF)) {
        wc3::LogError("Frame-rate instruction changed; refusing overwrite");return false;
    }
    unsigned char value=unlocked ? 0xFF : 0xFB;
    if (bytes[9]==value) return true;
    DWORD protection=0;
    if (!VirtualProtect(instruction_+9,1,PAGE_EXECUTE_READWRITE,&protection)) {
        wc3::LogError("Frame-rate protection failed win32=%lu",GetLastError());return false;
    }
    // A single operand byte changes only render demand; no on-disk game file is patched.
    instruction_[9]=value;
    FlushInstructionCache(GetCurrentProcess(),instruction_+9,1);
    DWORD ignored=0;
    if (!VirtualProtect(instruction_+9,1,protection,&ignored))
        wc3::LogError("Frame-rate protection restore failed win32=%lu",GetLastError());
    return true;
}
void NativeFrameRate::Configure(int maximumFPS) {
    if (requested_==maximumFPS) return;
    requested_=maximumFPS;previous_=0;
    int target=maximumFPS==0 ? 0 : std::clamp(maximumFPS,30,360);
    maximum_=instruction_ && WriteDemand(target>0) ? target : 0;
    wc3::Log("Frame-rate applied requested=%d cap=%d nativeLimiter=%d",maximumFPS,maximum_,maximum_==0);
}
void NativeFrameRate::Pace() {
    if (!maximum_ || frequency_.QuadPart<=0) return;
    LARGE_INTEGER now;QueryPerformanceCounter(&now);
    LONGLONG period=frequency_.QuadPart/maximum_;
    LONGLONG deadline=previous_+period;
    // Focus pauses, slow frames and live reloads never accumulate a catch-up burst of frames.
    if (!previous_ || now.QuadPart>=deadline) {previous_=now.QuadPart;return;}
    while (now.QuadPart<deadline) {
        LONGLONG remaining=deadline-now.QuadPart;
        LARGE_INTEGER due;due.QuadPart=-std::max<LONGLONG>(1,remaining*10000000/frequency_.QuadPart);
        if (timer_ && SetWaitableTimer(timer_,&due,0,nullptr,nullptr,FALSE)) WaitForSingleObject(timer_,50);
        else Sleep(1);
        QueryPerformanceCounter(&now);
    }
    previous_=now.QuadPart;
}
