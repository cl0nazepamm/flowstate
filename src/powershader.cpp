#include "powershader.h"
#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <utility>
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

// Try DAD drop on SME node view only — the only proven safe DAD target.
bool TryDropToSme(MtlBase* mb)
{
    if (!mb) return false;
    POINT screenPos{};
    if (!GetCursorPos(&screenPos)) return false;

    HWND smeHwnd = FindSmeNodeViewWindowAtPoint(screenPos);
    if (!smeHwnd) return false;

    IDADWindow* dadWindow = GetIDADWindow(smeHwnd);
    if (!dadWindow) return false;
    DADMgr* dadMgr = dadWindow->GetDADMgr();
    if (!dadMgr) { ReleaseIDADWindow(dadWindow); return false; }

    POINT clientPos = screenPos;
    ScreenToClient(smeHwnd, &clientPos);

    ReferenceTarget* dropThis = static_cast<ReferenceTarget*>(mb);
    const SClass_ID type = mb->SuperClassID();
    const BOOL ok = dadMgr->OkToDrop(dropThis, nullptr, smeHwnd, clientPos, type, FALSE);
    if (ok) dadMgr->Drop(dropThis, smeHwnd, clientPos, type, nullptr, FALSE);

    ReleaseIDADWindow(dadWindow);
    return ok == TRUE;
}

// Check if cursor is over an SME node view.
bool IsCursorOverSme()
{
    POINT p{}; GetCursorPos(&p);
    return FindSmeNodeViewWindowAtPoint(p) != nullptr;
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
constexpr int kWindowHeight = 540;
constexpr int kHeaderH      = 34;
constexpr UINT_PTR kSearchTimerId = 1;
constexpr UINT kSearchDebounceMs = 14;

enum class TabMode { All, Materials, Maps };
enum class ItemKind { ClassMaterial, ClassMap, SceneMaterial };

struct Item
{
    std::wstring label;
    std::wstring normLabel;   // pre-normalized for scoring
    std::wstring search;
    std::wstring key;
    std::wstring scriptName;
    std::wstring scriptKey;
    std::wstring category;
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

// Score an item against search tokens. Returns 0 = no match.
int ScoreMatch(const std::wstring& search, const std::wstring& label,
               const std::vector<std::wstring>& tokens)
{
    if (tokens.empty()) return 1;

    int score = 100;
    for (const std::wstring& tok : tokens)
    {
        size_t pos = search.find(tok);
        if (pos == std::wstring::npos) return 0;
        // Word-boundary bonus
        if (pos == 0 || search[pos - 1] == L' ') score += 10;
    }
    // First-token prefix bonus (matches label start)
    if (label.find(tokens[0]) == 0) score += 50;
    // Brevity bonus — shorter names rank higher
    score += std::max(0, 40 - static_cast<int>(label.size()));
    return score;
}

const wchar_t* TagForKind(ItemKind kind)
{
    switch (kind)
    {
    case ItemKind::SceneMaterial: return L"SCENE";
    case ItemKind::ClassMap:      return L"MAP";
    default:                      return L"MAT";
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

// Drag: viewport ray-hit → assign material to closest object under cursor.
static const wchar_t* kDragScript =
    L"("
    L"local m=meditMaterials[activeMeditSlot];"
    L"try("
    L"local r=mapScreenToWorldRay mouse.pos;"
    L"local hits=intersectRayScene r;"
    L"local nd=undefined;local best=1e9;"
    L"for h in hits do(local d=distance r.pos h[2].pos;"
    L"if d<best do(best=d;nd=h[1]));"
    L"if nd!=undefined and superclassof m==material do"
    L"(nd.material=m)"
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
        // Class cache is built lazily on first PowerShader open
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

        // ─── Custom header + border ─────────────────────────────
        case WM_ERASEBKGND:
        {
            HDC hdc = reinterpret_cast<HDC>(w);
            RECT rc; GetClientRect(h, &rc);
            FillRect(hdc, &rc, Theme::brBg);
            // Border
            HPEN bp = CreatePen(PS_SOLID, 1, Theme::border);
            SelectObject(hdc, bp); SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, 0, 0, rc.right, rc.bottom); DeleteObject(bp);
            // Title
            SetBkMode(hdc, TRANSPARENT);
            HFONT oldF = static_cast<HFONT>(SelectObject(hdc, Theme::fontBold));
            SetTextColor(hdc, Theme::accent);
            TextOutW(hdc, 10, 10, L"Power Shader", 12);
            // Close button
            if (self->hoverClose_) {
                HBRUSH hov = CreateSolidBrush(RGB(200, 60, 60));
                FillRect(hdc, &self->closeRect_, hov); DeleteObject(hov);
            }
            SetTextColor(hdc, Theme::text);
            DrawTextW(hdc, L"\u00D7", 1, &self->closeRect_,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            // Separator
            HPEN sep = CreatePen(PS_SOLID, 1, Theme::border);
            SelectObject(hdc, sep);
            MoveToEx(hdc, 8, kHeaderH - 4, nullptr);
            LineTo(hdc, rc.right - 8, kHeaderH - 4);
            DeleteObject(sep);
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

        case WM_LBUTTONUP:
        {
            if (self->quickDragging_ && self->quickDragId_ >= kQuickBase)
            {
                ReleaseCapture();
                int btnIdx = self->quickDragId_ - kQuickBase;
                self->quickDragging_ = false;
                self->quickDragId_ = -1;
                if (btnIdx >= 0 && btnIdx < static_cast<int>(std::size(kQuickButtons)))
                    self->ActivateAlias(kQuickButtons[btnIdx].alias, true);
                return 0;
            }
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

        // ─── Dark theme color handlers ──────────────────────────
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
            mis->itemHeight = 24;
            return TRUE;
        }
        case WM_DRAWITEM:
        {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(l);
            if (dis->CtlID == kListId)
                { self->DrawListItem(dis); return TRUE; }
            if ((dis->CtlID >= kQuickBase && dis->CtlID < kQuickBase + kQuickCount) ||
                dis->CtlID == kTabAllId || dis->CtlID == kTabMatId || dis->CtlID == kTabMapId ||
                dis->CtlID == kApplyId || dis->CtlID == kSceneId)
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

    static LRESULT CALLBACK QuickBtnProc(HWND h, UINT m, WPARAM w, LPARAM l,
                                         UINT_PTR, DWORD_PTR ref)
    {
        auto* self = reinterpret_cast<Palette*>(ref);
        switch (m)
        {
        case WM_LBUTTONDOWN:
            self->quickDragId_ = GetDlgCtrlID(h);
            self->quickDragging_ = false;
            GetCursorPos(&self->dragStart_);
            break;
        case WM_MOUSEMOVE:
            if (self->quickDragId_ >= 0 && (w & MK_LBUTTON) && !self->quickDragging_)
            {
                POINT p{}; GetCursorPos(&p);
                if (std::abs(p.x - self->dragStart_.x) > 6 ||
                    std::abs(p.y - self->dragStart_.y) > 6)
                {
                    self->quickDragging_ = true;
                    ReleaseCapture();
                    SetCapture(self->wnd_);
                    return 0;
                }
            }
            break;
        case WM_LBUTTONUP:
            self->quickDragId_ = -1;
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
            nullptr,
            WS_POPUP,
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

        // Search box
        edit_ = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            pad, y, cw, 24, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSearchId)),
            hInstance, nullptr);
        SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(Theme::fontBold), TRUE);
        SendMessageW(edit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search shaders..."));
        SetWindowSubclass(edit_, EditProc, 1, reinterpret_cast<DWORD_PTR>(this));
        y += 28;

        // Tab buttons (owner-drawn)
        const int tabGap = 3;
        int tabW = (cw - 2 * tabGap) / 3;
        for (int i = 0; i < 3; ++i) {
            static const wchar_t* tabLabels[] = { L"All", L"Materials", L"Maps" };
            static const int tabIds[] = { kTabAllId, kTabMatId, kTabMapId };
            CreateWindowExW(0, L"BUTTON", tabLabels[i],
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                pad + i * (tabW + tabGap), y, tabW, 22, h,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(tabIds[i])), hInstance, nullptr);
        }
        y += 26;

        // Quick buttons (owner-drawn)
        int x = pad;
        int btnW = 50, btnH = 24, gap = 3;
        for (int i = 0; i < static_cast<int>(std::size(kQuickButtons)); ++i)
        {
            std::wstring lbl = kQuickButtons[i].label;
            int bw = (lbl == L"NOISE" || lbl == L"GRAD") ? 58 : btnW;
            if (x + bw > pad + cw) { x = pad; y += btnH + gap; }
            HWND btn = CreateWindowExW(0, L"BUTTON", kQuickButtons[i].label,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                x, y, bw, btnH, h,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kQuickBase + i)),
                hInstance, nullptr);
            SetWindowSubclass(btn, QuickBtnProc, 1, reinterpret_cast<DWORD_PTR>(this));
            x += bw + gap;
        }
        y += btnH + 6;

        // Toggle buttons (owner-drawn)
        int halfW = (cw - gap) / 2;
        apply_ = CreateWindowExW(0, L"BUTTON", L"Apply to Sel",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            pad, y, halfW, 22, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kApplyId)), hInstance, nullptr);
        scene_ = CreateWindowExW(0, L"BUTTON", L"Scene Items",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            pad + halfW + gap, y, cw - halfW - gap, 22, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSceneId)), hInstance, nullptr);
        y += 26;

        // Results list (owner-drawn)
        int listH = cr.bottom - y - 26;
        list_ = CreateWindowExW(0, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY |
            LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_NODATA,
            pad, y, cw, listH, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kListId)), hInstance, nullptr);
        SetWindowSubclass(list_, ListProc, 1, reinterpret_cast<DWORD_PTR>(this));

        // Status bar
        status_ = CreateWindowExW(0, L"STATIC", L"Ready r4",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            pad, cr.bottom - 22, cw, 18, h, nullptr, hInstance, nullptr);
        SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(Theme::fontUI), TRUE);

        // Dark scrollbar on list
        HMODULE hUx = LoadLibraryW(L"uxtheme.dll");
        if (hUx)
        {
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
        if (!activeItems_ || fi < 0 || fi >= static_cast<int>(filtered_.size())) return;
        int si = filtered_[static_cast<size_t>(fi)];
        if (si < 0 || si >= static_cast<int>(activeItems_->size())) return;
        const Item& item = (*activeItems_)[static_cast<size_t>(si)];

        bool sel = (dis->itemState & ODS_SELECTED) != 0;
        FillRect(dis->hDC, &dis->rcItem, sel ? Theme::brAccent : Theme::brBg);

        // Name color by type
        COLORREF tc = sel ? Theme::textBrt : Theme::text;
        if (!sel)
        {
            if (item.kind == ItemKind::ClassMap) tc = Theme::mapClr;
            if (item.kind == ItemKind::SceneMaterial) tc = Theme::sceneClr;
        }

        SetBkMode(dis->hDC, TRANSPARENT);
        HFONT oldF = static_cast<HFONT>(SelectObject(dis->hDC, Theme::fontUI));

        // Right side: category · TAG
        std::wstring info = item.category;
        if (!info.empty()) info += L" \u00B7 ";
        info += TagForKind(item.kind);
        RECT rr = dis->rcItem;
        rr.right -= 6;
        SetTextColor(dis->hDC, sel ? Theme::textBrt : Theme::textDim);
        DrawTextW(dis->hDC, info.c_str(), -1, &rr,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        // Measure right text to clip left
        SIZE infoSz{};
        GetTextExtentPoint32W(dis->hDC, info.c_str(), static_cast<int>(info.size()), &infoSz);

        // Left side: item name
        RECT lr = dis->rcItem;
        lr.left += 8;
        lr.right -= infoSz.cx + 12;
        SetTextColor(dis->hDC, tc);
        DrawTextW(dis->hDC, item.label.c_str(), -1, &lr,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        SelectObject(dis->hDC, oldF);

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
        int id = static_cast<int>(dis->CtlID);
        bool active = false;

        // Determine active state based on control type
        if (id == kTabAllId)      active = (tab_ == TabMode::All);
        else if (id == kTabMatId) active = (tab_ == TabMode::Materials);
        else if (id == kTabMapId) active = (tab_ == TabMode::Maps);
        else if (id == kApplyId)  active = applyToSel_;
        else if (id == kSceneId)  active = sceneOnly_;
        else                      active = (dis->itemState & ODS_SELECTED) != 0;

        COLORREF bgc = active ? Theme::accent : Theme::panelLt;
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
        SetTextColor(dis->hDC, active ? Theme::textBrt : Theme::text);
        HFONT old = static_cast<HFONT>(SelectObject(dis->hDC, Theme::fontUI));
        DrawTextW(dis->hDC, buf, -1, &dis->rcItem,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dis->hDC, old);
    }

    // ─── Show / Hide ────────────────────────────────────────────
    void Show()
    {
        CancelPendingRebuild();
        SetWindowTextW(edit_, L"");
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
        if (id == kApplyId)
        {
            applyToSel_ = !applyToSel_;
            InvalidateRect(apply_, nullptr, FALSE);
            return;
        }
        if (id == kSceneId)
        {
            sceneOnly_ = !sceneOnly_;
            InvalidateRect(scene_, nullptr, FALSE);
            CancelPendingRebuild();
            if (sceneOnly_) RefreshSceneCache();
            Rebuild(true);
            return;
        }
        if (id == kTabAllId || id == kTabMatId || id == kTabMapId)
        {
            TabMode newTab = (id == kTabMatId) ? TabMode::Materials
                           : (id == kTabMapId) ? TabMode::Maps
                           : TabMode::All;
            if (newTab != tab_) {
                tab_ = newTab;
                // Invalidate all three tab buttons
                for (int tid : {kTabAllId, kTabMatId, kTabMapId})
                    if (HWND tw = GetDlgItem(wnd_, tid)) InvalidateRect(tw, nullptr, FALSE);
                CancelPendingRebuild();
                Rebuild(true);
            }
            return;
        }
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
        return sceneOnly_;
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
                item.normLabel = Normalize(item.label, true);
                item.search = Normalize(
                    item.label + L" " + std::wstring(className.data()), true);
                item.key = Normalize(item.label, false);
                item.kind = ItemKind::SceneMaterial;
                item.category = std::wstring(className.data());
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
        using CIDPair = std::pair<ULONG, ULONG>;
        std::set<CIDPair> seen;

        for (int i = list->GetFirst(ACC_PUBLIC); i != -1;
             i = list->GetNext(ACC_PUBLIC))
        {
            ClassEntry& ce = (*list)[i];
            ClassDesc* cd = ce.FullCD();
            if (!cd) continue;

            const Class_ID classId = ce.ClassID();
            if (!seen.insert({classId.PartA(), classId.PartB()}).second)
                continue;

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
            item.normLabel  = Normalize(name, true);
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
            item.category   = (categoryName && categoryName[0])
                ? std::wstring(categoryName) : L"";
            item.classDesc  = cd;
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
        const std::wstring normQ = Normalize(q, true);
        const std::vector<std::wstring> tokens = TokenizeQuery(normQ);

        auto passesTab = [&](const Item& item) -> bool
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
            return true;
        };

        struct Scored { int idx; int score; };
        std::vector<Scored> scored;
        scored.reserve(source.size());

        for (size_t i = 0; i < source.size(); ++i)
        {
            const Item& item = source[i];
            if (!passesTab(item)) continue;
            int s = ScoreMatch(item.search, item.normLabel, tokens);
            if (s > 0) scored.push_back({static_cast<int>(i), s});
        }

        // Sort by score descending when searching, alphabetical otherwise
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

        lastQuery_ = normQ;
        lastTab_ = tab_;
        lastSceneOnly_ = sceneOnly;

        SetStatus(std::to_wstring(filtered_.size()) + L" items" +
            (normQ.empty() ? L"" : (L"  |  " + normQ)));
    }

    // ─── Activation (C++ API core) ──────────────────────────────
    void ActivateAlias(const std::wstring& alias, bool drag = false)
    {
        EnsureClassCache();
        std::wstring key = Normalize(alias, false);
        for (const Item& item : classItems_)
            if (!item.live &&
                (item.key == key || item.scriptKey == key))
                { Activate(item, drag); return; }

        // Retry once after forcing a fresh cache build.
        if (!forcedAliasRetry_)
        {
            forcedAliasRetry_ = true;
            classCacheReady_ = false;
            EnsureClassCache();
            for (const Item& item : classItems_)
                if (!item.live &&
                    (item.key == key || item.scriptKey == key))
                    { Activate(item, drag); return; }
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

        // ── Create or reuse material/map instance ───────────────
        MtlBase* mb = item.live;
        if (!mb && item.classDesc)
            mb = static_cast<MtlBase*>(item.classDesc->Create(FALSE));
        if (!mb) { SetStatus(L"Create failed."); return; }
        const bool isNew = (item.live == nullptr);
        const bool isMat = (mb->SuperClassID() == MATERIAL_CLASS_ID);
        const bool isMap = (mb->SuperClassID() == TEXMAP_CLASS_ID);

        // Name new instances
        if (isNew)
            mb->SetName(MSTR((item.label + L"_" +
                std::to_wstring((GetTickCount() % 9000) + 1000)).c_str()));

        // Apply to selection (materials only)
        if (applyToSel_ && isMat)
        {
            Mtl* mtl = static_cast<Mtl*>(mb);
            for (int i = 0; i < ip->GetSelNodeCount(); ++i)
                if (INode* n = ip->GetSelNode(i)) n->SetMtl(mtl);
        }

        // Get medit slot
        int slot = 0;
        if (IMtlEditInterface* me = GetMtlEditInterface())
            slot = std::max(0, me->GetActiveMtlSlot());

        if (drag)
        {
            // ── Drag path ───────────────────────────────────────
            // 1. SME accepts both materials and maps via DAD
            bool dropped = TryDropToSme(mb);

            if (!dropped && isMat)
            {
                // 2. Materials: assign to object under cursor via MaxScript
                ip->PutMtlToMtlEditor(mb, slot);
                ExecuteMAXScriptScript(kDragScript, MAXScript::ScriptSource::Dynamic);
                dropped = true;
            }

            if (!dropped)
            {
                // 3. Fallback: put in medit palette
                ip->PutMtlToMtlEditor(mb, slot);
            }

            Hide();
        }
        else
        {
            // ── Click path: put in medit + spawn in SME ─────────
            ip->PutMtlToMtlEditor(mb, slot);
            ExecuteMAXScriptScript(kSmeAtSpawnScript, MAXScript::ScriptSource::Dynamic);
            Hide();
        }

        sceneCacheReady_ = false;
        ip->RedrawViews(ip->GetTime());
        SetStatus(drag ? L"Dropped." : L"Inserted.");
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
    RECT  closeRect_ = {};
    bool  hoverClose_ = false;
    bool  trackingMouse_ = false;
    bool  applyToSel_ = false;
    bool  sceneOnly_  = false;
    int   quickDragId_    = -1;
    bool  quickDragging_  = false;
};

} // anonymous namespace

// ── Exported API ────────────────────────────────────────────────
void Init(HINSTANCE) { Palette::Get().Init(); }
void Shutdown()      { Palette::Get().Shutdown(); }
void Toggle()        { Palette::Get().Toggle(); }
bool IsOpen()        { return false; } // TODO: add accessor to Palette

} // namespace PowerShader
