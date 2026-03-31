#include "modstack.h"
#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <max.h>
#include <gup.h>
#include <maxapi.h>
#include <plugin.h>
#include <inode.h>
#include <modstack.h>
#include <object.h>
#include <GetCOREInterface.h>
#include <maxscript/maxscript.h>
#include <imacroscript.h>
#include <hold.h>
#include <custcont.h>

extern HINSTANCE hInstance;

namespace ModStack {
namespace {

// ═════════════════════════════════════════════════════════════════
//  Theme (matches PowerParams/PowerShader)
// ═════════════════════════════════════════════════════════════════
namespace Theme
{
    constexpr COLORREF bg       = RGB(215, 218, 222);
    constexpr COLORREF panel    = RGB(225, 228, 232);
    constexpr COLORREF panelLt  = RGB(240, 242, 245);
    constexpr COLORREF accent   = RGB(150, 155, 165);
    constexpr COLORREF text     = RGB(30, 30, 30);
    constexpr COLORREF textDim  = RGB(100, 100, 100);
    constexpr COLORREF textBrt  = RGB(10, 10, 10);
    constexpr COLORREF border   = RGB(140, 145, 150);
    constexpr COLORREF wsmClr   = RGB(100, 80, 180);
    constexpr COLORREF macroClr = RGB(180, 120, 40);

    HBRUSH brBg     = nullptr;
    HBRUSH brPanel  = nullptr;
    HBRUSH brAccent = nullptr;
    HFONT  fontUI   = nullptr;
    HFONT  fontBold = nullptr;

    void Init()
    {
        if (brBg) return;
        brBg     = CreateSolidBrush(bg);
        brPanel  = CreateSolidBrush(panel);
        brAccent = CreateSolidBrush(accent);
        fontUI   = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        fontBold = CreateFontW(-15, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }
    void Shutdown()
    {
        auto del = [](HGDIOBJ& h) { if (h) { DeleteObject(h); h = nullptr; } };
        del(reinterpret_cast<HGDIOBJ&>(brBg));
        del(reinterpret_cast<HGDIOBJ&>(brPanel));
        del(reinterpret_cast<HGDIOBJ&>(brAccent));
        del(reinterpret_cast<HGDIOBJ&>(fontUI));
        del(reinterpret_cast<HGDIOBJ&>(fontBold));
    }
}

// ═════════════════════════════════════════════════════════════════
//  Constants
// ═════════════════════════════════════════════════════════════════
constexpr wchar_t kWndClass[]  = L"ModStackPaletteWnd";
constexpr int kSearchId  = 2001;
constexpr int kListId    = 2002;
constexpr int kWindowWidth  = 340;
constexpr int kWindowHeight = 420;
constexpr int kHeaderH      = 34;
constexpr UINT_PTR kSearchTimerId = 1;
constexpr UINT kSearchDebounceMs = 14;

// ═════════════════════════════════════════════════════════════════
//  Data
// ═════════════════════════════════════════════════════════════════
enum class ModKind { OSM, WSM, Macro };

struct ModItem
{
    std::wstring label;
    std::wstring normLabel;
    std::wstring search;
    std::wstring category;
    std::wstring internalName;
    ModKind kind = ModKind::OSM;
    ClassDesc* classDesc = nullptr;
    MacroID macroId = -1; // for macroscripts
};

// ═════════════════════════════════════════════════════════════════
//  Search helpers (shared logic with PowerShader)
// ═════════════════════════════════════════════════════════════════
std::wstring Normalize(const std::wstring& s, bool spaces)
{
    std::wstring out;
    out.reserve(s.size());
    bool lastSpace = true;
    for (wchar_t ch : s)
    {
        wchar_t c = static_cast<wchar_t>(towlower(ch));
        if (iswalnum(c)) { out.push_back(c); lastSpace = false; }
        else if (spaces && !lastSpace) { out.push_back(L' '); lastSpace = true; }
    }
    while (!out.empty() && out.back() == L' ') out.pop_back();
    return out;
}

std::vector<std::wstring> TokenizeQuery(const std::wstring& query)
{
    std::vector<std::wstring> tokens;
    size_t start = 0;
    while (start < query.size())
    {
        while (start < query.size() && query[start] == L' ') ++start;
        if (start >= query.size()) break;
        size_t end = query.find(L' ', start);
        if (end == std::wstring::npos) end = query.size();
        tokens.emplace_back(query.substr(start, end - start));
        start = end + 1;
    }
    return tokens;
}

int ScoreMatch(const std::wstring& search, const std::wstring& normLabel,
               const std::vector<std::wstring>& tokens)
{
    if (tokens.empty()) return 1;
    int score = 100;
    for (const std::wstring& tok : tokens)
    {
        size_t pos = search.find(tok);
        if (pos == std::wstring::npos) return 0;
        if (pos == 0 || search[pos - 1] == L' ') score += 10;
    }
    if (normLabel.find(tokens[0]) == 0) score += 50;
    score += std::max(0, 40 - static_cast<int>(normLabel.size()));
    return score;
}

// ═════════════════════════════════════════════════════════════════
//  Palette
// ═════════════════════════════════════════════════════════════════
class Palette
{
public:
    static Palette& Get() { static Palette p; return p; }

    bool Init()
    {
        if (inited_) return true;
        Theme::Init();

        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc   = WndProc;
        wc.hbrBackground = Theme::brBg;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hInstance      = hInstance;
        wc.lpszClassName  = kWndClass;
        RegisterClassExW(&wc);
        inited_ = true;
        return true;
    }

    void Shutdown()
    {
        CancelPendingRebuild();
        if (wnd_) { DestroyWindow(wnd_); wnd_ = nullptr; }
        Theme::Shutdown();
    }

    void Toggle()
    {
        if (!EnsureWindow()) return;
        if (IsWindowVisible(wnd_)) Hide(); else Show();
    }

    bool IsOpen() const { return wnd_ && IsWindowVisible(wnd_); }

private:
    // ─── Window proc ────────────────────────────────────────────
    static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        auto* self = reinterpret_cast<Palette*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE)
        {
            self = static_cast<Palette*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            return TRUE;
        }
        if (!self) return DefWindowProcW(h, m, w, l);

        switch (m)
        {
        case WM_CREATE:  self->OnCreate(h); return 0;
        case WM_COMMAND: self->OnCommand(LOWORD(w), HIWORD(w)); return 0;
        case WM_TIMER:   self->OnTimer(static_cast<UINT_PTR>(w)); return 0;
        case WM_CLOSE:   self->Hide(); return 0;

        case WM_ACTIVATE:
            if (LOWORD(w) == WA_INACTIVE) self->Hide();
            return 0;

        // ─── Custom header + border ─────────────────────────────
        case WM_ERASEBKGND:
        {
            HDC hdc = reinterpret_cast<HDC>(w);
            RECT rc; GetClientRect(h, &rc);
            FillRect(hdc, &rc, Theme::brBg);
            HPEN bp = CreatePen(PS_SOLID, 1, Theme::border);
            HPEN oldP = (HPEN)SelectObject(hdc, bp);
            SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, 0, 0, rc.right, rc.bottom);
            SelectObject(hdc, oldP); DeleteObject(bp);

            SetBkMode(hdc, TRANSPARENT);
            HFONT oldF = (HFONT)SelectObject(hdc, Theme::fontBold);
            SetTextColor(hdc, Theme::accent);
            TextOutW(hdc, 10, 10, L"Mod Stack", 9);

            if (self->hoverClose_) {
                HBRUSH hov = CreateSolidBrush(RGB(200, 60, 60));
                FillRect(hdc, &self->closeRect_, hov); DeleteObject(hov);
            }
            SetTextColor(hdc, Theme::text);
            DrawTextW(hdc, L"\u00D7", 1, &self->closeRect_,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            HPEN sep = CreatePen(PS_SOLID, 1, Theme::border);
            HPEN oldS = (HPEN)SelectObject(hdc, sep);
            MoveToEx(hdc, 8, kHeaderH - 4, nullptr);
            LineTo(hdc, rc.right - 8, kHeaderH - 4);
            SelectObject(hdc, oldS); DeleteObject(sep);
            SelectObject(hdc, oldF);
            return 1;
        }

        case WM_NCHITTEST:
        {
            POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
            ScreenToClient(h, &pt);
            if (PtInRect(&self->closeRect_, pt)) return HTCLIENT;
            if (pt.y < kHeaderH) return HTCAPTION;
            return HTCLIENT;
        }

        case WM_LBUTTONDOWN:
        {
            POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
            if (PtInRect(&self->closeRect_, pt)) { self->Hide(); return 0; }
            break;
        }

        case WM_MOUSEMOVE:
        {
            POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
            bool hover = PtInRect(&self->closeRect_, pt) != 0;
            if (hover != self->hoverClose_) {
                self->hoverClose_ = hover;
                InvalidateRect(h, &self->closeRect_, TRUE);
            }
            if (!self->trackingMouse_) {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, h, 0 };
                TrackMouseEvent(&tme);
                self->trackingMouse_ = true;
            }
            break;
        }

        case WM_MOUSELEAVE:
            if (self->hoverClose_) {
                self->hoverClose_ = false;
                InvalidateRect(h, &self->closeRect_, TRUE);
            }
            self->trackingMouse_ = false;
            break;

        case WM_CTLCOLOREDIT:
        {
            HDC hdc = reinterpret_cast<HDC>(w);
            SetTextColor(hdc, Theme::textBrt);
            SetBkColor(hdc, Theme::panel);
            return reinterpret_cast<LRESULT>(Theme::brPanel);
        }
        case WM_CTLCOLORSTATIC:
        {
            HDC hdc = reinterpret_cast<HDC>(w);
            SetTextColor(hdc, Theme::textDim);
            SetBkColor(hdc, Theme::bg);
            return reinterpret_cast<LRESULT>(Theme::brBg);
        }
        case WM_CTLCOLORLISTBOX:
        {
            HDC hdc = reinterpret_cast<HDC>(w);
            SetTextColor(hdc, Theme::text);
            SetBkColor(hdc, Theme::bg);
            return reinterpret_cast<LRESULT>(Theme::brBg);
        }

        case WM_MEASUREITEM:
        {
            auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(l);
            mis->itemHeight = 24;
            return TRUE;
        }
        case WM_DRAWITEM:
        {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(l);
            if (dis->CtlID == kListId)
                { self->DrawListItem(dis); return TRUE; }
            break;
        }
        }
        return DefWindowProcW(h, m, w, l);
    }

    static LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM w, LPARAM l,
                                     UINT_PTR, DWORD_PTR ref)
    {
        auto* self = reinterpret_cast<Palette*>(ref);
        if (m == WM_KEYDOWN)
        {
            if (w == VK_RETURN)  { self->ApplyCurrent(); return 0; }
            if (w == VK_DOWN)    { self->MoveSelection(1); return 0; }
            if (w == VK_UP)      { self->MoveSelection(-1); return 0; }
            if (w == VK_ESCAPE)  { self->Hide(); return 0; }
            if (w == VK_DELETE)  { self->DeleteTopModifier(); return 0; }
        }
        return DefSubclassProc(h, m, w, l);
    }

    // ─── Window management ──────────────────────────────────────
    bool EnsureWindow()
    {
        if (wnd_) return true;
        wnd_ = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED, kWndClass,
            nullptr, WS_POPUP,
            CW_USEDEFAULT, CW_USEDEFAULT, kWindowWidth, kWindowHeight,
            GetCOREInterface() ? GetCOREInterface()->GetMAXHWnd() : nullptr,
            nullptr, hInstance, this);
        return wnd_ != nullptr;
    }

    // ─── UI creation ────────────────────────────────────────────
    void OnCreate(HWND h)
    {
        wnd_ = h;
        const int pad = 8;
        RECT cr; GetClientRect(h, &cr);
        const int cw = cr.right - 2 * pad;
        closeRect_ = { cr.right - pad - 18, pad, cr.right - pad, pad + 18 };
        int y = kHeaderH;

        edit_ = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            pad, y, cw, 24, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSearchId)),
            hInstance, nullptr);
        SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(Theme::fontBold), TRUE);
        SendMessageW(edit_, EM_SETCUEBANNER, TRUE,
            reinterpret_cast<LPARAM>(L"Search modifiers...  (DEL = remove top)"));
        SetWindowSubclass(edit_, EditProc, 1, reinterpret_cast<DWORD_PTR>(this));
        y += 28;

        int listH = cr.bottom - y - 26;
        list_ = CreateWindowExW(0, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY |
            LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_NODATA,
            pad, y, cw, listH, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kListId)),
            hInstance, nullptr);

        status_ = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            pad, cr.bottom - 22, cw, 18, h, nullptr, hInstance, nullptr);
        SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(Theme::fontUI), TRUE);

        HMODULE hUx = LoadLibraryW(L"uxtheme.dll");
        if (hUx) {
            typedef HRESULT(WINAPI* SWTF)(HWND, LPCWSTR, LPCWSTR);
            auto swt = reinterpret_cast<SWTF>(GetProcAddress(hUx, "SetWindowTheme"));
            if (swt) swt(list_, L"DarkMode_Explorer", nullptr);
        }
    }

    // ─── Drawing ────────────────────────────────────────────────
    void DrawListItem(DRAWITEMSTRUCT* dis)
    {
        if (dis->itemID == static_cast<UINT>(-1)) return;
        int fi = static_cast<int>(dis->itemID);
        if (fi < 0 || fi >= static_cast<int>(filtered_.size())) return;
        int si = filtered_[static_cast<size_t>(fi)];
        if (si < 0 || si >= static_cast<int>(cache_.size())) return;
        const ModItem& item = cache_[static_cast<size_t>(si)];

        bool sel = (dis->itemState & ODS_SELECTED) != 0;
        FillRect(dis->hDC, &dis->rcItem, sel ? Theme::brAccent : Theme::brBg);

        COLORREF tc = sel ? Theme::textBrt : Theme::text;
        if (!sel && item.kind == ModKind::WSM)   tc = Theme::wsmClr;
        if (!sel && item.kind == ModKind::Macro) tc = Theme::macroClr;

        SetBkMode(dis->hDC, TRANSPARENT);
        HFONT oldF = (HFONT)SelectObject(dis->hDC, Theme::fontUI);

        // Right: category
        if (!item.category.empty()) {
            RECT rr = dis->rcItem;
            rr.right -= 6;
            SetTextColor(dis->hDC, sel ? Theme::textBrt : Theme::textDim);
            DrawTextW(dis->hDC, item.category.c_str(), -1, &rr,
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }

        SIZE catSz{};
        if (!item.category.empty())
            GetTextExtentPoint32W(dis->hDC, item.category.c_str(),
                static_cast<int>(item.category.size()), &catSz);

        // Left: name
        RECT lr = dis->rcItem;
        lr.left += 8;
        lr.right -= catSz.cx + 12;
        SetTextColor(dis->hDC, tc);
        DrawTextW(dis->hDC, item.label.c_str(), -1, &lr,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        SelectObject(dis->hDC, oldF);

        if (!sel) {
            HPEN pen = CreatePen(PS_SOLID, 1, Theme::border);
            HPEN oldP = (HPEN)SelectObject(dis->hDC, pen);
            MoveToEx(dis->hDC, dis->rcItem.left, dis->rcItem.bottom - 1, nullptr);
            LineTo(dis->hDC, dis->rcItem.right, dis->rcItem.bottom - 1);
            SelectObject(dis->hDC, oldP);
            DeleteObject(pen);
        }
    }

    // ─── Show / Hide ────────────────────────────────────────────
    void Show()
    {
        CancelPendingRebuild();
        SetWindowTextW(edit_, L"");

        // Show window immediately so it appears before cache build
        POINT p{}; GetCursorPos(&p);
        RECT wa{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        int x = std::clamp(static_cast<long>(p.x - kWindowWidth / 2),
                           wa.left, wa.right - static_cast<long>(kWindowWidth));
        int y = std::clamp(static_cast<long>(p.y - 36),
                           wa.top, wa.bottom - static_cast<long>(kWindowHeight));
        SetLayeredWindowAttributes(wnd_, 0, 0, LWA_ALPHA);
        SetWindowPos(wnd_, HWND_TOPMOST, x, y, kWindowWidth, kWindowHeight, 0);
        ShowWindow(wnd_, SW_SHOW);
        // Fade in — cubic ease-out, 80ms
        DWORD t0 = GetTickCount();
        for (;;) {
            float t = (float)(GetTickCount() - t0) / 80.0f;
            if (t >= 1.0f) { SetLayeredWindowAttributes(wnd_, 0, 255, LWA_ALPHA); break; }
            BYTE a = (BYTE)(255.0f * (1.0f - (1.0f-t)*(1.0f-t)*(1.0f-t)));
            SetLayeredWindowAttributes(wnd_, 0, a, LWA_ALPHA);
            MSG msg; while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        }
        SetActiveWindow(wnd_);
        SetForegroundWindow(wnd_);
        SetFocus(edit_);
        SendMessageW(edit_, EM_SETSEL, 0, -1);
        DisableAccelerators();

        // Build cache and populate list after window is visible
        EnsureCache();
        Rebuild();
    }

    void Hide()
    {
        CancelPendingRebuild();
        // Fade out — 50ms
        if (IsWindowVisible(wnd_)) {
            DWORD t0 = GetTickCount();
            for (;;) {
                float t = (float)(GetTickCount() - t0) / 50.0f;
                if (t >= 1.0f) break;
                BYTE a = (BYTE)(255.0f * (1.0f-t)*(1.0f-t)*(1.0f-t));
                SetLayeredWindowAttributes(wnd_, 0, a, LWA_ALPHA);
                MSG msg; while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
            }
        }
        ShowWindow(wnd_, SW_HIDE);
        SetLayeredWindowAttributes(wnd_, 0, 255, LWA_ALPHA);
        EnableAccelerators();
    }

    // ─── Commands ───────────────────────────────────────────────
    void OnCommand(int id, int code)
    {
        if (id == kSearchId && code == EN_CHANGE) { ScheduleRebuild(); return; }
        if (id == kListId && code == LBN_DBLCLK) { ApplyCurrent(); return; }
    }

    void OnTimer(UINT_PTR id)
    {
        if (id != kSearchTimerId) return;
        CancelPendingRebuild();
        Rebuild();
    }

    void ScheduleRebuild()
    {
        if (!wnd_ || rebuildPending_) return;
        rebuildPending_ = true;
        SetTimer(wnd_, kSearchTimerId, kSearchDebounceMs, nullptr);
    }

    void CancelPendingRebuild()
    {
        if (!wnd_ || !rebuildPending_) return;
        KillTimer(wnd_, kSearchTimerId);
        rebuildPending_ = false;
    }

    // ─── Cache ──────────────────────────────────────────────────
    void EnsureCache()
    {
        if (cacheReady_) return;
        cache_.clear();
        cache_.reserve(512);
        ScanClasses(OSM_CLASS_ID, ModKind::OSM);
        ScanClasses(WSM_CLASS_ID, ModKind::WSM);
        ScanMacros();
        std::sort(cache_.begin(), cache_.end(),
            [](const ModItem& a, const ModItem& b) { return a.label < b.label; });
        cacheReady_ = true;
    }

    void ScanClasses(SClass_ID sid, ModKind kind)
    {
        SubClassList* list = ClassDirectory::GetInstance().GetClassList(sid);
        if (!list) return;
        using CIDPair = std::pair<ULONG, ULONG>;
        std::set<CIDPair> seen;

        for (int i = list->GetFirst(ACC_PUBLIC); i != -1;
             i = list->GetNext(ACC_PUBLIC))
        {
            ClassEntry& ce = (*list)[i];
            // Use CD() instead of FullCD() to avoid triggering DLL loads.
            // Returns null for plugins not yet loaded — those are rarely needed.
            ClassDesc* cd = ce.CD();
            if (!cd) continue;

            const Class_ID classId = ce.ClassID();
            if (!seen.insert({classId.PartA(), classId.PartB()}).second)
                continue;

            const MCHAR* nonLocalized = cd->NonLocalizedClassName();
            const MCHAR* className    = cd->ClassName();
            const MCHAR* intName      = cd->InternalName();
            const MCHAR* catName      = cd->Category();

            std::wstring name = (nonLocalized && nonLocalized[0])
                ? std::wstring(nonLocalized)
                : std::wstring(className ? className : L"");
            if (name.empty() && intName && intName[0])
                name = std::wstring(intName);
            std::wstring key = Normalize(name, false);
            if (key.empty()) continue;

            ModItem item;
            item.label      = name;
            item.normLabel  = Normalize(name, true);
            item.search     = Normalize(
                name + L" " +
                std::wstring(className ? className : L"") + L" " +
                std::wstring(intName ? intName : L"") + L" " +
                std::wstring(catName ? catName : L""), true);
            item.category     = (catName && catName[0]) ? std::wstring(catName) : L"";
            item.internalName = (intName && intName[0]) ? std::wstring(intName) : L"";
            item.kind         = kind;
            item.classDesc    = cd;
            cache_.push_back(std::move(item));
        }
    }

    void ScanMacros()
    {
        MacroDir& dir = GetMacroScriptDir();
        int count = dir.Count();
        std::set<std::wstring> seen;
        for (int i = 0; i < count; i++) {
            MacroEntry* me = dir.GetMacro(i);
            if (!me) continue;
            MSTR name = me->GetName();
            MSTR cat  = me->GetCategory();
            MSTR btn  = me->GetButtonText();
            // Use buttonText if available, else name
            std::wstring label = (btn.data() && btn.data()[0]) ? std::wstring(btn.data())
                               : (name.data() && name.data()[0]) ? std::wstring(name.data()) : L"";
            if (label.empty()) continue;
            // Skip duplicates (same name+category)
            std::wstring dedupKey = label + L"|" + (cat.data() ? cat.data() : L"");
            if (!seen.insert(dedupKey).second) continue;

            ModItem item;
            item.label      = label;
            item.normLabel  = Normalize(label, true);
            item.search     = Normalize(
                label + L" " +
                (name.data() ? name.data() : L"") + L" " +
                (cat.data() ? cat.data() : L""), true);
            item.category   = (cat.data() && cat.data()[0]) ? std::wstring(cat.data()) : L"";
            item.kind       = ModKind::Macro;
            item.macroId    = me->GetID();
            cache_.push_back(std::move(item));
        }
    }

    // ─── List rebuild ───────────────────────────────────────────
    void Rebuild()
    {
        int len = GetWindowTextLengthW(edit_);
        std::wstring raw(static_cast<size_t>(len + 1), L'\0');
        GetWindowTextW(edit_, raw.data(), len + 1);
        raw.resize(static_cast<size_t>(len));
        std::wstring q = Normalize(raw, true);
        std::vector<std::wstring> tokens = TokenizeQuery(q);

        struct Scored { int idx; int score; };
        std::vector<Scored> scored;
        scored.reserve(cache_.size());

        for (size_t i = 0; i < cache_.size(); ++i)
        {
            int s = ScoreMatch(cache_[i].search, cache_[i].normLabel, tokens);
            if (s > 0) scored.push_back({static_cast<int>(i), s});
        }

        if (!tokens.empty())
            std::sort(scored.begin(), scored.end(),
                [](const Scored& a, const Scored& b) { return a.score > b.score; });

        filtered_.clear();
        filtered_.reserve(scored.size());
        for (const auto& s : scored) filtered_.push_back(s.idx);

        SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
        SendMessageW(list_, LB_RESETCONTENT, 0, 0);
        SendMessageW(list_, LB_SETCOUNT, static_cast<WPARAM>(filtered_.size()), 0);
        if (!filtered_.empty()) SendMessageW(list_, LB_SETCURSEL, 0, 0);
        SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(list_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);

        SetStatus(std::to_wstring(filtered_.size()) + L" modifiers" +
            (q.empty() ? L"" : (L"  |  " + q)));
    }

    // ─── Actions ────────────────────────────────────────────────
    void ApplyCurrent()
    {
        if (rebuildPending_) { CancelPendingRebuild(); Rebuild(); }

        int sel = static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0));
        if (sel < 0 || sel >= static_cast<int>(filtered_.size())) return;
        int si = filtered_[static_cast<size_t>(sel)];
        if (si < 0 || si >= static_cast<int>(cache_.size())) return;
        const ModItem& item = cache_[static_cast<size_t>(si)];

        Interface* ip = GetCOREInterface();

        if (item.kind == ModKind::Macro) {
            // Macroscript — execute it
            MacroDir& dir = GetMacroScriptDir();
            if (dir.ValidID(item.macroId))
                dir.Execute(item.macroId);
            SetStatus(L"Run: " + item.label);
            Hide();
            return;
        }

        if (!ip || ip->GetSelNodeCount() == 0) { SetStatus(L"No selection."); return; }

        // Modifier — add via C++ SDK
        if (item.classDesc) {
            void* obj = item.classDesc->Create(FALSE);
            Modifier* mod = static_cast<Modifier*>(obj);
            if (mod) {
                theHold.Begin();
                GetCOREInterface7()->AddModifier(*ip->GetSelNode(0), *mod);
                theHold.Accept(_T("Add Modifier"));
            }
        }

        if (ip) ip->RedrawViews(ip->GetTime());
        SetStatus(L"Added: " + item.label);
        Hide();
    }

    void DeleteTopModifier()
    {
        Interface* ip = GetCOREInterface();
        if (!ip || ip->GetSelNodeCount() == 0) { SetStatus(L"No selection."); return; }
        INode* node = ip->GetSelNode(0);
        if (!node) return;

        Object* obj = node->GetObjectRef();
        if (!obj || obj->SuperClassID() != GEN_DERIVOB_CLASS_ID)
        {
            SetStatus(L"No modifiers.");
            return;
        }

        IDerivedObject* dobj = static_cast<IDerivedObject*>(obj);
        if (dobj->NumModifiers() == 0) { SetStatus(L"No modifiers."); return; }

        Modifier* top = dobj->GetModifier(0);
        MSTR cn;
        if (top) top->GetClassName(cn, false);

        ExecuteMAXScriptScript(
            _T("deleteModifier $ $.modifiers[1]"),
            MAXScript::ScriptSource::Dynamic);

        ip->RedrawViews(ip->GetTime());
        SetStatus(L"Removed: " + std::wstring(cn.data() ? cn.data() : L"modifier"));
    }

    // ─── Helpers ────────────────────────────────────────────────
    void MoveSelection(int delta)
    {
        int count = static_cast<int>(SendMessageW(list_, LB_GETCOUNT, 0, 0));
        if (count <= 0) return;
        int cur = static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0));
        if (cur == LB_ERR) cur = 0;
        else cur = std::clamp(cur + delta, 0, count - 1);
        SendMessageW(list_, LB_SETCURSEL, cur, 0);
    }

    void SetStatus(const std::wstring& s)
    {
        if (status_) SetWindowTextW(status_, s.c_str());
    }

    // ─── State ──────────────────────────────────────────────────
    bool  inited_ = false;
    HWND  wnd_    = nullptr;
    HWND  edit_   = nullptr;
    HWND  list_   = nullptr;
    HWND  status_ = nullptr;
    RECT  closeRect_ = {};
    bool  hoverClose_ = false;
    bool  trackingMouse_ = false;
    bool  rebuildPending_ = false;
    bool  cacheReady_ = false;
    std::vector<ModItem> cache_;
    std::vector<int> filtered_;
};

} // anonymous namespace

// ── Exported API ────────────────────────────────────────────────
void Init(HINSTANCE)  { Palette::Get().Init(); }
void Shutdown()       { Palette::Get().Shutdown(); }
void Toggle()         { Palette::Get().Toggle(); }
bool IsOpen()         { return Palette::Get().IsOpen(); }

} // namespace ModStack
