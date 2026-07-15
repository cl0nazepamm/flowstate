#pragma once
#include <windows.h>

namespace ModStack {
    bool Init(HINSTANCE hInst, bool lightTheme = false);
    void Shutdown();
    void Toggle();
    bool IsOpen();
    void ReloadTheme(bool lightTheme);
}
