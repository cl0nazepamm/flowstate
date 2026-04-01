#pragma once
#include <windows.h>

namespace ModStack {
    void Init(HINSTANCE hInst, bool lightTheme = false);
    void Shutdown();
    void Toggle();
    bool IsOpen();
    void ReloadTheme(bool lightTheme);
}
