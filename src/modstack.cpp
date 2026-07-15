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
    bool lightTheme = false;
    COLORREF bg;
    COLORREF panel;
    COLORREF panelLt;
    COLORREF accent;
    COLORREF text;
    COLORREF textDim;
    COLORREF textBrt;
    COLORREF border;
    COLORREF wsmClr;
    COLORREF macroClr;

    HBRUSH brBg     = nullptr;
    HBRUSH brPanel  = nullptr;
    HBRUSH brAccent = nullptr;
    HFONT  fontUI   = nullptr;
    HFONT  fontBold = nullptr;

    void Update(bool light)
    {
        lightTheme = light;
        if (light) {
            bg       = RGB(215, 218, 222);
            panel    = RGB(225, 228, 232);
            panelLt  = RGB(240, 242, 245);
            accent   = RGB(150, 155, 165);
            text     = RGB(30, 30, 30);
            textDim  = RGB(100, 100, 100);
            textBrt  = RGB(10, 10, 10);
            border   = RGB(140, 145, 150);
            wsmClr   = RGB(100, 80, 180);
            macroClr = RGB(180, 120, 40);
        } else {
            bg       = RGB(46, 46, 46);
            panel    = RGB(56, 56, 56);
            panelLt  = RGB(68, 68, 68);
            accent   = RGB(38, 148, 168);
            text     = RGB(220, 220, 220);
            textDim  = RGB(140, 140, 140);
            textBrt  = RGB(255, 255, 255);
            border   = RGB(42, 42, 42);
            wsmClr   = RGB(200, 180, 255);
            macroClr = RGB(255, 200, 120);
        }

        if (brBg) DeleteObject(brBg);
        if (brPanel) DeleteObject(brPanel);
        if (brAccent) DeleteObject(brAccent);

        brBg     = CreateSolidBrush(bg);
        brPanel  = CreateSolidBrush(panel);
        brAccent = CreateSolidBrush(accent);
    }

    void Init(bool light)
    {
        Update(light);
        if (!fontUI) {
            fontUI   = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            fontBold = CreateFontW(-15, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        }
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
    SClass_ID superClassId = 0;
    Class_ID classId = Class_ID(0, 0);
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

std::wstring MxsStringLiteral(const std::wstring& s)
{
    std::wstring out = L"\"";
    for (wchar_t c : s)
    {
        if (c == L'\\' || c == L'"') out += L'\\';
        out += c;
    }
    out += L"\"";
    return out;
}

bool AddModifierViaModPanelScript(const std::wstring& primaryName,
                                  const std::wstring& fallbackName = L"")
{
    if (primaryName.empty() && fallbackName.empty()) return false;

    std::wstring script =
        L"try("
        L"local names = #(" + MxsStringLiteral(primaryName) + L"," + MxsStringLiteral(fallbackName) + L");"
        L"local m = undefined;"
        L"for n in names while m == undefined do if n != \"\" do try(m = execute (n + \"()\"))catch();"
        L"if m == undefined then \"0\" else "
        L"(undo \"Add Modifier\" on ("
        L"local before=0;for o in selection do try(before+=o.modifiers.count)catch();"
        L"max modify mode;try(subObjectLevel = 0)catch();"
        L"try(if modPanel.validModifier m then modPanel.addModToSelection m)catch();"
        L"local mid=0;for o in selection do try(mid+=o.modifiers.count)catch();"
        L"if mid <= before do (for o in selection do try(addModifier o m)catch());"
        L"local after=0;for o in selection do try(after+=o.modifiers.count)catch();"
        L"if after > before then \"1\" else \"0\"))"
        L")catch(\"0\")";
    FPValue result;
    BOOL ok = ExecuteMAXScriptScript(script.c_str(), MAXScript::ScriptSource::Dynamic,
        TRUE, &result);
    return ok && result.type == TYPE_STRING && result.s && wcscmp(result.s, L"1") == 0;
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

    bool Init(bool light)
    {
        if (inited_) return true;
        Theme::Init(light);

        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc   = WndProc;
        wc.hbrBackground = nullptr; // WM_ERASEBKGND paints with the live theme brush
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hInstance      = hInstance;
        wc.lpszClassName  = kWndClass;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            Theme::Shutdown();
            return false;
        }
        inited_ = true;
        return true;
    }

    void Shutdown()
    {
        CancelPendingRebuild();
        RestoreAccelerators();
        if (wnd_) { DestroyWindow(wnd_); wnd_ = nullptr; }
        edit_ = list_ = status_ = nullptr;
        cache_.clear();
        filtered_.clear();
        cacheReady_ = rebuildPending_ = false;
        UnregisterClassW(kWndClass, hInstance);
        Theme::Shutdown();
        inited_ = false;
    }

    void Toggle()
    {
        if (!EnsureWindow()) return;
        if (IsWindowVisible(wnd_)) Hide(); else Show();
    }

    bool IsOpen() const { return wnd_ && IsWindowVisible(wnd_); }

    void ReloadTheme(bool light)
    {
        Theme::Update(light);
        if (!wnd_) return;
        if (list_) {
            HMODULE hUx = LoadLibraryW(L"uxtheme.dll");
            if (hUx) {
                typedef HRESULT(WINAPI* SWTF)(HWND, LPCWSTR, LPCWSTR);
                auto swt = reinterpret_cast<SWTF>(GetProcAddress(hUx, "SetWindowTheme"));
                if (swt) swt(list_, Theme::lightTheme ? L"Explorer" : L"DarkMode_Explorer", nullptr);
                FreeLibrary(hUx);
            }
        }
        RedrawWindow(wnd_, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }

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
            HBRUSH oldB = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, 0, 0, rc.right, rc.bottom);
            SelectObject(hdc, oldB);
            SelectObject(hdc, oldP); DeleteObject(bp);

            SetBkMode(hdc, TRANSPARENT);
            HFONT oldF = (HFONT)SelectObject(hdc, Theme::fontBold);
            SetTextColor(hdc, Theme::accent);
            constexpr wchar_t title[] = L"Modifier Stack";
            TextOutW(hdc, 10, 10, title, static_cast<int>(_countof(title) - 1));

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

    static LRESULT CALLBACK ListProc(HWND h, UINT m, WPARAM w, LPARAM l,
                                     UINT_PTR, DWORD_PTR ref)
    {
        auto* self = reinterpret_cast<Palette*>(ref);
        if (m == WM_MBUTTONDOWN)
        {
            LRESULT hit = SendMessageW(h, LB_ITEMFROMPOINT, 0, l);
            if (!HIWORD(hit)) {
                int sel = LOWORD(hit);
                if (sel >= 0 && sel < static_cast<int>(self->filtered_.size())) {
                    int si = self->filtered_[sel];
                    if (si >= 0 && si < static_cast<int>(self->cache_.size())) {
                        const ModItem& item = self->cache_[si];
                        if (item.kind == ModKind::Macro) {
                            self->SetStatus(L"Cannot quick-access macros yet");
                        } else {
                            extern void TogglePowerParamsQuickModifier(const wchar_t*, const wchar_t*);
                            std::wstring internalName = self->ResolveConstructorName(item);
                            TogglePowerParamsQuickModifier(internalName.c_str(), item.label.c_str());
                            self->SetStatus(L"Quick Access toggled: " + item.label);
                            InvalidateRect(h, nullptr, FALSE);
                        }
                    }
                }
            }
            return 0;
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
        SetWindowSubclass(list_, ListProc, 1, reinterpret_cast<DWORD_PTR>(this));

        status_ = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            pad, cr.bottom - 22, cw, 18, h, nullptr, hInstance, nullptr);
        SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(Theme::fontUI), TRUE);

        HMODULE hUx = LoadLibraryW(L"uxtheme.dll");
        if (hUx) {
            typedef HRESULT(WINAPI* SWTF)(HWND, LPCWSTR, LPCWSTR);
            auto swt = reinterpret_cast<SWTF>(GetProcAddress(hUx, "SetWindowTheme"));
            if (swt) swt(list_, Theme::lightTheme ? L"Explorer" : L"DarkMode_Explorer", nullptr);
            FreeLibrary(hUx);
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

        extern bool IsPowerParamsQuickModifier(const wchar_t*);
        std::wstring iname = item.internalName.empty() ? item.label : item.internalName;
        bool isQuickAccess = (item.kind != ModKind::Macro) && IsPowerParamsQuickModifier(iname.c_str());

        std::wstring drawLabel = item.label;
        if (isQuickAccess) {
            drawLabel += L"  \u2605";
            if (!sel) tc = RGB(220, 180, 50); // Gold color for star items
        }

        // Left: name
        RECT lr = dis->rcItem;
        lr.left += 8;
        lr.right -= catSz.cx + 12;
        SetTextColor(dis->hDC, tc);
        DrawTextW(dis->hDC, drawLabel.c_str(), -1, &lr,
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
        RECT wa{};
        MONITORINFO mi{ sizeof(mi) };
        HMONITOR monitor = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
        if (monitor && GetMonitorInfoW(monitor, &mi)) wa = mi.rcWork;
        else SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        int x = std::clamp(static_cast<long>(p.x - kWindowWidth / 2),
                           wa.left, wa.right - static_cast<long>(kWindowWidth));
        int y = std::clamp(static_cast<long>(p.y - 36),
                           wa.top, wa.bottom - static_cast<long>(kWindowHeight));
        SetLayeredWindowAttributes(wnd_, 0, 0, LWA_ALPHA);
        SetWindowPos(wnd_, HWND_TOPMOST, x, y, kWindowWidth, kWindowHeight,
            SWP_NOACTIVATE);
        ShowWindow(wnd_, SW_SHOW);
        SetLayeredWindowAttributes(wnd_, 0, 255, LWA_ALPHA);
        SetActiveWindow(wnd_);
        SetForegroundWindow(wnd_);
        SetFocus(edit_);
        SendMessageW(edit_, EM_SETSEL, 0, -1);
        if (!acceleratorsDisabled_) {
            DisableAccelerators();
            acceleratorsDisabled_ = true;
        }

        // Build cache and populate list after window is visible
        EnsureCache();
        Rebuild();
    }

    void Hide()
    {
        CancelPendingRebuild();
        ShowWindow(wnd_, SW_HIDE);
        SetLayeredWindowAttributes(wnd_, 0, 255, LWA_ALPHA);
        RestoreAccelerators();
    }

    void RestoreAccelerators()
    {
        if (!acceleratorsDisabled_) return;
        EnableAccelerators();
        acceleratorsDisabled_ = false;
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
            const Class_ID classId = ce.ClassID();
            if (!seen.insert({classId.PartA(), classId.PartB()}).second)
                continue;

            // ClassEntry exposes display metadata without force-loading deferred
            // third-party DLLs. The selected entry is resolved on activation.
            MSTR nonLocalizedStr = ce.NonLocalizedClassName();
            MSTR classNameStr    = ce.ClassName();
            MSTR categoryStr     = ce.Category();
            const MCHAR* nonLocalized = nonLocalizedStr.data();
            const MCHAR* className    = classNameStr.data();
            const MCHAR* catName      = categoryStr.data();

            MSTR internalNameStr;
            if (ce.IsLoaded()) {
                if (ClassDesc* cd = ce.CD()) {
                    const MCHAR* intName = cd->InternalName();
                    if (intName && intName[0]) internalNameStr = intName;
                }
            }
            const MCHAR* intName = internalNameStr.data();
            if (!intName || !intName[0]) intName = nonLocalized;

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
            item.superClassId = sid;
            item.classId      = classId;
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
            std::stable_sort(scored.begin(), scored.end(),
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
    std::wstring ResolveConstructorName(const ModItem& item)
    {
        std::wstring ctorName = item.internalName;
        if (ClassEntry* ce = ClassDirectory::GetInstance().FindClassEntry(
                item.superClassId, item.classId)) {
            if (ClassDesc* cd = ce->FullCD()) {
                const MCHAR* internalName = cd->InternalName();
                if (internalName && internalName[0]) ctorName = internalName;
            }
        }
        return ctorName.empty() ? item.label : ctorName;
    }

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

        // Modifier — prefer Max's command-panel route so stack context and
        // modifier gizmos are initialized the same way as the native UI.
        // Resolve/load only the class the user chose. Deferred entries do not
        // always expose their MAXScript constructor through cached display data.
        std::wstring ctorName = ResolveConstructorName(item);
        bool added = AddModifierViaModPanelScript(ctorName, item.label);

        if (ip) ip->RedrawViews(ip->GetTime());
        SetStatus((added ? L"Added: " : L"Could not add: ") + item.label);
        Hide();
    }

    void DeleteTopModifier()
    {
        Interface* ip = GetCOREInterface();
        if (!ip || ip->GetSelNodeCount() == 0) { SetStatus(L"No selection."); return; }

        FPValue result;
        BOOL ok = ExecuteMAXScriptScript(
            _T("try(undo \"Remove Top Modifiers\" on (local n=0;for o in selection do try(if o.modifiers.count>0 do(deleteModifier o o.modifiers[1];n+=1))catch();n as string))catch(\"0\")"),
            MAXScript::ScriptSource::Dynamic, TRUE, &result);
        const int removed = (ok && result.type == TYPE_STRING && result.s)
            ? _wtoi(result.s) : 0;

        if (removed > 0) {
            ip->RedrawViews(ip->GetTime());
            SetStatus(L"Removed top modifier from " + std::to_wstring(removed) +
                (removed == 1 ? L" object." : L" objects."));
        } else {
            SetStatus(L"Could not remove modifier.");
        }
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
    bool  acceleratorsDisabled_ = false;
    std::vector<ModItem> cache_;
    std::vector<int> filtered_;
};

} // anonymous namespace

// ── Exported API ────────────────────────────────────────────────
bool Init(HINSTANCE, bool lightTheme)  { return Palette::Get().Init(lightTheme); }
void Shutdown()       { Palette::Get().Shutdown(); }
void Toggle()         { Palette::Get().Toggle(); }
bool IsOpen()         { return Palette::Get().IsOpen(); }
void ReloadTheme(bool lightTheme) { Palette::Get().ReloadTheme(lightTheme); }

} // namespace ModStack
