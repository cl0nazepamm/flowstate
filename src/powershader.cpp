#include "powershader.h"
#include <windows.h>
#include <UIAutomation.h>
#include <commctrl.h>
#include <gdiplus.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstdint>
#include <cwctype>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <thread>
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

MtlBase* GetParameterEditorOwner()
{
    FPValue result;
    const BOOL ok = ExecuteMAXScriptScript(
        L"try(if SME.isOpen() then SME.GetMtlInParamEditor() else false)catch(undefined)",
        MAXScript::ScriptSource::Dynamic, TRUE, &result);
    if (!ok) return nullptr;

    if (result.type == TYPE_BOOL)
    {
        if (result.b) return nullptr;
        IMtlEditInterface* medit = GetMtlEditInterface();
        return medit ? medit->GetCurMtl() : nullptr;
    }
    if (result.type == TYPE_MTL) return result.mtl;
    if (result.type == TYPE_TEXMAP) return result.tex;
    if (result.type != TYPE_REFTARG || !result.r) return nullptr;

    const SClass_ID type = result.r->SuperClassID();
    if (type != MATERIAL_CLASS_ID && type != TEXMAP_CLASS_ID) return nullptr;
    return static_cast<MtlBase*>(result.r);
}

// Qt pickers expose the ParamBlock2 internal name through UI Automation. Native
// material panels instead expose a picker HWND and a visible row label.
bool TryParameterEditorDrop(MtlBase* dropped)
{
    if (!dropped) return false;

    MtlBase* owner = GetParameterEditorOwner();
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

    // 2. Material/map parameter picker in Compact or Slate's Parameter Editor.
    if (TryParameterEditorDrop(mb)) return true;

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
constexpr int kTabFavId  = 1009;
constexpr int kToolsMenuId = 1010;
constexpr int kAutoApplyId = 1011;
constexpr int kShllRes128Id  = 1012;
constexpr int kShllRes256Id  = 1013;
constexpr int kShllRes512Id  = 1014;
constexpr int kShllRes1024Id = 1015;
constexpr int kWindowWidth  = 380;
constexpr int kWindowHeight = 540;   // maximum height; Favorites view shrinks to fit
constexpr int kHeaderH      = 34;
constexpr int kListItemH    = 30;    // owner-draw row height (WM_MEASUREITEM)
constexpr int kBottomM      = 26;    // space below the list for the status bar
constexpr UINT_PTR kSearchTimerId = 1;
constexpr UINT kSearchDebounceMs = 14;
constexpr UINT_PTR kPreviewTimerId = 2;
constexpr UINT kPreviewDebounceMs = 90;
constexpr UINT kRemoveBrickMessage = WM_APP + 1;

enum class TabMode { All, Materials, Maps };
enum class ItemKind { ClassMaterial, ClassMap, SceneMaterial, SceneMap };
enum class BrickGesture { None, LeftArmed, LeftDragging, MiddleArmed, MiddleDragging };

struct Item
{
    std::wstring label;
    std::wstring normLabel;   // pre-normalized for scoring
    std::wstring search;
    std::wstring key;         // legacy normalized alias (search/activation)
    std::wstring favoriteKey; // stable, collision-free persistent identity
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
    std::wstring alias;  // stable favorite key (legacy aliases migrate on load)
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

// Resolve U/V from each selected map's animatable sub-tree instead of renderer
// classes or property paths. Max has no semantic "U tiling" flag, so use track
// names only to recognize a tiling concept and its U/V (or X/Y, S/T) axis.
static const wchar_t* kLinkScript =
    L"(\n"
    L"fn fsNormTrackName trackName = (\n"
    L"local s = toLower (trackName as string)\n"
    L"for token in #(\"_\", \" \", \"-\", \".\", \"(\", \")\", \"[\", \"]\", \":\") do s = substituteString s token \"\"\n"
    L"s)\n"
    L"fn fsAxisKey trackName = (\n"
    L"local n = fsNormTrackName trackName\n"
    L"local score = case of (\n"
    L"((findString n \"tiling\") != undefined): 400\n"
    L"((findString n \"tile\") != undefined): 300\n"
    L"((findString n \"repeat\") != undefined): 200\n"
    L"((findString n \"scale\") != undefined): 100\n"
    L"default: 0)\n"
    L"if score == 0 or n.count < 2 do return undefined\n"
    L"local axis = 0\n"
    L"local stem = n\n"
    L"local first = n[1]\n"
    L"local last = n[n.count]\n"
    L"case of (\n"
    L"(last == \"x\"): (axis = 1; stem = substring n 1 (n.count - 1))\n"
    L"(last == \"y\"): (axis = 2; stem = substring n 1 (n.count - 1))\n"
    L"(first == \"x\"): (axis = 1; stem = substring n 2 (n.count - 1))\n"
    L"(first == \"y\"): (axis = 2; stem = substring n 2 (n.count - 1))\n"
    L"(first == \"u\"): (axis = 1; stem = substring n 2 (n.count - 1))\n"
    L"(first == \"v\"): (axis = 2; stem = substring n 2 (n.count - 1))\n"
    L"(last == \"u\"): (axis = 1; stem = substring n 1 (n.count - 1))\n"
    L"(last == \"v\"): (axis = 2; stem = substring n 1 (n.count - 1))\n"
    L"(first == \"s\"): (axis = 1; stem = substring n 2 (n.count - 1))\n"
    L"(first == \"t\"): (axis = 2; stem = substring n 2 (n.count - 1))\n"
    L"(last == \"s\"): (axis = 1; stem = substring n 1 (n.count - 1))\n"
    L"(last == \"t\"): (axis = 2; stem = substring n 1 (n.count - 1)))\n"
    L"if axis == 0 then undefined else #(axis, stem, score))\n"
    L"fn fsFindScalarUvAxes scalarTracks = (\n"
    L"local best = undefined\n"
    L"local bestScore = -1\n"
    L"for uItem in scalarTracks do (\n"
    L"local uKey = fsAxisKey uItem[1]\n"
    L"if uKey != undefined and uKey[1] == 1 do (\n"
    L"for vItem in scalarTracks do (\n"
    L"local vKey = fsAxisKey vItem[1]\n"
    L"if vKey != undefined and vKey[1] == 2 and vKey[2] == uKey[2] do (\n"
    L"local score = uKey[3] + vKey[3]\n"
    L"if score > bestScore do (\n"
    L"best = #(#scalar, uItem[2], vItem[2])\n"
    L"bestScore = score)))))\n"
    L"best)\n"
    L"fn fsFindUvAxes owner depth = (\n"
    L"if owner == undefined or depth > 5 do return undefined\n"
    L"local scalarTracks = #()\n"
    L"local childTracks = #()\n"
    L"local pointTrack = undefined\n"
    L"local count = try(owner.numSubs)catch(0)\n"
    L"for i = 1 to count do (\n"
    L"local track = try(owner[i])catch(undefined)\n"
    L"if track != undefined do (\n"
    L"local trackName = fsNormTrackName (try(getSubAnimName owner i localizedName:false)catch(\"\"))\n"
    L"local trackValue = try(track.value)catch(undefined)\n"
    L"local valueClass = try(classof trackValue)catch(undefined)\n"
    L"if valueClass == Float do append scalarTracks #(trackName, track)\n"
    L"if pointTrack == undefined and valueClass == Point3 and (trackName == \"tiling\" or trackName == \"uvtiling\") do pointTrack = track\n"
    L"local trackObject = try(track.object)catch(undefined)\n"
    L"local isChildMap = try((superclassof trackObject) == textureMap)catch(false)\n"
    L"if not isChildMap do isChildMap = try((superclassof trackValue) == textureMap)catch(false)\n"
    L"if not isChildMap do append childTracks track\n"
    L"))\n"
    L"if pointTrack != undefined do return #(#point3, pointTrack)\n"
    L"local scalarAxes = fsFindScalarUvAxes scalarTracks\n"
    L"if scalarAxes != undefined do return scalarAxes\n"
    L"for child in childTracks do (\n"
    L"local found = fsFindUvAxes child (depth + 1)\n"
    L"if found != undefined do return found\n"
    L")\n"
    L"undefined)\n"
    L"fn fsUvValues axes = (\n"
    L"if axes[1] == #point3 then (\n"
    L"local value = axes[2].value\n"
    L"[value.x, value.y])\n"
    L"else ([axes[2].value, axes[3].value]))\n"
    L"fn fsLinkUvAxes axes uCtrl vCtrl = (\n"
    L"if axes[1] == #point3 then (\n"
    L"local track = axes[2]\n"
    L"local oldTiling = track.value\n"
    L"local tilingCtrl = try(track.controller)catch(undefined)\n"
    L"if classof tilingCtrl != Point3_XYZ do (\n"
    L"tilingCtrl = Point3_XYZ()\n"
    L"tilingCtrl.value = oldTiling\n"
    L"track.controller = tilingCtrl\n"
    L")\n"
    L"tilingCtrl.x.controller = uCtrl\n"
    L"tilingCtrl.y.controller = vCtrl)\n"
    L"else (\n"
    L"local uTrack = axes[2]\n"
    L"local vTrack = axes[3]\n"
    L"uTrack.controller = uCtrl\n"
    L"vTrack.controller = vCtrl)\n"
    L"true)\n"
    L"if SME.isOpen() do (\n"
    L"local v = SME.GetView (SME.activeView)\n"
    L"local sn = v.GetSelectedNodes()\n"
    L"if sn.count >= 2 do (\n"
    L"local axesList = #()\n"
    L"for i = 1 to sn.count do (\n"
    L"local r = try(sn[i].reference)catch(undefined)\n"
    L"local isMap = try((superclassof r) == textureMap)catch(false)\n"
    L"if isMap do (\n"
    L"local axes = try(fsFindUvAxes r 0)catch(undefined)\n"
    L"if axes != undefined do append axesList axes\n"
    L"))\n"
    L"if axesList.count >= 2 do (\n"
    L"local sourceValues = fsUvValues axesList[1]\n"
    L"local uCtrl = bezier_float()\n"
    L"local vCtrl = bezier_float()\n"
    L"uCtrl.value = sourceValues.x\n"
    L"vCtrl.value = sourceValues.y\n"
    L"for axes in axesList do try(fsLinkUvAxes axes uCtrl vCtrl)catch()\n"
    L"true\n"
    L"))))";

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

std::wstring MakeClassFavoriteKey(SClass_ID sid, const Class_ID& classId)
{
    return L"class:" + std::to_wstring(static_cast<unsigned long long>(sid)) +
        L":" + std::to_wstring(static_cast<unsigned long long>(classId.PartA())) +
        L":" + std::to_wstring(static_cast<unsigned long long>(classId.PartB()));
}

std::wstring MakeSceneFavoriteKey(MtlBase* item, const std::wstring& normalizedName)
{
    if (!item) return {};
    const Class_ID classId = item->ClassID();
    return L"scene:" +
        std::to_wstring(static_cast<unsigned long long>(item->SuperClassID())) +
        L":" + std::to_wstring(static_cast<unsigned long long>(classId.PartA())) +
        L":" + std::to_wstring(static_cast<unsigned long long>(classId.PartB())) +
        L":" + normalizedName;
}

bool IsStableFavoriteKey(const std::wstring& key)
{
    return key.compare(0, 6, L"class:") == 0 ||
           key.compare(0, 6, L"scene:") == 0;
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
// Dropping on an object that is part of the current selection assigns to
// every selected object instead (replaces the old Apply toggle).
static const wchar_t* kDragScript =
    L"("
    L"local m=meditMaterials[activeMeditSlot];"
    L"try("
    L"local r=mapScreenToWorldRay mouse.pos;"
    L"local hits=intersectRayScene r;"
    L"local nd=undefined;local best=1e9;"
    L"for h in hits do(local d=distance r.pos h[2].pos;"
    L"if d<best do(best=d;nd=h[1]));"
    L"if nd!=undefined and superclassof m==material do("
    L"if nd.isSelected and selection.count>1 then("
    L"for o in selection do try(o.material=m)catch()"
    L")else(nd.material=m)"
    L")"
    L")catch()"
    L")";

// ═══════════════════════════════════════════════════════════════
//  Texture Preview Popup — fully async, debounced
//
//  Selection changes never touch the disk: they bump a generation counter
//  and restart a short debounce timer. Once the selection has been stable,
//  the file is stat'd, decoded and pre-scaled on a single worker thread
//  (latest-request-wins), and the finished bitmap is handed back to the UI
//  thread through a mutex-guarded slot + an empty window message. Results
//  whose generation no longer matches are discarded, so a slow network EXR
//  can arrive arbitrarily late without ever blocking the list.
// ═══════════════════════════════════════════════════════════════
static ULONG_PTR     g_gdipToken = 0;
static HWND          g_previewWnd = nullptr;
static Gdiplus::Image* g_previewImg = nullptr;   // pre-scaled; UI thread only
static bool          g_previewClassRegistered = false;
constexpr wchar_t kPreviewClass[] = L"FlowStatePreview";
constexpr int kPreviewSize = 128;

static std::atomic<unsigned long long> g_previewGen{ 0 };
static std::wstring g_previewShownPath;          // UI thread only

struct PreviewLoader {
    std::thread th;
    std::mutex mx;
    std::condition_variable cv;
    bool quit = false;
    // Latest pending request — newer submissions overwrite older ones
    bool hasReq = false;
    std::wstring reqPath;
    unsigned long long reqGen = 0;
    HWND notifyWnd = nullptr;
    // Finished result slot (null image = load failed → hide the popup)
    bool readyValid = false;
    Gdiplus::Bitmap* readyImg = nullptr;
    unsigned long long readyGen = 0;
    std::wstring readyPath;

    ~PreviewLoader() {
        // Normal teardown is StopPreviewLoader() from Palette::Shutdown().
        // If static destruction gets here with the thread still alive,
        // detach — joining during DLL unload risks the loader-lock deadlock.
        if (th.joinable()) {
            { std::lock_guard<std::mutex> lk(mx); quit = true; }
            cv.notify_one();
            th.detach();
        }
    }
};
static PreviewLoader g_previewLoader;

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

// Worker thread: decode + aspect-fit pre-scale in one pass. The UI paint
// then blits a tiny cached bitmap at 1:1 instead of re-decoding/re-scaling
// the full-res image on every WM_PAINT.
static Gdiplus::Bitmap* LoadScaledPreview(const std::wstring& path)
{
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return nullptr;
    Gdiplus::Bitmap src(path.c_str(), FALSE);
    if (src.GetLastStatus() != Gdiplus::Ok) return nullptr;
    const int iw = static_cast<int>(src.GetWidth());
    const int ih = static_cast<int>(src.GetHeight());
    if (iw <= 0 || ih <= 0) return nullptr;
    const int box = kPreviewSize - 4;   // matches the paint inset
    const float scale = (std::min)(static_cast<float>(box) / iw,
                                   static_cast<float>(box) / ih);
    const int dw = (std::max)(1, static_cast<int>(iw * scale));
    const int dh = (std::max)(1, static_cast<int>(ih * scale));
    auto* dst = new Gdiplus::Bitmap(dw, dh, PixelFormat32bppPARGB);
    if (dst->GetLastStatus() != Gdiplus::Ok) { delete dst; return nullptr; }
    Gdiplus::Graphics gfx(dst);
    gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    gfx.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    gfx.DrawImage(&src, Gdiplus::Rect(0, 0, dw, dh), 0, 0, iw, ih,
        Gdiplus::UnitPixel);
    return dst;
}

static void PreviewWorkerMain()
{
    // Background CPU + I/O priority: previews are allowed to be late; the
    // decode must never compete with Max for the disk or a core.
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
    auto& L = g_previewLoader;
    std::unique_lock<std::mutex> lk(L.mx);
    for (;;) {
        L.cv.wait(lk, [&L] { return L.quit || L.hasReq; });
        if (L.quit) return;
        std::wstring path = std::move(L.reqPath);
        const unsigned long long gen = L.reqGen;
        const HWND notify = L.notifyWnd;
        L.hasReq = false;
        lk.unlock();

        Gdiplus::Bitmap* img = nullptr;
        if (gen == g_previewGen.load(std::memory_order_acquire))
            img = LoadScaledPreview(path);

        lk.lock();
        if (gen != g_previewGen.load(std::memory_order_acquire)) {
            delete img;   // selection moved on while decoding — drop it
        } else {
            delete L.readyImg;
            L.readyImg = img;
            L.readyValid = true;
            L.readyGen = gen;
            L.readyPath = std::move(path);
            if (notify) PostMessageW(notify, WM_USER + 51, 0, 0);
        }
    }
}

static void SubmitPreviewLoad(const std::wstring& path, HWND notify)
{
    InitGdiPlus();   // the worker uses GDI+; start it from the UI thread
    auto& L = g_previewLoader;
    {
        std::lock_guard<std::mutex> lk(L.mx);
        L.reqPath = path;
        L.reqGen = g_previewGen.load(std::memory_order_acquire);
        L.notifyWnd = notify;
        L.hasReq = true;
        if (!L.th.joinable()) L.th = std::thread(PreviewWorkerMain);
    }
    L.cv.notify_one();
}

static void StopPreviewLoader()
{
    auto& L = g_previewLoader;
    {
        std::lock_guard<std::mutex> lk(L.mx);
        L.quit = true;
        L.hasReq = false;
    }
    L.cv.notify_one();
    if (L.th.joinable()) L.th.join();
    std::lock_guard<std::mutex> lk(L.mx);
    L.quit = false;   // allow a clean relaunch if the module re-inits
    L.readyValid = false;
    delete L.readyImg; L.readyImg = nullptr;
    L.reqPath.clear();
    L.readyPath.clear();
    L.notifyWnd = nullptr;
}

static bool EnsurePreviewWindow(HWND paletteWnd)
{
    if (g_previewWnd) return true;
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
    return g_previewWnd != nullptr;
}

static void PositionAndShowPreview(HWND paletteWnd)
{
    if (!g_previewWnd) return;
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
    g_previewShownPath.clear();
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

    // View modes persisted in the [config] section of FlowState.cfg
    void WriteConfigLines(FILE* f) const
    {
        if (tab_ == TabMode::Materials) fwprintf(f, L"PSTab=1\n");
        if (tab_ == TabMode::Maps)      fwprintf(f, L"PSTab=2\n");
        if (favsOnly_)                  fwprintf(f, L"PSFavs=1\n");
        if (sceneOnly_)                 fwprintf(f, L"PSScene=1\n");
        if (applyToSel_)                fwprintf(f, L"PSAutoApply=1\n");
        if (shllRes_ != 256)            fwprintf(f, L"PSShllRes=%d\n", shllRes_);
    }
    void ReadConfigLine(const std::wstring& l)
    {
        if (l == L"PSTab=1")  tab_ = TabMode::Materials;
        if (l == L"PSTab=2")  tab_ = TabMode::Maps;
        if (l == L"PSFavs=1") favsOnly_ = true;
        if (l == L"PSScene=1") sceneOnly_ = true;
        if (l == L"PSAutoApply=1") applyToSel_ = true;
        if (l.compare(0, 10, L"PSShllRes=") == 0) {
            int v = _wtoi(l.c_str() + 10);
            if (v == 128 || v == 256 || v == 512 || v == 1024) shllRes_ = v;
        }
    }
    void ResetModes()
    {
        tab_ = TabMode::All;
        favsOnly_ = sceneOnly_ = false;
        applyToSel_ = false;
        shllRes_ = 256;
    }

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
        StopPreviewLoader();   // join the worker before GDI+ goes away
        HidePreview();
        RestoreAccelerators();
        if (g_previewWnd) { DestroyWindow(g_previewWnd); g_previewWnd = nullptr; }
        if (wnd_) { DestroyWindow(wnd_); wnd_ = nullptr; }
        edit_ = list_ = toolsMenu_ = autoApply_ = scene_ = favs_ = status_ = nullptr;
        renameEdit_ = nullptr;
        brickBtns_.clear();
        renameIdx_ = brickDragFrom_ = -1;
        renaming_ = dragging_ = false;
        brickGesture_ = BrickGesture::None;
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
        sceneOnly_ = favsOnly_ = false;
        applyToSel_ = false;
        hoverClose_ = trackingMouse_ = false;
        closeRect_ = {};
        dragStart_ = {};
        shllRes_ = 256;
        brickAreaH_ = 0;
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
        ApplyListTheme(list_);
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

        case kRemoveBrickMessage:
            self->RemoveBrickFav(static_cast<int>(w));
            return 0;

        case WM_USER + 51:
        {
            // A pre-scaled preview finished on the worker thread — swap it
            // in if it still matches the current selection generation.
            Gdiplus::Bitmap* img = nullptr;
            unsigned long long gen = 0;
            bool valid = false;
            std::wstring path;
            {
                auto& L = g_previewLoader;
                std::lock_guard<std::mutex> lk(L.mx);
                valid = L.readyValid;
                L.readyValid = false;
                img = L.readyImg; L.readyImg = nullptr;
                gen = L.readyGen;
                path = std::move(L.readyPath);
            }
            if (!valid) return 0;
            if (gen != g_previewGen.load(std::memory_order_acquire) ||
                !IsWindowVisible(h)) {
                delete img;   // late result — the list has moved on
                return 0;
            }
            if (!img) { HidePreview(); return 0; }   // load failed
            if (!EnsurePreviewWindow(h)) { delete img; return 0; }
            delete g_previewImg;
            g_previewImg = img;
            g_previewShownPath = std::move(path);
            PositionAndShowPreview(h);
            return 0;
        }

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
            // Brick left-drag mirrors list-row drag: only release outside the
            // palette creates/drops the item. Releasing over FlowState is a
            // safe no-op, never a DAD call against our own controls.
            if (self->brickGesture_ == BrickGesture::LeftDragging &&
                self->brickDragFrom_ >= 0) {
                POINT screenPos{};
                GetCursorPos(&screenPos);
                const int from = self->brickDragFrom_;
                const bool outside = self->IsExternalDropPoint(screenPos);
                std::wstring alias;
                if (outside && from < static_cast<int>(self->brickFavs_.size()))
                    alias = self->brickFavs_[static_cast<size_t>(from)].alias;

                // Keep the shared external-drag flag alive through Max's drop
                // callback, exactly like ListProc. Only the brick source state
                // is cleared before entering external code.
                self->brickGesture_ = BrickGesture::None;
                self->brickDragFrom_ = -1;
                if (GetCapture() == h) ReleaseCapture();
                if (!alias.empty()) self->ActivateAlias(alias, true);
                self->dragging_ = false;
                self->RedrawDragHeader();
                return 0;
            }
            break;

        case WM_MBUTTONUP:
            if (self->brickGesture_ == BrickGesture::MiddleDragging &&
                self->brickDragFrom_ >= 0) {
                POINT screenPos{};
                GetCursorPos(&screenPos);
                const int from = self->brickDragFrom_;
                const int to = self->BrickIndexFromScreenPoint(screenPos);
                self->ClearBrickGesture();
                // RebuildBrickUI destroys the old brick HWNDs, so capture must
                // be gone before the ordered vector and controls are changed.
                if (GetCapture() == h) ReleaseCapture();
                self->ReorderBrickFav(from, to);
                return 0;
            }
            break;

        case WM_CAPTURECHANGED:
            // A menu, another Max window, or deactivation can steal capture.
            if ((self->brickGesture_ == BrickGesture::LeftDragging ||
                 self->brickGesture_ == BrickGesture::MiddleDragging) &&
                reinterpret_cast<HWND>(l) != h) {
                self->ClearBrickGesture();
                return 0;
            }
            break;

        case WM_CANCELMODE:
            if (self->brickGesture_ == BrickGesture::LeftDragging ||
                self->brickGesture_ == BrickGesture::MiddleDragging) {
                self->ClearBrickGesture();
                if (GetCapture() == h) ReleaseCapture();
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
            break;
        }

        case WM_CONTEXTMENU:
        {
            HWND target = reinterpret_cast<HWND>(w);
            int cid = target ? GetDlgCtrlID(target) : 0;
            if (cid == kToolsMenuId) {
                self->ShowHeaderMenu();
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
            mis->itemHeight = kListItemH;
            return TRUE;
        }
        case WM_DRAWITEM:
        {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(l);
            if (dis->CtlID == kListId)
                { self->DrawListItem(dis); return TRUE; }
            if (dis->CtlID == kTabMatId || dis->CtlID == kTabMapId ||
                dis->CtlID == kTabFavId || dis->CtlID == kToolsMenuId ||
                dis->CtlID == kAutoApplyId || dis->CtlID == kSceneId ||
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
            if (self->ListContentFits()) {
                self->EnforceListScrollInvariant();
                return 0;  // fitted list never scrolls
            }
            break;
        case WM_VSCROLL:
            if (self->ListContentFits()) {
                // The control can expose its non-client scrollbar before it
                // dispatches this message. Re-hide it, not merely the scroll.
                self->EnforceListScrollInvariant();
                return 0;
            }
            break;
        case WM_STYLECHANGING:
            if (w == GWL_STYLE && self->ListContentFits()) {
                // Reject any late internal attempt to restore WS_VSCROLL.
                // Overflow mode is unaffected because ListContentFits is false.
                auto* change = reinterpret_cast<STYLESTRUCT*>(l);
                if (change) change->styleNew &= ~WS_VSCROLL;
            }
            break;
        case WM_KEYDOWN:
            // Keep keyboard navigation on our invariant-preserving path. The
            // stock listbox can recreate its scrollbar while ensuring the last
            // exact-fit row is visible, even after WS_VSCROLL was removed.
            if (w == VK_DOWN) { self->MoveSelection(1); return 0; }
            if (w == VK_UP)   { self->MoveSelection(-1); return 0; }
            break;
        case WM_LBUTTONUP:
        {
            const bool wasDragging = self->dragging_;
            if (GetCapture() == h) ReleaseCapture();
            if (wasDragging)
            {
                POINT p{}; GetCursorPos(&p);
                if (self->IsExternalDropPoint(p))
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
            self->brickDragFrom_ = GetDlgCtrlID(h) - kBrickBase;
            self->brickGesture_ = BrickGesture::LeftArmed;
            GetCursorPos(&self->dragStart_);
            break;
        case WM_MBUTTONDOWN:
            self->brickDragFrom_ = GetDlgCtrlID(h) - kBrickBase;
            self->brickGesture_ = BrickGesture::MiddleArmed;
            GetCursorPos(&self->dragStart_);
            SetCapture(h);
            if (GetCapture() != h) self->ClearBrickGesture();
            return 0;
        case WM_MOUSEMOVE:
            if (self->brickDragFrom_ >= 0) {
                POINT p{};
                GetCursorPos(&p);
                const bool moved = std::abs(p.x - self->dragStart_.x) > 6 ||
                                   std::abs(p.y - self->dragStart_.y) > 6;

                if (moved && self->brickGesture_ == BrickGesture::LeftArmed &&
                    (w & MK_LBUTTON)) {
                    self->brickGesture_ = BrickGesture::LeftDragging;
                    self->dragging_ = true;
                    SetCapture(self->wnd_);
                    if (GetCapture() != self->wnd_) self->ClearBrickGesture();
                    else self->RedrawDragHeader();
                    return 0;
                }

                if (moved && self->brickGesture_ == BrickGesture::MiddleArmed &&
                    (w & MK_MBUTTON)) {
                    self->brickGesture_ = BrickGesture::MiddleDragging;
                    SetCapture(self->wnd_);
                    if (GetCapture() != self->wnd_) self->ClearBrickGesture();
                    return 0;
                }
            }
            break;
        case WM_LBUTTONUP:
            if (self->brickGesture_ == BrickGesture::LeftArmed) {
                self->brickGesture_ = BrickGesture::None;
                self->brickDragFrom_ = -1;
                break; // preserve the normal BUTTON click notification
            }
            if (self->brickGesture_ == BrickGesture::LeftDragging) {
                self->ClearBrickGesture();
                return 0;
            }
            break;
        case WM_MBUTTONUP:
            if (self->brickGesture_ == BrickGesture::MiddleArmed) {
                const int from = self->brickDragFrom_;
                self->ClearBrickGesture();
                if (GetCapture() == h) ReleaseCapture();
                // Defer destruction of this button until its window proc has
                // returned. A plain middle-click retains the remove shortcut.
                PostMessageW(self->wnd_, kRemoveBrickMessage,
                    static_cast<WPARAM>(from), 0);
                return 0;
            }
            return 0;
        case WM_CAPTURECHANGED:
            // Transfer to the palette is intentional after either threshold.
            if (reinterpret_cast<HWND>(l) != self->wnd_)
                self->ClearBrickGesture();
            break;
        case WM_CANCELMODE:
            self->ClearBrickGesture();
            if (GetCapture() == h) ReleaseCapture();
            return 0;
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
    void ApplyListTheme(HWND list)
    {
        if (!list) return;
        HMODULE hUx = LoadLibraryW(L"uxtheme.dll");
        if (!hUx) return;
        using SetWindowThemeFn = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);
        auto setTheme = reinterpret_cast<SetWindowThemeFn>(
            GetProcAddress(hUx, "SetWindowTheme"));
        if (setTheme)
            setTheme(list, Theme::lightTheme ? L"Explorer" : L"DarkMode_Explorer", nullptr);
        FreeLibrary(hUx);
    }

    HWND CreateResultsList(int x, int y, int w, int h, bool withScrollbar)
    {
        DWORD style = WS_CHILD | WS_VISIBLE | LBS_NOTIFY |
            LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_NODATA;
        if (withScrollbar) style |= WS_VSCROLL;

        HWND list = CreateWindowExW(0, L"LISTBOX", L"", style,
            x, y, w, h, wnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kListId)),
            hInstance, nullptr);
        if (list) {
            SetWindowSubclass(list, ListProc, 1, reinterpret_cast<DWORD_PTR>(this));
            ApplyListTheme(list);
        }
        return list;
    }

    void RecreateResultsList(bool withScrollbar, int x, int y, int w, int h,
                             int count, int selection, int top)
    {
        const bool restoreFocus = list_ && GetFocus() == list_;
        HWND old = list_;
        list_ = nullptr;
        if (old) DestroyWindow(old);

        list_ = CreateResultsList(x, y, w, h, withScrollbar);
        if (!list_) return;

        SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
        SendMessageW(list_, LB_SETCOUNT, static_cast<WPARAM>((std::max)(0, count)), 0);
        if (selection >= 0 && selection < count)
            SendMessageW(list_, LB_SETCURSEL, selection, 0);
        SendMessageW(list_, LB_SETTOPINDEX,
            withScrollbar ? (std::max)(0, top) : 0, 0);
        SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
        if (restoreFocus) SetFocus(list_);
        RedrawWindow(list_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    }

    void OnCreate(HWND h)
    {
        wnd_ = h;
        const int pad = 8;
        RECT cr; GetClientRect(h, &cr);
        const int cw = cr.right - 2 * pad;
        closeRect_ = { cr.right - pad - 18, pad, cr.right - pad, pad + 18 };
        int y = kHeaderH;

        // Header controls: tools menu | persistent Auto Apply | close ×
        const int hbH = 18, hbGap = 3;
        const int autoW = 30, menuW = 24;
        int hbX = closeRect_.left - 4 - autoW;
        autoApply_ = CreateWindowExW(0, L"BUTTON", L"AUTO",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            hbX, pad, autoW, hbH, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAutoApplyId)), hInstance, nullptr);
        hbX -= hbGap + menuW;
        toolsMenu_ = CreateWindowExW(0, L"BUTTON", L"\x22EF",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            hbX, pad, menuW, hbH, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kToolsMenuId)), hInstance, nullptr);

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

        // Filter row: Shaders | Maps | Favs | Scene. Shaders/Maps are
        // exclusive type tabs (clicking the active one = show ALL); Favs and
        // Scene are independent toggles that layer on top of them and each
        // other (e.g. Scene+Favs = pinned scene items only).
        const int tabGap = 3;
        int tabW = (cw - 3 * tabGap) / 4;
        CreateWindowExW(0, L"BUTTON", L"Shaders",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            pad, y, tabW, 22, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTabMatId)), hInstance, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Maps",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            pad + tabW + tabGap, y, tabW, 22, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTabMapId)), hInstance, nullptr);
        favs_ = CreateWindowExW(0, L"BUTTON", L"Favorites",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            pad + 2 * (tabW + tabGap), y, tabW, 22, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTabFavId)), hInstance, nullptr);
        scene_ = CreateWindowExW(0, L"BUTTON", L"Scene",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            pad + 3 * (tabW + tabGap), y, cw - 3 * (tabW + tabGap), 22, h,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSceneId)), hInstance, nullptr);
        y += 26;

        // Results list (owner-drawn)
        listBaseY_ = y;
        int listH = cr.bottom - y - 26;
        // Start without a scrollbar. Layout recreates this control with
        // WS_VSCROLL only after the window reaches its height cap and rows
        // genuinely overflow.
        list_ = CreateResultsList(pad, y, cw, listH, false);

        // Status bar
        status_ = CreateWindowExW(0, L"STATIC", L"Ready",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            pad, cr.bottom - 22, cw, 18, h, nullptr, hInstance, nullptr);
        SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(Theme::fontUI), TRUE);

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

        // Left side: favorite indicator + item name
        // Solid diamond = file pin, hollow diamond = brick favorite
        bool isPinned =
            std::find(filePins_.begin(), filePins_.end(), item.favoriteKey) != filePins_.end();
        bool isBrick = !isPinned && std::any_of(brickFavs_.begin(), brickFavs_.end(),
            [&](const BrickFav& bf) { return bf.alias == item.favoriteKey; });
        RECT lr = dis->rcItem;
        lr.left += 8;
        if (isPinned || isBrick) {
            // Accent-on-accent is invisible on the selected row \u2014 use the
            // bright text color there instead.
            SetTextColor(dis->hDC, sel ? Theme::textBrt : Theme::accent);
            DrawTextW(dis->hDC, isPinned ? L"\u25C6 " : L"\u25C7 ", 2, &lr,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
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
        else if (id == kTabFavId) active = favsOnly_;
        else if (id == kSceneId)  active = sceneOnly_;
        else if (id == kAutoApplyId) active = applyToSel_;
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

        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, active ? Theme::textBrt : Theme::text);
        HFONT old = static_cast<HFONT>(SelectObject(dis->hDC, Theme::fontUI));
        if (id == kAutoApplyId) {
            RECT box = {
                dis->rcItem.left + 4,
                dis->rcItem.top + 4,
                dis->rcItem.left + 14,
                dis->rcItem.bottom - 4
            };
            HPEN checkPen = CreatePen(PS_SOLID, 1,
                active ? Theme::textBrt : Theme::textDim);
            HPEN oldCheckPen = static_cast<HPEN>(SelectObject(dis->hDC, checkPen));
            HBRUSH oldCheckBrush = static_cast<HBRUSH>(
                SelectObject(dis->hDC, GetStockObject(NULL_BRUSH)));
            Rectangle(dis->hDC, box.left, box.top, box.right, box.bottom);
            if (applyToSel_) {
                MoveToEx(dis->hDC, box.left + 2, box.top + 5, nullptr);
                LineTo(dis->hDC, box.left + 4, box.bottom - 2);
                LineTo(dis->hDC, box.right - 2, box.top + 2);
            }
            SelectObject(dis->hDC, oldCheckBrush);
            SelectObject(dis->hDC, oldCheckPen);
            DeleteObject(checkPen);

            RECT label = dis->rcItem;
            label.left = box.right + 1;
            DrawTextW(dis->hDC, L"A", -1, &label,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            wchar_t buf[32] = {};
            GetWindowTextW(dis->hwndItem, buf, 32);
            DrawTextW(dis->hDC, buf, -1, &dis->rcItem,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(dis->hDC, old);
    }

    // ─── Show / Hide ────────────────────────────────────────────
    void RedrawDragHeader()
    {
        if (!wnd_) return;
        RECT header = { 0, 0, kWindowWidth, kHeaderH };
        RedrawWindow(wnd_, &header, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    }

    bool IsExternalDropPoint(POINT screenPos) const
    {
        HWND under = WindowFromPoint(screenPos);
        if (!under) return true;
        if (under == wnd_ || IsChild(wnd_, under)) return false;
        // The preview is another top-level FlowState window. Treat it as
        // internal too so neither rows nor bricks ever probe it as a Max DAD
        // target.
        if (g_previewWnd &&
            (under == g_previewWnd || IsChild(g_previewWnd, under))) return false;
        return true;
    }

    void ClearBrickGesture()
    {
        const bool wasExternalDrag =
            brickGesture_ == BrickGesture::LeftDragging;
        brickGesture_ = BrickGesture::None;
        brickDragFrom_ = -1;
        if (wasExternalDrag) {
            dragging_ = false;
            RedrawDragHeader();
        }
    }

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
        SetWindowPos(wnd_, HWND_TOPMOST, x, y, kWindowWidth, DesiredWindowHeight(),
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
        HWND capture = GetCapture();
        if (capture && (capture == wnd_ || IsChild(wnd_, capture)))
            ReleaseCapture();
        ClearBrickGesture();
        // Drop any in-flight preview load and pending debounce
        g_previewGen.fetch_add(1, std::memory_order_release);
        if (wnd_) KillTimer(wnd_, kPreviewTimerId);
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
    void ShowHeaderMenu()
    {
        if (!wnd_ || !toolsMenu_) return;

        HMENU popup = CreatePopupMenu();
        HMENU resolution = CreatePopupMenu();
        if (!popup || !resolution) {
            if (resolution) DestroyMenu(resolution);
            if (popup) DestroyMenu(popup);
            return;
        }

        AppendMenuW(popup, MF_STRING, kLinkId, L"Link bitmap tiling values");
        AppendMenuW(popup, MF_STRING, kShllId, L"Quick Shell Material");
        AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(resolution, MF_STRING, kShllRes128Id, L"128 px");
        AppendMenuW(resolution, MF_STRING, kShllRes256Id, L"256 px");
        AppendMenuW(resolution, MF_STRING, kShllRes512Id, L"512 px");
        AppendMenuW(resolution, MF_STRING, kShllRes1024Id, L"1024 px");
        AppendMenuW(popup, MF_POPUP, reinterpret_cast<UINT_PTR>(resolution),
            L"Quick Shell Resolution");

        const UINT checkedId =
            shllRes_ == 128 ? kShllRes128Id :
            shllRes_ == 512 ? kShllRes512Id :
            shllRes_ == 1024 ? kShllRes1024Id : kShllRes256Id;
        CheckMenuRadioItem(resolution, kShllRes128Id, kShllRes1024Id,
            checkedId, MF_BYCOMMAND);

        RECT buttonRect{};
        GetWindowRect(toolsMenu_, &buttonRect);
        SetForegroundWindow(wnd_);
        const UINT command = TrackPopupMenuEx(popup,
            TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTALIGN | TPM_TOPALIGN,
            buttonRect.right, buttonRect.bottom, wnd_, nullptr);
        DestroyMenu(popup); // also destroys the attached resolution submenu
        PostMessageW(wnd_, WM_NULL, 0, 0);

        if (command == kLinkId) {
            ExecuteMAXScriptScript(kLinkScript, MAXScript::ScriptSource::Dynamic);
        } else if (command == kShllId) {
            ExecuteShellCommand(shllRes_);
        } else if (command >= kShllRes128Id && command <= kShllRes1024Id) {
            shllRes_ =
                command == kShllRes128Id ? 128 :
                command == kShllRes512Id ? 512 :
                command == kShllRes1024Id ? 1024 : 256;
            SetStatus(L"Quick Shell preview " + std::to_wstring(shllRes_) + L" px");
            FlowState_SaveSettings();
        }
    }

    void OnCommand(int id, int code)
    {
        if (id == kSearchId && code == EN_CHANGE) { ScheduleRebuild(); return; }
        if (id == kToolsMenuId) {
            ShowHeaderMenu();
            return;
        }
        if (id == kAutoApplyId)
        {
            applyToSel_ = !applyToSel_;
            InvalidateRect(autoApply_, nullptr, FALSE);
            SetStatus(applyToSel_ ? L"Auto Apply on." : L"Auto Apply off.");
            FlowState_SaveSettings();
            return;
        }
        if (id == kTabFavId)
        {
            favsOnly_ = !favsOnly_;
            InvalidateRect(favs_, nullptr, FALSE);
            CancelPendingRebuild();
            Rebuild(true);
            FlowState_SaveSettings();
            return;
        }
        if (id == kSceneId)
        {
            sceneOnly_ = !sceneOnly_;
            InvalidateRect(scene_, nullptr, FALSE);
            CancelPendingRebuild();
            if (sceneOnly_) RefreshSceneCache();
            Rebuild(true);
            FlowState_SaveSettings();
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
            FlowState_SaveSettings();
            return;
        }
        if (id == kListId && code == LBN_SELCHANGE) {
            EnforceListScrollInvariant();
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
        // Runs on every selection change while scrolling — never touch the
        // disk or the material graph here. Invalidate any in-flight load and
        // restart the debounce; the real work starts in OnTimer once the
        // selection has settled for kPreviewDebounceMs.
        g_previewGen.fetch_add(1, std::memory_order_release);
        if (!wnd_ || !IsWindowVisible(wnd_)) {
            // Activation can hide the palette before a queued refresh is
            // dispatched — never let it resurrect the topmost preview popup.
            if (wnd_) KillTimer(wnd_, kPreviewTimerId);
            HidePreview();
            return;
        }
        SetTimer(wnd_, kPreviewTimerId, kPreviewDebounceMs, nullptr);
    }

    // Debounce elapsed — resolve the selected item's bitmap path and hand it
    // to the worker. The previous popup stays up until the replacement
    // arrives (no flicker); items without a bitmap hide it right away.
    void StartPreviewLoad()
    {
        if (!wnd_ || !IsWindowVisible(wnd_)) { HidePreview(); return; }
        std::wstring fn;
        int sel = (int)SendMessage(list_, LB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < (int)filtered_.size() && activeItems_) {
            const Item& item = (*activeItems_)[filtered_[sel]];
            if (item.live) fn = GetTexmapFilename(item.live);
        }
        if (fn.empty()) { HidePreview(); return; }
        if (fn == g_previewShownPath && g_previewImg) {
            PositionAndShowPreview(wnd_);   // same file — no reload needed
            return;
        }
        SubmitPreviewLoad(fn, wnd_);
    }

    void OnTimer(UINT_PTR id)
    {
        if (id == kPreviewTimerId) {
            KillTimer(wnd_, kPreviewTimerId);
            StartPreviewLoad();
            return;
        }
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
            [](const Item& a, const Item& b) {
                if (a.label != b.label) return a.label < b.label;
                return a.favoriteKey < b.favoriteKey;
            });

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
                [](const Item& a, const Item& b) {
                    if (a.label != b.label) return a.label < b.label;
                    return a.favoriteKey < b.favoriteKey;
                });
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
        item.favoriteKey = MakeSceneFavoriteKey(m, item.key);
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
            [](const Item& a, const Item& b) {
                if (a.label != b.label) return a.label < b.label;
                return a.favoriteKey < b.favoriteKey;
            });
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
            item.favoriteKey = MakeClassFavoriteKey(sid, classId);
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

    std::wstring ResolveStoredFavoriteKey(const std::wstring& stored) const
    {
        if (stored.empty() || IsStableFavoriteKey(stored)) return stored;

        // Old configurations stored only a normalized display name. Resolve
        // that inherently ambiguous token to one deterministic class identity
        // so a single "composite" pin can never fan out to several classes.
        for (const Item& item : classItems_)
            if (item.key == stored || item.scriptKey == stored)
                return item.favoriteKey;

        // Scene aliases can only be resolved after that cache has been built.
        // Keep unavailable tokens intact so loading a scene/plugin later can
        // still migrate them instead of silently discarding the favorite.
        if (sceneCacheReady_) {
            for (const Item& item : sceneItems_)
                if (item.key == stored || item.scriptKey == stored)
                    return item.favoriteKey;
        }
        return stored;
    }

    bool CanonicalizeFavoriteStorage()
    {
        bool changed = false;

        std::vector<std::wstring> pins;
        pins.reserve(filePins_.size());
        std::set<std::wstring> seenPins;
        for (const std::wstring& stored : filePins_) {
            std::wstring key = ResolveStoredFavoriteKey(stored);
            if (key != stored) changed = true;
            if (!key.empty() && seenPins.insert(key).second)
                pins.push_back(std::move(key));
            else
                changed = true;
        }

        std::vector<BrickFav> bricks;
        bricks.reserve(brickFavs_.size());
        std::set<std::wstring> seenBricks;
        for (const BrickFav& stored : brickFavs_) {
            std::wstring key = ResolveStoredFavoriteKey(stored.alias);
            if (key != stored.alias) changed = true;
            if (!key.empty() && seenBricks.insert(key).second)
                bricks.push_back({std::move(key), stored.label});
            else
                changed = true;
        }

        if (changed) {
            filePins_.swap(pins);
            brickFavs_.swap(bricks);
        }
        return changed;
    }

    // ─── List rebuild ───────────────────────────────────────────
    void Rebuild(bool forceFull, bool preserveView = false)
    {
        int preservedTopIndex = 0;
        int preservedSelectionIndex = LB_ERR;
        std::wstring preservedTopKey;
        std::wstring preservedSelectionKey;
        if (preserveView && list_ && activeItems_) {
            preservedTopIndex =
                static_cast<int>(SendMessageW(list_, LB_GETTOPINDEX, 0, 0));
            preservedSelectionIndex =
                static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0));

            auto keyAt = [&](int filteredIndex) -> std::wstring {
                if (filteredIndex < 0 ||
                    filteredIndex >= static_cast<int>(filtered_.size())) return {};
                const int sourceIndex = filtered_[filteredIndex];
                if (sourceIndex < 0 ||
                    sourceIndex >= static_cast<int>(activeItems_->size())) return {};
                return (*activeItems_)[sourceIndex].favoriteKey;
            };
            preservedTopKey = keyAt(preservedTopIndex);
            preservedSelectionKey = keyAt(preservedSelectionIndex);
        }

        const bool sceneOnly = IsSceneOnly();
        EnsureClassCache();
        if (sceneOnly && !sceneCacheReady_) RefreshSceneCache();
        if (CanonicalizeFavoriteStorage()) FlowState_SaveSettings();

        const std::vector<Item>& source = sceneOnly ? sceneItems_ : classItems_;
        activeItems_ = &source;

        const std::wstring q = ReadNormalizedQuery();
        const std::wstring normQ = Normalize(q, true);
        const std::vector<std::wstring> tokens = TokenizeQuery(normQ);

        // Favorites is intentionally the right-click pin set only. Brick
        // favorites already occupy the button strip above the list; they keep
        // their hollow diamond in normal results but do not duplicate here.
        std::set<std::wstring> favKeys;
        if (favsOnly_)
            favKeys.insert(filePins_.begin(), filePins_.end());

        auto passesTab = [&](const Item& item) -> bool
        {
            bool isScene = (item.kind == ItemKind::SceneMaterial || item.kind == ItemKind::SceneMap);
            if (sceneOnly != isScene) return false;
            if (favsOnly_ && favKeys.find(item.favoriteKey) == favKeys.end()) return false;
            if (tab_ == TabMode::Materials &&
                item.kind != ItemKind::ClassMaterial && item.kind != ItemKind::SceneMaterial) return false;
            if (tab_ == TabMode::Maps &&
                item.kind != ItemKind::ClassMap && item.kind != ItemKind::SceneMap) return false;
            return true;
        };

        struct Scored { int idx; int score; };
        std::vector<Scored> scored;
        scored.reserve(source.size());
        std::set<std::wstring> emittedFavoriteKeys;

        for (size_t i = 0; i < source.size(); ++i)
        {
            const Item& item = source[i];
            if (!passesTab(item)) continue;
            // Defense in depth: a bad registry or repeated scene reference
            // still cannot render the same favorite identity more than once.
            if (favsOnly_ && !emittedFavoriteKeys.insert(item.favoriteKey).second)
                continue;
            int s = ScoreMatch(item.search, item.normLabel, tokens);
            if (s > 0) scored.push_back({static_cast<int>(i), s});
        }

        // Sort by score descending when searching, alphabetical otherwise
        if (!tokens.empty())
            std::stable_sort(scored.begin(), scored.end(),
                [](const Scored& a, const Scored& b) { return a.score > b.score; });

        filtered_.clear();
        filtered_.reserve(scored.size());

        // Right-click pins go first, alphabetically — source order is already
        // sorted by label. Brick favorites are deliberately excluded from
        // this grouping because their buttons are already pinned above.
        // Skip grouping while searching (score order wins) and in Favs view
        // (everything shown is a favorite; plain alphabetical order wins).
        if (tokens.empty() && !filePins_.empty() && !favsOnly_) {
            std::set<std::wstring> pinSet(filePins_.begin(), filePins_.end());
            for (const auto& s : scored)
                if (pinSet.count(source[static_cast<size_t>(s.idx)].favoriteKey))
                    filtered_.push_back(s.idx);
            for (const auto& s : scored)
                if (!pinSet.count(source[static_cast<size_t>(s.idx)].favoriteKey))
                    filtered_.push_back(s.idx);
        } else {
            for (const auto& s : scored) filtered_.push_back(s.idx);
        }

        int restoredTopIndex = 0;
        int restoredSelectionIndex = filtered_.empty() ? LB_ERR : 0;
        if (preserveView && !filtered_.empty()) {
            auto findKey = [&](const std::wstring& key) -> int {
                if (key.empty()) return LB_ERR;
                for (int i = 0; i < static_cast<int>(filtered_.size()); ++i) {
                    const Item& item =
                        source[static_cast<size_t>(filtered_[static_cast<size_t>(i)])];
                    if (item.favoriteKey == key) return i;
                }
                return LB_ERR;
            };

            restoredTopIndex = findKey(preservedTopKey);
            if (restoredTopIndex == LB_ERR)
                restoredTopIndex = std::clamp(
                    preservedTopIndex, 0, static_cast<int>(filtered_.size()) - 1);

            restoredSelectionIndex = findKey(preservedSelectionKey);
            if (restoredSelectionIndex == LB_ERR && preservedSelectionIndex != LB_ERR)
                restoredSelectionIndex = std::clamp(
                    preservedSelectionIndex, 0, static_cast<int>(filtered_.size()) - 1);
        }

        SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
        SendMessageW(list_, LB_RESETCONTENT, 0, 0);
        SendMessageW(list_, LB_SETCOUNT, static_cast<WPARAM>(filtered_.size()), 0);
        if (restoredSelectionIndex != LB_ERR)
            SendMessageW(list_, LB_SETCURSEL, restoredSelectionIndex, 0);
        if (!filtered_.empty())
            SendMessageW(list_, LB_SETTOPINDEX, restoredTopIndex, 0);
        SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
        // Auto-compact to the result count. This also re-syncs the listbox
        // scroll state at the final geometry — see LayoutListAndStatus.
        ApplyWindowHeight();
        RedrawWindow(list_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        if (wnd_ && IsWindowVisible(wnd_)) UpdatePreviewForSelection();
        else HidePreview();

        lastQuery_ = normQ;
        lastTab_ = tab_;
        lastSceneOnly_ = sceneOnly;

        if (favsOnly_ && filtered_.empty() && normQ.empty())
            SetStatus(L"No favorites yet. Right-click an item to add it.");
        else
            SetStatus(std::to_wstring(filtered_.size()) + L" items" +
                (normQ.empty() ? L"" : (L"  |  " + normQ)));
    }

    // ─── Activation (C++ API core) ──────────────────────────────
    void ActivateAlias(const std::wstring& alias, bool drag = false)
    {
        EnsureClassCache();
        std::wstring key = IsStableFavoriteKey(alias)
            ? alias : Normalize(alias, false);
        for (const Item& item : classItems_)
            if (!item.live &&
                (item.favoriteKey == key || item.key == key || item.scriptKey == key))
                { Activate(item, drag); return; }

        // Retry once after forcing a fresh cache build.
        if (!forcedAliasRetry_)
        {
            forcedAliasRetry_ = true;
            classCacheReady_ = false;
            EnsureClassCache();
            for (const Item& item : classItems_)
                if (!item.live &&
                    (item.favoriteKey == key || item.key == key || item.scriptKey == key))
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
            } else {
                // Always retain the created item in a material-editor slot.
                // GetMtlEditInterface() can be unavailable transiently even
                // though the core interface still accepts the slot update.
                ip->PutMtlToMtlEditor(mb, slot);
            }

            if (applyToSel_ && isMat && ip->GetSelNodeCount() > 0) {
                Mtl* mtl = static_cast<Mtl*>(mb);
                const int selectedCount = ip->GetSelNodeCount();
                for (int i = 0; i < selectedCount; ++i)
                    if (INode* node = ip->GetSelNode(i)) node->SetMtl(mtl);
            }
            Hide();
        }

        theHold.Accept(_T("Assign Material"));

        sceneCacheReady_ = false;
        ip->RedrawViews(ip->GetTime());
        SetStatus(drag ? L"Dropped." : L"Inserted.");
    }

    // ─── Helpers ────────────────────────────────────────────────
    // The scrollbar is legal only after auto-compact reaches its height cap
    // and the rows genuinely exceed the list client. This makes the window
    // geometry—not the listbox's off-by-one internal page math—the authority.
    bool ListContentFits() const
    {
        if (!list_) return true;
        RECT lr; GetClientRect(list_, &lr);
        const int count =
            static_cast<int>(SendMessageW(list_, LB_GETCOUNT, 0, 0));
        if (count <= 0) return true;
        RECT pr{}; if (wnd_) GetClientRect(wnd_, &pr);
        const bool reachedMaxHeight = wnd_ && pr.bottom >= kWindowHeight;
        const bool rowsOverflow = count * kListItemH > lr.bottom;
        return !reachedMaxHeight || !rowsOverflow;
    }

    void EnforceListScrollInvariant()
    {
        if (!list_) return;

        const bool fits = ListContentFits();
        LONG_PTR style = GetWindowLongPtrW(list_, GWL_STYLE);
        const bool hasBar = (style & WS_VSCROLL) != 0;
        if (fits == hasBar) {
            style = fits ? (style & ~WS_VSCROLL) : (style | WS_VSCROLL);
            SetWindowLongPtrW(list_, GWL_STYLE, style);
            SetWindowPos(list_, nullptr, 0, 0, 0, 0,
                SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER |
                SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }

        // ShowScrollBar is intentional in addition to the style update:
        // LISTBOX can call it internally while bringing a selection into view.
        ShowScrollBar(list_, SB_VERT, fits ? FALSE : TRUE);
        if (fits && SendMessageW(list_, LB_GETTOPINDEX, 0, 0) != 0)
            SendMessageW(list_, LB_SETTOPINDEX, 0, 0);
    }

    void MoveSelection(int delta)
    {
        int count = static_cast<int>(SendMessageW(list_, LB_GETCOUNT, 0, 0));
        if (count <= 0) return;
        const int current =
            static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0));
        const int next = current == LB_ERR
            ? 0 : std::clamp(current + delta, 0, count - 1);

        // Do nothing at either boundary. Re-sending the last/first selection
        // makes the native listbox run its ensure-visible scroll path again.
        if (current != LB_ERR && next == current) return;

        const bool fits = ListContentFits();
        if (fits) SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
        SendMessageW(list_, LB_SETCURSEL, next, 0);

        if (fits) {
            // The stock control may adjust its top index while selecting the
            // last exact-fit row. Restore it before redraw is enabled so that
            // internal motion is never presented as a one-frame snap.
            SendMessageW(list_, LB_SETTOPINDEX, 0, 0);
            ShowScrollBar(list_, SB_VERT, FALSE);
            SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
            RedrawWindow(list_, nullptr, nullptr,
                RDW_INVALIDATE | RDW_UPDATENOW);
        } else {
            EnforceListScrollInvariant();
        }
        // LB_SETCURSEL doesn't fire LBN_SELCHANGE — schedule the (debounced)
        // preview update ourselves so it follows keyboard navigation too.
        UpdatePreviewForSelection();
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
        return (*activeItems_)[si].favoriteKey;
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
        Rebuild(true, true);
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
        if (list_) InvalidateRect(list_, nullptr, FALSE); // refresh hollow diamond
    }

    void RemoveBrickFav(int brickIdx)
    {
        if (brickIdx < 0 || brickIdx >= static_cast<int>(brickFavs_.size())) return;
        brickFavs_.erase(brickFavs_.begin() + brickIdx);
        SaveBrickFavs();
        RebuildBrickUI();
        if (list_) InvalidateRect(list_, nullptr, FALSE); // remove hollow diamond
        SetStatus(L"Removed from favorites.");
    }

    void SaveBrickFavs()
    {
        FlowState_SaveSettings(); // unified save
    }

    int BrickIndexFromScreenPoint(POINT screenPos) const
    {
        if (brickBtns_.empty()) return -1;

        RECT brickBounds{};
        bool haveBounds = false;
        int nearest = -1;
        long long nearestDistance = (std::numeric_limits<long long>::max)();

        for (int i = 0; i < static_cast<int>(brickBtns_.size()); ++i) {
            HWND button = brickBtns_[static_cast<size_t>(i)];
            if (!button || !IsWindow(button)) continue;

            RECT rect{};
            if (!GetWindowRect(button, &rect)) continue;
            if (PtInRect(&rect, screenPos)) return i;

            if (!haveBounds) {
                brickBounds = rect;
                haveBounds = true;
            } else {
                brickBounds.left = (std::min)(brickBounds.left, rect.left);
                brickBounds.top = (std::min)(brickBounds.top, rect.top);
                brickBounds.right = (std::max)(brickBounds.right, rect.right);
                brickBounds.bottom = (std::max)(brickBounds.bottom, rect.bottom);
            }

            const long long dx = screenPos.x - (rect.left + rect.right) / 2;
            const long long dy = screenPos.y - (rect.top + rect.bottom) / 2;
            const long long distance = dx * dx + dy * dy;
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearest = i;
            }
        }

        // Accept the small gaps between buttons, but dropping outside the
        // brick strip cancels the reorder.
        if (!haveBounds) return -1;
        InflateRect(&brickBounds, 6, 6);
        return PtInRect(&brickBounds, screenPos) ? nearest : -1;
    }

    void ReorderBrickFav(int from, int to)
    {
        const int count = static_cast<int>(brickFavs_.size());
        if (from < 0 || from >= count || to < 0 || to >= count || from == to)
            return;

        BrickFav moved = std::move(brickFavs_[static_cast<size_t>(from)]);
        brickFavs_.erase(brickFavs_.begin() + from);
        brickFavs_.insert(brickFavs_.begin() + to, std::move(moved));
        SaveBrickFavs();
        RebuildBrickUI();
        SetStatus(L"Favorites reordered.");
    }

    // ─── Dynamic window height (Favorites shrink-wrap) ──────────
    int ListTop() const { return listBaseY_ + brickAreaH_; }

    // Auto-compact, all views: fit the window to the item count — no
    // scrollbar — growing with the list until the standard height, where
    // the scrollbar takes over.
    int DesiredWindowHeight() const
    {
        int rows = (std::max)(1, static_cast<int>(filtered_.size()));
        return (std::min)(kWindowHeight, ListTop() + rows * kListItemH + kBottomM);
    }

    void LayoutListAndStatus()
    {
        if (!wnd_) return;
        RECT cr; GetClientRect(wnd_, &cr);
        const int pad = 8, cw = cr.right - 2 * pad;
        if (list_) {
            const int listH = (cr.bottom - kBottomM) - ListTop();
            SetWindowPos(list_, nullptr, pad, ListTop(), cw, listH,
                SWP_NOZORDER);
            // The listbox only recomputes scrollability on content changes,
            // never on resize — a stale state leaves phantom scrollbars or a
            // wheel-scrollable exact-fit list. Re-issuing the count (cheap
            // with LBS_NODATA) forces a full recalc at the final geometry;
            // then restore the view. The listbox clamps the top index, so an
            // exact-fit list pins to 0 automatically.
            const int count =
                static_cast<int>(SendMessageW(list_, LB_GETCOUNT, 0, 0));
            if (count >= 0) {
                const int sel =
                    static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0));
                const int top =
                    static_cast<int>(SendMessageW(list_, LB_GETTOPINDEX, 0, 0));
                SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
                // Reset, then set: LB_SETCOUNT with an unchanged count can be
                // treated as a no-op by the control, which keeps the stale
                // scrollbar alive. Emptying first forces the real recalc.
                SendMessageW(list_, LB_RESETCONTENT, 0, 0);
                SendMessageW(list_, LB_SETCOUNT, count, 0);
                if (sel >= 0) SendMessageW(list_, LB_SETCURSEL, sel, 0);
                const bool fits = ListContentFits();
                SendMessageW(list_, LB_SETTOPINDEX,
                    fits ? 0 : (std::max)(0, top), 0);
                SendMessageW(list_, WM_SETREDRAW, TRUE, 0);

                // A LISTBOX created with WS_VSCROLL retains internal scrollbar
                // state even after the style is cleared and can resurrect the
                // bar while bringing its last row into view. Cross the mode
                // boundary by creating a fresh control with the correct style;
                // compact controls have never owned a scrollbar at all.
                const bool hasScrollbar =
                    (GetWindowLongPtrW(list_, GWL_STYLE) & WS_VSCROLL) != 0;
                const bool needsScrollbar = !fits;
                if (hasScrollbar != needsScrollbar) {
                    RecreateResultsList(needsScrollbar, pad, ListTop(), cw, listH,
                        count, sel, fits ? 0 : top);
                } else {
                    RedrawWindow(list_, nullptr, nullptr, RDW_INVALIDATE);
                }

                // Reconcile top index and visibility after the final control
                // and geometry are both in place.
                EnforceListScrollInvariant();
            }
        }
        if (status_)
            SetWindowPos(status_, nullptr, pad, cr.bottom - 22, cw, 18,
                SWP_NOZORDER);
    }

    // Resize the window to DesiredWindowHeight, keeping the top edge put but
    // never letting the bottom leave the work area, then reflow the
    // bottom-anchored children.
    void ApplyWindowHeight()
    {
        if (!wnd_) return;
        const int desired = DesiredWindowHeight();
        RECT wr; GetWindowRect(wnd_, &wr);
        if (wr.bottom - wr.top != desired) {
            RECT wa{};
            MONITORINFO mi{ sizeof(mi) };
            HMONITOR mon = MonitorFromWindow(wnd_, MONITOR_DEFAULTTONEAREST);
            if (mon && GetMonitorInfoW(mon, &mi)) wa = mi.rcWork;
            else SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
            int y = wr.top;
            if (y + desired > wa.bottom)
                y = (std::max)(static_cast<int>(wa.top),
                               static_cast<int>(wa.bottom) - desired);
            SetWindowPos(wnd_, nullptr, wr.left, y, kWindowWidth, desired,
                SWP_NOZORDER | SWP_NOACTIVATE);
            // A layered window can keep presenting its old-size surface
            // until repainted — force a synchronous full refresh so live
            // toggles resize visually, not just logically.
            RedrawWindow(wnd_, nullptr, nullptr,
                RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        }
        LayoutListAndStatus();
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

        brickAreaH_ = 0;
        if (!brickFavs_.empty()) {
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
            brickAreaH_ = maxY - listBaseY_;
        }

        // Reflow list/status below the brick area (shrink-wraps in Favorites)
        ApplyWindowHeight();
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
    HWND toolsMenu_ = nullptr;
    HWND autoApply_ = nullptr;
    HWND scene_     = nullptr;
    HWND favs_      = nullptr;
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
    bool  favsOnly_   = false;  // Favorites filter — brick pins stay above the list
    bool  applyToSel_ = false;  // Persisted Auto Apply for clicked materials
    bool  acceleratorsDisabled_ = false;
    int   listBaseY_  = 0;
    int   brickAreaH_ = 0;      // height of the brick rows between filters and list
    // Dual favorites
    std::vector<HWND> brickBtns_;            // brick button HWNDs
    HWND  renameEdit_ = nullptr;
    int   renameIdx_  = -1;
    bool  renaming_   = false;
    int   brickDragFrom_ = -1;
    BrickGesture brickGesture_ = BrickGesture::None;
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
    Palette::Get().ResetModes();
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
void WriteConfigLines(FILE* f)                  { Palette::Get().WriteConfigLines(f); }
void ReadPinsLine(const std::wstring& line)     { ReadPinsLineImpl(line); }
void ReadBricksLine(const std::wstring& line)   { ReadBricksLineImpl(line); }
void ReadConfigLine(const std::wstring& line)   { Palette::Get().ReadConfigLine(line); }
void ClearPersistent()                          { ClearPersistentImpl(); }

} // namespace PowerShader
