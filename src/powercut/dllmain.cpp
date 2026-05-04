#include "PowerCutMod.h"

HINSTANCE hInstance = nullptr;

// ── Singleton class descriptor (Meyer's singleton — safe init order) ─
PowerCutClassDesc* GetPowerCutDesc() {
    static PowerCutClassDesc desc;
    return &desc;
}

// ── DLL entry ───────────────────────────────────────────────────
BOOL WINAPI DllMain(HINSTANCE hinstDLL, ULONG fdwReason, LPVOID) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        MaxSDK::Util::UseLanguagePackLocale();
        hInstance = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}

// ── Lib exports ─────────────────────────────────────────────────
__declspec(dllexport) const TCHAR* LibDescription() {
    return _T("PowerCut - Spline Boolean Modifier");
}

__declspec(dllexport) int LibNumberClasses() {
    return 1;
}

__declspec(dllexport) ClassDesc* LibClassDesc(int i) {
    switch (i) {
        case 0: return GetPowerCutDesc();
        default: return nullptr;
    }
}

__declspec(dllexport) ULONG LibVersion() {
    return VERSION_3DSMAX;
}

__declspec(dllexport) int LibInitialize() {
    return TRUE;
}

__declspec(dllexport) int LibShutdown() {
    return TRUE;
}
