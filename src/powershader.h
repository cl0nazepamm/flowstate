#pragma once
#include <windows.h>

namespace PowerShader {
    void Init(HINSTANCE hInst);
    void Shutdown();
    void Toggle();
    bool IsOpen();
}
