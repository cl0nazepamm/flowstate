#include <max.h>
#include <gup.h>
#include <iparamb2.h>
#include <plugapi.h>
#include <maxapi.h>
#include <custcont.h>
#include <string>

// ── Plugin Identity ─────────────────────────────────────────────
#define WALKER_CLASS_ID  Class_ID(0x7A1CE200, 0x3B4D5E6F)
#define WALKER_NAME      _T("Walker")
#define WALKER_CATEGORY  _T("MCP")

extern HINSTANCE hInstance;
HINSTANCE hInstance = nullptr;

// ── Overlay Config ──────────────────────────────────────────────
static const TCHAR* kOverlayClass  = _T("WalkerOverlay");
static const int    kTimerMs       = 50;
static const int    kPad           = 10;
static const int    kFontPx        = 14;
static const COLORREF kBg          = RGB(28, 28, 32);
static const COLORREF kBorder      = RGB(70, 70, 80);
static const COLORREF kTitleClr    = RGB(100, 170, 255);
static const COLORREF kLabelClr    = RGB(180, 180, 190);
static const COLORREF kValueClr    = RGB(255, 255, 255);
static const COLORREF kDimClr      = RGB(100, 100, 110);

// ── Globals ─────────────────────────────────────────────────────
static HWND     g_overlay  = nullptr;
static UINT_PTR g_timer    = 0;
static HWND     g_lastCtrl = nullptr;
static HFONT    g_font     = nullptr;
static HFONT    g_fontBold = nullptr;

struct WalkerInfo {
    std::wstring rollup;
    std::wstring label;
    std::wstring value;
    std::wstring type;   // "Spinner" or "Slider"
    int          ctrlID = 0;
    bool         valid  = false;
};

static WalkerInfo g_info;

// ── Find nearest static text label to the left of a control ─────
static std::wstring FindLabel(HWND hwnd) {
    HWND parent = GetParent(hwnd);
    if (!parent) return L"";

    RECT sr;
    GetWindowRect(hwnd, &sr);
    int spinMidY = (sr.top + sr.bottom) / 2;

    std::wstring best;
    int bestDist = 99999;

    HWND ch = GetWindow(parent, GW_CHILD);
    while (ch) {
        if (ch != hwnd) {
            TCHAR cls[64];
            GetClassName(ch, cls, 64);
            if (_tcsicmp(cls, _T("Static")) == 0) {
                RECT cr;
                GetWindowRect(ch, &cr);
                int midY = (cr.top + cr.bottom) / 2;
                int dy   = abs(midY - spinMidY);
                int dx   = sr.left - cr.right;

                if (dy < 18 && dx >= -30 && dx < bestDist) {
                    TCHAR text[256];
                    GetWindowText(ch, text, 256);
                    if (text[0]) {
                        best     = text;
                        bestDist = dx;
                    }
                }
            }
        }
        ch = GetWindow(ch, GW_HWNDNEXT);
    }

    while (!best.empty() && (best.back() == L':' || best.back() == L' '))
        best.pop_back();
    return best;
}

// ── Walk parents to find rollup panel title ─────────────────────
static std::wstring FindRollup(HWND hwnd) {
    HWND cur = GetParent(hwnd);
    while (cur) {
        HWND parent = GetParent(cur);
        if (!parent) break;

        TCHAR cls[64];
        GetClassName(parent, cls, 64);
        if (_tcscmp(cls, ROLLUPWINDOWCLASS) == 0) {
            IRollupWindow* irw = GetIRollup(parent);
            if (irw) {
                int idx = irw->GetPanelIndex(cur);
                if (idx >= 0) {
                    MSTR title = irw->GetPanelTitle(idx);
                    std::wstring result(title.data());
                    ReleaseIRollup(irw);
                    return result;
                }
                ReleaseIRollup(irw);
            }
        }
        cur = parent;
    }
    return L"";
}

// ── Gather info about a spinner or slider control ───────────────
static WalkerInfo GatherInfo(HWND hwnd, bool isSlider) {
    WalkerInfo info;

    float fv = 0.0f;
    int   iv = 0;

    if (isSlider) {
        ISliderControl* sl = GetISlider(hwnd);
        if (!sl) return info;
        fv = sl->GetFVal();
        iv = sl->GetIVal();
        ReleaseISlider(sl);
    } else {
        ISpinnerControl* sp = GetISpinner(hwnd);
        if (!sp) return info;
        fv = sp->GetFVal();
        iv = sp->GetIVal();
        ReleaseISpinner(sp);
    }

    info.valid  = true;
    info.ctrlID = GetDlgCtrlID(hwnd);
    info.type   = isSlider ? L"Slider" : L"Spinner";

    // Format value — show int if it looks integer, otherwise float
    if (static_cast<float>(iv) == fv) {
        info.value = std::to_wstring(iv);
    } else {
        wchar_t buf[64];
        swprintf(buf, 64, L"%.4g", fv);
        info.value = buf;
    }

    info.label  = FindLabel(hwnd);
    info.rollup = FindRollup(hwnd);
    return info;
}

// ── Find spinner sibling for a CustEdit ─────────────────────────
static HWND FindAdjacentSpinner(HWND editHwnd) {
    HWND parent = GetParent(editHwnd);
    if (!parent) return nullptr;

    RECT er;
    GetWindowRect(editHwnd, &er);

    HWND sib = GetWindow(parent, GW_CHILD);
    while (sib) {
        if (sib != editHwnd) {
            TCHAR cls[64];
            GetClassName(sib, cls, 64);
            if (_tcscmp(cls, SPINNERWINDOWCLASS) == 0) {
                RECT sr;
                GetWindowRect(sib, &sr);
                if (abs(sr.top - er.top) < 5 &&
                    sr.left >= er.right - 5 &&
                    sr.left <= er.right + 15) {
                    return sib;
                }
            }
        }
        sib = GetWindow(sib, GW_HWNDNEXT);
    }
    return nullptr;
}

// ── Overlay painting ────────────────────────────────────────────
static void PaintOverlay(HWND hwnd) {
    if (!g_info.valid) return;

    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);

    // Double-buffer
    HDC mem    = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HBITMAP old = (HBITMAP)SelectObject(mem, bmp);

    // Background
    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    // Accent bar at top
    RECT accent = { 0, 0, rc.right, 3 };
    HBRUSH accentBr = CreateSolidBrush(kTitleClr);
    FillRect(mem, &accent, accentBr);
    DeleteObject(accentBr);

    // Border
    HPEN pen = CreatePen(PS_SOLID, 1, kBorder);
    HPEN oldPen   = (HPEN)SelectObject(mem, pen);
    HBRUSH hollow = (HBRUSH)GetStockObject(NULL_BRUSH);
    SelectObject(mem, hollow);
    Rectangle(mem, 0, 0, rc.right, rc.bottom);
    SelectObject(mem, oldPen);
    DeleteObject(pen);

    SetBkMode(mem, TRANSPARENT);

    int y = kPad + 3;  // below accent bar
    int x = kPad;

    // Title: Rollup > Label
    std::wstring title;
    if (!g_info.rollup.empty() && !g_info.label.empty())
        title = g_info.rollup + L"  \x25B8  " + g_info.label;
    else if (!g_info.label.empty())
        title = g_info.label;
    else if (!g_info.rollup.empty())
        title = g_info.rollup;

    if (!title.empty()) {
        HFONT of = (HFONT)SelectObject(mem, g_fontBold);
        SetTextColor(mem, kTitleClr);
        RECT tr = { x, y, rc.right - kPad, rc.bottom };
        DrawText(mem, title.c_str(), -1, &tr, DT_LEFT | DT_SINGLELINE);
        y += kFontPx + 6;
        SelectObject(mem, of);
    }

    HFONT of = (HFONT)SelectObject(mem, g_font);

    // Value line
    {
        SetTextColor(mem, kLabelClr);
        std::wstring lbl = L"Value: ";
        RECT vr = { x, y, rc.right - kPad, rc.bottom };
        DrawText(mem, lbl.c_str(), -1, &vr, DT_LEFT | DT_SINGLELINE);

        SIZE sz;
        GetTextExtentPoint32(mem, lbl.c_str(), (int)lbl.length(), &sz);

        SetTextColor(mem, kValueClr);
        RECT vr2 = { x + sz.cx, y, rc.right - kPad, rc.bottom };
        DrawText(mem, g_info.value.c_str(), -1, &vr2, DT_LEFT | DT_SINGLELINE);
        y += kFontPx + 4;
    }

    // Type & ID line
    {
        SetTextColor(mem, kDimClr);
        std::wstring meta = g_info.type + L"  |  ID: " + std::to_wstring(g_info.ctrlID);
        RECT mr = { x, y, rc.right - kPad, rc.bottom };
        DrawText(mem, meta.c_str(), -1, &mr, DT_LEFT | DT_SINGLELINE);
    }

    SelectObject(mem, of);

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT:      PaintOverlay(hwnd); return 0;
    case WM_NCHITTEST:  return HTTRANSPARENT;
    case WM_ERASEBKGND: return 1;
    default:            return DefWindowProc(hwnd, msg, wp, lp);
    }
}

// ── Timer callback — poll mouse and detect controls ─────────────
static void CALLBACK WalkerTick(HWND, UINT, UINT_PTR, DWORD) {
    POINT pt;
    GetCursorPos(&pt);
    HWND hwnd = WindowFromPoint(pt);

    if (!hwnd) {
        if (g_lastCtrl) { ShowWindow(g_overlay, SW_HIDE); g_lastCtrl = nullptr; }
        return;
    }

    TCHAR cls[64];
    GetClassName(hwnd, cls, 64);

    // Identify the target control
    HWND   target   = nullptr;
    bool   isSlider = false;

    if (_tcscmp(cls, SPINNERWINDOWCLASS) == 0) {
        target = hwnd;
    } else if (_tcscmp(cls, SLIDERWINDOWCLASS) == 0) {
        target   = hwnd;
        isSlider = true;
    } else if (_tcscmp(cls, CUSTEDITWINDOWCLASS) == 0) {
        target = FindAdjacentSpinner(hwnd);
    }

    if (!target) {
        if (g_lastCtrl) { ShowWindow(g_overlay, SW_HIDE); g_lastCtrl = nullptr; }
        return;
    }

    // Gather info (always refresh for live value updates)
    g_info     = GatherInfo(target, isSlider);
    g_lastCtrl = target;

    if (!g_info.valid) {
        ShowWindow(g_overlay, SW_HIDE);
        return;
    }

    // ── Measure text to size overlay ────────────────────────────
    HDC hdc = GetDC(g_overlay);
    HFONT of = (HFONT)SelectObject(hdc, g_fontBold);

    std::wstring title;
    if (!g_info.rollup.empty() && !g_info.label.empty())
        title = g_info.rollup + L"  \x25B8  " + g_info.label;
    else if (!g_info.label.empty())
        title = g_info.label;
    else if (!g_info.rollup.empty())
        title = g_info.rollup;

    SIZE titleSz = {};
    if (!title.empty())
        GetTextExtentPoint32(hdc, title.c_str(), (int)title.length(), &titleSz);

    SelectObject(hdc, g_font);

    std::wstring valLine  = L"Value: " + g_info.value;
    std::wstring metaLine = g_info.type + L"  |  ID: " + std::to_wstring(g_info.ctrlID);

    SIZE valSz, metaSz;
    GetTextExtentPoint32(hdc, valLine.c_str(),  (int)valLine.length(),  &valSz);
    GetTextExtentPoint32(hdc, metaLine.c_str(), (int)metaLine.length(), &metaSz);

    SelectObject(hdc, of);
    ReleaseDC(g_overlay, hdc);

    int maxW = titleSz.cx;
    if (valSz.cx  > maxW) maxW = valSz.cx;
    if (metaSz.cx > maxW) maxW = metaSz.cx;

    int w = maxW + kPad * 2 + 4;
    int lines = (title.empty() ? 0 : 1) + 2;
    int h = kPad * 2 + 3 + lines * (kFontPx + 5);  // +3 for accent bar

    // ── Position overlay near cursor ────────────────────────────
    int ox = pt.x + 20;
    int oy = pt.y + 15;

    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(hMon, &mi);

    if (ox + w > mi.rcWork.right)   ox = pt.x - w - 10;
    if (oy + h > mi.rcWork.bottom)  oy = pt.y - h - 10;
    if (ox < mi.rcWork.left)        ox = mi.rcWork.left;
    if (oy < mi.rcWork.top)         oy = mi.rcWork.top;

    SetWindowPos(g_overlay, HWND_TOPMOST, ox, oy, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_overlay, nullptr, FALSE);
}

// ── GUP Plugin ──────────────────────────────────────────────────
class WalkerGUP : public GUP {
public:
    DWORD Start() override {
        // Register overlay window class
        WNDCLASSEX wc  = {};
        wc.cbSize      = sizeof(wc);
        wc.lpfnWndProc = OverlayProc;
        wc.hInstance    = hInstance;
        wc.lpszClassName = kOverlayClass;
        wc.hCursor      = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassEx(&wc);

        // Fonts
        g_font = CreateFont(kFontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, _T("Segoe UI"));
        g_fontBold = CreateFont(kFontPx, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, _T("Segoe UI"));

        // Hidden overlay window
        g_overlay = CreateWindowEx(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            kOverlayClass, nullptr, WS_POPUP,
            0, 0, 1, 1,
            nullptr, nullptr, hInstance, nullptr);

        // Start polling
        g_timer = SetTimer(nullptr, 0, kTimerMs, WalkerTick);

        return GUPRESULT_KEEP;
    }

    void Stop() override {
        if (g_timer)   { KillTimer(nullptr, g_timer); g_timer = 0; }
        if (g_overlay) { DestroyWindow(g_overlay);    g_overlay = nullptr; }
        if (g_font)    { DeleteObject(g_font);         g_font = nullptr; }
        if (g_fontBold){ DeleteObject(g_fontBold);     g_fontBold = nullptr; }
        UnregisterClass(kOverlayClass, hInstance);
    }

    void      DeleteThis() override { delete this; }
    DWORD_PTR Control(DWORD) override { return 0; }
};

// ── Class Descriptor ────────────────────────────────────────────
class WalkerClassDesc : public ClassDesc2 {
public:
    int          IsPublic() override             { return TRUE; }
    void*        Create(BOOL) override           { return new WalkerGUP(); }
    const TCHAR* ClassName() override            { return WALKER_NAME; }
    const TCHAR* NonLocalizedClassName() override { return WALKER_NAME; }
    SClass_ID    SuperClassID() override         { return GUP_CLASS_ID; }
    Class_ID     ClassID() override              { return WALKER_CLASS_ID; }
    const TCHAR* Category() override             { return WALKER_CATEGORY; }
    const TCHAR* InternalName() override         { return WALKER_NAME; }
    HINSTANCE    HInstance() override             { return hInstance; }
};

static WalkerClassDesc walkerDesc;

// ── DLL Boilerplate ─────────────────────────────────────────────
BOOL WINAPI DllMain(HINSTANCE hinstDLL, ULONG fdwReason, LPVOID) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        hInstance = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}

__declspec(dllexport) const TCHAR* LibDescription()      { return WALKER_NAME; }
__declspec(dllexport) int          LibNumberClasses()     { return 1; }
__declspec(dllexport) ClassDesc*   LibClassDesc(int i)    { return i == 0 ? &walkerDesc : nullptr; }
__declspec(dllexport) ULONG        LibVersion()           { return VERSION_3DSMAX; }
__declspec(dllexport) int          LibInitialize()        { return TRUE; }
__declspec(dllexport) int          LibShutdown()          { return TRUE; }
