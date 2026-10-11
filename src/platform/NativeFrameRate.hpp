#pragma once
#include <Windows.h>
#include <cstdint>

// Own the verified 1.26a render-demand flag and a client-only presentation clock.
class NativeFrameRate {
public:
    ~NativeFrameRate();
    bool Install(uintptr_t gameBase);
    void Configure(int maximumFPS);
    void Pace();
private:
    bool WriteDemand(bool unlocked);
    unsigned char* instruction_=nullptr;
    HANDLE timer_=nullptr;
    LARGE_INTEGER frequency_{};
    LONGLONG previous_=0;
    int requested_=-1, maximum_=0;
};
