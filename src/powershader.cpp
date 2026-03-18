#include "powershader.h"
#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <max.h>
#include <gup.h>
#include <maxapi.h>
#include <plugin.h>
#include <inode.h>
#include <imtledit.h>
#include <GetCOREInterface.h>
#include <maxscript/maxscript.h>
#include <custcont.h>
#include <Materials/MtlBase.h>
#include <Materials/MtlLib.h>

#define POWER_SHADER_NAME _T("Power Shader")

extern HINSTANCE hInstance;

namespace PowerShader {
namespace {

constexpr wchar_t kSmeNodeViewClass[] = L"DragDropWindow";

bool IsWindowClass(HWND hwnd, const wchar_t* className)
{
    if (!hwnd || !className) return false;
    wchar_t cls[128] = {};
    int len = GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls)));
    return len > 0 && _wcsicmp(cls, className) == 0;
}

HWND FindSmeNodeViewWindowAtPoint(const POINT& screenPos)
{
    HWND h = WindowFromPoint(screenPos);
    while (h)
    {
        if (IsWindowClass(h, kSmeNodeViewClass)) return h;
        HWND parent = GetParent(h);
        if (!parent || parent == h) break;
        h = parent;
    }
    return nullptr;
}

bool TryDropMtlBaseToSmeUnderCursor(MtlBase* mb, bool* outHoveringSme = nullptr)
{
    if (outHoveringSme) *outHoveringSme = false;
    if (!mb) return false;

    POINT screenPos{};
    if (!GetCursorPos(&screenPos)) return false;
    HWND viewHwnd = FindSmeNodeViewWindowAtPoint(screenPos);
    if (!viewHwnd) return false;
    if (outHoveringSme) *outHoveringSme = true;

    IDADWindow* dadWindow = GetIDADWindow(viewHwnd);
    if (!dadWindow) return false;
    DADMgr* dadMgr = dadWindow->GetDADMgr();
    if (!dadMgr)
    {
        ReleaseIDADWindow(dadWindow);
        return false;
    }

    POINT clientPos = screenPos;
    if (!ScreenToClient(viewHwnd, &clientPos))
    {
        ReleaseIDADWindow(dadWindow);
        return false;
    }

    ReferenceTarget* dropThis = static_cast<ReferenceTarget*>(mb);
    const SClass_ID type = mb->SuperClassID();
    const BOOL canDrop = dadMgr->OkToDrop(dropThis, nullptr, viewHwnd, clientPos, type, FALSE);
    if (canDrop)
        dadMgr->Drop(dropThis, viewHwnd, clientPos, type, nullptr, FALSE);

    ReleaseIDADWindow(dadWindow);
    return canDrop == TRUE;
}

// ═══════════════════════════════════════════════════════════════
//  Dark Theme
// ═══════════════════════════════════════════════════════════════
namespace Theme
{
    constexpr COLORREF bg       = RGB(46, 46, 46);
    constexpr COLORREF panel    = RGB(56, 56, 56);
    constexpr COLORREF panelLt  = RGB(68, 68, 68);
    constexpr COLORREF panelHov = RGB(80, 80, 80);
    constexpr COLORREF accent   = RGB(38, 148, 168);
    constexpr COLORREF text     = RGB(220, 220, 220);
    constexpr COLORREF textDim  = RGB(140, 140, 140);
    constexpr COLORREF textBrt  = RGB(255, 255, 255);
    constexpr COLORREF border   = RGB(42, 42, 42);
    constexpr COLORREF mapClr   = RGB(180, 200, 255);
    constexpr COLORREF sceneClr = RGB(100, 200, 100);

    HBRUSH brBg      = nullptr;
    HBRUSH brPanel   = nullptr;
    HBRUSH brPanelLt = nullptr;
    HBRUSH brAccent  = nullptr;
    HFONT  fontUI    = nullptr;
    HFONT  fontBold  = nullptr;

    void Init()
    {
        if (brBg) return;
        brBg      = CreateSolidBrush(bg);
        brPanel   = CreateSolidBrush(panel);
        brPanelLt = CreateSolidBrush(panelLt);
        brAccent  = CreateSolidBrush(accent);
        fontUI    = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        fontBold  = CreateFontW(-15, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }
    void Shutdown()
    {
        auto del = [](HGDIOBJ& h) { if (h) { DeleteObject(h); h = nullptr; } };
        del(reinterpret_cast<HGDIOBJ&>(brBg));
        del(reinterpret_cast<HGDIOBJ&>(brPanel));
        del(reinterpret_cast<HGDIOBJ&>(brPanelLt));
        del(reinterpret_cast<HGDIOBJ&>(brAccent));
        del(reinterpret_cast<HGDIOBJ&>(fontUI));
        del(reinterpret_cast<HGDIOBJ&>(fontBold));
    }
}

// ═══════════════════════════════════════════════════════════════
//  Constants
// ═══════════════════════════════════════════════════════════════
constexpr UINT kHotkeyId = 0x514A;
constexpr wchar_t kHotkeyClass[] = L"PowerShaderHotkeyWnd";
constexpr wchar_t kPaletteClass[] = L"PowerShaderPaletteWnd";
constexpr int kSearchId  = 1001;
constexpr int kListId    = 1002;
constexpr int kApplyId   = 1003;
constexpr int kSceneId   = 1004;
constexpr int kTabAllId  = 1005;
constexpr int kTabMatId  = 1006;
constexpr int kTabMapId  = 1007;
constexpr int kQuickBase = 1100;
constexpr int kQuickCount = 11;
constexpr int kWindowWidth  = 380;
constexpr int kWindowHeight = 560;
constexpr UINT_PTR kSearchTimerId = 1;
constexpr UINT kSearchDebounceMs = 14;

enum class TabMode { All, Materials, Maps };
enum class ItemKind { ClassMaterial, ClassMap, SceneMaterial };

struct Item
{
    std::wstring label;
    std::wstring search;
    std::wstring key;
    std::wstring scriptName;
    std::wstring scriptKey;
    std::wstring display;
    ItemKind kind = ItemKind::ClassMaterial;
    ClassDesc* classDesc = nullptr;
    MtlBase* live = nullptr;
};

struct QuickButton
{
    const wchar_t* label;
    const wchar_t* alias;
};

const QuickButton kQuickButtons[] = {
    {L"ARND", L"ai_standard_surface"},
    {L"PHYS", L"physicalmaterial"},
    {L"USD",  L"maxusdpreviewsurface"},
    {L"PBR",  L"OpenPBR_Material"},
    {L"GLTF", L"glTFMaterial"},
    {L"OSL",  L"OSL_uberBitmap2b"},
    {L"BMP",  L"bitmaptexture"},
    {L"RS",   L"RS_Texture"},
    {L"TY",   L"tybitmap"},
    {L"NOISE",L"osl_UberNoise"},
    {L"GRAD", L"Gradient_Ramp"},
};

// ═══════════════════════════════════════════════════════════════
//  Search helpers
// ═══════════════════════════════════════════════════════════════
std::wstring Normalize(const std::wstring& s, bool spaces)
{
    std::wstring out;
    out.reserve(s.size());
    bool lastSpace = true;
    for (wchar_t ch : s)
    {
        wchar_t c = static_cast<wchar_t>(towlower(ch));
        if (iswalnum(c))
        {
            out.push_back(c);
            lastSpace = false;
        }
        else if (spaces && !lastSpace)
        {
            out.push_back(L' ');
            lastSpace = true;
        }
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

bool MatchOrderedTokens(const std::wstring& haystack,
                        const std::vector<std::wstring>& tokens)
{
    size_t cursor = 0;
    for (const std::wstring& token : tokens)
    {
        cursor = haystack.find(token, cursor);
        if (cursor == std::wstring::npos) return false;
        cursor += token.size();
    }
    return true;
}

bool StartsWith(const std::wstring& value, const std::wstring& prefix)
{
    return value.size() >= prefix.size()
        && value.compare(0, prefix.size(), prefix) == 0;
}

const wchar_t* TagForKind(ItemKind kind)
{
    switch (kind)
    {
    case ItemKind::SceneMaterial: return L" [SCENE]";
    case ItemKind::ClassMap:      return L" [MAP]";
    default:                      return L" [MAT]";
    }
}

// ═══════════════════════════════════════════════════════════════
//  Thin MaxScript bridges (no C++ SDK for these)
// ═══════════════════════════════════════════════════════════════
// SME node creation at deterministic position: selected node + offset,
// else last node + offset, else [0,0].
static const wchar_t* kSmeAtSpawnScript =
    L"("
    L"if not SME.isOpen() do try(SME.Open())catch();"
    L"if SME.isOpen() do ("
    L"local v=sme.getView sme.activeView;"
    L"try("
    L"local p=[0,0];"
    L"try("
    L"local sn=v.GetSelectedNodes();"
    L"if sn!=undefined and sn.count>0 then("
    L"p=sn[1].position+[120,40]"
    L")else if v.GetNumNodes()>0 do("
    L"local ln=v.GetNode (v.GetNumNodes());"
    L"if ln!=undefined do p=ln.position+[120,40]"
    L")"
    L")catch();"
    L"local n=v.CreateNode meditMaterials[activeMeditSlot] p;"
    L"v.SelectNone();try(n.selected=true)catch();"
    L")catch()"
    L")"
    L")";

// Drag: viewport ray-hit first, SME fallback at cursor position.
static const wchar_t* kDragScript =
    L"("
    L"local m=meditMaterials[activeMeditSlot];"
    L"local dropped=false;"
    L"try("
    L"local r=mapScreenToWorldRay mouse.pos;"
    L"local hits=intersectRayScene r;"
    L"local nd=undefined;local best=1e9;"
    L"for h in hits do(local d=distance r.pos h[2].pos;"
    L"if d<best do(best=d;nd=h[1]));"
    L"if nd!=undefined and superclassof m==material do"
    L"(nd.material=m;dropped=true)"
    L")catch()"
    L")";

// ═══════════════════════════════════════════════════════════════
//  DWM dark title bar
// ═══════════════════════════════════════════════════════════════
void EnableDarkTitleBar(HWND hwnd)
{
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (!hDwm) return;
    typedef HRESULT(WINAPI* FN)(HWND, DWORD, LPCVOID, DWORD);
    auto fn = reinterpret_cast<FN>(GetProcAddress(hDwm, "DwmSetWindowAttribute"));
    if (fn) { BOOL v = TRUE; fn(hwnd, 20, &v, sizeof(v)); }
}

// ═══════════════════════════════════════════════════════════════
//  Palette
// ═══════════════════════════════════════════════════════════════
class Palette
{
public:
    static Palette& Get() { static Palette p; return p; }

    bool Init()
    {
        if (hotkeyWnd_) return true;
        Theme::Init();
        INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
        InitCommonControlsEx(&icc);

        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc   = HotkeyProc;
        wc.hInstance      = hInstance;
        wc.lpszClassName  = kHotkeyClass;
        RegisterClassExW(&wc);

        wc.lpfnWndProc   = PaletteProc;
        wc.hbrBackground = Theme::brBg;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = kPaletteClass;
        RegisterClassExW(&wc);

        hotkeyWnd_ = CreateWindowExW(0, kHotkeyClass, L"", 0, 0, 0, 0, 0,
            HWND_MESSAGE, nullptr, hInstance, this);
        if (!hotkeyWnd_) return false;
        EnsureClassCache();
        return true;
    }

    void Shutdown()
    {
        CancelPendingRebuild();
        if (hotkeyWnd_) { DestroyWindow(hotkeyWnd_); hotkeyWnd_ = nullptr; }
        if (wnd_) { DestroyWindow(wnd_); wnd_ = nullptr; }
        Theme::Shutdown();
    }

    void Toggle()
    {
        if (!EnsureWindow()) return;
        if (IsWindowVisible(wnd_)) Hide(); else Show();
    }

private:
    // ─── Window procedures ──────────────────────────────────────
    static LRESULT CALLBACK HotkeyProc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        auto* self = reinterpret_cast<Palette*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE)
        {
            self = static_cast<Palette*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            return TRUE;
        }
        if (self && m == WM_HOTKEY && w == kHotkeyId) { self->Toggle(); return 0; }
        return DefWindowProcW(h, m, w, l);
    }

    static LRESULT CALLBACK PaletteProc(HWND h, UINT m, WPARAM w, LPARAM l)
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
        case WM_CREATE:
            self->OnCreate(h);
            return 0;

        case WM_COMMAND:
            self->OnCommand(LOWORD(w), HIWORD(w));
            return 0;

        case WM_TIMER:
            self->OnTimer(static_cast<UINT_PTR>(w));
            return 0;

        case WM_ACTIVATE:
            if (LOWORD(w) == WA_INACTIVE && !self->dragging_) self->Hide();
            return 0;

        case WM_CLOSE:
            self->Hide();
            return 0;

        // ─── Dark theme color handlers ──────────────────────────
        case WM_ERASEBKGND:
        {
            RECT rc; GetClientRect(h, &rc);
            FillRect(reinterpret_cast<HDC>(w), &rc, Theme::brBg);
            return 1;
        }
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
        case WM_CTLCOLORBTN:
        {
            HDC hdc = reinterpret_cast<HDC>(w);
            SetTextColor(hdc, Theme::text);
            SetBkColor(hdc, Theme::bg);
            return reinterpret_cast<LRESULT>(Theme::brBg);
        }

        // ─── Owner-draw handlers ────────────────────────────────
        case WM_MEASUREITEM:
        {
            auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(l);
            mis->itemHeight = 26;
            return TRUE;
        }
        case WM_DRAWITEM:
        {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(l);
            if (dis->CtlID == kListId)
                { self->DrawListItem(dis); return TRUE; }
            if (dis->CtlID >= kQuickBase && dis->CtlID < kQuickBase + kQuickCount)
                { self->DrawButton(dis); return TRUE; }
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
            if (w == VK_RETURN) { self->ActivateCurrent(false); return 0; }
            if (w == VK_DOWN)   { self->MoveSelection(1); return 0; }
            if (w == VK_UP)     { self->MoveSelection(-1); return 0; }
            if (w == VK_ESCAPE) { self->Hide(); return 0; }
        }
        return DefSubclassProc(h, m, w, l);
    }

    static LRESULT CALLBACK ListProc(HWND h, UINT m, WPARAM w, LPARAM l,
                                     UINT_PTR, DWORD_PTR ref)
    {
        auto* self = reinterpret_cast<Palette*>(ref);
        switch (m)
        {
        case WM_LBUTTONDOWN:
        {
            LRESULT hit = SendMessageW(h, LB_ITEMFROMPOINT, 0, l);
            if (HIWORD(hit) == 0)
            {
                self->dragIndex_ = LOWORD(hit);
                self->dragging_ = false;
                GetCursorPos(&self->dragStart_);
                SetCapture(h);
            }
            break;
        }
        case WM_MOUSEMOVE:
            if (GetCapture() == h && (w & MK_LBUTTON))
            {
                POINT p{}; GetCursorPos(&p);
                if (std::abs(p.x - self->dragStart_.x) > 6 ||
                    std::abs(p.y - self->dragStart_.y) > 6)
                    self->dragging_ = true;
            }
            break;
        case WM_LBUTTONUP:
            if (GetCapture() == h) ReleaseCapture();
            if (self->dragging_)
            {
                POINT p{}; GetCursorPos(&p);
                HWND under = WindowFromPoint(p);
                if (!under || (under != self->wnd_ && !IsChild(self->wnd_, under)))
                    self->ActivateByIndex(self->dragIndex_, true);
            }
            self->dragging_ = false;
            self->dragIndex_ = -1;
            break;
        }
        return DefSubclassProc(h, m, w, l);
    }

    // ─── Window management ──────────────────────────────────────
    bool EnsureWindow()
    {
        if (wnd_) return true;
        wnd_ = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kPaletteClass,
            L"Power Shader r4  |  Ctrl+Shift+P",
            WS_POPUP | WS_CAPTION | WS_SYSMENU,
            CW_USEDEFAULT, CW_USEDEFAULT, kWindowWidth, kWindowHeight,
            GetCOREInterface() ? GetCOREInterface()->GetMAXHWnd() : nullptr,
            nullptr, hInstance, this);
        if (wnd_) EnableDarkTitleBar(wnd_);
        return wnd_ != nullptr;
    }

    // ─── UI creation ────────────────────────────────────────────
    void OnCreate(HWND h)
    {
        wnd_ = h;
        const int pad = 8;
        RECT cr; GetClientRect(h, &cr);
        const int cw = cr.right - 2 * pad;
        int y = pad;

        // Search box
        edit_ = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            pad, y, cw, 28, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSearchId)),
            hInstance, nullptr);
        SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(Theme::fontBold), TRUE);
        SendMessageW(edit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search shaders..."));
        SetWindowSubclass(edit_, EditProc, 1, reinterpret_cast<DWORD_PTR>(this));
        y += 36;

        // Tab radio buttons
        int tabW = cw / 3;
        HWND tabAll = CreateWindowExW(0, L"BUTTON", L"All",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
            pad, y, tabW, 22, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTabAllId)), hInstance, nullptr);
        HWND tabMat = CreateWindowExW(0, L"BUTTON", L"Materials",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            pad + tabW, y, tabW, 22, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTabMatId)), hInstance, nullptr);
        HWND tabMap = CreateWindowExW(0, L"BUTTON", L"Maps",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            pad + tabW * 2, y, tabW, 22, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTabMapId)), hInstance, nullptr);
        CheckRadioButton(h, kTabAllId, kTabMapId, kTabAllId);
        y += 30;

        // Quick buttons (owner-drawn)
        int x = pad;
        int btnW = 50, btnH = 26, gap = 4;
        for (int i = 0; i < static_cast<int>(std::size(kQuickButtons)); ++i)
        {
            std::wstring lbl = kQuickButtons[i].label;
            int bw = (lbl == L"NOISE" || lbl == L"GRAD") ? 60 : btnW;
            if (x + bw > pad + cw) { x = pad; y += btnH + gap; }
            CreateWindowExW(0, L"BUTTON", kQuickButtons[i].label,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                x, y, bw, btnH, h,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kQuickBase + i)),
                hInstance, nullptr);
            x += bw + gap;
        }
        y += btnH + 8;

        // Checkboxes
        apply_ = CreateWindowExW(0, L"BUTTON", L"Apply to selection",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            pad, y, 140, 20, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kApplyId)), hInstance, nullptr);
        scene_ = CreateWindowExW(0, L"BUTTON", L"Scene items",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            pad + 148, y, 110, 20, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSceneId)), hInstance, nullptr);
        y += 28;

        // Results list (owner-drawn)
        int listH = cr.bottom - y - 32;
        list_ = CreateWindowExW(0, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY |
            LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
            pad, y, cw, listH, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kListId)), hInstance, nullptr);
        SetWindowSubclass(list_, ListProc, 1, reinterpret_cast<DWORD_PTR>(this));

        // Status bar
        status_ = CreateWindowExW(0, L"STATIC", L"Ready r4",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            pad, cr.bottom - 24, cw, 20, h, nullptr, hInstance, nullptr);

        // Fonts + dark theme on themed controls
        for (HWND c : {tabAll, tabMat, tabMap, apply_, scene_, status_})
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(Theme::fontUI), TRUE);
        // Dark mode theme on controls (Win10 1903+)
        HMODULE hUx = LoadLibraryW(L"uxtheme.dll");
        if (hUx)
        {
            typedef HRESULT(WINAPI* SWTF)(HWND, LPCWSTR, LPCWSTR);
            auto swt = reinterpret_cast<SWTF>(GetProcAddress(hUx, "SetWindowTheme"));
            if (swt)
                for (HWND c : {tabAll, tabMat, tabMap, apply_, scene_, list_})
                    swt(c, L"DarkMode_Explorer", nullptr);
        }
    }

    // ─── Drawing ────────────────────────────────────────────────
    void DrawListItem(DRAWITEMSTRUCT* dis)
    {
        if (dis->itemID == static_cast<UINT>(-1)) return;

        bool sel = (dis->itemState & ODS_SELECTED) != 0;
        FillRect(dis->hDC, &dis->rcItem, sel ? Theme::brAccent : Theme::brBg);

        wchar_t buf[256] = {};
        SendMessageW(dis->hwndItem, LB_GETTEXT, dis->itemID, reinterpret_cast<LPARAM>(buf));

        COLORREF tc = sel ? Theme::textBrt : Theme::text;
        if (!sel)
        {
            const LRESULT kindRaw = SendMessageW(dis->hwndItem, LB_GETITEMDATA, dis->itemID, 0);
            const ItemKind kind = static_cast<ItemKind>(kindRaw);
            if (kind == ItemKind::ClassMap) tc = Theme::mapClr;
            if (kind == ItemKind::SceneMaterial) tc = Theme::sceneClr;
        }

        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, tc);
        HFONT old = static_cast<HFONT>(SelectObject(dis->hDC, Theme::fontUI));
        RECT rc = dis->rcItem;
        rc.left += 8;
        DrawTextW(dis->hDC, buf, -1, &rc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dis->hDC, old);

        if (!sel)
        {
            HPEN pen = CreatePen(PS_SOLID, 1, Theme::border);
            HPEN oldP = static_cast<HPEN>(SelectObject(dis->hDC, pen));
            MoveToEx(dis->hDC, dis->rcItem.left, dis->rcItem.bottom - 1, nullptr);
            LineTo(dis->hDC, dis->rcItem.right, dis->rcItem.bottom - 1);
            SelectObject(dis->hDC, oldP);
            DeleteObject(pen);
        }
    }

    void DrawButton(DRAWITEMSTRUCT* dis)
    {
        bool pressed = (dis->itemState & ODS_SELECTED) != 0;

        COLORREF bgc = pressed ? Theme::accent : Theme::panelLt;
        HBRUSH br = CreateSolidBrush(bgc);
        FillRect(dis->hDC, &dis->rcItem, br);
        DeleteObject(br);

        HPEN pen = CreatePen(PS_SOLID, 1, Theme::border);
        HPEN oldP = static_cast<HPEN>(SelectObject(dis->hDC, pen));
        HBRUSH oldBr = static_cast<HBRUSH>(
            SelectObject(dis->hDC, GetStockObject(NULL_BRUSH)));
        Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top,
                  dis->rcItem.right, dis->rcItem.bottom);
        SelectObject(dis->hDC, oldBr);
        SelectObject(dis->hDC, oldP);
        DeleteObject(pen);

        wchar_t buf[32] = {};
        GetWindowTextW(dis->hwndItem, buf, 32);
        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, pressed ? Theme::textBrt : Theme::text);
        HFONT old = static_cast<HFONT>(SelectObject(dis->hDC, Theme::fontUI));
        DrawTextW(dis->hDC, buf, -1, &dis->rcItem,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dis->hDC, old);
    }

    // ─── Show / Hide ────────────────────────────────────────────
    void Show()
    {
        CancelPendingRebuild();
        EnsureClassCache();
        if (IsSceneOnly()) RefreshSceneCache();
        Rebuild(true);
        POINT p{}; GetCursorPos(&p);
        RECT wa{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        int x = std::clamp(static_cast<long>(p.x - kWindowWidth / 2),
                           wa.left, wa.right - static_cast<long>(kWindowWidth));
        int y = std::clamp(static_cast<long>(p.y - 36),
                           wa.top, wa.bottom - static_cast<long>(kWindowHeight));
        ShowWindow(wnd_, SW_SHOW);
        SetWindowPos(wnd_, HWND_TOPMOST, x, y, kWindowWidth, kWindowHeight, 0);
        SetActiveWindow(wnd_);
        SetForegroundWindow(wnd_);
        SetFocus(edit_);
        SendMessageW(edit_, EM_SETSEL, 0, -1);
        // Stop Max from stealing keyboard input (M, S, P etc. are Max shortcuts)
        DisableAccelerators();
    }

    void Hide()
    {
        CancelPendingRebuild();
        ShowWindow(wnd_, SW_HIDE);
        dragging_ = false;
        dragIndex_ = -1;
        // Restore Max keyboard shortcuts
        EnableAccelerators();
    }

    // ─── Commands ───────────────────────────────────────────────
    void OnCommand(int id, int code)
    {
        if (id == kSearchId && code == EN_CHANGE) { ScheduleRebuild(); return; }
        if (id == kSceneId)
        {
            CancelPendingRebuild();
            if (IsSceneOnly()) RefreshSceneCache();
            Rebuild(true);
            return;
        }
        if (id == kTabAllId) { CancelPendingRebuild(); tab_ = TabMode::All;       Rebuild(true); return; }
        if (id == kTabMatId) { CancelPendingRebuild(); tab_ = TabMode::Materials; Rebuild(true); return; }
        if (id == kTabMapId) { CancelPendingRebuild(); tab_ = TabMode::Maps;      Rebuild(true); return; }
        if (id == kListId && code == LBN_DBLCLK)
            { ActivateCurrent(false); return; }
        if (id >= kQuickBase && id < kQuickBase + kQuickCount)
            { ActivateAlias(kQuickButtons[id - kQuickBase].alias); return; }
    }

    void OnTimer(UINT_PTR id)
    {
        if (id != kSearchTimerId) return;
        CancelPendingRebuild();
        Rebuild(false);
    }

    // ─── Data scanning ─────────────────────────────────────────
    bool IsSceneOnly() const
    {
        return SendMessageW(scene_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    std::wstring ReadNormalizedQuery() const
    {
        int len = GetWindowTextLengthW(edit_);
        std::wstring q(static_cast<size_t>(len + 1), L'\0');
        GetWindowTextW(edit_, q.data(), len + 1);
        q.resize(static_cast<size_t>(len));
        return Normalize(q, true);
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

    void EnsureClassCache()
    {
        if (classCacheReady_) return;

        classItems_.clear();
        classItems_.reserve(1024);
        AddClassList(MATERIAL_CLASS_ID, ItemKind::ClassMaterial, classItems_);
        AddClassList(TEXMAP_CLASS_ID, ItemKind::ClassMap, classItems_);
        std::sort(classItems_.begin(), classItems_.end(),
            [](const Item& a, const Item& b) { return a.label < b.label; });

        classCacheReady_ = true;
    }

    void RefreshSceneCache()
    {
        sceneItems_.clear();
        Interface* ip = GetCOREInterface();
        if (ip && ip->GetSceneMtls())
        {
            MtlBaseLib* lib = ip->GetSceneMtls();
            sceneItems_.reserve(lib->Count());
            for (int i = 0; i < lib->Count(); ++i)
            {
                MtlBase* m = (*lib)[i];
                if (!m) continue;
                Item item;
                MSTR className = m->ClassName();
                item.label = m->GetName().Length()
                    ? std::wstring(m->GetName().data())
                    : std::wstring(className.data());
                item.search = Normalize(
                    item.label + L" " + std::wstring(className.data()), true);
                item.key = Normalize(item.label, false);
                item.kind = ItemKind::SceneMaterial;
                item.display = item.label + TagForKind(item.kind);
                item.live = m;
                sceneItems_.push_back(std::move(item));
            }
        }
        std::sort(sceneItems_.begin(), sceneItems_.end(),
            [](const Item& a, const Item& b) { return a.label < b.label; });
        sceneCacheReady_ = true;
    }

    void AddClassList(SClass_ID sid, ItemKind kind, std::vector<Item>& out)
    {
        SubClassList* list = ClassDirectory::GetInstance().GetClassList(sid);
        if (!list) return;
        std::vector<Class_ID> seenClassIds;
        seenClassIds.reserve(static_cast<size_t>(list->Count(ACC_ALL)));

        for (int i = list->GetFirst(ACC_ALL); i != -1;
             i = list->GetNext(ACC_ALL))
        {
            ClassEntry& ce = (*list)[i];
            ClassDesc* cd = ce.FullCD();
            if (!cd) continue;

            const Class_ID classId = ce.ClassID();
            bool seen = false;
            for (const Class_ID& id : seenClassIds)
            {
                if (id == classId) { seen = true; break; }
            }
            if (seen) continue;

            const MCHAR* nonLocalized = cd->NonLocalizedClassName();
            const MCHAR* className    = cd->ClassName();
            const MCHAR* internalName = cd->InternalName();
            const MCHAR* categoryName = cd->Category();
            std::wstring name = (nonLocalized && nonLocalized[0])
                ? std::wstring(nonLocalized)
                : std::wstring(className ? className : L"");
            if (name.empty() && internalName && internalName[0])
                name = std::wstring(internalName);
            std::wstring key = Normalize(name, false);
            if (key.empty()) continue;
            Item item;
            item.label      = name;
            item.search     = Normalize(
                name + L" " +
                std::wstring(className ? className : L"") + L" " +
                std::wstring(internalName ? internalName : L"") + L" " +
                std::wstring(categoryName ? categoryName : L""), true);
            item.key        = key;
            item.scriptName = (internalName && internalName[0])
                ? std::wstring(internalName) : L"";
            item.scriptKey  = Normalize(item.scriptName, false);
            item.kind       = kind;
            item.display    = item.label + TagForKind(item.kind);
            item.classDesc  = cd;
            seenClassIds.push_back(classId);
            out.push_back(std::move(item));
        }
    }

    // ─── List rebuild ───────────────────────────────────────────
    void Rebuild(bool forceFull)
    {
        const bool sceneOnly = IsSceneOnly();
        EnsureClassCache();
        if (sceneOnly && !sceneCacheReady_) RefreshSceneCache();

        const std::vector<Item>& source = sceneOnly ? sceneItems_ : classItems_;
        activeItems_ = &source;

        const std::wstring q = ReadNormalizedQuery();
        const std::vector<std::wstring> tokens = TokenizeQuery(q);

        const bool sameMode = (sceneOnly == lastSceneOnly_) &&
                              (sceneOnly || tab_ == lastTab_);
        const bool canIncremental = !forceFull &&
            sameMode &&
            !lastQuery_.empty() &&
            q.size() > lastQuery_.size() &&
            StartsWith(q, lastQuery_);

        std::vector<int> nextFiltered;
        nextFiltered.reserve(canIncremental ? filtered_.size() : source.size());

        auto passes = [&](const Item& item) -> bool
        {
            bool isScene = item.kind == ItemKind::SceneMaterial;
            if (sceneOnly != isScene) return false;
            if (!sceneOnly)
            {
                if (tab_ == TabMode::Materials &&
                    item.kind != ItemKind::ClassMaterial) return false;
                if (tab_ == TabMode::Maps &&
                    item.kind != ItemKind::ClassMap) return false;
            }
            return tokens.empty() || MatchOrderedTokens(item.search, tokens);
        };

        if (canIncremental)
        {
            for (int idx : filtered_)
            {
                if (idx < 0 || idx >= static_cast<int>(source.size())) continue;
                if (passes(source[static_cast<size_t>(idx)]))
                    nextFiltered.push_back(idx);
            }
        }
        else
        {
            for (size_t i = 0; i < source.size(); ++i)
                if (passes(source[i])) nextFiltered.push_back(static_cast<int>(i));
        }

        SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
        SendMessageW(list_, LB_RESETCONTENT, 0, 0);
        size_t chars = 0;
        for (int idx : nextFiltered)
            chars += source[static_cast<size_t>(idx)].display.size() + 1;
        SendMessageW(list_, LB_INITSTORAGE,
            static_cast<WPARAM>(nextFiltered.size()),
            static_cast<LPARAM>(chars * sizeof(wchar_t)));
        for (int idx : nextFiltered)
        {
            const Item& item = source[static_cast<size_t>(idx)];
            LRESULT row = SendMessageW(list_, LB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(item.display.c_str()));
            if (row >= 0)
                SendMessageW(list_, LB_SETITEMDATA, static_cast<WPARAM>(row),
                    static_cast<LPARAM>(item.kind));
        }
        if (!nextFiltered.empty()) SendMessageW(list_, LB_SETCURSEL, 0, 0);
        SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(list_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);

        filtered_.swap(nextFiltered);
        lastQuery_ = q;
        lastTab_ = tab_;
        lastSceneOnly_ = sceneOnly;

        SetStatus(std::to_wstring(filtered_.size()) + L" items" +
            (q.empty() ? L"" : (L"  |  " + q)));
    }

    // ─── Activation (C++ API core) ──────────────────────────────
    void ActivateAlias(const std::wstring& alias)
    {
        EnsureClassCache();
        std::wstring key = Normalize(alias, false);
        for (const Item& item : classItems_)
            if (!item.live &&
                (item.key == key || item.scriptKey == key))
                { Activate(item, false); return; }

        // Retry once after forcing a fresh cache build.
        if (!forcedAliasRetry_)
        {
            forcedAliasRetry_ = true;
            classCacheReady_ = false;
            EnsureClassCache();
            for (const Item& item : classItems_)
                if (!item.live &&
                    (item.key == key || item.scriptKey == key))
                    { Activate(item, false); return; }
        }

        SetStatus(L"Class not available.");
    }

    void ActivateCurrent(bool drag)
    {
        if (rebuildPending_)
        {
            CancelPendingRebuild();
            Rebuild(false);
        }
        ActivateByIndex(
            static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0)), drag);
    }

    void ActivateByIndex(int idx, bool drag)
    {
        if (!activeItems_) return;
        if (idx < 0 || idx >= static_cast<int>(filtered_.size())) return;
        int sourceIdx = filtered_[static_cast<size_t>(idx)];
        if (sourceIdx < 0 || sourceIdx >= static_cast<int>(activeItems_->size())) return;
        Activate((*activeItems_)[static_cast<size_t>(sourceIdx)], drag);
    }

    void Activate(const Item& item, bool drag)
    {
        Interface* ip = GetCOREInterface();
        if (!ip) { SetStatus(L"No interface."); return; }

        // Drag-drop should feel instant: hide palette before heavy work.
        const bool hiddenEarly = drag;
        if (hiddenEarly) Hide();

        // ── C++ API: Create or reuse material instance ──────────
        MtlBase* mb = item.live;
        if (!mb && item.classDesc)
            mb = static_cast<MtlBase*>(item.classDesc->Create(FALSE));
        if (!mb) { SetStatus(L"Create failed."); return; }

        // ── C++ API: Name new instances ─────────────────────────
        if (!item.live)
            mb->SetName(MSTR((item.label + L"_" +
                std::to_wstring((GetTickCount() % 9000) + 1000)).c_str()));

        const bool applyChecked =
            SendMessageW(apply_, BM_GETCHECK, 0, 0) == BST_CHECKED;

        // ── C++ API: Apply to selected scene nodes ──────────────
        if (applyChecked && mb->SuperClassID() == MATERIAL_CLASS_ID)
        {
            Mtl* mtl = static_cast<Mtl*>(mb);
            for (int i = 0; i < ip->GetSelNodeCount(); ++i)
                if (INode* n = ip->GetSelNode(i)) n->SetMtl(mtl);
        }

        // ── C++ API: Get active medit slot for optional placement ─
        int slot = 0;
        if (IMtlEditInterface* me = GetMtlEditInterface())
            slot = std::max(0, me->GetActiveMtlSlot());

        // ── SME / viewport placement ────────────────────────────
        // Node creation in SME is handled through native DragDropWindow DAD.
        // Viewport assignment still uses MaxScript ray-hit path.
        bool activationFailed = false;
        if (drag)
        {
            bool hoveringSme = false;
            const bool droppedIntoSme = TryDropMtlBaseToSmeUnderCursor(mb, &hoveringSme);
            if (!droppedIntoSme)
            {
                if (!hoveringSme)
                {
                    ip->PutMtlToMtlEditor(mb, slot);
                    ExecuteMAXScriptScript(kDragScript, MAXScript::ScriptSource::Dynamic);
                }
                else
                {
                    // Avoid wrong-coordinate fallback if we're over SME but DAD failed.
                    activationFailed = true;
                }
            }
        }
        else
        {
            ip->PutMtlToMtlEditor(mb, slot);
            ExecuteMAXScriptScript(kSmeAtSpawnScript, MAXScript::ScriptSource::Dynamic);
        }

        // Scene material list can change after creating/applying a material.
        sceneCacheReady_ = false;

        if (!drag) ip->RedrawViews(ip->GetTime());
        if (activationFailed)
        {
            SetStatus(L"SME drop failed.");
            return;
        }
        SetStatus(drag ? L"Dropped." : L"Inserted.");
        if (!hiddenEarly) Hide();
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
    HWND hotkeyWnd_ = nullptr;
    HWND wnd_       = nullptr;
    HWND edit_      = nullptr;
    HWND list_      = nullptr;
    HWND apply_     = nullptr;
    HWND scene_     = nullptr;
    HWND status_    = nullptr;
    TabMode tab_    = TabMode::All;
    std::vector<Item> classItems_;
    std::vector<Item> sceneItems_;
    const std::vector<Item>* activeItems_ = nullptr;
    std::vector<int> filtered_;
    std::wstring lastQuery_;
    TabMode lastTab_ = TabMode::All;
    bool lastSceneOnly_ = false;
    bool classCacheReady_ = false;
    bool forcedAliasRetry_ = false;
    bool sceneCacheReady_ = false;
    bool rebuildPending_ = false;
    bool  dragging_  = false;
    int   dragIndex_ = -1;
    POINT dragStart_ = {};
};

} // anonymous namespace

// ── Exported API ────────────────────────────────────────────────
void Init(HINSTANCE) { Palette::Get().Init(); }
void Shutdown()      { Palette::Get().Shutdown(); }
void Toggle()        { Palette::Get().Toggle(); }
bool IsOpen()        { return false; } // TODO: add accessor to Palette

} // namespace PowerShader
