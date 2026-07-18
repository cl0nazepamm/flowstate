#include "powershader.h"
#include <windows.h>
#include <UIAutomation.h>
#include <commctrl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cwctype>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

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
#include <stdmat.h>
#include <bitmap.h>

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

// Try DAD drop on a specific window
bool TryDADDropOn(MtlBase* mb, HWND target)
{
    if (!mb || !target) return false;
    POINT screenPos{}; GetCursorPos(&screenPos);
    IDADWindow* dadWindow = GetIDADWindow(target);
    if (!dadWindow) return false;
    DADMgr* dadMgr = dadWindow->GetDADMgr();
    if (!dadMgr) { ReleaseIDADWindow(dadWindow); return false; }
    POINT clientPos = screenPos;
    ScreenToClient(target, &clientPos);
    ReferenceTarget* dropThis = static_cast<ReferenceTarget*>(mb);
    const SClass_ID type = mb->SuperClassID();
    const BOOL ok = dadMgr->OkToDrop(dropThis, nullptr, target, clientPos, type, FALSE);
    if (ok) dadMgr->Drop(dropThis, target, clientPos, type, nullptr, FALSE);
    ReleaseIDADWindow(dadWindow);
    return ok == TRUE;
}

bool AssignParameterDrop(MtlBase* owner, MtlBase* dropped, const std::wstring& paramName)
{
    if (!owner || !dropped || owner == dropped || paramName.empty()) return false;

    const SClass_ID droppedType = dropped->SuperClassID();
    if (droppedType != TEXMAP_CLASS_ID && droppedType != MATERIAL_CLASS_ID) return false;

    Interface* ip = GetCOREInterface();
    const TimeValue time = ip ? ip->GetTime() : 0;
    for (int blockIndex = 0; blockIndex < owner->NumParamBlocks(); ++blockIndex)
    {
        IParamBlock2* pb = owner->GetParamBlock(blockIndex);
        if (!pb) continue;

        for (int paramIndex = 0; paramIndex < pb->NumParams(); ++paramIndex)
        {
            const ParamID paramId = pb->IndextoID(paramIndex);
            const ParamDef& def = pb->GetParamDef(paramId);
            if (!def.int_name || _wcsicmp(def.int_name, paramName.c_str()) != 0) continue;

            BOOL assigned = FALSE;
            if (droppedType == TEXMAP_CLASS_ID && def.type == TYPE_TEXMAP)
                assigned = pb->SetValue(paramId, time, static_cast<Texmap*>(dropped));
            else if (droppedType == MATERIAL_CLASS_ID && def.type == TYPE_MTL)
                assigned = pb->SetValue(paramId, time, static_cast<Mtl*>(dropped));

            if (assigned)
            {
                owner->NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE);
                return true;
            }
            return false;
        }
    }
    return false;
}

std::wstring NormalizeSlotName(std::wstring_view value)
{
    std::wstring normalized;
    normalized.reserve(value.size());
    for (wchar_t ch : value)
    {
        if (iswalnum(ch)) normalized.push_back(static_cast<wchar_t>(towlower(ch)));
    }
    return normalized;
}

void AddUniqueName(std::vector<std::wstring>& names, std::wstring name)
{
    if (name.empty()) return;
    if (std::find(names.begin(), names.end(), name) == names.end())
        names.push_back(std::move(name));
}

bool IsStandardMaterial(MtlBase* owner)
{
    if (!owner) return false;
    const Class_ID id = owner->ClassID();
    return id == Class_ID(DMTL_CLASS_ID, 0) || id == Class_ID(DMTL2_CLASS_ID, 0);
}

std::vector<std::wstring> BuildDropLabelKeys(const std::wstring& label, bool standardMaterial)
{
    std::vector<std::wstring> keys;
    const std::wstring raw = NormalizeSlotName(label);
    AddUniqueName(keys, raw);

    struct Alias { const wchar_t* label; const wchar_t* slot; };
    static constexpr Alias aliases[] = {
        { L"baseweight", L"base" },
        { L"metalness", L"metallic" },
        { L"specularweight", L"specular" },
        { L"specularroughness", L"roughness" },
        { L"specularior", L"ior" },
        { L"specularanisotropy", L"anisotropytexture" },
        { L"specularrotation", L"rotation" },
        { L"transmissionweight", L"transmission" },
        { L"scatter", L"scattering" },
        { L"extraroughness", L"roughnessextra" },
        { L"subsurfaceweight", L"subsurface" },
        { L"subsurfaceradius", L"radius" },
        { L"overridemedium", L"medium" },
        { L"coatingweight", L"coating" },
        { L"sheenweight", L"sheen" },
        { L"filmthickness", L"filmwidth" },
        { L"filmthicknessnm", L"filmwidth" },
    };
    for (const Alias& alias : aliases)
    {
        if (raw == alias.label)
        {
            AddUniqueName(keys, alias.slot);
            break;
        }
    }

    if (standardMaterial)
    {
        if (raw == L"ambient") AddUniqueName(keys, L"ambientcolor");
        if (raw == L"diffuse") AddUniqueName(keys, L"diffusecolor");
        if (raw == L"specular") AddUniqueName(keys, L"specularcolor");
    }
    return keys;
}

int ResolveSubTexmapSlot(MtlBase* owner, const std::wstring& label)
{
    if (!owner || label.empty()) return -1;
    const std::vector<std::wstring> labelKeys =
        BuildDropLabelKeys(label, IsStandardMaterial(owner));
    if (labelKeys.empty()) return -1;

    int match = -1;
    for (int slot = 0; slot < owner->NumSubTexmaps(); ++slot)
    {
        std::vector<std::wstring> slotKeys;
        const MSTR internalName = owner->GetSubTexmapSlotName(slot, false);
        const MSTR localizedName = owner->GetSubTexmapSlotName(slot, true);
        if (internalName.data())
            AddUniqueName(slotKeys, NormalizeSlotName(internalName.data()));
        if (localizedName.data())
            AddUniqueName(slotKeys, NormalizeSlotName(localizedName.data()));

        bool matches = false;
        for (const std::wstring& labelKey : labelKeys)
        {
            if (std::find(slotKeys.begin(), slotKeys.end(), labelKey) != slotKeys.end())
            {
                matches = true;
                break;
            }
        }
        if (!matches) continue;
        if (match >= 0 && match != slot) return -1;
        match = slot;
    }
    return match;
}

struct RowLabelSearch
{
    RECT target{};
    DWORD processId = 0;
    std::wstring label;
    long long score = 0x7fffffffffffffffLL;
};

BOOL CALLBACK FindRowLabelWindow(HWND hwnd, LPARAM data)
{
    RowLabelSearch* search = reinterpret_cast<RowLabelSearch*>(data);
    if (!search || !IsWindowVisible(hwnd)) return TRUE;

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId != search->processId) return TRUE;

    wchar_t className[64] = {};
    if (!GetClassNameW(hwnd, className, static_cast<int>(std::size(className))) ||
        _wcsicmp(className, L"Static") != 0)
        return TRUE;

    const int textLength = GetWindowTextLengthW(hwnd);
    if (textLength <= 0) return TRUE;
    std::vector<wchar_t> text(static_cast<size_t>(textLength) + 1, L'\0');
    GetWindowTextW(hwnd, text.data(), textLength + 1);
    if (NormalizeSlotName(text.data()).empty()) return TRUE;

    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) return TRUE;
    const int targetCenter = (search->target.top + search->target.bottom) / 2;
    const int labelCenter = (rect.top + rect.bottom) / 2;
    const int verticalDistance = std::abs(targetCenter - labelCenter);
    const int targetHeight = search->target.bottom - search->target.top;
    const int verticalTolerance = std::max(14, targetHeight / 2 + 4);
    if (verticalDistance > verticalTolerance || rect.right > search->target.left + 4)
        return TRUE;

    const LONG horizontalDistance = std::max<LONG>(0, search->target.left - rect.right);
    const long long score = static_cast<long long>(verticalDistance) * 10000LL +
        horizontalDistance;
    if (score < search->score)
    {
        search->score = score;
        search->label.assign(text.data());
    }
    return TRUE;
}

std::wstring FindNearbyRowLabel(HWND control)
{
    if (!control || !IsWindow(control)) return {};
    HWND root = GetAncestor(control, GA_ROOT);
    if (!root) return {};

    RowLabelSearch search;
    if (!GetWindowRect(control, &search.target)) return {};
    GetWindowThreadProcessId(control, &search.processId);
    EnumChildWindows(root, FindRowLabelWindow, reinterpret_cast<LPARAM>(&search));
    return search.label;
}

std::wstring GetAutomationName(IUIAutomationElement* element)
{
    if (!element) return {};
    BSTR value = nullptr;
    if (FAILED(element->get_CurrentName(&value)) || !value) return {};
    std::wstring name(value, SysStringLen(value));
    SysFreeString(value);
    return name;
}

std::wstring GetAutomationClassName(IUIAutomationElement* element)
{
    if (!element) return {};
    BSTR value = nullptr;
    if (FAILED(element->get_CurrentClassName(&value)) || !value) return {};
    std::wstring name(value, SysStringLen(value));
    SysFreeString(value);
    return name;
}

bool IsGenericPickerName(const std::wstring& name)
{
    const std::wstring key = NormalizeSlotName(name);
    return key.empty() || key == L"value" || key == L"nomap" || key == L"none" ||
        key == L"user1" || key == L"dropdownbutton";
}

bool AssignNamedSubTexmapDrop(MtlBase* owner, MtlBase* dropped, const std::wstring& label)
{
    if (!owner || !dropped || owner == dropped ||
        dropped->SuperClassID() != TEXMAP_CLASS_ID)
        return false;

    const int slot = ResolveSubTexmapSlot(owner, label);
    if (slot < 0) return false;

    Texmap* map = static_cast<Texmap*>(dropped);
    owner->SetSubTexmap(slot, map);
    if (owner->GetSubTexmap(slot) != map) return false;

    if (IsStandardMaterial(owner))
        static_cast<StdMat*>(owner)->EnableMap(slot, TRUE);

    owner->NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE);
    if (Interface* ip = GetCOREInterface()) ip->RedrawViews(ip->GetTime());
    return true;
}

// Qt pickers expose the ParamBlock2 internal name through UI Automation. Native
// material panels instead expose a picker HWND and a visible row label.
bool TryQtMeditParameterDrop(MtlBase* dropped)
{
    if (!dropped) return false;

    IMtlEditInterface* medit = GetMtlEditInterface();
    MtlBase* owner = medit ? medit->GetCurMtl() : nullptr;
    if (!owner || owner == dropped) return false;

    const HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = SUCCEEDED(initResult);

    IUIAutomation* automation = nullptr;
    HRESULT result = CoCreateInstance(
        CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&automation));
    if (FAILED(result) || !automation)
    {
        if (uninitialize) CoUninitialize();
        return false;
    }

    POINT cursor{};
    GetCursorPos(&cursor);
    IUIAutomationElement* element = nullptr;
    result = automation->ElementFromPoint(cursor, &element);

    IUIAutomationTreeWalker* walker = nullptr;
    if (SUCCEEDED(result) && element)
        automation->get_RawViewWalker(&walker);

    bool assigned = false;
    HWND nativePicker = nullptr;
    std::wstring pickerName;
    for (int depth = 0; element && depth < 6 && !assigned; ++depth)
    {
        int processId = 0;
        CONTROLTYPEID controlType = 0;
        UIA_HWND nativeHandle = nullptr;
        element->get_CurrentProcessId(&processId);
        element->get_CurrentControlType(&controlType);
        element->get_CurrentNativeWindowHandle(&nativeHandle);

        if (processId == static_cast<int>(GetCurrentProcessId()) &&
            controlType == UIA_ButtonControlTypeId)
        {
            BSTR automationId = nullptr;
            if (SUCCEEDED(element->get_CurrentAutomationId(&automationId)) && automationId)
            {
                std::wstring fullId(automationId, SysStringLen(automationId));
                SysFreeString(automationId);
                const size_t dot = fullId.find_last_of(L'.');
                const std::wstring paramName =
                    dot == std::wstring::npos ? fullId : fullId.substr(dot + 1);
                assigned = AssignParameterDrop(owner, dropped, paramName);
            }
        }

        if (!assigned && !nativePicker &&
            processId == static_cast<int>(GetCurrentProcessId()) && nativeHandle)
        {
            const std::wstring className = GetAutomationClassName(element);
            if ((_wcsicmp(className.c_str(), L"CustButton") == 0 ||
                 _wcsicmp(className.c_str(), L"ComboBox") == 0) &&
                IsWindow(static_cast<HWND>(nativeHandle)))
            {
                nativePicker = static_cast<HWND>(nativeHandle);
                const std::wstring name = GetAutomationName(element);
                if (!IsGenericPickerName(name)) pickerName = name;
            }
        }

        if (!assigned && walker)
        {
            IUIAutomationElement* parent = nullptr;
            walker->GetParentElement(element, &parent);
            element->Release();
            element = parent;
        }
    }

    if (!assigned && nativePicker)
    {
        std::wstring label = FindNearbyRowLabel(nativePicker);
        if (label.empty()) label = pickerName;
        assigned = AssignNamedSubTexmapDrop(owner, dropped, label);
    }

    if (element) element->Release();
    if (walker) walker->Release();
    automation->Release();
    if (uninitialize) CoUninitialize();
    return assigned;
}

// Try context-aware drop — SME DAD, Qt material parameters, then legacy DAD controls
bool TryDADDrop(MtlBase* mb)
{
    if (!mb) return false;
    POINT screenPos{}; GetCursorPos(&screenPos);

    // 1. SME node view (known safe DAD target)
    HWND smeHwnd = FindSmeNodeViewWindowAtPoint(screenPos);
    if (smeHwnd && TryDADDropOn(mb, smeHwnd)) return true;

    // 2. Qt material/map parameter picker in the Compact Material Editor.
    if (TryQtMeditParameterDrop(mb)) return true;

    // 3. Direct window under cursor — only try if it has DAD
    //    (medit sample slots, color swatches, etc.)
    HWND under = WindowFromPoint(screenPos);
    if (under && under != smeHwnd) {
        // Safety: only try DAD on small child controls, not main Max windows
        RECT wr; GetWindowRect(under, &wr);
        int w = wr.right - wr.left, h = wr.bottom - wr.top;
        if (w < 300 && h < 300) {
            if (TryDADDropOn(mb, under)) return true;
        }
    }
    return false;
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
    bool lightTheme = false;
    COLORREF bg;
    COLORREF panel;
    COLORREF panelLt;
    COLORREF panelHov;
    COLORREF accent;
    COLORREF text;
    COLORREF textDim;
    COLORREF textBrt;
    COLORREF border;
    COLORREF mapClr;
    COLORREF sceneClr;

    HBRUSH brBg      = nullptr;
    HBRUSH brPanel   = nullptr;
    HBRUSH brPanelLt = nullptr;
    HBRUSH brAccent  = nullptr;
    HFONT  fontUI    = nullptr;
    HFONT  fontBold  = nullptr;

    void Update(bool light)
    {
        lightTheme = light;
        if (light) {
            bg       = RGB(215, 218, 222);
            panel    = RGB(225, 228, 232);
            panelLt  = RGB(240, 242, 245);
            panelHov = RGB(250, 250, 250);
            accent   = RGB(150, 155, 165);
            text     = RGB(30, 30, 30);
            textDim  = RGB(100, 100, 100);
            textBrt  = RGB(10, 10, 10);
            border   = RGB(140, 145, 150);
            mapClr   = RGB(80, 100, 180);
            sceneClr = RGB(50, 120, 50);
        } else {
            bg       = RGB(46, 46, 46);
            panel    = RGB(56, 56, 56);
            panelLt  = RGB(68, 68, 68);
            panelHov = RGB(80, 80, 80);
            accent   = RGB(38, 148, 168);
            text     = RGB(220, 220, 220);
            textDim  = RGB(140, 140, 140);
            textBrt  = RGB(255, 255, 255);
            border   = RGB(42, 42, 42);
            mapClr   = RGB(180, 200, 255);
            sceneClr = RGB(100, 200, 100);
        }

        if (brBg) DeleteObject(brBg);
        if (brPanel) DeleteObject(brPanel);
        if (brPanelLt) DeleteObject(brPanelLt);
        if (brAccent) DeleteObject(brAccent);

        brBg      = CreateSolidBrush(bg);
        brPanel   = CreateSolidBrush(panel);
        brPanelLt = CreateSolidBrush(panelLt);
        brAccent  = CreateSolidBrush(accent);
    }

    void Init(bool light)
    {
        Update(light);
        if (!fontUI) {
            fontUI    = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            fontBold  = CreateFontW(-15, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        }
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
constexpr wchar_t kPaletteClass[] = L"PowerShaderPaletteWnd";
constexpr int kSearchId  = 1001;
constexpr int kListId    = 1002;
constexpr int kLinkId    = 1003;
constexpr int kShllId    = 1008;
constexpr int kSceneId   = 1004;
constexpr int kTabMatId  = 1006;
constexpr int kTabMapId  = 1007;
constexpr int kApplyId   = 1009;
constexpr int kWindowWidth  = 380;
constexpr int kWindowHeight = 540;
constexpr int kHeaderH      = 34;
constexpr UINT_PTR kSearchTimerId = 1;
constexpr UINT kSearchDebounceMs = 14;

enum class TabMode { All, Materials, Maps };
enum class ItemKind { ClassMaterial, ClassMap, SceneMaterial, SceneMap };

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

// ═══════════════════════════════════════════════════════════════
//  Dual favorites
// ═══════════════════════════════════════════════════════════════
struct BrickFav
{
    std::wstring alias;  // normalized class alias
    std::wstring label;  // display label (max 4 chars)
};
constexpr int kBrickBase = 1200;
constexpr int kBrickMax  = 24;  // max persistent brick buttons

// ═══════════════════════════════════════════════════════════════
//  SHLL — Shell Material Preview (pure C++)
// ═══════════════════════════════════════════════════════════════
enum class PBR { None, BaseColor, Roughness, Metalness, Normal, Bump, Emission, Opacity };

static PBR DetectSlotFromName(const wchar_t* n) {
    if (!n || !n[0]) return PBR::None;
    std::wstring s(n); for (auto& c : s) c = towlower(c);
    if (s.find(L"base_color") != s.npos || s.find(L"diffuse") != s.npos || s.find(L"albedo") != s.npos) return PBR::BaseColor;
    if (s.find(L"roughness") != s.npos) return PBR::Roughness;
    if (s.find(L"metalness") != s.npos || s.find(L"metallic") != s.npos) return PBR::Metalness;
    if (s.find(L"normal") != s.npos) return PBR::Normal;
    if (s.find(L"bump") != s.npos || s.find(L"height") != s.npos) return PBR::Bump;
    if (s.find(L"emission") != s.npos || s.find(L"emissive") != s.npos || s.find(L"selfillum") != s.npos) return PBR::Emission;
    if (s.find(L"opacity") != s.npos || s.find(L"alpha") != s.npos || s.find(L"transparency") != s.npos || s.find(L"cutout") != s.npos) return PBR::Opacity;
    return PBR::None;
}

static PBR DetectSlotFromFile(const std::wstring& path) {
    // Extract filename without extension, lowercase
    size_t slash = path.find_last_of(L"\\/");
    size_t dot   = path.find_last_of(L'.');
    std::wstring f = path.substr(slash == path.npos ? 0 : slash + 1,
        dot == path.npos ? path.npos : dot - (slash == path.npos ? 0 : slash + 1));
    for (auto& c : f) c = towlower(c);
    if (f.find(L"basecolor") != f.npos || f.find(L"base_color") != f.npos || f.find(L"diffuse") != f.npos || f.find(L"albedo") != f.npos || f.find(L"_diff") != f.npos || f.find(L"_col") != f.npos) return PBR::BaseColor;
    if (f.find(L"roughness") != f.npos || f.find(L"_rough") != f.npos) return PBR::Roughness;
    if (f.find(L"metallic") != f.npos || f.find(L"metalness") != f.npos || f.find(L"_metal") != f.npos) return PBR::Metalness;
    if (f.find(L"normal") != f.npos || f.find(L"_nrm") != f.npos || f.find(L"_nor") != f.npos) return PBR::Normal;
    if (f.find(L"bump") != f.npos || f.find(L"height") != f.npos || f.find(L"_disp") != f.npos) return PBR::Bump;
    if (f.find(L"emissive") != f.npos || f.find(L"emission") != f.npos || f.find(L"_glow") != f.npos) return PBR::Emission;
    if (f.find(L"opacity") != f.npos || f.find(L"_alpha") != f.npos) return PBR::Opacity;
    return PBR::None;
}

static bool IsImageFile(const wchar_t* path) {
    if (!path || !path[0]) return false;
    const wchar_t* dot = wcsrchr(path, L'.');
    if (!dot) return false;
    std::wstring ext(dot); for (auto& c : ext) c = towlower(c);
    return ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".tif" ||
           ext == L".tiff" || ext == L".exr" || ext == L".hdr" || ext == L".bmp" ||
           ext == L".tga" || ext == L".dds" || ext == L".psd" || ext == L".tx";
}

// Extract texture file path from any Texmap. Texmap graphs are allowed to be
// cyclic, so keep a per-search visited set rather than assuming a tree.
static std::wstring ExtractTexPathImpl(Texmap* tex, std::set<Texmap*>& visited) {
    if (!tex || !visited.insert(tex).second) return {};
    // BitmapTex — direct path
    if (tex->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
        const MCHAR* n = static_cast<BitmapTex*>(tex)->GetMapName();
        if (n && n[0] && IsImageFile(n)) return n;
    }
    // Scan param blocks for TYPE_FILENAME — only accept image files
    for (int b = 0; b < tex->NumParamBlocks(); b++) {
        IParamBlock2* pb = tex->GetParamBlock(b);
        if (!pb) continue;
        for (int i = 0; i < pb->NumParams(); i++) {
            ParamID pid = pb->IndextoID(i);
            const ParamDef& d = pb->GetParamDef(pid);
            if (d.type == TYPE_FILENAME) {
                const MCHAR* fn = pb->GetStr(pid);
                if (fn && fn[0] && IsImageFile(fn)) return fn;
            }
        }
    }
    // Recurse into sub-texmaps
    for (int i = 0; i < tex->NumSubTexmaps(); i++) {
        std::wstring p = ExtractTexPathImpl(tex->GetSubTexmap(i), visited);
        if (!p.empty()) return p;
    }
    return {};
}

static std::wstring ExtractTexPath(Texmap* tex) {
    std::set<Texmap*> visited;
    return ExtractTexPathImpl(tex, visited);
}

// Walk material tree, collect PBR map entries
struct PBRMap { PBR slot; std::wstring path; };

static void CollectPBRMaps(MtlBase* mb, std::vector<PBRMap>& out, std::set<MtlBase*>& visited) {
    if (!mb || visited.count(mb)) return;
    visited.insert(mb);
    // Walk texture slots
    for (int i = 0; i < mb->NumSubTexmaps(); i++) {
        Texmap* sub = mb->GetSubTexmap(i);
        if (!sub) continue;
        std::wstring path = ExtractTexPath(sub);
        if (!path.empty()) {
            MSTR slotName = mb->GetSubTexmapSlotName(i, false);
            PBR slot = DetectSlotFromName(slotName.data());
            if (slot == PBR::None) slot = DetectSlotFromFile(path);
            if (slot != PBR::None) {
                bool dup = false;
                for (auto& e : out) if (e.slot == slot) { dup = true; break; }
                if (!dup) out.push_back({slot, path});
            }
        }
        // Walk deeper into texmap chain
        CollectPBRMaps(sub, out, visited);
    }
    // Walk sub-materials
    if (mb->SuperClassID() == MATERIAL_CLASS_ID) {
        Mtl* mtl = static_cast<Mtl*>(mb);
        for (int i = 0; i < mtl->NumSubMtls(); i++)
            CollectPBRMaps(mtl->GetSubMtl(i), out, visited);
    }
}

// Resize a single bitmap file via Max SDK
static bool ResizeBitmapFile(const std::wstring& src, const std::wstring& dst, int res) {
    BitmapInfo srcBi;
    srcBi.SetName(src.c_str());
    BMMRES status;
    Bitmap* srcBmp = TheManager->Load(&srcBi, &status);
    if (!srcBmp) return false;
    BitmapInfo dstBi;
    dstBi.SetName(dst.c_str());
    const int srcW = srcBmp->Width();
    const int srcH = srcBmp->Height();
    int dstW = res;
    int dstH = res;
    if (srcW > 0 && srcH > 0) {
        if (srcW > srcH)
            dstH = std::max(1, static_cast<int>((static_cast<long long>(res) * srcH) / srcW));
        else if (srcH > srcW)
            dstW = std::max(1, static_cast<int>((static_cast<long long>(res) * srcW) / srcH));
    }
    dstBi.SetWidth(static_cast<WORD>(dstW));
    dstBi.SetHeight(static_cast<WORD>(dstH));
    dstBi.SetType(BMM_TRUE_32);
    Bitmap* dstBmp = TheManager->Create(&dstBi);
    if (!dstBmp) { srcBmp->DeleteThis(); return false; }
    bool ok = dstBmp->CopyImage(srcBmp, COPY_IMAGE_RESIZE_HI_QUALITY,
        BMM_Color_64(0,0,0,0)) != 0;
    if (ok) ok = (dstBmp->OpenOutput(&dstBi) == BMMRES_SUCCESS);
    if (ok) {
        ok = (dstBmp->Write(&dstBi) == BMMRES_SUCCESS);
        dstBmp->Close(&dstBi);
    }
    srcBmp->DeleteThis();
    dstBmp->DeleteThis();
    return ok;
}

static std::wstring SourcePathPrefix(const std::wstring& path) {
    // Stable FNV-1a hash keeps common names such as BaseColor.png from
    // overwriting another selected asset's resized texture.
    std::uint64_t hash = 14695981039346656037ull;
    for (wchar_t ch : path) {
        wchar_t normalized = (ch == L'/') ? L'\\' : static_cast<wchar_t>(towlower(ch));
        hash ^= static_cast<std::uint64_t>(normalized);
        hash *= 1099511628211ull;
    }
    wchar_t text[18] = {};
    swprintf_s(text, std::size(text), L"%016llx_", static_cast<unsigned long long>(hash));
    return text;
}

// Resize with UDIM support — returns path to use (with <UDIM> preserved)
static std::wstring ResizeTexture(const std::wstring& srcPath, const std::wstring& tmpDir, int res) {
    // Extract parts
    size_t slash = srcPath.find_last_of(L"\\/");
    size_t dot   = srcPath.find_last_of(L'.');
    std::wstring dir  = (slash != srcPath.npos) ? srcPath.substr(0, slash + 1) : L"";
    std::wstring file = srcPath.substr(slash == srcPath.npos ? 0 : slash + 1,
        dot == srcPath.npos ? srcPath.npos : dot - (slash == srcPath.npos ? 0 : slash + 1));
    std::wstring ext  = (dot != srcPath.npos) ? srcPath.substr(dot) : L"";
    const std::wstring prefix = SourcePathPrefix(srcPath);

    // UDIM handling
    if (file.find(L"<UDIM>") != file.npos) {
        std::wstring pattern = dir + file + ext;
        std::wstring glob = pattern;
        size_t pos = glob.find(L"<UDIM>"); glob.replace(pos, 6, L"*");
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(glob.c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE) return {};
        bool anyOk = false;
        do {
            std::wstring tileSrc = dir + fd.cFileName;
            std::wstring tileDst = tmpDir + L"\\" + prefix + fd.cFileName;
            if (ResizeBitmapFile(tileSrc, tileDst, res)) anyOk = true;
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
        return anyOk ? (tmpDir + L"\\" + prefix + file + ext) : std::wstring{};
    }

    // Single file
    if (GetFileAttributesW(srcPath.c_str()) == INVALID_FILE_ATTRIBUTES) return {};
    std::wstring outName = prefix + file + ext;
    std::wstring dst = tmpDir + L"\\" + outName;
    return ResizeBitmapFile(srcPath, dst, res) ? dst : srcPath;
}

// Set a PB2 param by name
static bool SetPB2Texmap(MtlBase* m, const wchar_t* name, Texmap* tex) {
    for (int b = 0; b < m->NumParamBlocks(); b++) {
        IParamBlock2* pb = m->GetParamBlock(b);
        if (!pb) continue;
        for (int i = 0; i < pb->NumParams(); i++) {
            ParamID pid = pb->IndextoID(i);
            const ParamDef& d = pb->GetParamDef(pid);
            if (d.int_name && _wcsicmp(d.int_name, name) == 0 && d.type == TYPE_TEXMAP)
                return pb->SetValue(pid, 0, tex) != FALSE;
        }
    }
    return false;
}
static bool SetPB2Bool(MtlBase* m, const wchar_t* name, BOOL val) {
    for (int b = 0; b < m->NumParamBlocks(); b++) {
        IParamBlock2* pb = m->GetParamBlock(b);
        if (!pb) continue;
        for (int i = 0; i < pb->NumParams(); i++) {
            ParamID pid = pb->IndextoID(i);
            const ParamDef& d = pb->GetParamDef(pid);
            if (d.int_name && _wcsicmp(d.int_name, name) == 0 && d.type == TYPE_BOOL)
                return pb->SetValue(pid, 0, val) != FALSE;
        }
    }
    return false;
}
static bool SetPB2Int(MtlBase* m, const wchar_t* name, int val) {
    for (int b = 0; b < m->NumParamBlocks(); b++) {
        IParamBlock2* pb = m->GetParamBlock(b);
        if (!pb) continue;
        for (int i = 0; i < pb->NumParams(); i++) {
            ParamID pid = pb->IndextoID(i);
            const ParamDef& d = pb->GetParamDef(pid);
            if (d.int_name && _wcsicmp(d.int_name, name) == 0 &&
                (d.type == TYPE_INT || d.type == TYPE_RADIOBTN_INDEX || d.type == TYPE_INDEX))
                return pb->SetValue(pid, 0, val) != FALSE;
        }
    }
    return false;
}
static bool SetPB2Mtl(MtlBase* m, const wchar_t* name, Mtl* sub) {
    for (int b = 0; b < m->NumParamBlocks(); b++) {
        IParamBlock2* pb = m->GetParamBlock(b);
        if (!pb) continue;
        for (int i = 0; i < pb->NumParams(); i++) {
            ParamID pid = pb->IndextoID(i);
            const ParamDef& d = pb->GetParamDef(pid);
            if (d.int_name && _wcsicmp(d.int_name, name) == 0 && d.type == TYPE_MTL)
                return pb->SetValue(pid, 0, sub) != FALSE;
        }
    }
    return false;
}

// Find a ClassDesc by superclass + name
static ClassDesc* FindClassByName(SClass_ID sid, const wchar_t* name) {
    SubClassList* list = ClassDirectory::GetInstance().GetClassList(sid);
    if (!list) return nullptr;
    for (int i = list->GetFirst(ACC_PUBLIC); i != -1; i = list->GetNext(ACC_PUBLIC)) {
        ClassEntry& ce = (*list)[i];
        ClassDesc* cd = ce.FullCD();
        if (cd && _wcsicmp(cd->ClassName(), name) == 0) return cd;
    }
    return nullptr;
}

// Create a texture node for the preview — UberBitmap2 preferred, BitmapTex fallback
static Texmap* CreatePreviewTexmap(const std::wstring& path) {
    // Use OSL_uberBitmap2b — the native MaxScript constructor that handles
    // all OSL shader loading internally. No manual OSLPath setup needed.
    auto esc = [](std::wstring s) {
        for (size_t p = s.find(L'\\'); p != std::wstring::npos; p = s.find(L'\\', p + 2))
            s.insert(p, L"\\");
        return s;
    };
    std::wstring script =
        L"try(local m=OSL_uberBitmap2b();m.filename=\"" + esc(path) + L"\";m)catch(undefined)";
    FPValue r;
    if (ExecuteMAXScriptScript(script.c_str(), MAXScript::ScriptSource::Dynamic, TRUE, &r)) {
        if (r.type == TYPE_REFTARG && r.r)
            return static_cast<Texmap*>(r.r);
        if (r.type == TYPE_TEXMAP && r.tex)
            return r.tex;
    }
    // Fallback to BitmapTex only if UberBitmap2 is not installed
    BitmapTex* bt = NewDefaultBitmapTex();
    if (bt) bt->SetMapName(path.c_str());
    return bt;
}

static void ExecuteShellCommand(int resolution) {
    Interface* ip = GetCOREInterface();
    if (!ip || ip->GetSelNodeCount() == 0) return;

    // Get temp dir
    MSTR tmpBase = ip->GetDir(APP_TEMP_DIR);
    std::wstring tmpDir = std::wstring(tmpBase.data()) + L"\\PowerShader_SHLL";
    CreateDirectoryW(tmpDir.c_str(), nullptr);

    // Find Normal Bump class for normal maps
    ClassDesc* normalBumpCD = FindClassByName(TEXMAP_CLASS_ID, L"Normal Bump");

    std::map<Mtl*, Mtl*> shellCache; // srcMat → shellMat
    int done = 0;
    const bool ownHold = !theHold.Holding();
    if (ownHold) theHold.Begin();

    for (int ni = 0; ni < ip->GetSelNodeCount(); ni++) {
        INode* node = ip->GetSelNode(ni);
        if (!node) continue;
        Mtl* src = node->GetMtl();
        if (!src) continue;
        // Skip if already a Shell
        if (src->ClassID() == Class_ID(BAKE_SHELL_CLASS_ID, 0)) continue;

        // Check cache
        auto cit = shellCache.find(src);
        if (cit != shellCache.end()) { node->SetMtl(cit->second); done++; continue; }

        // Collect PBR maps
        std::vector<PBRMap> maps;
        std::set<MtlBase*> visited;
        CollectPBRMaps(src, maps, visited);
        if (maps.empty()) continue;

        // Create PhysicalMaterial for preview
        Mtl* phys = NewPhysicalMaterial();
        if (!phys) continue;
        std::wstring bn = src->GetName().data();
        phys->SetName(MSTR((L"preview_" + bn).c_str()));

        // Wire each PBR map
        int wiredMaps = 0;
        for (auto& mp : maps) {
            std::wstring resized = ResizeTexture(mp.path, tmpDir, resolution);
            if (resized.empty()) continue;

            Texmap* tx = CreatePreviewTexmap(resized);
            if (!tx) continue;

            bool assigned = false;
            switch (mp.slot) {
            case PBR::BaseColor:
                assigned = SetPB2Texmap(phys, L"base_color_map", tx);
                if (assigned) SetPB2Bool(phys, L"base_color_map_on", TRUE);
                break;
            case PBR::Roughness:
                assigned = SetPB2Texmap(phys, L"roughness_map", tx);
                if (assigned) SetPB2Bool(phys, L"roughness_map_on", TRUE);
                break;
            case PBR::Metalness:
                assigned = SetPB2Texmap(phys, L"metalness_map", tx);
                if (assigned) SetPB2Bool(phys, L"metalness_map_on", TRUE);
                break;
            case PBR::Normal: {
                Texmap* normalWrapper = nullptr;
                if (normalBumpCD) {
                    normalWrapper = static_cast<Texmap*>(normalBumpCD->Create(FALSE));
                    if (normalWrapper) {
                        if (SetPB2Texmap(normalWrapper, L"normal_map", tx))
                            assigned = SetPB2Texmap(phys, L"bump_map", normalWrapper);
                        if (assigned) SetPB2Bool(phys, L"bump_map_on", TRUE);
                    }
                }
                if (!assigned) {
                    assigned = SetPB2Texmap(phys, L"bump_map", tx);
                    if (assigned) SetPB2Bool(phys, L"bump_map_on", TRUE);
                    if (normalWrapper) {
                        // If direct assignment also failed, destroy the target
                        // first so the wrapper receives reference-deletion
                        // notification before it is itself destroyed.
                        if (!assigned) {
                            tx->DeleteThis();
                            tx = nullptr;
                        }
                        normalWrapper->DeleteThis();
                    }
                }
                break;
            }
            case PBR::Bump:
                assigned = SetPB2Texmap(phys, L"bump_map", tx);
                if (assigned) SetPB2Bool(phys, L"bump_map_on", TRUE);
                break;
            case PBR::Emission:
                assigned = SetPB2Texmap(phys, L"emit_color_map", tx);
                if (assigned) SetPB2Bool(phys, L"emit_color_map_on", TRUE);
                break;
            case PBR::Opacity:
                assigned = SetPB2Texmap(phys, L"cutout_map", tx);
                if (assigned) SetPB2Bool(phys, L"cutout_map_on", TRUE);
                break;
            default: break;
            }
            if (!assigned && tx) tx->DeleteThis();
            if (assigned) ++wiredMaps;
        }

        if (wiredMaps == 0) {
            phys->DeleteThis();
            continue;
        }

        // Create Shell_Material
        Mtl* shell = static_cast<Mtl*>(
            ip->CreateInstance(MATERIAL_CLASS_ID, Class_ID(BAKE_SHELL_CLASS_ID, 0)));
        if (!shell) {
            phys->DeleteThis();
            continue;
        }
        shell->SetName(MSTR((L"shell_" + bn).c_str()));
        const bool originalSet = SetPB2Mtl(shell, L"originalMaterial", src);
        const bool bakedSet = SetPB2Mtl(shell, L"bakedMaterial", phys);
        if (!originalSet || !bakedSet) {
            phys->DeleteThis();
            shell->DeleteThis();
            continue;
        }
        SetPB2Int(shell, L"viewportMtlIndex", 1);
        SetPB2Int(shell, L"renderMtlIndex", 0);

        node->SetMtl(shell);
        shellCache[src] = shell;
        done++;
    }

    if (done > 0) {
        if (ownHold) theHold.Accept(_T("Create SHLL Preview Materials"));
        ip->RedrawViews(ip->GetTime());
    } else if (ownHold) {
        theHold.Cancel();
    }
}

static const wchar_t* kLinkScript =
    L"(\n"
    L"if SME.isOpen() do (\n"
    L"local v = SME.GetView (SME.activeView)\n"
    L"local sn = v.GetSelectedNodes()\n"
    L"if sn.count >= 2 do (\n"
    L"local maps = #()\n"
    L"for i = 1 to sn.count do (\n"
    L"local r = try(sn[i].reference)catch(undefined)\n"
    L"if r != undefined do (\n"
    L"local c = classof r\n"
    L"if c == ai_image or c == Bitmaptexture or c == OSLMap or c == RS_Texture do append maps r\n"
    L"))\n"
    L"if maps.count >= 2 do (\n"
    L"local ctrl = bezier_float()\n"
    L"local c1 = classof maps[1]\n"
    L"ctrl.value = case c1 of (\n"
    L"ai_image: maps[1].sscale\n"
    L"Bitmaptexture: maps[1].coords.U_Tiling\n"
    L"RS_Texture: maps[1].scale_x\n"
    L"OSLMap: (try(maps[1].scale)catch(1.0))\n"
    L"default: 1.0)\n"
    L"for m in maps do (\n"
    L"local c = classof m\n"
    L"case c of (\n"
    L"ai_image: (try(m.sscale.controller = ctrl)catch(); try(m.tscale.controller = ctrl)catch())\n"
    L"Bitmaptexture: (try(m.coords.U_Tiling.controller = ctrl)catch(); try(m.coords.V_Tiling.controller = ctrl)catch())\n"
    L"RS_Texture: (try(m.scale_x.controller = ctrl)catch(); try(m.scale_y.controller = ctrl)catch())\n"
    L"OSLMap: (try(m.scale.controller = ctrl)catch())\n"
    L"))))))";

static const wchar_t* kFlushScript =
    L"try(actionMan.executeAction 695602995 \"2\";true)catch(false)";

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

std::wstring StripOSLVersionSuffix(const std::wstring& value)
{
    size_t end = value.size();
    while (end > 0 && iswdigit(value[end - 1])) --end;
    return end == value.size() ? value : value.substr(0, end);
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
    case ItemKind::SceneMap:      return L"SCENE";
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
//  Texture Preview Popup
// ═══════════════════════════════════════════════════════════════
static ULONG_PTR     g_gdipToken = 0;
static HWND          g_previewWnd = nullptr;
static Gdiplus::Image* g_previewImg = nullptr;
static bool          g_previewClassRegistered = false;
constexpr wchar_t kPreviewClass[] = L"FlowStatePreview";
constexpr int kPreviewSize = 128;

static void InitGdiPlus() {
    if (!g_gdipToken) {
        Gdiplus::GdiplusStartupInput si;
        Gdiplus::GdiplusStartup(&g_gdipToken, &si, nullptr);
    }
}

static std::wstring GetTexmapFilename(MtlBase* m) {
    if (!m) return {};
    if (m->SuperClassID() == TEXMAP_CLASS_ID)
        return ExtractTexPath(static_cast<Texmap*>(m));
    for (int i = 0; i < m->NumSubTexmaps(); i++) {
        std::wstring path = ExtractTexPath(m->GetSubTexmap(i));
        if (!path.empty()) return path;
    }
    return {};
}

static LRESULT CALLBACK PreviewProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        // Background
        HBRUSH bg = CreateSolidBrush(RGB(30, 30, 30));
        FillRect(hdc, &rc, bg); DeleteObject(bg);
        // Border
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(55, 55, 55));
        HPEN op = (HPEN)SelectObject(hdc, pen);
        HBRUSH ob = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, 0, 0, rc.right, rc.bottom);
        SelectObject(hdc, ob);
        SelectObject(hdc, op); DeleteObject(pen);
        // Image
        if (g_previewImg) {
            Gdiplus::Graphics gfx(hdc);
            gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            // Fit image in the box with aspect ratio
            int iw = g_previewImg->GetWidth(), ih = g_previewImg->GetHeight();
            if (iw > 0 && ih > 0) {
                int bw = rc.right - 4, bh = rc.bottom - 4;
                float scale = (std::min)((float)bw / iw, (float)bh / ih);
                int dw = (int)(iw * scale), dh = (int)(ih * scale);
                int dx = 2 + (bw - dw) / 2, dy = 2 + (bh - dh) / 2;
                gfx.DrawImage(g_previewImg, dx, dy, dw, dh);
            }
        }
        EndPaint(h, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND) return 1;
    return DefWindowProc(h, msg, w, l);
}

static void ShowPreview(const std::wstring& path, HWND paletteWnd) {
    if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (g_previewWnd) ShowWindow(g_previewWnd, SW_HIDE);
        delete g_previewImg;
        g_previewImg = nullptr;
        return;
    }
    InitGdiPlus();
    // Load image
    delete g_previewImg;
    g_previewImg = Gdiplus::Image::FromFile(path.c_str());
    if (!g_previewImg || g_previewImg->GetLastStatus() != Gdiplus::Ok) {
        delete g_previewImg; g_previewImg = nullptr;
        if (g_previewWnd) ShowWindow(g_previewWnd, SW_HIDE);
        return;
    }
    // Create window if needed
    if (!g_previewWnd) {
        if (!g_previewClassRegistered) {
            WNDCLASSEXW wc{ sizeof(wc) };
            wc.lpfnWndProc = PreviewProc;
            wc.hInstance = hInstance;
            wc.lpszClassName = kPreviewClass;
            g_previewClassRegistered = RegisterClassExW(&wc) != 0 ||
                GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        }
        if (g_previewClassRegistered) {
            g_previewWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                kPreviewClass, L"", WS_POPUP, 0, 0, kPreviewSize, kPreviewSize,
                paletteWnd, nullptr, hInstance, nullptr);
        }
    }
    if (!g_previewWnd) {
        delete g_previewImg;
        g_previewImg = nullptr;
        return;
    }
    // Position to the left of the palette
    RECT pr; GetWindowRect(paletteWnd, &pr);
    RECT wa{};
    MONITORINFO mi{ sizeof(mi) };
    HMONITOR monitor = MonitorFromWindow(paletteWnd, MONITOR_DEFAULTTONEAREST);
    if (monitor && GetMonitorInfoW(monitor, &mi)) wa = mi.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = pr.left - kPreviewSize - 4;
    if (x < wa.left) x = pr.right + 4;
    if (x + kPreviewSize > wa.right)
        x = (std::max)(wa.left, pr.left - kPreviewSize - 4);
    int y = std::clamp(pr.top, wa.top, wa.bottom - kPreviewSize);
    SetWindowPos(g_previewWnd, HWND_TOPMOST, x, y, kPreviewSize, kPreviewSize,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_previewWnd, nullptr, FALSE);
}

static void HidePreview() {
    if (g_previewWnd) ShowWindow(g_previewWnd, SW_HIDE);
    delete g_previewImg; g_previewImg = nullptr;
}

// ═══════════════════════════════════════════════════════════════
//  Palette
// ═══════════════════════════════════════════════════════════════
class Palette
{
public:
    static Palette& Get() { static Palette p; return p; }

    // Exposed for unified config persistence
    std::vector<std::wstring> filePins_;     // file-local pins
    std::vector<BrickFav> brickFavs_;        // persistent brick favorites

    bool Init(bool light)
    {
        if (inited_) return true;
        Theme::Init(light);
        INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
        InitCommonControlsEx(&icc);

        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc   = PaletteProc;
        wc.hbrBackground = nullptr; // WM_ERASEBKGND paints with the live theme brush
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hInstance     = hInstance;
        wc.lpszClassName = kPaletteClass;
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
        HidePreview();
        RestoreAccelerators();
        if (g_previewWnd) { DestroyWindow(g_previewWnd); g_previewWnd = nullptr; }
        if (wnd_) { DestroyWindow(wnd_); wnd_ = nullptr; }
        edit_ = list_ = link_ = shll_ = scene_ = apply_ = status_ = nullptr;
        renameEdit_ = nullptr;
        brickBtns_.clear();
        renameIdx_ = brickDragId_ = -1;
        renaming_ = brickDragging_ = dragging_ = false;
        activeItems_ = nullptr;
        filtered_.clear();
        classItems_.clear();
        sceneItems_.clear();
        filePins_.clear();
        brickFavs_.clear();
        lastQuery_.clear();
        tab_ = lastTab_ = TabMode::All;
        lastSceneOnly_ = false;
        classCacheReady_ = classCacheBuilding_ = false;
        oslCategoryReady_ = oslCategoryBuilding_ = false;
        forcedAliasRetry_ = false;
        sceneCacheReady_ = rebuildPending_ = false;
        sceneOnly_ = applyToSel_ = false;
        hoverClose_ = trackingMouse_ = false;
        closeRect_ = {};
        dragStart_ = {};
        shllRes_ = 256;
        UnregisterClassW(kPaletteClass, hInstance);
        if (g_previewClassRegistered) {
            UnregisterClassW(kPreviewClass, hInstance);
            g_previewClassRegistered = false;
        }
        Theme::Shutdown();
        if (g_gdipToken) { Gdiplus::GdiplusShutdown(g_gdipToken); g_gdipToken = 0; }
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
        Rebuild(true);
        RebuildBrickUI(false);
    }

private:
    // ─── Window procedures ──────────────────────────────────────
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

        case WM_USER + 50:
            if (IsWindowVisible(h)) self->UpdatePreviewForSelection();
            else HidePreview();
            return 0;

        case WM_SHOWWINDOW:
            if (!w) HidePreview();
            break;

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
            HPEN oldP = static_cast<HPEN>(SelectObject(hdc, bp));
            HBRUSH oldB = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
            Rectangle(hdc, 0, 0, rc.right, rc.bottom);
            SelectObject(hdc, oldB);
            SelectObject(hdc, oldP);
            DeleteObject(bp);
            // Title
            SetBkMode(hdc, TRANSPARENT);
            HFONT oldF = static_cast<HFONT>(SelectObject(hdc, Theme::fontBold));
            SetTextColor(hdc, Theme::accent);
            const wchar_t* title = self->dragging_
                ? L"release to create"
                : L"flowstate.";
            TextOutW(hdc, 10, 10, title, lstrlenW(title));
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
            HPEN oldS = static_cast<HPEN>(SelectObject(hdc, sep));
            MoveToEx(hdc, 8, kHeaderH - 4, nullptr);
            LineTo(hdc, rc.right - 8, kHeaderH - 4);
            SelectObject(hdc, oldS);
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
            // Finish rename on click outside
            if (self->renameEdit_) self->FinishRename();
            // Brick drag completion
            if (self->brickDragging_ && self->brickDragId_ >= kBrickBase) {
                ReleaseCapture();
                int bi = self->brickDragId_ - kBrickBase;
                self->brickDragging_ = false;
                self->brickDragId_ = -1;
                if (bi >= 0 && bi < static_cast<int>(self->brickFavs_.size()))
                    self->ActivateAlias(self->brickFavs_[bi].alias, true);
                return 0;
            }
            break;

        case WM_PARENTNOTIFY:
        {
            // Left-click on list → update preview (LBN_SELCHANGE doesn't always fire on click)
            if (LOWORD(w) == WM_LBUTTONDOWN) {
                POINT cp = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
                HWND child = ChildWindowFromPoint(h, cp);
                if (child == self->list_) {
                    // Post a delayed update — selection isn't set yet during PARENTNOTIFY
                    PostMessage(h, WM_USER + 50, 0, 0);
                }
            }
            // Middle-click on brick button → remove it
            if (LOWORD(w) == WM_MBUTTONDOWN) {
                POINT cp = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
                HWND child = ChildWindowFromPoint(h, cp);
                int cid = child ? GetDlgCtrlID(child) : 0;
                if (cid >= kBrickBase && cid < kBrickBase + kBrickMax) {
                    self->RemoveBrickFav(cid - kBrickBase);
                    return 0;
                }
            }
            break;
        }

        case WM_CONTEXTMENU:
        {
            HWND target = reinterpret_cast<HWND>(w);
            int cid = target ? GetDlgCtrlID(target) : 0;
            // Right-click SHLL → cycle resolution
            if (cid == kShllId) {
                static const int kRes[] = {128, 256, 512, 1024};
                int cur = 0;
                for (int i = 0; i < 4; i++) if (kRes[i] == self->shllRes_) { cur = i; break; }
                self->shllRes_ = kRes[(cur + 1) % 4];
                std::wstring lbl = L"SHLL " + std::to_wstring(self->shllRes_);
                SetWindowTextW(self->shll_, lbl.c_str());
                InvalidateRect(self->shll_, nullptr, FALSE);
                return 0;
            }
            // Right-click on brick button → rename
            if (cid >= kBrickBase && cid < kBrickBase + kBrickMax) {
                self->RenameBrickFav(cid - kBrickBase);
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
            mis->itemHeight = 30;
            return TRUE;
        }
        case WM_DRAWITEM:
        {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(l);
            if (dis->CtlID == kListId)
                { self->DrawListItem(dis); return TRUE; }
            if (dis->CtlID == kTabMatId || dis->CtlID == kTabMapId ||
                dis->CtlID == kLinkId || dis->CtlID == kShllId ||
                dis->CtlID == kSceneId || dis->CtlID == kApplyId ||
                (dis->CtlID >= kBrickBase && dis->CtlID < kBrickBase + kBrickMax))
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
                    std::abs(p.y - self->dragStart_.y) > 6) {
                    if (!self->dragging_) {
                        self->dragging_ = true;
                        RECT header = { 0, 0, kWindowWidth, kHeaderH };
                        RedrawWindow(self->wnd_, &header, nullptr,
                            RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
                    }
                }

                // Once this becomes a PowerShader drag, do not pass motion to
                // the native listbox. Its built-in capture handling otherwise
                // auto-scrolls the results while the cursor is outside it.
                if (self->dragging_) return 0;
            }
            break;
        case WM_MOUSEWHEEL:
            if (self->dragging_) return 0;
            break;
        case WM_LBUTTONUP:
        {
            const bool wasDragging = self->dragging_;
            if (GetCapture() == h) ReleaseCapture();
            if (wasDragging)
            {
                POINT p{}; GetCursorPos(&p);
                HWND under = WindowFromPoint(p);
                if (!under || (under != self->wnd_ && !IsChild(self->wnd_, under)))
                    self->ActivateByIndex(self->dragIndex_, true);
            }
            self->dragging_ = false;
            self->dragIndex_ = -1;
            {
                RECT header = { 0, 0, kWindowWidth, kHeaderH };
                RedrawWindow(self->wnd_, &header, nullptr,
                    RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
            }
            if (wasDragging) return 0;
            break;
        }
        case WM_RBUTTONDOWN:
        {
            // Right-click = file-local pin (toggle at start of list)
            LRESULT hit = SendMessageW(h, LB_ITEMFROMPOINT, 0, l);
            if (HIWORD(hit) == 0) self->ToggleFilePin(LOWORD(hit));
            return 0;
        }
        case WM_MBUTTONDOWN:
        {
            // Middle-click = persistent brick favorite
            LRESULT hit = SendMessageW(h, LB_ITEMFROMPOINT, 0, l);
            if (HIWORD(hit) == 0) self->ToggleBrickFav(LOWORD(hit));
            return 0;
        }
        }
        return DefSubclassProc(h, m, w, l);
    }

    static LRESULT CALLBACK BrickBtnProc(HWND h, UINT m, WPARAM w, LPARAM l,
                                         UINT_PTR, DWORD_PTR ref)
    {
        auto* self = reinterpret_cast<Palette*>(ref);
        switch (m)
        {
        case WM_LBUTTONDOWN:
            self->brickDragId_ = GetDlgCtrlID(h);
            self->brickDragging_ = false;
            GetCursorPos(&self->dragStart_);
            break;
        case WM_MOUSEMOVE:
            if (self->brickDragId_ >= 0 && (w & MK_LBUTTON) && !self->brickDragging_)
            {
                POINT p{}; GetCursorPos(&p);
                if (std::abs(p.x - self->dragStart_.x) > 6 ||
                    std::abs(p.y - self->dragStart_.y) > 6)
                {
                    self->brickDragging_ = true;
                    ReleaseCapture();
                    SetCapture(self->wnd_);
                    return 0;
                }
            }
            break;
        case WM_LBUTTONUP:
            self->brickDragId_ = -1;
            break;
        }
        return DefSubclassProc(h, m, w, l);
    }

    static LRESULT CALLBACK RenameEditProc(HWND h, UINT m, WPARAM w, LPARAM l,
                                           UINT_PTR, DWORD_PTR ref)
    {
        auto* self = reinterpret_cast<Palette*>(ref);
        if (!self) return DefSubclassProc(h, m, w, l);

        switch (m)
        {
        case WM_KEYDOWN:
            if (w == VK_RETURN) { self->FinishRename(true); return 0; }
            if (w == VK_ESCAPE) { self->FinishRename(false); return 0; }
            break;
        case WM_CHAR:
            if (w == VK_RETURN || w == VK_ESCAPE) return 0;
            break;
        case WM_KILLFOCUS:
            self->FinishRename(true);
            return 0;
        case WM_NCDESTROY:
            RemoveWindowSubclass(h, RenameEditProc, 1);
            break;
        }
        return DefSubclassProc(h, m, w, l);
    }

    // ─── Window management ──────────────────────────────────────
    bool EnsureWindow()
    {
        if (wnd_) return true;
        wnd_ = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED, kPaletteClass,
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

        // Filter toggles: Shaders | Maps (clicking active one = show ALL)
        const int tabGap = 3;
        int tabW = (cw - tabGap) / 2;
        CreateWindowExW(0, L"BUTTON", L"Shaders",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            pad, y, tabW, 22, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTabMatId)), hInstance, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Maps",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            pad + tabW + tabGap, y, cw - tabW - tabGap, 22, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTabMapId)), hInstance, nullptr);
        y += 26;

        // Command row: LINK | SHLL | Scene | Apply
        const int gap = 3;
        int btnW4 = (cw - 3 * gap) / 4;
        link_ = CreateWindowExW(0, L"BUTTON", L"LINK",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            pad, y, btnW4, 22, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLinkId)), hInstance, nullptr);
        shll_ = CreateWindowExW(0, L"BUTTON", L"SHLL",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            pad + btnW4 + gap, y, btnW4, 22, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kShllId)), hInstance, nullptr);
        scene_ = CreateWindowExW(0, L"BUTTON", L"Scene",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            pad + 2 * (btnW4 + gap), y, btnW4, 22, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSceneId)), hInstance, nullptr);
        apply_ = CreateWindowExW(0, L"BUTTON", L"Apply",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            pad + 3 * (btnW4 + gap), y, cw - 3 * (btnW4 + gap), 22, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kApplyId)), hInstance, nullptr);
        y += 26;

        // Results list (owner-drawn)
        listBaseY_ = y;
        int listH = cr.bottom - y - 26;
        list_ = CreateWindowExW(0, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY |
            LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_NODATA,
            pad, y, cw, listH, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kListId)), hInstance, nullptr);
        SetWindowSubclass(list_, ListProc, 1, reinterpret_cast<DWORD_PTR>(this));

        // Status bar
        status_ = CreateWindowExW(0, L"STATIC", L"Ready",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            pad, cr.bottom - 22, cw, 18, h, nullptr, hInstance, nullptr);
        SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(Theme::fontUI), TRUE);

        // Dark scrollbar on list
        HMODULE hUx = LoadLibraryW(L"uxtheme.dll");
        if (hUx)
        {
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
            if (item.kind == ItemKind::ClassMap || item.kind == ItemKind::SceneMap) tc = Theme::mapClr;
            if (item.kind == ItemKind::SceneMaterial || item.kind == ItemKind::SceneMap) tc = Theme::sceneClr;
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

        // Left side: pin indicator + item name
        bool isPinned = std::find(filePins_.begin(), filePins_.end(), item.key) != filePins_.end();
        RECT lr = dis->rcItem;
        lr.left += 8;
        if (isPinned) {
            SetTextColor(dis->hDC, Theme::accent);
            DrawTextW(dis->hDC, L"\u25C6 ", 2, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            lr.left += 14;
        }
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
        if (id == kTabMatId)      active = (tab_ == TabMode::Materials);
        else if (id == kTabMapId) active = (tab_ == TabMode::Maps);
        else if (id == kSceneId)  active = sceneOnly_;
        else if (id == kApplyId)  active = applyToSel_;
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
        EnsureOSLCategories();
        if (IsSceneOnly()) RefreshSceneCache();
        Rebuild(true);
        RebuildBrickUI();
        POINT p{}; GetCursorPos(&p);
        RECT wa{};
        MONITORINFO mi{ sizeof(mi) };
        HMONITOR monitor = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
        if (monitor && GetMonitorInfoW(monitor, &mi)) wa = mi.rcWork;
        else SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        int x = std::clamp(static_cast<long>(p.x - kWindowWidth / 2),
                           wa.left, wa.right - static_cast<long>(kWindowWidth));
        int y = std::clamp(static_cast<long>(p.y + 20),
                           wa.top, wa.bottom - static_cast<long>(kWindowHeight));

        HWND mainMax = GetCOREInterface() ? GetCOREInterface()->GetMAXHWnd() : nullptr;
        
        HWND testHwnds[] = { WindowFromPoint(p), GetForegroundWindow() };
        bool isPalette = false;
        bool isSme = false;
        HWND dlg = nullptr;

        for (HWND h : testHwnds) {
            if (!h) continue;
            HWND curr = h;
            while (curr && curr != mainMax) {
                wchar_t title[256] = {};
                GetWindowTextW(curr, title, 256);
                std::wstring t = title;
                for (auto& c : t) c = towlower(c);
                
                if (t.find(L"slate") != std::wstring::npos || t.find(L"sme") != std::wstring::npos) {
                    isSme = true;
                    break;
                }
                
                if (t.find(L"material editor") != std::wstring::npos ||
                    t.find(L"material/map browser") != std::wstring::npos ||
                    t.find(L"material browser") != std::wstring::npos ||
                    t.find(L"material palette") != std::wstring::npos) {
                    isPalette = true;
                    dlg = curr;
                    break;
                }
                curr = GetParent(curr);
            }
            if (isPalette || isSme) break;
        }
        
        if (isPalette && !isSme && dlg) {
            RECT sr; GetWindowRect(dlg, &sr);
            long distR = p.x > sr.right ? p.x - sr.right : sr.right - p.x;
            long distL = p.x > sr.left ? p.x - sr.left : sr.left - p.x;
            
            if (distR <= distL) {
                x = sr.right + 8;
                if (x + kWindowWidth > wa.right) x = sr.left - kWindowWidth - 8;
            } else {
                x = sr.left - kWindowWidth - 8;
                if (x < wa.left) x = sr.right + 8;
            }
            
            x = std::clamp(static_cast<long>(x), wa.left, wa.right - static_cast<long>(kWindowWidth));
            
            // Vertical detection (thirds matching)
            long paletteH = sr.bottom - sr.top;
            if (p.y < sr.top + paletteH / 3) {
                y = sr.top;
            } else if (p.y > sr.bottom - paletteH / 3) {
                y = sr.bottom - kWindowHeight;
            } else {
                y = sr.top + paletteH / 2 - kWindowHeight / 2;
            }
            
            y = std::clamp(static_cast<long>(y), wa.top, wa.bottom - static_cast<long>(kWindowHeight));
        }

        // Position while fully transparent, then reveal at the final location.
        // This preserves the no-flicker spawn order without pumping a nested
        // message loop from inside the mouse hook.
        SetLayeredWindowAttributes(wnd_, 0, 0, LWA_ALPHA);
        SetWindowPos(wnd_, HWND_TOPMOST, x, y, kWindowWidth, kWindowHeight,
            SWP_NOACTIVATE);
        ShowWindow(wnd_, SW_SHOW);
        SetLayeredWindowAttributes(wnd_, 0, 255, LWA_ALPHA);
        SetActiveWindow(wnd_);
        SetForegroundWindow(wnd_);
        SetFocus(edit_);
        SendMessageW(edit_, EM_SETSEL, 0, -1);
        // Stop Max from stealing keyboard input (M, S, P etc. are Max shortcuts)
        if (!acceleratorsDisabled_) {
            DisableAccelerators();
            acceleratorsDisabled_ = true;
        }
    }

    void Hide()
    {
        CancelPendingRebuild();
        FinishRename(true);
        HidePreview();
        ShowWindow(wnd_, SW_HIDE);
        SetLayeredWindowAttributes(wnd_, 0, 255, LWA_ALPHA);
        dragging_ = false;
        dragIndex_ = -1;
        // Restore Max keyboard shortcuts
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
        if (id == kLinkId) {
            ExecuteMAXScriptScript(kLinkScript, MAXScript::ScriptSource::Dynamic);
            return;
        }
        if (id == kShllId) {
            ExecuteShellCommand(shllRes_);
            return;
        }
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
        if (id == kTabMatId || id == kTabMapId)
        {
            // Toggle: clicking active tab deactivates it (shows ALL)
            TabMode clicked = (id == kTabMatId) ? TabMode::Materials : TabMode::Maps;
            tab_ = (tab_ == clicked) ? TabMode::All : clicked;
            for (int tid : {kTabMatId, kTabMapId})
                if (HWND tw = GetDlgItem(wnd_, tid)) InvalidateRect(tw, nullptr, FALSE);
            CancelPendingRebuild();
            Rebuild(true);
            return;
        }
        if (id == kListId && code == LBN_SELCHANGE) {
            UpdatePreviewForSelection();
            return;
        }
        if (id == kListId && code == LBN_DBLCLK)
            { ActivateCurrent(false); return; }
        // Brick favorite click → activate that alias
        if (id >= kBrickBase && id < kBrickBase + kBrickMax) {
            int bi = id - kBrickBase;
            if (bi >= 0 && bi < static_cast<int>(brickFavs_.size()))
                ActivateAlias(brickFavs_[bi].alias);
            return;
        }
    }

    void UpdatePreviewForSelection()
    {
        // List clicks post a delayed refresh. Activation can hide the palette
        // before that message is dispatched, so never let a stale refresh
        // resurrect the independent topmost preview popup.
        if (!wnd_ || !IsWindowVisible(wnd_)) {
            HidePreview();
            return;
        }
        int sel = (int)SendMessage(list_, LB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < (int)filtered_.size() && activeItems_) {
            const Item& item = (*activeItems_)[filtered_[sel]];
            if (item.live) {
                std::wstring fn = GetTexmapFilename(item.live);
                if (!fn.empty()) { ShowPreview(fn, wnd_); return; }
            }
        }
        HidePreview();
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

    // Build a map of OSL shader name → folder category
    void ScanOSLFolder(const std::wstring& dir, const std::wstring& category,
                       std::map<std::wstring, std::wstring>& nameToCategory)
    {
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW((dir + L"\\*.osl").c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                std::wstring filename(fd.cFileName);
                size_t dot = filename.rfind(L'.');
                std::wstring name = (dot != std::wstring::npos) ? filename.substr(0, dot) : filename;
                std::wstring norm = Normalize(name, false);
                if (!norm.empty()) {
                    nameToCategory[norm] = category;
                    std::wstring versionless = StripOSLVersionSuffix(norm);
                    if (versionless != norm && nameToCategory.find(versionless) == nameToCategory.end())
                        nameToCategory.emplace(std::move(versionless), category);
                }
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
        hFind = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (fd.cFileName[0] == L'.') continue;
                ScanOSLFolder(dir + L"\\" + fd.cFileName, std::wstring(fd.cFileName), nameToCategory);
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
    }

    void EnsureClassCache()
    {
        if (classCacheReady_ || classCacheBuilding_) return;

        classCacheBuilding_ = true;
        classItems_.clear();
        classItems_.reserve(1024);
        AddClassList(MATERIAL_CLASS_ID, ItemKind::ClassMaterial, classItems_);
        AddClassList(TEXMAP_CLASS_ID, ItemKind::ClassMap, classItems_);

        std::sort(classItems_.begin(), classItems_.end(),
            [](const Item& a, const Item& b) { return a.label < b.label; });

        classCacheReady_ = true;
        oslCategoryReady_ = false;
        classCacheBuilding_ = false;
    }

    void EnsureOSLCategories()
    {
        if (!classCacheReady_ || oslCategoryReady_ || oslCategoryBuilding_) return;

        oslCategoryBuilding_ = true;
        std::map<std::wstring, std::wstring> oslCategories;

        auto scanIfExists = [&](const std::wstring& dir, const std::wstring& cat) {
            if (GetFileAttributesW(dir.c_str()) != INVALID_FILE_ATTRIBUTES)
                ScanOSLFolder(dir, cat, oslCategories);
        };

        Interface* ip = GetCOREInterface();
        if (ip) {
            MSTR maxRoot = ip->GetDir(APP_MAX_SYS_ROOT_DIR);
            scanIfExists(std::wstring(maxRoot.data()) + L"\\OSL", L"OSL");
        }

        std::wstring appPlugins = L"C:\\ProgramData\\Autodesk\\ApplicationPlugins";
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((appPlugins + L"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (fd.cFileName[0] == L'.') continue;
                std::wstring plugDir = appPlugins + L"\\" + fd.cFileName + L"\\Contents";
                WIN32_FIND_DATAW fd2;
                HANDLE h2 = FindFirstFileW((plugDir + L"\\*").c_str(), &fd2);
                if (h2 != INVALID_HANDLE_VALUE) {
                    do {
                        if (!(fd2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                        if (fd2.cFileName[0] == L'.') continue;
                        std::wstring oslDir = plugDir + L"\\" + fd2.cFileName + L"\\Contents\\OSL";
                        scanIfExists(oslDir, std::wstring(fd2.cFileName));
                    } while (FindNextFileW(h2, &fd2));
                    FindClose(h2);
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }

        if (!oslCategories.empty()) {
            for (auto& item : classItems_) {
                auto it = oslCategories.find(item.key);
                if (it == oslCategories.end())
                    it = oslCategories.find(StripOSLVersionSuffix(item.key));
                if (it == oslCategories.end()) continue;
                item.category = it->second;
                item.search = Normalize(item.label + L" " + it->second + L" OSL", true);
            }
            std::sort(classItems_.begin(), classItems_.end(),
                [](const Item& a, const Item& b) { return a.label < b.label; });
        }

        oslCategoryReady_ = true;
        oslCategoryBuilding_ = false;

        if (wnd_ && IsWindowVisible(wnd_) && !IsSceneOnly()) {
            Rebuild(true);
        }
    }

    void CollectSceneItem(MtlBase* m, std::set<MtlBase*>& visited)
    {
        if (!m || visited.count(m)) return;
        visited.insert(m);

        Item item;
        MSTR className = m->ClassName();
        item.label = m->GetName().Length()
            ? std::wstring(m->GetName().data())
            : std::wstring(className.data());
        item.normLabel = Normalize(item.label, true);
        item.search = Normalize(
            item.label + L" " + std::wstring(className.data()), true);
        item.key = Normalize(item.label, false);
        item.kind = (m->SuperClassID() == MATERIAL_CLASS_ID)
            ? ItemKind::SceneMaterial : ItemKind::SceneMap;
        item.category = std::wstring(className.data());
        item.live = m;
        sceneItems_.push_back(std::move(item));

        // Recurse into sub-texmaps (child maps of materials/texmaps)
        for (int s = 0; s < m->NumSubTexmaps(); s++) {
            Texmap* sub = m->GetSubTexmap(s);
            if (sub) CollectSceneItem(sub, visited);
        }
        // Recurse into sub-materials
        if (m->SuperClassID() == MATERIAL_CLASS_ID) {
            Mtl* mtl = static_cast<Mtl*>(m);
            for (int s = 0; s < mtl->NumSubMtls(); s++) {
                Mtl* sub = mtl->GetSubMtl(s);
                if (sub) CollectSceneItem(sub, visited);
            }
        }
    }

    void RefreshSceneCache()
    {
        sceneItems_.clear();
        Interface* ip = GetCOREInterface();
        if (ip && ip->GetSceneMtls())
        {
            MtlBaseLib* lib = ip->GetSceneMtls();
            std::set<MtlBase*> visited;
            for (int i = 0; i < lib->Count(); ++i) {
                MtlBase* m = (*lib)[i];
                if (m) CollectSceneItem(m, visited);
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
            bool isScene = (item.kind == ItemKind::SceneMaterial || item.kind == ItemKind::SceneMap);
            if (sceneOnly != isScene) return false;
            if (tab_ == TabMode::Materials &&
                item.kind != ItemKind::ClassMaterial && item.kind != ItemKind::SceneMaterial) return false;
            if (tab_ == TabMode::Maps &&
                item.kind != ItemKind::ClassMap && item.kind != ItemKind::SceneMap) return false;
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
            std::stable_sort(scored.begin(), scored.end(),
                [](const Scored& a, const Scored& b) { return a.score > b.score; });

        filtered_.clear();
        filtered_.reserve(scored.size());

        // File-pinned items go first (only when not actively searching)
        if (tokens.empty() && !filePins_.empty()) {
            std::set<int> pinIndices;
            for (const auto& pin : filePins_) {
                for (size_t i = 0; i < source.size(); i++) {
                    if (source[i].key == pin && passesTab(source[i])) {
                        filtered_.push_back(static_cast<int>(i));
                        pinIndices.insert(static_cast<int>(i));
                        break;
                    }
                }
            }
            for (const auto& s : scored)
                if (pinIndices.find(s.idx) == pinIndices.end())
                    filtered_.push_back(s.idx);
        } else {
            for (const auto& s : scored) filtered_.push_back(s.idx);
        }

        SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
        SendMessageW(list_, LB_RESETCONTENT, 0, 0);
        SendMessageW(list_, LB_SETCOUNT, static_cast<WPARAM>(filtered_.size()), 0);
        if (!filtered_.empty()) SendMessageW(list_, LB_SETCURSEL, 0, 0);
        SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(list_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        if (wnd_ && IsWindowVisible(wnd_)) UpdatePreviewForSelection();
        else HidePreview();

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

        // Name new instances
        if (isNew)
            mb->SetName(MSTR((item.label + L"_" +
                std::to_wstring((GetTickCount() % 9000) + 1000)).c_str()));

        // Get medit slot
        int slot = 0;
        if (IMtlEditInterface* me = GetMtlEditInterface())
            slot = std::max(0, me->GetActiveMtlSlot());

        theHold.Begin();

        if (drag)
        {
            // ── Drag path ───────────────────────────────────────
            // 1. Try context-aware drop (SME, Qt material parameters, legacy DAD controls)
            bool dropped = TryDADDrop(mb);

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
            // ── Click path: context-aware placement ─────────────
            // Check if SME is open
            FPValue smeResult;
            BOOL smeOk = ExecuteMAXScriptScript(L"SME.isOpen()",
                MAXScript::ScriptSource::Dynamic, TRUE, &smeResult);
            bool smeOpen = smeOk && smeResult.type == TYPE_BOOL && smeResult.b;

            if (smeOpen) {
                // SME is open — drop into it
                ip->PutMtlToMtlEditor(mb, slot);
                ExecuteMAXScriptScript(kSmeAtSpawnScript, MAXScript::ScriptSource::Dynamic);
                // Also assign to selection if Apply is on
                if (applyToSel_ && isMat && ip->GetSelNodeCount() > 0) {
                    Mtl* mtl = static_cast<Mtl*>(mb);
                    for (int i = 0; i < ip->GetSelNodeCount(); ++i)
                        if (INode* n = ip->GetSelNode(i)) n->SetMtl(mtl);
                }
            } else if (isMat && applyToSel_ && ip->GetSelNodeCount() > 0) {
                // Apply on + has selection — assign to selected objects
                Mtl* mtl = static_cast<Mtl*>(mb);
                for (int i = 0; i < ip->GetSelNodeCount(); ++i)
                    if (INode* n = ip->GetSelNode(i)) n->SetMtl(mtl);
                ip->PutMtlToMtlEditor(mb, slot);
            } else {
                // Always retain the created item in a material-editor slot.
                // GetMtlEditInterface() can be unavailable transiently even
                // though the core interface still accepts the slot update.
                ip->PutMtlToMtlEditor(mb, slot);
            }
            Hide();
        }

        theHold.Accept(_T("Assign Material"));

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

    // ─── File-local pins (stored in max file via rootNode appData) ──
    std::wstring GetItemAlias(int filteredIdx)
    {
        if (!activeItems_ || filteredIdx < 0 ||
            filteredIdx >= static_cast<int>(filtered_.size())) return {};
        int si = filtered_[filteredIdx];
        if (si < 0 || si >= static_cast<int>(activeItems_->size())) return {};
        return (*activeItems_)[si].key;
    }
    std::wstring GetItemLabel(int filteredIdx)
    {
        if (!activeItems_ || filteredIdx < 0 ||
            filteredIdx >= static_cast<int>(filtered_.size())) return {};
        int si = filtered_[filteredIdx];
        if (si < 0 || si >= static_cast<int>(activeItems_->size())) return {};
        return (*activeItems_)[si].label;
    }

    void ToggleFilePin(int filteredIdx)
    {
        std::wstring alias = GetItemAlias(filteredIdx);
        if (alias.empty()) return;
        auto it = std::find(filePins_.begin(), filePins_.end(), alias);
        const bool wasPinned = it != filePins_.end();
        if (wasPinned) filePins_.erase(it);
        else filePins_.insert(filePins_.begin(), alias);
        SaveFilePins();
        Rebuild(true);
        SetStatus(wasPinned ? L"Unpinned." : L"Pinned.");
    }

    void SaveFilePins()
    {
        FlowState_SaveSettings(); // unified save
    }

    // ─── Persistent brick favorites (saved in FlowState.cfg) ────
    void ToggleBrickFav(int filteredIdx)
    {
        if (!activeItems_ || filteredIdx < 0 ||
            filteredIdx >= static_cast<int>(filtered_.size())) return;
        const int sourceIdx = filtered_[filteredIdx];
        if (sourceIdx < 0 || sourceIdx >= static_cast<int>(activeItems_->size())) return;
        if ((*activeItems_)[sourceIdx].live) {
            SetStatus(L"Scene items cannot be saved as persistent favorites.");
            return;
        }
        std::wstring alias = GetItemAlias(filteredIdx);
        if (alias.empty()) return;
        // Check if already a brick fav
        auto it = std::find_if(brickFavs_.begin(), brickFavs_.end(),
            [&](const BrickFav& bf) { return bf.alias == alias; });
        if (it != brickFavs_.end()) {
            brickFavs_.erase(it);
            SetStatus(L"Removed from favorites.");
        } else {
            if (static_cast<int>(brickFavs_.size()) >= kBrickMax) {
                SetStatus(L"Max favorites reached.");
                return;
            }
            std::wstring lbl = GetItemLabel(filteredIdx);
            // Default label: first 3 letters uppercase
            std::wstring shortLbl;
            for (size_t i = 0; i < lbl.size() && shortLbl.size() < 3; i++)
                if (iswalnum(lbl[i])) shortLbl += towupper(lbl[i]);
            brickFavs_.push_back({alias, shortLbl});
            SetStatus(L"Added to favorites.");
        }
        SaveBrickFavs();
        RebuildBrickUI();
    }

    void RemoveBrickFav(int brickIdx)
    {
        if (brickIdx < 0 || brickIdx >= static_cast<int>(brickFavs_.size())) return;
        brickFavs_.erase(brickFavs_.begin() + brickIdx);
        SaveBrickFavs();
        RebuildBrickUI();
        SetStatus(L"Removed from favorites.");
    }

    void SaveBrickFavs()
    {
        FlowState_SaveSettings(); // unified save
    }

    void RebuildBrickUI(bool commitRename = true)
    {
        FinishRename(commitRename);
        // Destroy existing brick buttons
        for (HWND bh : brickBtns_)
            if (bh) DestroyWindow(bh);
        brickBtns_.clear();
        if (!wnd_) return;

        RECT cr; GetClientRect(wnd_, &cr);
        int pad = 8, cw = cr.right - 2 * pad;

        if (brickFavs_.empty()) {
            // No bricks — restore list to original position
            SetWindowPos(list_, nullptr, pad, listBaseY_, cw,
                (cr.bottom - 26) - listBaseY_, SWP_NOZORDER);
            InvalidateRect(wnd_, nullptr, TRUE);
            return;
        }

        // Calculate brick layout — buttons stretch to fill row width
        int bwMin = 50, bh2 = 22, gap = 3;
        int cols = std::max(1, (cw + gap) / (bwMin + gap));
        int bw = (cw - (cols - 1) * gap) / cols; // stretch to fill
        int x = pad, y2 = listBaseY_;
        int maxY = y2;
        int col = 0;
        for (int i = 0; i < static_cast<int>(brickFavs_.size()) && i < kBrickMax; i++) {
            if (col >= cols) { col = 0; x = pad; y2 += bh2 + gap; }
            HWND btn = CreateWindowExW(0, L"BUTTON", brickFavs_[i].label.c_str(),
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                x, y2, bw, bh2, wnd_,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBrickBase + i)),
                hInstance, nullptr);
            SetWindowSubclass(btn, BrickBtnProc, 1, reinterpret_cast<DWORD_PTR>(this));
            brickBtns_.push_back(btn);
            maxY = y2 + bh2 + gap;
            x += bw + gap;
            col++;
        }

        // Move list below brick area
        SetWindowPos(list_, nullptr, pad, maxY, cw,
            (cr.bottom - 26) - maxY, SWP_NOZORDER);
        InvalidateRect(wnd_, nullptr, TRUE);
    }

    void RenameBrickFav(int brickIdx)
    {
        if (brickIdx < 0 || brickIdx >= static_cast<int>(brickFavs_.size())) return;
        FinishRename(true);
        if (brickIdx >= static_cast<int>(brickBtns_.size())) return;
        // Simple inline rename: prompt via small EDIT overlay
        HWND btn = brickBtns_[brickIdx];
        if (!btn || !IsWindow(btn)) return;
        RECT br; GetWindowRect(btn, &br);
        POINT p = {br.left, br.top}; ScreenToClient(wnd_, &p);
        HWND ed = CreateWindowExW(0, L"EDIT", brickFavs_[brickIdx].label.c_str(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_CENTER | ES_AUTOHSCROLL,
            p.x, p.y, 50, 22, wnd_, nullptr, hInstance, nullptr);
        SendMessageW(ed, WM_SETFONT, reinterpret_cast<WPARAM>(Theme::fontUI), TRUE);
        SendMessageW(ed, EM_SETLIMITTEXT, 4, 0); // max 4 chars
        SendMessageW(ed, EM_SETSEL, 0, -1);
        SetWindowSubclass(ed, RenameEditProc, 1, reinterpret_cast<DWORD_PTR>(this));
        SetFocus(ed);
        // Store info for completion
        renameEdit_ = ed;
        renameIdx_ = brickIdx;
    }

    void FinishRename(bool commit = true)
    {
        if (renaming_ || !renameEdit_) return;

        renaming_ = true;
        HWND edit = renameEdit_;
        int idx = renameIdx_;
        renameEdit_ = nullptr;
        renameIdx_ = -1;

        std::wstring lbl;
        if (commit && idx >= 0 && idx < static_cast<int>(brickFavs_.size()) && IsWindow(edit)) {
            wchar_t buf[8] = {};
            GetWindowTextW(edit, buf, 8);
            lbl = buf;
            if (!lbl.empty()) {
                brickFavs_[idx].label = lbl;
                SaveBrickFavs();
                if (idx < static_cast<int>(brickBtns_.size()) && brickBtns_[idx] && IsWindow(brickBtns_[idx]))
                    SetWindowTextW(brickBtns_[idx], lbl.c_str());
            }
        }

        if (edit && IsWindow(edit)) {
            RemoveWindowSubclass(edit, RenameEditProc, 1);
            DestroyWindow(edit);
        }

        renaming_ = false;
    }

    // ─── State ──────────────────────────────────────────────────
    bool inited_     = false;
    HWND wnd_       = nullptr;
    HWND edit_      = nullptr;
    HWND list_      = nullptr;
    HWND link_      = nullptr;
    HWND shll_      = nullptr;
    HWND scene_     = nullptr;
    HWND apply_     = nullptr;
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
    bool classCacheBuilding_ = false;
    bool oslCategoryReady_ = false;
    bool oslCategoryBuilding_ = false;
    bool forcedAliasRetry_ = false;
    bool sceneCacheReady_ = false;
    bool rebuildPending_ = false;
    bool  dragging_  = false;
    int   dragIndex_ = -1;
    POINT dragStart_ = {};
    RECT  closeRect_ = {};
    bool  hoverClose_ = false;
    bool  trackingMouse_ = false;
    bool  sceneOnly_  = false;
    bool  applyToSel_ = false;  // Apply material to selection (off by default — just creates in SME)
    bool  acceleratorsDisabled_ = false;
    int   listBaseY_  = 0;
    // Dual favorites
    std::vector<HWND> brickBtns_;            // brick button HWNDs
    HWND  renameEdit_ = nullptr;
    int   renameIdx_  = -1;
    bool  renaming_   = false;
    int   brickDragId_   = -1;
    bool  brickDragging_ = false;
    int   shllRes_       = 256; // SHLL preview resolution
};

// ── Internal helpers for unified config (accessible from Exported API below) ──
static void WritePinsSectionImpl(FILE* f) {
    fwprintf(f, L"[pins]\n");
    auto& p = Palette::Get();
    for (auto& pin : p.filePins_)
        fwprintf(f, L"%s\n", pin.c_str());
}

static void WriteBricksSectionImpl(FILE* f) {
    fwprintf(f, L"[bricks]\n");
    auto& p = Palette::Get();
    for (auto& bf : p.brickFavs_)
        fwprintf(f, L"%s|%s\n", bf.alias.c_str(), bf.label.c_str());
}

static void ReadPinsLineImpl(const std::wstring& line) {
    if (line.empty()) return;
    auto& pins = Palette::Get().filePins_;
    if (std::find(pins.begin(), pins.end(), line) == pins.end())
        pins.push_back(line);
}

static void ReadBricksLineImpl(const std::wstring& line) {
    size_t sep = line.find(L'|');
    if (sep == std::wstring::npos || sep == 0) return;
    std::wstring alias = line.substr(0, sep);
    std::wstring label = line.substr(sep + 1, 4);
    if (label.empty()) return;
    auto& bricks = Palette::Get().brickFavs_;
    auto existing = std::find_if(bricks.begin(), bricks.end(),
        [&](const BrickFav& item) { return item.alias == alias; });
    if (existing == bricks.end() && static_cast<int>(bricks.size()) < kBrickMax)
        bricks.push_back({std::move(alias), std::move(label)});
}

static void ClearPersistentImpl() {
    Palette::Get().filePins_.clear();
    Palette::Get().brickFavs_.clear();
}

} // anonymous namespace

// ── Exported API ────────────────────────────────────────────────
bool Init(HINSTANCE, bool lightTheme) { return Palette::Get().Init(lightTheme); }
void Shutdown()      { Palette::Get().Shutdown(); }
void Toggle()        { Palette::Get().Toggle(); }
bool IsOpen()        { return Palette::Get().IsOpen(); }
void ReloadTheme(bool lightTheme) { Palette::Get().ReloadTheme(lightTheme); }

void WritePinsSection(FILE* f)                  { WritePinsSectionImpl(f); }
void WriteBricksSection(FILE* f)                { WriteBricksSectionImpl(f); }
void ReadPinsLine(const std::wstring& line)     { ReadPinsLineImpl(line); }
void ReadBricksLine(const std::wstring& line)   { ReadBricksLineImpl(line); }
void ClearPersistent()                          { ClearPersistentImpl(); }

} // namespace PowerShader
