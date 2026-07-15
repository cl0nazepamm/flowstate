#pragma once
#include <windows.h>
#include <string>

// Defined in flowstate.cpp — saves all settings to unified FlowState.cfg
void FlowState_SaveSettings();

namespace PowerShader {
    bool Init(HINSTANCE hInst, bool lightTheme = false);
    void Shutdown();
    void Toggle();
    bool IsOpen();
    void ReloadTheme(bool lightTheme);

    // Unified config persistence (FlowState.cfg sections)
    void WritePinsSection(FILE* f);
    void WriteBricksSection(FILE* f);
    void ReadPinsLine(const std::wstring& line);
    void ReadBricksLine(const std::wstring& line);
    void ClearPersistent();
}
