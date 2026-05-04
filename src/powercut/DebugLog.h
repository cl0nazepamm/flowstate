#pragma once
#include <cstdio>
#include <cstdarg>
#include <windows.h>

// Writes to C:\temp\powercut.log — create C:\temp if it doesn't exist
inline void PBLog(const char* fmt, ...) {
    static FILE* fp = nullptr;
    if (!fp) {
        CreateDirectoryA("C:\\temp", nullptr);
        fp = fopen("C:\\temp\\powercut.log", "w");
        if (!fp) return;
    }
    va_list args;
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);
    fprintf(fp, "\n");
    fflush(fp);
}
