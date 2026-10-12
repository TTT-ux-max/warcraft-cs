#include "../src/platform/NativeMovementGuard.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>

namespace {
uintptr_t base=0;wc3::Handle actor=1;bool casting=false;
std::array<unsigned char,0x200> unit={};
using Speed=float* (__fastcall*)(uintptr_t,uintptr_t,float*);
Speed guarded=nullptr;int calls=0;
float* __fastcall Native(uintptr_t,uintptr_t,float* result) {++calls;*result=340;return result;}
bool Hook(void*,void* callback,void** original) {guarded=reinterpret_cast<Speed>(callback);*original=reinterpret_cast<void*>(Native);return true;}
}
namespace wc3 {
void Log(const char*,...) {}
void Trace(const char*,...) {}
uintptr_t UnitObject(Handle h) {return h==1 ? uintptr_t(unit.data()) : 0;}
}
int main() {
    static_assert(sizeof(uintptr_t)==4,"Native call-site verification is x86.");
    auto image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x300000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    assert(image);base=uintptr_t(image);uintptr_t movement=17;std::memcpy(unit.data()+0x1EC,&movement,4);
    auto actorGetter=[](){return actor;};auto cast=[](){return casting;};
    NativeMovementGuard::Install(base,actorGetter,cast,Hook);assert(!guarded);
    const unsigned char motor[]={0x51,0x83,0x79,0x7C,0};std::memcpy(image+0x201C60,motor,sizeof(motor));
    image[0x26DE3F]=0xE8;int delta=0x201C60-0x26DE44;std::memcpy(image+0x26DE40,&delta,4);
    NativeMovementGuard::Install(base,actorGetter,cast,Hook);assert(guarded);float result=-1;
    guarded(movement,0,&result);assert(!result && !calls);guarded(18,0,&result);assert(result==340 && calls==1);
    // Execute the real stats call-site ABI: its native result survives FPS even though walking returns zero.
    const unsigned char forward[]={0x90,0x90,0xFF,0x74,0x24,0x04};std::memcpy(image+0x26DE39,forward,sizeof(forward));
    delta=int(reinterpret_cast<uintptr_t>(guarded)-(base+0x26DE44));std::memcpy(image+0x26DE40,&delta,4);
    const unsigned char finish[]={0xC2,0x04,0};std::memcpy(image+0x26DE44,finish,sizeof(finish));
    reinterpret_cast<Speed>(base+0x26DE39)(movement,0,&result);assert(result==340 && calls==2);
    casting=true;guarded(movement,0,&result);assert(result==340 && calls==3);
    casting=false;actor=0;guarded(movement,0,&result);assert(result==340 && calls==4);
    VirtualFree(image,0,MEM_RELEASE);std::puts("PASS native walking suppressed; stats, explicit spells, other units and RTS speed remain native.");
}
