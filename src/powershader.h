#pragma once
#include <windows.h>

namespace PowerShader {
    void Init(HINSTANCE hInst, bool lightTheme = false);
    void Shutdown();
    void Toggle();
    bool IsOpen();
    void ReloadTheme(bool lightTheme);
}
