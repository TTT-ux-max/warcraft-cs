#pragma once
#include "ShortcutAction.hpp"
#include <array>
#include <string>
#include <windows.h>

// One validated snapshot serves input dispatch, held movement and displayed shortcut labels.
class ShortcutBindings {
public:
    static constexpr size_t Count=size_t(ShortcutAction::Count);
    ShortcutBindings();
    int Key(ShortcutAction action) const { return keys_[size_t(action)]; }
    void Set(ShortcutAction action,int key) { keys_[size_t(action)]=key; }
    bool Matches(ShortcutAction action,int key) const { return key!=0 && Key(action)==key; }
    bool Valid() const;
    static const char* Name(ShortcutAction action);
    static int Parse(std::string value);
    static std::string Label(int key);
private:
    std::array<int,Count> keys_;
};
