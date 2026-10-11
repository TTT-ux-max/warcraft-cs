#pragma once
#include <windows.h>

// Explicit fixture builds may run without owning the desktop; ordinary DLLs never include this switch.
namespace BackgroundGameplay {
inline bool Enabled() {
    static const bool enabled=[] {char value[2]={};return GetEnvironmentVariableA("WCS_BACKGROUND_TEST",value,2)==1 && value[0]=='1';}();
    return enabled;
}
}
