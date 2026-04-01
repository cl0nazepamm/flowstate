#include <max.h>
#include <gup.h>
#include <iparamb2.h>
#include <plugapi.h>
#include <maxapi.h>
#include <modstack.h>
#include <object.h>
#include <actiontable.h>
#include <custcont.h>
#include <iparamm2.h>
#include <iepoly.h>
#include <splshape.h>
#include <maxscript/maxscript.h>
#include <hold.h>
#include <windowsx.h>
#include "powershader.h"
#include "modstack.h"
#include <string>
#include <vector>
#include <set>
#include <map>

#define PPARAM_CLASS_ID  Class_ID(0x7A1CE200, 0x3B4D5E6F)
#define PPARAM_NAME      _T("PowerParams")
#define PPARAM_CATEGORY  _T("MCP")

extern HINSTANCE hInstance;
HINSTANCE hInstance = nullptr;

static const ActionTableId   kTableId   = 0x7A1CE201;
static const ActionContextId kContextId = 0x7A1CE202;
static const int             kToggleId  = 1;

// ── Config ──────────────────────────────────────────────────────
static const TCHAR* kWndClass = _T("PowerParamsPanel");
static const int kPad       = 10;
static const int kFontPx    = 13;
static const int kFontHdr   = 15;
static const int kLineH     = 24;
static const int kHeaderH   = 42;
static const int kGroupGap  = 6;
static const int kEditW     = 96;
static const int kEditH     = 22;
static const int kMaxParams = 50;
static const int kMinW      = 320;
static const int kRefreshMs = 500;
static const int kBtnW      = 30;    // side button width
static const int kBtnH      = 20;    // side button height
static const int kBtnGap    = 2;     // gap between side buttons
static const int kSideGap   = 4;     // gap between button strip and panel
static const int kMaxPanelH = 700;

// Theme colors
static COLORREF kBg        = RGB(215, 218, 222);
static COLORREF kBorder    = RGB(140, 145, 150);
static COLORREF kAccent    = RGB(150, 155, 165);
static COLORREF kGroupClr  = RGB(40, 40, 40);
static COLORREF kLabelClr  = RGB(60, 60, 60);
static COLORREF kValueClr  = RGB(20, 20, 20);
static COLORREF kEditBg    = RGB(240, 242, 245);
static COLORREF kEditFocus = RGB(255, 255, 255);
static COLORREF kCloseHov  = RGB(180, 50, 50);
static COLORREF kBtnBg     = RGB(225, 228, 232);
static COLORREF kBtnHov    = RGB(240, 242, 245);
static COLORREF kBtnAct    = RGB(180, 185, 190);

static void UpdateTheme(bool light) {
    if (light) {
        kBg        = RGB(215, 218, 222);
        kBorder    = RGB(140, 145, 150);
        kAccent    = RGB(150, 155, 165);
        kGroupClr  = RGB(40, 40, 40);
        kLabelClr  = RGB(60, 60, 60);
        kValueClr  = RGB(20, 20, 20);
        kEditBg    = RGB(240, 242, 245);
        kEditFocus = RGB(255, 255, 255);
        kCloseHov  = RGB(180, 50, 50);
        kBtnBg     = RGB(225, 228, 232);
        kBtnHov    = RGB(240, 242, 245);
        kBtnAct    = RGB(180, 185, 190);
    } else {
        kBg        = RGB(56, 56, 56);
        kBorder    = RGB(42, 42, 42);
        kAccent    = RGB(38, 148, 168);   // teal
        kGroupClr  = RGB(190, 190, 190);
        kLabelClr  = RGB(195, 195, 195);
        kValueClr  = RGB(230, 230, 230);
        kEditBg    = RGB(42, 42, 42);
        kEditFocus = RGB(48, 58, 65);
        kCloseHov  = RGB(200, 60, 60);
        kBtnBg     = RGB(68, 68, 68);
        kBtnHov    = RGB(80, 80, 80);
        kBtnAct    = RGB(38, 148, 168);   // active = teal
    }
}

static std::wstring MakeParamLabel(const MCHAR* rawName) {
    if (!rawName || !rawName[0]) return L"?";
    std::wstring label(rawName);
    for (auto& ch : label) {
        if (ch == L'_') ch = L' ';
    }
    return label;
}

// Compatibility fallback for operation settings when live rollout controls are unavailable.
struct FallbackOpParam { const TCHAR* label; ParamID pid; bool isFloat; };

static const FallbackOpParam kConnectParams[]     = { {_T("Segments"),(ParamID)ep_connect_edge_segments,false}, {_T("Pinch"),(ParamID)ep_connect_edge_pinch,true}, {_T("Slide"),(ParamID)ep_connect_edge_slide,true} };
static const FallbackOpParam kBridgeParams[]      = { {_T("Segments"),(ParamID)ep_bridge_segments,false}, {_T("Taper"),(ParamID)ep_bridge_taper,true}, {_T("Bias"),(ParamID)ep_bridge_bias,true}, {_T("Twist 1"),(ParamID)ep_bridge_twist_1,true}, {_T("Twist 2"),(ParamID)ep_bridge_twist_2,true} };
static const FallbackOpParam kExtrudeFaceParams[] = { {_T("Height"),(ParamID)ep_face_extrude_height,true}, {_T("Type"),(ParamID)ep_extrusion_type,false} };
static const FallbackOpParam kExtrudeEdgeParams[] = { {_T("Height"),(ParamID)ep_edge_extrude_height,true}, {_T("Width"),(ParamID)ep_edge_extrude_width,true}, {_T("Type"),(ParamID)ep_extrusion_type,false} };
static const FallbackOpParam kExtrudeVertParams[] = { {_T("Width"),(ParamID)ep_vertex_extrude_width,true}, {_T("Height"),(ParamID)ep_vertex_extrude_height,true}, {_T("Type"),(ParamID)ep_extrusion_type,false} };
static const FallbackOpParam kBevelParams[]       = { {_T("Height"),(ParamID)ep_bevel_height,true}, {_T("Outline"),(ParamID)ep_bevel_outline,true}, {_T("Type"),(ParamID)ep_bevel_type,false} };
static const FallbackOpParam kChamferEdgeParams[] = { {_T("Amount"),(ParamID)ep_edge_chamfer,true}, {_T("Segments"),(ParamID)ep_edge_chamfer_segments,false}, {_T("Depth"),(ParamID)ep_edge_chamfer_depth,true}, {_T("Tension"),(ParamID)ep_edge_chamfer_tension,true} };
static const FallbackOpParam kChamferVertParams[] = { {_T("Amount"),(ParamID)ep_vertex_chamfer,true}, {_T("Depth"),(ParamID)ep_vertex_chamfer_depth,true} };
static const FallbackOpParam kInsetParams[]       = { {_T("Amount"),(ParamID)ep_inset,true}, {_T("Type"),(ParamID)ep_inset_type,false} };
static const FallbackOpParam kOutlineParams[]     = { {_T("Amount"),(ParamID)ep_outline,true} };

static const FallbackOpParam* LookupFallbackParams(int op, int selLevel, int& count, std::wstring& title) {
    title.clear();
    switch (op) {
    case epop_connect_edges:    title=L"Connect";  count=3; return kConnectParams;
    case epop_bridge_border: case epop_bridge_polygon: case epop_bridge_edge:
                                title=L"Bridge";   count=5; return kBridgeParams;
    case epop_extrude:
        if (selLevel==EP_SL_EDGE)   { title=L"Extrude Edge";   count=3; return kExtrudeEdgeParams; }
        if (selLevel==EP_SL_VERTEX) { title=L"Extrude Vertex"; count=3; return kExtrudeVertParams; }
        title=L"Extrude Face"; count=2; return kExtrudeFaceParams;
    case epop_bevel:            title=L"Bevel";    count=3; return kBevelParams;
    case epop_chamfer:
        if (selLevel==EP_SL_VERTEX) { title=L"Chamfer Vertex"; count=2; return kChamferVertParams; }
        title=L"Chamfer Edge"; count=4; return kChamferEdgeParams;
    case epop_inset:            title=L"Inset";    count=2; return kInsetParams;
    case epop_outline:          title=L"Outline";  count=1; return kOutlineParams;
    default: count=0; return nullptr;
    }
}

// ── Data ────────────────────────────────────────────────────────
struct EditField {
    HWND         hwnd = nullptr;   // only used by favorites strip
    std::wstring label;
    std::wstring key;
    std::wstring msPath;  // MaxScript object path for PB1 params (e.g. "$.modifiers[2]")
    int          keyOrdinal = 0;
    IParamBlock2* pb  = nullptr;
    ParamID      id   = 0;
    ParamType2   type = (ParamType2)0;
    int          logY = 0;   // logical Y before scroll
};

struct ActionBtn {
    std::wstring label;
    FPInterface* iface = nullptr;
    FunctionID   fnId  = 0;
    int          logY  = 0;
};

struct GroupHeader {
    std::wstring title;
    int startIdx = 0;
    int count    = 0;
    int actionStart = 0;
    int actionCount = 0;
    Modifier* mod = nullptr;
};

// ── Globals ─────────────────────────────────────────────────────
static HWND     g_panel      = nullptr;
static HWND     g_btnLeft    = nullptr;   // floating sub-obj button strip
static HWND     g_btnRight   = nullptr;   // floating ops button strip
static const TCHAR* kBtnStripClass = _T("PPBtnStrip");
static HFONT    g_font       = nullptr;
static HFONT    g_fontBold   = nullptr;
static HBRUSH   g_brEdit     = nullptr;
static HBRUSH   g_brEditFoc  = nullptr;
static WNDPROC  g_origEdit   = nullptr;
static int      g_hoverParam = -1;    // param index under cursor
static int      g_editParam  = -1;    // param being edited (-1 = none)
static HWND     g_editHwnd   = nullptr; // single active edit control
static HHOOK    g_mouseHook  = nullptr;
static HHOOK    g_kbHook     = nullptr;

// Modifier search (embedded in header)
static bool     g_modSearch      = false;  // search mode active
static HWND     g_modSearchEdit  = nullptr;
static std::vector<int> g_modSearchResults; // indices into ModStack cache
static int      g_modSearchSel   = 0;      // selected result index
static int      g_modSearchScrollY = 0;
static bool     g_open       = false;
static bool     g_hoverClose    = false;
static RECT     g_closeRect     = {};
static bool     g_hoverModStack = false;
static RECT     g_modStackRect  = {};
static bool     g_hoverVisBtn   = false;
static RECT     g_visBtnRect    = {};
static bool     g_visEditMode   = false;  // visibility edit mode — shows hidden params, click to toggle

static const UINT WM_PP_TOGGLE   = WM_USER + 100;

// Forward declarations
static LRESULT CALLBACK ModSearchEditProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
static void KillActiveEdit(bool apply);

// Modifier search cache
struct ModCacheEntry { std::wstring label; std::wstring normLabel; std::wstring search; std::wstring internalName; ClassDesc* cd = nullptr; SClass_ID sid = 0; };
static std::vector<ModCacheEntry> g_modCache;
static bool g_modCacheReady = false;

static void EnsureModCache() {
    if (g_modCacheReady) return;
    g_modCache.clear();
    auto scan = [](SClass_ID sid) {
        SubClassList* list = ClassDirectory::GetInstance().GetClassList(sid);
        if (!list) return;
        using CIDPair = std::pair<ULONG, ULONG>;
        std::set<CIDPair> seen;
        for (int i = list->GetFirst(ACC_PUBLIC); i != -1; i = list->GetNext(ACC_PUBLIC)) {
            ClassEntry& ce = (*list)[i];
            ClassDesc* cd = ce.CD();
            if (!cd) continue;
            Class_ID cid = ce.ClassID();
            if (!seen.insert({cid.PartA(), cid.PartB()}).second) continue;
            const MCHAR* cn = cd->NonLocalizedClassName();
            const MCHAR* cl = cd->ClassName();
            const MCHAR* intN = cd->InternalName();
            std::wstring name = (cn && cn[0]) ? cn : (cl ? cl : L"");
            if (name.empty() && intN && intN[0]) name = intN;
            if (name.empty()) continue;
            std::wstring norm; norm.reserve(name.size());
            for (auto c : name) { wchar_t lc = towlower(c); if (iswalnum(lc)) norm += lc; else if (!norm.empty() && norm.back() != L' ') norm += L' '; }
            while (!norm.empty() && norm.back() == L' ') norm.pop_back();
            ModCacheEntry e;
            e.label = name; e.normLabel = norm; e.search = norm;
            e.internalName = (intN && intN[0]) ? intN : L"";
            e.cd = cd; e.sid = sid;
            g_modCache.push_back(std::move(e));
        }
    };
    scan(OSM_CLASS_ID); scan(WSM_CLASS_ID);
    std::sort(g_modCache.begin(), g_modCache.end(),
        [](const ModCacheEntry& a, const ModCacheEntry& b) { return a.label < b.label; });
    g_modCacheReady = true;
}

static void FilterModSearch(const std::wstring& query) {
    g_modSearchResults.clear();
    if (query.empty()) { for (int i = 0; i < (int)g_modCache.size(); i++) g_modSearchResults.push_back(i); return; }
    std::wstring norm; for (auto c : query) { wchar_t lc = towlower(c); if (iswalnum(lc)) norm += lc; else if (!norm.empty() && norm.back() != L' ') norm += L' '; }
    std::vector<std::wstring> tokens; size_t s = 0;
    while (s < norm.size()) { while (s < norm.size() && norm[s] == L' ') s++; size_t e = norm.find(L' ', s); if (e == std::wstring::npos) e = norm.size(); if (e > s) tokens.push_back(norm.substr(s, e - s)); s = e + 1; }
    struct Scored { int idx; int score; };
    std::vector<Scored> scored;
    for (int i = 0; i < (int)g_modCache.size(); i++) {
        auto& mc = g_modCache[i]; int score = 100;
        for (auto& tok : tokens) { size_t pos = mc.search.find(tok); if (pos == std::wstring::npos) { score = 0; break; } if (pos == 0 || mc.search[pos-1] == L' ') score += 10; }
        if (score > 0) { if (!tokens.empty() && mc.normLabel.find(tokens[0]) == 0) score += 50; score += std::max(0, 40 - (int)mc.label.size()); scored.push_back({i, score}); }
    }
    std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) { return a.score > b.score; });
    for (auto& s2 : scored) g_modSearchResults.push_back(s2.idx);
    g_modSearchSel = 0; g_modSearchScrollY = 0;
}

static void ApplyModSearchResult() {
    if (g_modSearchSel < 0 || g_modSearchSel >= (int)g_modSearchResults.size()) return;
    auto& mc = g_modCache[g_modSearchResults[g_modSearchSel]];
    Interface* ip = GetCOREInterface();
    if (!ip || ip->GetSelNodeCount() == 0) return;
    INode* node = ip->GetSelNode(0);
    if (!node) return;

    // Create modifier via C++ SDK — bypasses MaxScript name issues
    if (mc.cd) {
        void* obj = mc.cd->Create(FALSE);
        Modifier* mod = static_cast<Modifier*>(obj);
        if (mod) {
            theHold.Begin();
            GetCOREInterface7()->AddModifier(*node, *mod);
            theHold.Accept(_T("Add Modifier"));
            ip->RedrawViews(ip->GetTime());
            return;
        }
    }

    // Fallback to MaxScript if ClassDesc failed
    std::wstring name = mc.internalName.empty() ? mc.label : mc.internalName;
    std::wstring s = L"try(addModifier $ (" + name + L"()))catch()";
    ExecuteMAXScriptScript(s.c_str(), MAXScript::ScriptSource::Dynamic);
    ip->RedrawViews(ip->GetTime());
}

static void UpdateModSearch() {
    if (!g_modSearchEdit) return;
    TCHAR buf[256]; GetWindowText(g_modSearchEdit, buf, 256);
    bool wasSearch = g_modSearch;
    g_modSearch = (buf[0] != 0);
    if (g_modSearch && !wasSearch) {
        // Entering search mode
        KillActiveEdit(false);
        g_hoverParam = -1;
        g_modSearchScrollY = 0;
        // Expand panel if too narrow/short for search results
        RECT wr; GetWindowRect(g_panel, &wr);
        int pw = wr.right - wr.left;
        int ph = wr.bottom - wr.top;
        int needW = std::max(pw, 360);
        int needH = std::max(ph, 400);
        if (needW != pw || needH != ph) {
            SetWindowPos(g_panel, nullptr, 0, 0, needW, needH,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            if (g_modSearchEdit) {
                RECT rc2; GetClientRect(g_panel, &rc2);
                SetWindowPos(g_modSearchEdit, nullptr, kPad, kPad - 1,
                    rc2.right - kPad * 2 - 62, kFontHdr + 2, SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
    }
    if (g_modSearch) {
        EnsureModCache();
        FilterModSearch(buf);
    }
    InvalidateRect(g_panel, nullptr, FALSE);
}

static void ExitModSearch() {
    g_modSearch = false;
    if (g_modSearchEdit) SetWindowText(g_modSearchEdit, _T(""));
    g_modSearchResults.clear();
    InvalidateRect(g_panel, nullptr, FALSE);
}

// Favorites strip
static HWND     g_favWnd     = nullptr;
static const TCHAR* kFavClass = _T("PPFavStrip");
static std::set<std::wstring> g_favorites;
static std::vector<EditField> g_favEdits;
static const int kFavMaxParams = 12;

static ULONG        g_nodeHandle       = 0;

// EPoly takeover state — active tool detected on panel open
static int          g_epolyOp       = -1;
static FPInterface* g_epolyFP       = nullptr;
static bool         g_epolyPreview  = false;


// Context-aware tool system
enum ObjContext { CTX_NONE, CTX_EPOLY, CTX_SPLINE };
static ObjContext g_ctx = CTX_NONE;
static FPInterface* g_epolyForButtons = nullptr;
static SplineShape* g_splineForButtons = nullptr;

// Spline operation values (local storage — ISplineOps is empty stubs)
static float g_splineVals[] = { 0.1f, 0.0f, 0.0f, 0.0f }; // Weld, Fillet, Chamfer, Outline
// Spline op IDs offset by sentinel to avoid collision with PB1 ef.id=0
constexpr int kSpSentinel = 9000;
enum { kSpWeld=kSpSentinel, kSpFillet=kSpSentinel+1, kSpChamfer=kSpSentinel+2, kSpOutline=kSpSentinel+3 };
constexpr int kSpCount = 4;
static const TCHAR* kSpLabels[] = { _T("Weld"), _T("Fillet"), _T("Chamfer"), _T("Outline") };

struct BtnDef { const TCHAR* label; int id; };

// EPoly buttons
static const BtnDef kEPolySubObj[] = {
    {_T("V"),1}, {_T("E"),2}, {_T("B"),3}, {_T("F"),4}, {_T("El"),5}
};
static const BtnDef kEPolyOps[] = {
    {_T("EXT"),epop_extrude}, {_T("CON"),epop_connect_edges},
    {_T("BRG"),epop_bridge_edge}, {_T("CHM"),epop_chamfer},
    {_T("BVL"),epop_bevel}, {_T("INS"),epop_inset},
    {_T("OTL"),epop_outline}, {_T("REM"),epop_remove}
};

// Spline buttons
static const BtnDef kSplineSubObj[] = {
    {_T("V"),SS_VERTEX}, {_T("S"),SS_SEGMENT}, {_T("Sp"),SS_SPLINE}
};
static const BtnDef kSplineOps[] = {
    {_T("REF"),ScmRefine}, {_T("FIL"),ScmFillet},
    {_T("CHM"),ScmChamfer}, {_T("OTL"),ScmOutline},
    {_T("CON"),ScmConnect}, {_T("TRM"),ScmTrim},
    {_T("EXD"),ScmExtend}, {_T("BOL"),ScmUnion}
};
static int g_hoverBtn  = -1;  // hovered button ID for strip windows
static bool g_showSubObj = false; // sub-object toggles (off by default, configurable)
static bool g_tabShader  = true;  // Tab key opens PowerShader when SME focused (on by default)
static bool g_lightTheme = false; // light theme (brushed aluminium)
static bool  g_mmDragging = false;  // middle-mouse panel drag
static POINT g_mmStart = {};
static RECT  g_mmPanelRect = {};

// Left-click drag on param label to scrub value
static bool  g_lmbDragging = false;
static int   g_lmbDragIdx  = -1;
static int   g_lmbDragStartX = 0;
static int   g_lmbDragAccum  = 0;  // accumulated sub-threshold movement for ints
static bool  g_lmbDragMoved  = false; // true once mouse moves enough to be a drag (not a click)

// Module enable flags (read from FlowState.ini)
static bool g_enablePowerParams = true;
static bool g_enablePowerShader = true;

struct QuickMod {
    std::wstring internalName;
    std::wstring label;
    std::wstring shortLabel;
};
static std::vector<QuickMod> g_quickMods;

static void PositionBtnStrips();

void TogglePowerParamsQuickModifier(const wchar_t* internalName, const wchar_t* label) {
    if (!internalName || !label) return;
    std::wstring iname(internalName);
    for (auto it = g_quickMods.begin(); it != g_quickMods.end(); ++it) {
        if (it->internalName == iname) {
            g_quickMods.erase(it);
            if (g_open && g_btnRight) InvalidateRect(g_btnRight, nullptr, TRUE);
            PositionBtnStrips();
            return;
        }
    }
    
    std::wstring sl;
    std::wstring lbl(label);
    for (wchar_t c : lbl) if (iswupper(c)) sl += c;
    if (sl.empty() || sl.length() == 1) {
        sl = lbl.substr(0, std::min(size_t(3), lbl.size()));
        for (auto& c : sl) c = towupper(c);
    }
    if (sl.length() > 4) sl = sl.substr(0, 4);

    g_quickMods.push_back({iname, lbl, sl});
    if (g_open && g_btnRight) InvalidateRect(g_btnRight, nullptr, TRUE);
    PositionBtnStrips();
}

bool IsPowerParamsQuickModifier(const wchar_t* internalName) {
    if (!internalName) return false;
    std::wstring iname(internalName);
    for (const auto& qm : g_quickMods) {
        if (qm.internalName == iname) return true;
    }
    return false;
}

// XButton1 dynamic assignment — two slots: vertical (V) and horizontal (H)
// Keyed per base-object class so each object type remembers its own assignments
struct XB1Slot {
    std::wstring key;
    std::wstring label;
    IParamBlock2* pb = nullptr;
    ParamID pid = 0;
    ParamType2 type = (ParamType2)0;
    std::wstring msPath;
    int spIdx = -1;
    float displayVal = 0.f; // tracked value for OSD display
    void Clear() { key.clear(); label.clear(); pb=nullptr; pid=0; type=(ParamType2)0; msPath.clear(); spIdx=-1; displayVal=0.f; }
    bool Active() const { return !key.empty(); }
};
struct XB1Pair { std::wstring vKey, hKey; };
static std::map<std::wstring, XB1Pair> g_xb1Map; // baseClass → assignments
static XB1Slot g_xb1V; // active vertical slot (resolved for current selection)
static XB1Slot g_xb1H; // active horizontal slot
static std::wstring g_xb1Class; // current base class context
static HWND         g_osdWnd = nullptr;
static UINT_PTR     g_osdTimer = 0;
static HWND         g_dragTip = nullptr; // cursor-following value tooltip

// LoadModuleFlags is now handled by LoadSettings() — reads [config] section from FlowState.cfg
static void LoadModuleFlags() {
    // No-op: LoadSettings() handles everything now.
    // Kept as stub so call sites don't need to change yet.
}

static std::wstring GetSelBaseClass() {
    Interface* ip = GetCOREInterface();
    if (!ip || ip->GetSelNodeCount() == 0) return {};
    INode* nd = ip->GetSelNode(0);
    if (!nd) return {};
    Object* obj = nd->GetObjectRef();
    while (obj && obj->SuperClassID() == GEN_DERIVOB_CLASS_ID)
        obj = ((IDerivedObject*)obj)->GetObjRef();
    if (!obj) return {};
    MSTR cn; obj->GetClassName(cn, false);
    return (cn.data() && cn.data()[0]) ? std::wstring(cn.data()) : L"Object";
}

// Stash current active slots back into the map
static void StashXB1() {
    if (!g_xb1Class.empty()) {
        if (g_xb1V.Active() || g_xb1H.Active())
            g_xb1Map[g_xb1Class] = { g_xb1V.key, g_xb1H.key };
        else
            g_xb1Map.erase(g_xb1Class);
    }
}

// Load slots for the current selection's base class (keys only — resolve at drag time)
static std::wstring PrettyLabel(const std::wstring& raw, const std::wstring& groupTitle); // forward decl
static std::wstring LabelFromKey(const std::wstring& key) {
    size_t sep = key.find(L':');
    if (sep == std::wstring::npos) return key;
    std::wstring cls = key.substr(0, sep);
    std::wstring raw = key.substr(sep + 1);
    return PrettyLabel(raw, cls);
}

static void SyncXB1ToSelection() {
    std::wstring cls = GetSelBaseClass();
    if (cls == g_xb1Class) return; // no change
    StashXB1();
    g_xb1V.Clear(); g_xb1H.Clear();
    g_xb1Class = cls;
    auto it = g_xb1Map.find(cls);
    if (it != g_xb1Map.end()) {
        if (!it->second.vKey.empty()) {
            g_xb1V.key = it->second.vKey;
            g_xb1V.label = LabelFromKey(it->second.vKey);
        }
        if (!it->second.hKey.empty()) {
            g_xb1H.key = it->second.hKey;
            g_xb1H.label = LabelFromKey(it->second.hKey);
        }
    }
}

static void SaveSettings(); // forward declaration
static void SaveXB1Assignment() {
    StashXB1();
    SaveSettings(); // XB1 data is now in [config] section of FlowState.cfg
}

// ── Fade animation (non-blocking, ease-out curve) ───────────────
constexpr UINT_PTR kFadeTimerId = 99;
constexpr int kFadeDurationMs = 80;   // total fade time
constexpr int kFadeIntervalMs = 10;   // timer tick

struct FadeState {
    HWND hwnd = nullptr;
    bool fadingIn = false;
    bool fadingOut = false;
    DWORD startTime = 0;
    POINT basePos = {};
    POINT compPos[4] = {};
    // Companion windows to fade alongside
    HWND companions[4] = {};
    int numCompanions = 0;
};
static FadeState g_fade;

static void FadeSetAlpha(BYTE alpha) {
    SetLayeredWindowAttributes(g_fade.hwnd, 0, alpha, LWA_ALPHA);
    for (int i = 0; i < g_fade.numCompanions; i++)
        if (g_fade.companions[i] && IsWindowVisible(g_fade.companions[i]))
            SetLayeredWindowAttributes(g_fade.companions[i], 0, alpha, LWA_ALPHA);
}

static void FadeSetPos(float curve) {
    // fadingIn: curve goes 0->1. offset goes 15->0. pos = base + offset (slides UP to base)
    // fadingOut: curve goes 0->1. offset goes 0->10. pos = base + offset (slides DOWN from base)
    int offset = g_fade.fadingIn ? (int)(15.0f * (1.0f - curve)) : (int)(10.0f * curve);
    
    SetWindowPos(g_fade.hwnd, nullptr, g_fade.basePos.x, g_fade.basePos.y + offset, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    for (int i = 0; i < g_fade.numCompanions; i++) {
        if (g_fade.companions[i] && IsWindowVisible(g_fade.companions[i])) {
            SetWindowPos(g_fade.companions[i], nullptr, g_fade.compPos[i].x, g_fade.compPos[i].y + offset, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
}

static void CALLBACK FadeTimerProc(HWND, UINT, UINT_PTR, DWORD) {
    float t = (float)(GetTickCount() - g_fade.startTime) / (float)kFadeDurationMs;
    if (t >= 1.0f) t = 1.0f;
    // Cubic ease-out: fast start, smooth settle for both alpha and pos
    float curve = g_fade.fadingIn ? (1.0f - (1.0f-t)*(1.0f-t)*(1.0f-t)) : (t*t*t);
    BYTE alpha = g_fade.fadingIn ? (BYTE)(curve * 255.0f) : (BYTE)((1.0f - curve) * 255.0f);
    FadeSetAlpha(alpha);
    FadeSetPos(curve);
    if (t >= 1.0f) {
        KillTimer(g_fade.hwnd, kFadeTimerId);
        if (g_fade.fadingOut) {
            ShowWindow(g_fade.hwnd, SW_HIDE);
            FadeSetAlpha(255); // reset for next show
            SetWindowPos(g_fade.hwnd, nullptr, g_fade.basePos.x, g_fade.basePos.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            for (int i = 0; i < g_fade.numCompanions; i++) {
                if (g_fade.companions[i]) SetWindowPos(g_fade.companions[i], nullptr, g_fade.compPos[i].x, g_fade.compPos[i].y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        } else {
            SetWindowPos(g_fade.hwnd, nullptr, g_fade.basePos.x, g_fade.basePos.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            for (int i = 0; i < g_fade.numCompanions; i++) {
                if (g_fade.companions[i]) SetWindowPos(g_fade.companions[i], nullptr, g_fade.compPos[i].x, g_fade.compPos[i].y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
        g_fade.fadingIn = g_fade.fadingOut = false;
    }
}

static void FadeIn(HWND hwnd, HWND* companions = nullptr, int numComp = 0) {
    g_fade.hwnd = hwnd;
    g_fade.fadingIn = true; g_fade.fadingOut = false;
    g_fade.startTime = GetTickCount();
    g_fade.numCompanions = std::min(numComp, 4);
    RECT r; GetWindowRect(hwnd, &r); g_fade.basePos = {r.left, r.top};
    for (int i = 0; i < g_fade.numCompanions; i++) {
        g_fade.companions[i] = companions[i];
        if (companions[i]) {
            RECT cr; GetWindowRect(companions[i], &cr);
            g_fade.compPos[i] = {cr.left, cr.top};
            ShowWindow(companions[i], SW_SHOWNA);
        }
    }
    FadeSetAlpha(0);
    ShowWindow(hwnd, SW_SHOWNA);
    SetTimer(hwnd, kFadeTimerId, kFadeIntervalMs, FadeTimerProc);
}

static void FadeOut(HWND hwnd) {
    if (!IsWindowVisible(hwnd)) return;
    g_fade.hwnd = hwnd;
    g_fade.fadingOut = true; g_fade.fadingIn = false;
    g_fade.startTime = GetTickCount();
    RECT r; GetWindowRect(hwnd, &r); g_fade.basePos = {r.left, r.top};
    for (int i = 0; i < g_fade.numCompanions; i++) {
        if (g_fade.companions[i]) {
            RECT cr; GetWindowRect(g_fade.companions[i], &cr);
            g_fade.compPos[i] = {cr.left, cr.top};
        }
    }
    SetTimer(hwnd, kFadeTimerId, kFadeIntervalMs, FadeTimerProc);
}

// Forward declarations (defined later in file)
static bool IsFloat(ParamType2 t);
static bool IsInt(ParamType2 t);
static std::wstring PropFromKey(const EditField& ef);
static bool FindParam(INode* node, const std::wstring& key,
                      IParamBlock2*& outPB, ParamID& outID, ParamType2& outType);

static void ShowOSD(const std::wstring& text) {
    HWND maxWnd = GetCOREInterface() ? GetCOREInterface()->GetMAXHWnd() : nullptr;
    if (!maxWnd) return;
    RECT wr; GetWindowRect(maxWnd, &wr);
    int cx = (wr.left + wr.right) / 2;
    int cy = (wr.top + wr.bottom) / 2;

    if (!g_osdWnd) {
        static bool regOsd = false;
        if (!regOsd) {
            WNDCLASSEX wc = { sizeof(wc) };
            wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
                if (m == WM_PAINT) {
                    PAINTSTRUCT ps; HDC hdc = BeginPaint(h, &ps);
                    RECT rc; GetClientRect(h, &rc);
                    HBRUSH bg = CreateSolidBrush(RGB(30, 30, 30));
                    FillRect(hdc, &rc, bg); DeleteObject(bg);
                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, RGB(38, 148, 168));
                    HFONT f = CreateFont(-18, 0,0,0, FW_BOLD, 0,0,0, DEFAULT_CHARSET,
                        0,0,CLEARTYPE_QUALITY,0, _T("Segoe UI"));
                    HFONT old = (HFONT)SelectObject(hdc, f);
                    wchar_t buf[128] = {};
                    GetWindowTextW(h, buf, 128);
                    DrawTextW(hdc, buf, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(hdc, old); DeleteObject(f);
                    EndPaint(h, &ps);
                    return 0;
                }
                if (m == WM_TIMER) { ShowWindow(h, SW_HIDE); KillTimer(h, 1); return 0; }
                return DefWindowProc(h, m, w, l);
            };
            wc.hInstance = hInstance;
            wc.lpszClassName = _T("FlowStateOSD");
            RegisterClassEx(&wc);
            regOsd = true;
        }
        g_osdWnd = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
            _T("FlowStateOSD"), _T(""), WS_POPUP,
            0, 0, 280, 36, nullptr, nullptr, hInstance, nullptr);
    }
    SetWindowTextW(g_osdWnd, text.c_str());
    SetWindowPos(g_osdWnd, HWND_TOPMOST, cx - 140, cy - 18, 280, 36, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_osdWnd, nullptr, TRUE);
    SetTimer(g_osdWnd, 1, 1200, nullptr);
}

static std::wstring FmtFloat(float v) {
    wchar_t buf[32];
    float a = v < 0 ? -v : v;
    if (a >= 100.f)     swprintf(buf, 32, L"%.0f", v);
    else if (a >= 1.f)  swprintf(buf, 32, L"%.1f", v);
    else                swprintf(buf, 32, L"%.2f", v);
    return buf;
}

static std::wstring ReadSlotValue(const XB1Slot& slot) {
    if (!slot.Active()) return {};
    Interface* ip = GetCOREInterface();
    TimeValue t = ip ? ip->GetTime() : 0;
    if (slot.pb) {
        if (IsFloat(slot.type)) return FmtFloat(slot.pb->GetFloat(slot.pid, t));
        if (slot.type == TYPE_BOOL) return slot.pb->GetInt(slot.pid, t) ? L"On" : L"Off";
        return std::to_wstring(slot.pb->GetInt(slot.pid, t));
    }
    if (slot.spIdx >= 0) return FmtFloat(g_splineVals[slot.spIdx]);
    // PB1 — use tracked displayVal (set at drag start, updated per frame)
    return FmtFloat(slot.displayVal);
    return L"--";
}

static HFONT g_tipFont = nullptr;
static HBRUSH g_tipBg  = nullptr;

static void ShowDragTip(const POINT& pt, const std::wstring& text) {
    if (!g_tipFont) g_tipFont = CreateFont(-12, 0,0,0, FW_NORMAL, 0,0,0, DEFAULT_CHARSET,
        0,0,CLEARTYPE_QUALITY,0, _T("Segoe UI"));
    if (!g_tipBg) g_tipBg = CreateSolidBrush(RGB(30, 30, 30));

    if (!g_dragTip) {
        static bool regTip = false;
        if (!regTip) {
            WNDCLASSEX wc = { sizeof(wc) };
            wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
                if (m == WM_PAINT) {
                    PAINTSTRUCT ps; HDC hdc = BeginPaint(h, &ps);
                    RECT rc; GetClientRect(h, &rc);
                    FillRect(hdc, &rc, g_tipBg);
                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, RGB(200, 220, 230));
                    HFONT old = (HFONT)SelectObject(hdc, g_tipFont);
                    wchar_t buf[128] = {};
                    GetWindowTextW(h, buf, 128);
                    DrawTextW(hdc, buf, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(hdc, old);
                    EndPaint(h, &ps);
                    return 0;
                }
                return DefWindowProc(h, m, w, l);
            };
            wc.hInstance = hInstance;
            wc.lpszClassName = _T("FlowStateDragTip");
            RegisterClassEx(&wc);
            regTip = true;
        }
        g_dragTip = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
            _T("FlowStateDragTip"), _T(""), WS_POPUP,
            0, 0, 1, 1, nullptr, nullptr, hInstance, nullptr);
    }
    // Measure and position — no font alloc
    HDC hdc = GetDC(g_dragTip);
    HFONT old = (HFONT)SelectObject(hdc, g_tipFont);
    SIZE sz; GetTextExtentPoint32W(hdc, text.c_str(), (int)text.size(), &sz);
    SelectObject(hdc, old);
    ReleaseDC(g_dragTip, hdc);

    int w = sz.cx + 8, h = sz.cy + 4;
    SetWindowTextW(g_dragTip, text.c_str());
    SetWindowPos(g_dragTip, HWND_TOPMOST, pt.x + 16, pt.y + 16, w, h,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_dragTip, nullptr, TRUE);
}

static void HideDragTip() {
    if (g_dragTip) ShowWindow(g_dragTip, SW_HIDE);
}

static int g_contentStartY = 0;  // where scrollable content begins

// Tool tip overlay (shows active EPoly tool name near cursor)
static HWND g_toolTip = nullptr;
static const TCHAR* kToolTipClass = _T("PPToolTip");

// Scroll
static int g_scrollY   = 0;
static int g_contentH  = 0;  // total content height
static int g_viewH     = 0;  // visible content area height


static std::vector<EditField>   g_edits;
static std::vector<ActionBtn>   g_actions;
static std::vector<GroupHeader> g_groups;
static std::wstring             g_nodeName;
static POINT                    g_panelPos = {0,0};
static bool                     g_freshOpen = false; // true during OpenPanel, prevents BuildLayout from overriding cursor pos

// ── Persistent settings ─────────────────────────────────────────
static std::set<std::wstring> g_collapsed;
static std::set<std::wstring> g_hidden;

static std::wstring GetCfgPath() {
    Interface* ip = GetCOREInterface();
    if (!ip) return L"";
    MSTR dirStr = ip->GetDir(APP_PLUGCFG_DIR);
    const MCHAR* dir = dirStr.data();
    if (!dir || !dir[0]) return L"";
    return std::wstring(dir) + L"\\FlowState.cfg";
}

static void SaveSettings() {
    std::wstring path = GetCfgPath();
    if (path.empty()) return;
    FILE* f = _wfopen(path.c_str(), L"w");
    if (!f) return;

    // [config] — module flags & XB1 assignments
    fwprintf(f, L"[config]\n");
    if (!g_enablePowerParams) fwprintf(f, L"PowerParams=0\n");
    if (!g_enablePowerShader) fwprintf(f, L"PowerShader=0\n");
    if (g_showSubObj)         fwprintf(f, L"SubObjToggles=1\n");
    if (!g_tabShader)         fwprintf(f, L"TabShader=0\n");
    if (g_lightTheme)         fwprintf(f, L"LightTheme=1\n");
    for (auto& [cls, pair] : g_xb1Map) {
        if (pair.vKey.empty() && pair.hKey.empty()) continue;
        fwprintf(f, L"XB1:%s=%s|%s\n", cls.c_str(), pair.vKey.c_str(), pair.hKey.c_str());
    }

    // [params] — collapsed, hidden, favorites, quick mods
    fwprintf(f, L"[params]\n");
    for (auto& s : g_collapsed) fwprintf(f, L"C:%s\n", s.c_str());
    for (auto& s : g_hidden)    fwprintf(f, L"H:%s\n", s.c_str());
    for (auto& s : g_favorites) fwprintf(f, L"F:%s\n", s.c_str());
    for (auto& qm : g_quickMods)
        fwprintf(f, L"Q:%s|%s\n", qm.internalName.c_str(), qm.label.c_str());

    // [pins] and [bricks] — PowerShader data
    PowerShader::WritePinsSection(f);
    PowerShader::WriteBricksSection(f);

    fclose(f);
}

// Non-static wrapper so powershader.cpp can trigger a full save
void FlowState_SaveSettings() { SaveSettings(); }

static void LoadSettings() {
    g_collapsed.clear(); g_hidden.clear(); g_favorites.clear(); g_quickMods.clear();
    PowerShader::ClearPersistent();

    std::wstring path = GetCfgPath();
    if (path.empty()) { UpdateTheme(false); PowerShader::ReloadTheme(false); ModStack::ReloadTheme(false); return; }
    FILE* f = _wfopen(path.c_str(), L"r");
    if (!f) {
        // Try legacy files for migration
        std::wstring dir = path.substr(0, path.rfind(L'\\') + 1);
        f = _wfopen((dir + L"PowerParams.cfg").c_str(), L"r");
        if (f) {
            // Legacy format: no sections, just prefixed lines
            wchar_t line[512];
            while (fgetws(line, 512, f)) {
                size_t len = wcslen(line);
                while (len > 0 && (line[len-1]==L'\n'||line[len-1]==L'\r')) line[--len]=0;
                if (len < 3 || line[1] != L':') continue;
                std::wstring val(line + 2);
                switch (line[0]) {
                case L'C': g_collapsed.insert(val); break;
                case L'F': if (!val.empty() && val[0] != L':') g_favorites.insert(val); break;
                case L'H': g_hidden.insert(val); break;
                case L'Q': { size_t p = val.find(L'|'); if (p != std::wstring::npos) TogglePowerParamsQuickModifier(val.substr(0,p).c_str(), val.substr(p+1).c_str()); break; }
                }
            }
            fclose(f);
            SaveSettings(); // migrate to new unified file
            UpdateTheme(false); PowerShader::ReloadTheme(false); ModStack::ReloadTheme(false);
            return;
        }
        UpdateTheme(false); PowerShader::ReloadTheme(false); ModStack::ReloadTheme(false);
        return;
    }

    enum Section { SEC_NONE, SEC_CONFIG, SEC_PARAMS, SEC_PINS, SEC_BRICKS };
    Section sec = SEC_NONE;
    wchar_t line[512];
    while (fgetws(line, 512, f)) {
        size_t len = wcslen(line);
        while (len > 0 && (line[len-1]==L'\n'||line[len-1]==L'\r')) line[--len]=0;
        if (len == 0) continue;

        // Section headers
        if (wcscmp(line, L"[config]") == 0)  { sec = SEC_CONFIG; continue; }
        if (wcscmp(line, L"[params]") == 0)  { sec = SEC_PARAMS; continue; }
        if (wcscmp(line, L"[pins]") == 0)    { sec = SEC_PINS;   continue; }
        if (wcscmp(line, L"[bricks]") == 0)  { sec = SEC_BRICKS; continue; }

        std::wstring l(line);

        if (sec == SEC_CONFIG) {
            if (l == L"PowerParams=0")   g_enablePowerParams = false;
            if (l == L"PowerShader=0")   g_enablePowerShader = false;
            if (l == L"SubObjToggles=1") g_showSubObj = true;
            if (l == L"TabShader=0")     g_tabShader = false;
            if (l == L"LightTheme=1")    g_lightTheme = true;
            if (l.compare(0, 4, L"XB1:") == 0) {
                size_t eq = l.find(L'=', 4);
                if (eq != std::wstring::npos) {
                    std::wstring cls = l.substr(4, eq - 4);
                    std::wstring val = l.substr(eq + 1);
                    size_t pipe = val.find(L'|');
                    if (pipe != std::wstring::npos)
                        g_xb1Map[cls] = { val.substr(0, pipe), val.substr(pipe + 1) };
                }
            }
        }
        else if (sec == SEC_PARAMS) {
            if (len < 3 || line[1] != L':') continue;
            std::wstring val(line + 2);
            switch (line[0]) {
            case L'C': g_collapsed.insert(val); break;
            case L'F': if (!val.empty() && val[0] != L':') g_favorites.insert(val); break;
            case L'H': g_hidden.insert(val); break;
            case L'Q': { size_t p = val.find(L'|'); if (p != std::wstring::npos) TogglePowerParamsQuickModifier(val.substr(0,p).c_str(), val.substr(p+1).c_str()); break; }
            }
        }
        else if (sec == SEC_PINS) {
            PowerShader::ReadPinsLine(l);
        }
        else if (sec == SEC_BRICKS) {
            PowerShader::ReadBricksLine(l);
        }
    }
    fclose(f);
    UpdateTheme(g_lightTheme);
    PowerShader::ReloadTheme(g_lightTheme);
    ModStack::ReloadTheme(g_lightTheme);
}

// ── Material Editor detection ────────────────────────────────────
static bool IsMaterialEditorFocused() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    // Check window title for material editor keywords
    TCHAR title[256] = {};
    GetWindowText(fg, title, 256);
    if (_tcsstr(title, _T("Material Editor"))) return true;
    if (_tcsstr(title, _T("Slate Material"))) return true;
    if (_tcsstr(title, _T("Material/Map Browser"))) return true;
    // Also check parent
    HWND parent = GetParent(fg);
    if (parent) {
        GetWindowText(parent, title, 256);
        if (_tcsstr(title, _T("Material Editor"))) return true;
        if (_tcsstr(title, _T("Slate Material"))) return true;
    }
    return false;
}

static const UINT WM_PP_SHADER   = WM_USER + 104;
static const UINT WM_PP_MODSTACK = WM_USER + 105;

// ── Forward declarations ────────────────────────────────────────
static void TogglePanel();
static void ClosePanel();
static void KillActiveEdit(bool apply);
static void SpawnEditAt(int paramIdx);
static int  FindParamAtY(int clickY);
static LRESULT CALLBACK ModSearchEditProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
static void BuildLayout();

// ── Mouse hook — XButton2=panel, XButton1=pin ───────────────────
// Uses WH_MOUSE (thread-level) instead of WH_MOUSE_LL (system-wide)
// to avoid blocking the system input pipeline when Max is busy.
static LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wp, LPARAM lp) {
    if (nCode < 0) return CallNextHookEx(g_mouseHook, nCode, wp, lp);

    MOUSEHOOKSTRUCT* ms = (MOUSEHOOKSTRUCT*)lp;

    // Click outside panel = instant close
    if (g_open && !g_epolyPreview &&
        (wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN || wp == WM_MBUTTONDOWN)) {
        if (ms->hwnd != g_panel && !IsChild(g_panel, ms->hwnd) &&
            ms->hwnd != g_btnLeft && ms->hwnd != g_btnRight &&
            ms->hwnd != g_favWnd && !IsChild(g_favWnd, ms->hwnd)) {
            PostMessage(g_panel, WM_USER + 101, 0, 0);
        }
    }

    // ── XButton1 hold-drag tools ──────────────────────────────
    enum DragMode { DRAG_NONE, DRAG_TIME, DRAG_OPACITY };
    static DragMode s_dragMode = DRAG_NONE;
    static int   s_lastMouseX  = 0;
    static int   s_lastMouseY  = 0;
    static float s_dragFloat   = 0;
    static bool  s_xb1Dragging = false;

    // XButton1 assigned param drag — uses cached data, works with panel closed
    if (s_xb1Dragging && wp == WM_MOUSEMOVE && (g_xb1V.Active() || g_xb1H.Active())) {
        MOUSEHOOKSTRUCT* ms2 = (MOUSEHOOKSTRUCT*)lp;
        int dy = s_lastMouseY - ms2->pt.y;  // up = positive
        int dx = ms2->pt.x - s_lastMouseX;  // right = positive
        s_lastMouseX = ms2->pt.x;
        s_lastMouseY = ms2->pt.y;

        Interface* ip = GetCOREInterface();
        TimeValue t = ip ? ip->GetTime() : 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool changed = false;

        auto adjustSlot = [&](XB1Slot& slot, int delta) {
            if (!slot.Active() || delta == 0) return;
            float step = (float)delta;
            int selCount = ip ? ip->GetSelNodeCount() : 0;

            if (slot.pb) {
                // PB2 — resolve and adjust on ALL selected nodes
                for (int ni = 0; ni < selCount; ni++) {
                    INode* nd = ip->GetSelNode(ni);
                    if (!nd) continue;
                    IParamBlock2* pb = nullptr; ParamID pid = 0; ParamType2 pt = (ParamType2)0;
                    if (!FindParam(nd, slot.key, pb, pid, pt)) continue;
                    if (IsFloat(pt)) {
                        float cur = pb->GetFloat(pid, t);
                        float a = cur<0?-cur:cur;
                        // Proportional: ~1% of value per pixel, min 0.001
                        float sc = a > 0.001f ? a * 0.01f : 0.001f;
                        if (shift) sc *= 10.0f; if (ctrl) sc *= 0.1f;
                        pb->SetValue(pid, t, cur + step * sc);
                    } else if (pt == TYPE_BOOL) {
                        pb->SetValue(pid, t, pb->GetInt(pid, t) ? 0 : 1);
                    } else {
                        // Ints: 1 per 3 pixels
                        int cur = pb->GetInt(pid, t);
                        int s = delta / 3;
                        if (s == 0) s = delta > 0 ? 1 : -1;
                        if (shift) s *= 10;
                        pb->SetValue(pid, t, cur + s);
                    }
                }
            } else if (slot.spIdx >= 0) {
                float a = g_splineVals[slot.spIdx]<0?-g_splineVals[slot.spIdx]:g_splineVals[slot.spIdx];
                float sc = a > 0.001f ? a * 0.01f : 0.001f;
                if (shift) sc *= 10.0f; if (ctrl) sc *= 0.1f;
                g_splineVals[slot.spIdx] += step * sc;
                slot.displayVal = g_splineVals[slot.spIdx];
            } else {
                // PB1 — loop all selected objects via MaxScript
                float a = slot.displayVal<0?-slot.displayVal:slot.displayVal;
                float sc = a > 0.001f ? a * 0.01f : 0.001f;
                if (shift) sc *= 10.0f; if (ctrl) sc *= 0.1f;
                float inc = step * sc;
                slot.displayVal += inc;
                size_t sep = slot.key.find(L':');
                if (sep != std::wstring::npos) {
                    std::wstring prop = slot.key.substr(sep + 1);
                    std::wstring objPath = slot.msPath;
                    if (!objPath.empty() && objPath[0] == L'$') objPath = objPath.substr(1);
                    std::wstring sc2 = L"for obj in selection do try(local o=obj" + objPath +
                        L";local v=getProperty o #" + prop +
                        L";setProperty o #" + prop + L" (v+" + std::to_wstring(inc) + L"))catch()";
                    ExecuteMAXScriptScript(sc2.c_str(), MAXScript::ScriptSource::Dynamic);
                }
            }
            changed = true;
        };

        adjustSlot(g_xb1V, dy);
        adjustSlot(g_xb1H, dx);

        if (changed && ip) {
            for (int ni = 0; ni < ip->GetSelNodeCount(); ni++) {
                INode* nd = ip->GetSelNode(ni);
                if (nd) nd->NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE);
            }
            ip->RedrawViews(t);

            // Cursor tooltip with live values
            std::wstring tip;
            if (g_xb1V.Active()) tip += g_xb1V.label + L": " + ReadSlotValue(g_xb1V);
            if (g_xb1V.Active() && g_xb1H.Active()) tip += L"  ";
            if (g_xb1H.Active()) tip += g_xb1H.label + L": " + ReadSlotValue(g_xb1H);
            ShowDragTip(ms2->pt, tip);
        }
        return 1;
    }
    if (s_xb1Dragging && wp == WM_XBUTTONUP) {
        s_xb1Dragging = false;
        theHold.Accept(_T("XB1 Drag"));
        HideDragTip();
        return 1;
    }

    if (s_dragMode != DRAG_NONE && wp == WM_MOUSEMOVE) {
        MOUSEHOOKSTRUCT* ms2 = (MOUSEHOOKSTRUCT*)lp;
        int dx = ms2->pt.x - s_lastMouseX;
        s_lastMouseX = ms2->pt.x;
        if (dx != 0) {
            Interface* ip = GetCOREInterface();
            if (s_dragMode == DRAG_TIME && ip) {
                float speed = 1.0f;
                if (GetKeyState(VK_MENU) & 0x8000) speed = 0.2f;
                s_dragFloat += (float)dx / 10.0f * speed;
                Interval range = ip->GetAnimRange();
                float minF = (float)range.Start() / GetTicksPerFrame();
                float maxF = (float)range.End() / GetTicksPerFrame();
                if (s_dragFloat < minF) s_dragFloat = minF;
                if (s_dragFloat > maxF) s_dragFloat = maxF;
                ip->SetTime((TimeValue)(s_dragFloat * GetTicksPerFrame()), TRUE);
            }
            else if (s_dragMode == DRAG_OPACITY && ip && ip->GetSelNodeCount() > 0) {
                s_dragFloat += (float)dx * 0.005f;
                if (s_dragFloat < 0.0f) s_dragFloat = 0.0f;
                if (s_dragFloat > 1.0f) s_dragFloat = 1.0f;
                for (int i = 0; i < ip->GetSelNodeCount(); i++) {
                    INode* nd = ip->GetSelNode(i);
                    if (nd) nd->SetVisibility(ip->GetTime(), s_dragFloat);
                }
                ip->RedrawViews(ip->GetTime());
            }
        }
        return 1;
    }

    if (wp == WM_XBUTTONDOWN || wp == WM_XBUTTONUP) {
        WORD xbutton = HIWORD(reinterpret_cast<MOUSEHOOKSTRUCTEX*>(lp)->mouseData);

        // XButton1: Shift=time slider, Ctrl=opacity slider
        if (xbutton == XBUTTON1) {
            if (wp == WM_XBUTTONDOWN) {
                bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                Interface* ip = GetCOREInterface();
                if (shift && ip) {
                    s_dragMode = DRAG_TIME;
                    s_lastMouseX = ((MOUSEHOOKSTRUCT*)lp)->pt.x;
                    s_dragFloat = (float)ip->GetTime() / GetTicksPerFrame();
                    return 1;
                }
                if (ctrl && ip && ip->GetSelNodeCount() > 0) {
                    s_dragMode = DRAG_OPACITY;
                    s_lastMouseX = ((MOUSEHOOKSTRUCT*)lp)->pt.x;
                    s_dragFloat = ip->GetSelNode(0)->GetVisibility(ip->GetTime());
                    if (s_dragFloat < 0) s_dragFloat = 1.0f;
                    return 1;
                }
                // No modifier = dynamic assignment or drag assigned param
                if (g_open && g_favWnd && IsWindowVisible(g_favWnd)) {
                    // Check if cursor is over a favorite edit
                    POINT cp = ((MOUSEHOOKSTRUCT*)lp)->pt;
                    for (int fi = 0; fi < (int)g_favEdits.size(); fi++) {
                        if (!g_favEdits[fi].hwnd) continue;
                        RECT fr; GetWindowRect(g_favEdits[fi].hwnd, &fr);
                        fr.top -= 11; // kFavLabelH
                        if (PtInRect(&fr, cp)) {
                            const std::wstring& fkey = g_favEdits[fi].key;
                            const std::wstring& flbl = g_favEdits[fi].label;
                            // Assignment only stores keys — PB resolved at drag start
                            SyncXB1ToSelection();
                            if (g_xb1V.key == fkey) {
                                g_xb1V.Clear();
                                ShowOSD(L"\u2195 OFF" + (g_xb1H.Active() ? (L"  \u2194 " + g_xb1H.label) : L""));
                            } else if (g_xb1H.key == fkey) {
                                g_xb1H.Clear();
                                ShowOSD((g_xb1V.Active() ? (L"\u2195 " + g_xb1V.label + L"  ") : L"") + L"\u2194 OFF");
                            } else if (!g_xb1V.Active()) {
                                g_xb1V.key = fkey; g_xb1V.label = flbl;
                                ShowOSD(L"\u2195 " + flbl + (g_xb1H.Active() ? (L"  \u2194 " + g_xb1H.label) : L""));
                            } else if (!g_xb1H.Active()) {
                                g_xb1H.key = fkey; g_xb1H.label = flbl;
                                ShowOSD(L"\u2195 " + g_xb1V.label + L"  \u2194 " + flbl);
                            } else {
                                g_xb1H.key = fkey; g_xb1H.label = flbl;
                                ShowOSD(L"\u2195 " + g_xb1V.label + L"  \u2194 " + flbl);
                            }
                            SaveXB1Assignment();
                            return 1;
                        }
                    }
                }
                // XButton1 alone with assignment — sync to selection and resolve PB
                SyncXB1ToSelection();
                if (g_xb1V.Active() || g_xb1H.Active()) {
                    // Resolve PB pointers fresh from current selection
                    auto resolveSlot = [](XB1Slot& slot) {
                        if (!slot.Active()) return;
                        slot.pb = nullptr; slot.spIdx = -1;
                        slot.msPath.clear(); slot.type = (ParamType2)TYPE_FLOAT;
                        // Search g_edits if panel is open
                        for (auto& ge : g_edits) {
                            if (ge.key == slot.key) {
                                slot.pb = ge.pb; slot.pid = ge.id;
                                slot.type = ge.type; slot.msPath = ge.msPath;
                                if (!ge.pb && (int)ge.id >= kSpSentinel && (int)ge.id < kSpSentinel + kSpCount)
                                    slot.spIdx = (int)ge.id - kSpSentinel;
                                return;
                            }
                        }
                        // Panel closed — resolve PB2 from selection via FindParam
                        Interface* ip2 = GetCOREInterface();
                        if (!ip2 || ip2->GetSelNodeCount() == 0) { slot.Clear(); return; }
                        INode* nd = ip2->GetSelNode(0);
                        IParamBlock2* pb = nullptr; ParamID pid = 0; ParamType2 pt = (ParamType2)0;
                        if (FindParam(nd, slot.key, pb, pid, pt)) {
                            slot.pb = pb; slot.pid = pid; slot.type = pt;
                        } else {
                            // PB1 fallback — use MaxScript path
                            size_t sep = slot.key.find(L':');
                            if (sep == std::wstring::npos) { slot.Clear(); return; }
                            std::wstring cls = slot.key.substr(0, sep);
                            // Walk modifiers
                            Object* walk = nd->GetObjectRef();
                            while (walk && walk->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
                                IDerivedObject* dv = static_cast<IDerivedObject*>(walk);
                                for (int mi = 0; mi < dv->NumModifiers(); mi++) {
                                    Modifier* mod = dv->GetModifier(mi);
                                    if (!mod) continue;
                                    MSTR mcn; mod->GetClassName(mcn, false);
                                    if (mcn.data() && _wcsicmp(mcn.data(), cls.c_str()) == 0) {
                                        slot.msPath = L"$.modifiers[" + std::to_wstring(mi+1) + L"]";
                                        return;
                                    }
                                }
                                walk = dv->GetObjRef();
                            }
                            // Base object
                            slot.msPath = L"$.baseObject";
                        }
                    };
                    resolveSlot(g_xb1V);
                    resolveSlot(g_xb1H);
                    // Read initial PB1 values (one-time MaxScript call)
                    auto initDisplayVal = [](XB1Slot& slot) {
                        if (!slot.Active()) return;
                        if (slot.pb) {
                            Interface* ip3 = GetCOREInterface();
                            TimeValue t3 = ip3 ? ip3->GetTime() : 0;
                            slot.displayVal = IsFloat(slot.type) ? slot.pb->GetFloat(slot.pid, t3) : (float)slot.pb->GetInt(slot.pid, t3);
                        } else if (slot.spIdx >= 0) {
                            slot.displayVal = g_splineVals[slot.spIdx];
                        } else {
                            size_t sep = slot.key.find(L':');
                            if (sep != std::wstring::npos) {
                                std::wstring prop = slot.key.substr(sep + 1);
                                std::wstring path = slot.msPath.empty() ? L"$.baseObject" : slot.msPath;
                                std::wstring s = L"try((getProperty " + path + L" #" + prop + L") as string)catch(\"0\")";
                                FPValue r; ExecuteMAXScriptScript(s.c_str(), MAXScript::ScriptSource::Dynamic, TRUE, &r);
                                if (r.type == TYPE_STRING && r.s) slot.displayVal = (float)_wtof(r.s);
                            }
                        }
                    };
                    initDisplayVal(g_xb1V);
                    initDisplayVal(g_xb1H);
                    if (g_xb1V.Active() || g_xb1H.Active()) {
                        s_lastMouseX = ((MOUSEHOOKSTRUCT*)lp)->pt.x;
                        s_lastMouseY = ((MOUSEHOOKSTRUCT*)lp)->pt.y;
                        s_xb1Dragging = true;
                        theHold.Begin();
                        return 1;
                    }
                }
                return 0;
            }
            if (wp == WM_XBUTTONUP && s_dragMode != DRAG_NONE) {
                s_dragMode = DRAG_NONE;
                return 1;
            }
        }

        if (xbutton == XBUTTON2 && wp == WM_XBUTTONDOWN) {
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool matEd = IsMaterialEditorFocused();
            if (ctrl && shift) {
                // Ctrl+Shift+XButton2 → swap V/H slider assignments
                SyncXB1ToSelection();
                if (g_xb1V.Active() || g_xb1H.Active()) {
                    std::swap(g_xb1V, g_xb1H);
                    SaveXB1Assignment();
                    ShowOSD(L"\u2195 " + (g_xb1V.Active() ? g_xb1V.label : L"OFF") +
                            L"  \u2194 " + (g_xb1H.Active() ? g_xb1H.label : L"OFF"));
                }
                return 1;
            } else if (ctrl) {
                // Ctrl+XButton2 → clear XB1 slider assignments
                SyncXB1ToSelection();
                if (g_xb1V.Active() || g_xb1H.Active()) {
                    g_xb1V.Clear(); g_xb1H.Clear();
                    SaveXB1Assignment();
                    ShowOSD(L"Sliders cleared");
                }
                return 1;
            } else if ((shift || matEd) && g_enablePowerShader) {
                PostMessage(g_panel, WM_PP_SHADER, 0, 0);
            } else if (g_enablePowerParams) {
                PostMessage(g_panel, WM_PP_TOGGLE, 0, 0);
            }
            return 1;
        }
    }

    return CallNextHookEx(g_mouseHook, nCode, wp, lp);
}

// ── Keyboard hook — Tab opens PowerShader when SME focused ──────
static LRESULT CALLBACK KbHookProc(int nCode, WPARAM wp, LPARAM lp) {
    if (nCode >= 0 && g_tabShader && wp == VK_TAB && !(lp & (1 << 31))) {
        // Key-down only (bit 31 = 0), ignore repeats (bit 30)
        if (!(lp & (1 << 30)) && IsMaterialEditorFocused()) {
            PostMessage(g_panel, WM_PP_SHADER, 0, 0);
            return 1; // eat the Tab
        }
    }
    return CallNextHookEx(g_kbHook, nCode, wp, lp);
}

// ── Param helpers ───────────────────────────────────────────────
static bool IsFloat(ParamType2 t) { return t==TYPE_FLOAT||t==TYPE_ANGLE||t==TYPE_PCNT_FRAC||t==TYPE_WORLD||t==TYPE_COLOR_CHANNEL; }
static bool IsInt(ParamType2 t)   { return t==TYPE_INT||t==TYPE_TIMEVALUE||t==TYPE_RADIOBTN_INDEX||t==TYPE_INDEX; }


static int GetNextKeyOrdinal(const std::wstring& key) {
    int ord = 0;
    for (const auto& ef : g_edits)
        if (ef.key == key) ++ord;
    return ord;
}

// Find a param on the current node by its persistent key (ClassName:ParamName)
static bool FindParam(INode* node, const std::wstring& key,
                      IParamBlock2*& outPB, ParamID& outID, ParamType2& outType) {
    size_t sep = key.find(L':');
    if (sep == std::wstring::npos) return false;
    std::wstring wantClass = key.substr(0, sep);
    std::wstring wantParam = key.substr(sep + 1);
    auto search = [&](IParamBlock2* pb) -> bool {
        if (!pb) return false;
        for (int i = 0; i < pb->NumParams(); i++) {
            ParamID pid = pb->IndextoID(i);
            const ParamDef& d = pb->GetParamDef(pid);
            if (d.int_name && std::wstring(d.int_name) == wantParam) {
                outPB = pb; outID = pid; outType = d.type;
                return true;
            }
        }
        return false;
    };
    Object* obj = node->GetObjectRef();
    while (obj && obj->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
        IDerivedObject* d = static_cast<IDerivedObject*>(obj);
        for (int m = 0; m < d->NumModifiers(); m++) {
            Modifier* mod = d->GetModifier(m);
            if (!mod) continue;
            MSTR cn; mod->GetClassName(cn, false);
            if (std::wstring(cn.data()) != wantClass) continue;
            for (int b = 0; b < mod->NumParamBlocks(); b++)
                if (search(mod->GetParamBlock(b))) return true;
        }
        obj = d->GetObjRef();
    }
    if (obj) {
        MSTR cn; obj->GetClassName(cn, false);
        if (std::wstring(cn.data()) == wantClass) {
            for (int b = 0; b < obj->NumParamBlocks(); b++)
                if (search(obj->GetParamBlock(b))) return true;
            // Fallback: scan references for PB2
            for (int r = 0; r < obj->NumRefs(); r++) {
                RefTargetHandle ref = obj->GetReference(r);
                if (ref && ref->SuperClassID() == PARAMETER_BLOCK2_CLASS_ID)
                    if (search(static_cast<IParamBlock2*>(ref))) return true;
            }
        }
    }
    return false;
}

// Clean up SDK internal names for display: "lightRadius" → "Radius", "edge_chamfer" → "Chamfer"
static std::wstring PrettyLabel(const std::wstring& raw, const std::wstring& groupTitle) {
    std::wstring s = raw;
    // Strip common class-name prefixes (case-insensitive)
    std::wstring lower = s;
    for (auto& c : lower) c = towlower(c);
    std::wstring groupLower = groupTitle;
    for (auto& c : groupLower) c = towlower(c);
    // Remove spaces from group name for prefix matching
    std::wstring gpx;
    for (auto c : groupLower) if (c != L' ') gpx += c;
    if (!gpx.empty() && lower.find(gpx) == 0 && s.size() > gpx.size() + 1) {
        // Only strip if remainder is a meaningful word (>1 char, starts with letter)
        wchar_t next = s[gpx.size()];
        if (iswalpha(next) || next == L'_')
            s = s.substr(gpx.size());
    }
    // Strip leading underscores
    while (!s.empty() && s[0] == L'_') s.erase(0, 1);
    // Replace underscores with spaces
    for (auto& c : s) if (c == L'_') c = L' ';
    // Insert spaces at camelCase boundaries: "myRadius" → "my Radius", "UVWMap" → "UVW Map"
    std::wstring out;
    for (size_t i = 0; i < s.size(); i++) {
        if (i > 0 && s[i-1] != L' ') {
            if (iswupper(s[i]) && !iswupper(s[i-1]))
                out += L' ';
            else if (i + 1 < s.size() && iswupper(s[i-1]) && iswupper(s[i]) && iswlower(s[i+1]))
                out += L' '; // "UVWMap" → "UVW Map"
        }
        out += s[i];
    }
    // Capitalize first letter
    if (!out.empty()) out[0] = towupper(out[0]);
    // Trim leading/trailing spaces
    while (!out.empty() && out[0] == L' ') out.erase(0, 1);
    while (!out.empty() && out.back() == L' ') out.pop_back();
    if (out.empty()) out = raw;
    return out;
}

static void CollectParams(IParamBlock2* pb, const std::wstring& groupTitle, int& total) {
    if (!pb) return;
    int n = pb->NumParams();
    for (int i = 0; i < n && total < kMaxParams; i++) {
        ParamID pid = pb->IndextoID(i);
        const ParamDef& d = pb->GetParamDef(pid);
        if (d.type & TYPE_TAB) continue;
        if (!IsFloat(d.type) && !IsInt(d.type) && d.type != TYPE_BOOL) continue;

        std::wstring rawName = d.int_name ? d.int_name : L"?";
        std::wstring key = groupTitle + L":" + rawName;

        if (!g_visEditMode && g_hidden.count(key)) continue;  // skip hidden

        EditField ef;
        ef.label = PrettyLabel(rawName, groupTitle);
        ef.key   = key;
        ef.keyOrdinal = GetNextKeyOrdinal(key);
        ef.pb    = pb;
        ef.id    = pid;
        ef.type  = d.type;
        g_edits.push_back(ef);
        total++;
    }
}



// Collect params from an object — tries NumParamBlocks first,
// falls back to scanning references for ParamBlock2,
// then falls back to IParamBlock (v1) for legacy shapes.
static void CollectAllParams(ReferenceTarget* obj, const std::wstring& groupTitle, int& tot) {
    if (!obj) return;
    int found = 0;
    // Primary: PB2 via NumParamBlocks API
    for (int b = 0; b < obj->NumParamBlocks(); b++) {
        IParamBlock2* pb = obj->GetParamBlock(b);
        if (pb) { CollectParams(pb, groupTitle, tot); found++; }
    }
    // Fallback 1: scan refs for PB2
    if (found == 0) {
        for (int r = 0; r < obj->NumRefs(); r++) {
            RefTargetHandle ref = obj->GetReference(r);
            if (ref && ref->SuperClassID() == PARAMETER_BLOCK2_CLASS_ID) {
                CollectParams(static_cast<IParamBlock2*>(ref), groupTitle, tot);
                found++;
            }
        }
    }
    // Fallback 2: legacy objects (PB1) — use MaxScript property enumeration
    if (found == 0 && tot < kMaxParams) {
        // Determine MaxScript path: modifier → $.modifiers[N], base → $.baseObject
        std::wstring msPath;
        if (obj->SuperClassID() == OSM_CLASS_ID || obj->SuperClassID() == WSM_CLASS_ID) {
            Interface* ipM = GetCOREInterface();
            if (ipM && ipM->GetSelNodeCount() > 0) {
                INode* nd = ipM->GetSelNode(0);
                Object* walk = nd ? nd->GetObjectRef() : nullptr;
                while (walk && walk->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
                    IDerivedObject* d = static_cast<IDerivedObject*>(walk);
                    for (int mi = 0; mi < d->NumModifiers(); mi++)
                        if (d->GetModifier(mi) == (Modifier*)obj) {
                            msPath = L"$.modifiers[" + std::to_wstring(mi + 1) + L"]";
                            break;
                        }
                    if (!msPath.empty()) break;
                    walk = d->GetObjRef();
                }
            }
            if (msPath.empty()) return;
        } else {
            msPath = L"$.baseObject";
        }

        // Build a MaxScript that returns "propName|type|value\n" for each numeric property
        std::wstring script =
            L"try(local s=\"\"; local o=" + msPath + L"; "
            L"for p in (getPropNames o) do ("
            L"try(local v=getProperty o p; local c=classof v; "
            L"if c==Float or c==Integer or c==BooleanClass do ("
            L"s+=p as string+\"|\"+(if c==Float then \"f\" else if c==Integer then \"i\" else \"b\")"
            L"+\"|\"+(v as string)+\"\\n\"))catch()); s)catch(\"\")";
        FPValue result;
        BOOL ok = ExecuteMAXScriptScript(script.c_str(), MAXScript::ScriptSource::Dynamic,
            TRUE, &result);
        if (ok && result.type == TYPE_STRING && result.s) {
            std::wstring data(result.s);
            size_t pos = 0;
            while (pos < data.size() && tot < kMaxParams) {
                size_t nl = data.find(L'\n', pos);
                if (nl == std::wstring::npos) nl = data.size();
                std::wstring line = data.substr(pos, nl - pos);
                pos = nl + 1;
                // Parse "propName|type|value"
                size_t sep1 = line.find(L'|');
                size_t sep2 = line.find(L'|', sep1 + 1);
                if (sep1 == std::wstring::npos || sep2 == std::wstring::npos) continue;
                std::wstring propName = line.substr(0, sep1);
                wchar_t typeChar = line[sep1 + 1];
                // Skip internal/duplicate meta params
                if (propName.find(L"typeIn") == 0 || propName.find(L"typein") == 0) continue;
                // Skip render_* and viewport_* alias duplicates (spline rendering props)
                if (propName.find(L"render_") == 0 || propName.find(L"viewport_") == 0) continue;

                std::wstring key = groupTitle + L":" + propName;
                if (!g_visEditMode && g_hidden.count(key)) continue;

                EditField ef;
                ef.label = PrettyLabel(propName, groupTitle);
                ef.key   = key;
                ef.msPath = msPath;
                ef.keyOrdinal = GetNextKeyOrdinal(key);
                ef.pb    = nullptr;
                ef.id    = (ParamID)0;
                ef.type  = (typeChar == L'f') ? (ParamType2)TYPE_FLOAT
                         : (typeChar == L'b') ? (ParamType2)TYPE_BOOL
                         : (ParamType2)TYPE_INT;
                g_edits.push_back(ef);
                tot++;
            }
        }
    }
}

// ── Find EPoly on BASE OBJECT only (not modifiers) ──────────────
// Edit Poly modifier is a different SDK class that bugs out.
// Only Editable Poly base object works cleanly with our tools.
static EPoly* FindEPoly(INode* node) {
    if (!node) return nullptr;
    Object* obj = node->GetObjectRef();
    while (obj && obj->SuperClassID() == GEN_DERIVOB_CLASS_ID)
        obj = ((IDerivedObject*)obj)->GetObjRef();
    if (obj) return (EPoly*)obj->GetInterface(EPOLY_INTERFACE);
    return nullptr;
}

// Find SplineShape on BASE OBJECT only (not Edit Spline modifier).
// Edit Spline modifier is a different SDK class that bugs out.
// Only Editable Spline base object works cleanly with our tools.
static SplineShape* FindSplineShape(INode* node) {
    if (!node) return nullptr;
    Object* obj = node->GetObjectRef();
    while (obj && obj->SuperClassID() == GEN_DERIVOB_CLASS_ID)
        obj = ((IDerivedObject*)obj)->GetObjRef();
    if (obj && obj->ClassID() == splineShapeClassID)
        return static_cast<SplineShape*>(obj);
    return nullptr;
}

// Returns EPoly only if it's the currently active modifier (A_MOD_BEING_EDITED)
// or the base object. Edit Poly modifiers that aren't selected in the stack
// won't respond to tool operations.
static EPoly* FindActiveEPoly(INode* node) {
    if (!node) return nullptr;
    Object* obj = node->GetObjectRef();
    while (obj && obj->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
        IDerivedObject* d = static_cast<IDerivedObject*>(obj);
        for (int m = 0; m < d->NumModifiers(); m++) {
            Modifier* mod = d->GetModifier(m);
            EPoly* ep = (EPoly*)mod->GetInterface(EPOLY_INTERFACE);
            if (ep && mod->TestAFlag(A_MOD_BEING_EDITED)) return ep;
        }
        obj = d->GetObjRef();
    }
    // Base object is always "active"
    if (obj) return (EPoly*)obj->GetInterface(EPOLY_INTERFACE);
    return nullptr;
}

// ── EPoly preview helpers ───────────────────────────────────────
// Refresh live preview after param change
static void EPolyRefresh() {
    if (!g_epolyPreview || !g_epolyFP) return;
    FPValue d;
    g_epolyFP->Invoke(epfn_preview_invalidate, d);
    if (auto* ip = GetCOREInterface()) ip->RedrawViews(ip->GetTime());
}

// Accept the active preview (commit the operation)
static void EPolyAccept() {
    if (!g_epolyPreview || !g_epolyFP) return;
    FPValue d;
    g_epolyFP->Invoke(epfn_preview_accept, d);
    g_epolyPreview = false;
}

// Cancel the active preview (revert)
static void EPolyCancel() {
    if (!g_epolyPreview || !g_epolyFP) return;
    FPValue d;
    g_epolyFP->Invoke(epfn_preview_cancel, d);
    g_epolyPreview = false;
}

// Remove the op group from panel
static void RemoveOpGroup() {
    g_epolyOp = -1;
    g_epolyFP = nullptr;
    if (!g_groups.empty()) {
        int cnt = g_groups[0].count;
        int start = g_groups[0].startIdx;
        KillActiveEdit(false);
        g_edits.erase(g_edits.begin() + start, g_edits.begin() + start + cnt);
        g_groups.erase(g_groups.begin());
        for (auto& gh : g_groups) gh.startIdx -= cnt;
        BuildLayout();
    }
}

// EXIT = accept preview + remove op group
static void EPolyDrop() {
    EPolyAccept();
    RemoveOpGroup();
}

// CANCEL = cancel preview (revert) + remove op group
static void EPolyCancelDrop() {
    EPolyCancel();
    RemoveOpGroup();
}

// ── Gather params ───────────────────────────────────────────────
static void GatherParams() {
    g_groups.clear();
    g_edits.clear();
    g_actions.clear();
    g_nodeName.clear();
    g_nodeHandle = 0;
    g_epolyOp = -1;
    g_epolyFP = nullptr;
    g_epolyPreview = false;
    g_ctx = CTX_NONE;
    g_epolyForButtons = nullptr;
    g_splineForButtons = nullptr;
    g_scrollY = 0;

    Interface* ip = GetCOREInterface();
    if (!ip || ip->GetSelNodeCount() == 0) return;
    INode* node = ip->GetSelNode(0);
    if (!node) return;
    const MCHAR* nn = node->GetName();
    g_nodeName = nn ? nn : L"";
    g_nodeHandle = node->GetHandle();

    Object* obj = node->GetObjectRef();
    if (!obj) return;

    // ── EPoly detection ────────────────────────────────────────
    {
        EPoly* ep = FindEPoly(node);
        if (ep) {
            g_ctx = CTX_EPOLY;
            g_epolyForButtons = (FPInterface*)ep;
            IParamBlock2* pb = ep->getParamBlock();
            FPInterface* fp = (FPInterface*)ep;

            // Takeover: is a tool active? (GetCommandMode >= 0)
            if (pb && fp) {
                FPValue modeVal;
                fp->Invoke(epfn_get_command_mode, modeVal);

                // Map command mode → operation code (don't rely on GetLastOperation)
                int opFromMode = -1;
                switch (modeVal.i) {
                case epmode_chamfer_edge: case epmode_chamfer_vertex:
                    opFromMode = epop_chamfer; break;
                case epmode_extrude_face: case epmode_extrude_edge: case epmode_extrude_vertex:
                    opFromMode = epop_extrude; break;
                case epmode_bevel:
                    opFromMode = epop_bevel; break;
                case epmode_inset_face:
                    opFromMode = epop_inset; break;
                case epmode_outline:
                    opFromMode = epop_outline; break;
                }

                if (opFromMode >= 0) {
                    FPValue slVal;
                    fp->Invoke(epfn_get_epoly_sel_level, slVal);

                    int cnt = 0; std::wstring title;
                    const FallbackOpParam* fb = LookupFallbackParams(opFromMode, slVal.i, cnt, title);
                    if (fb && cnt > 0) {
                        MSTR cn; ((Animatable*)ep)->GetClassName(cn, false);
                        std::wstring cls = cn.data() ? cn.data() : L"EPoly";

                        GroupHeader gh;
                        gh.title = title;
                        gh.startIdx = (int)g_edits.size();
                        for (int i = 0; i < cnt; i++) {
                            std::wstring key = cls + L":" + fb[i].label;
                            if (!g_visEditMode && g_hidden.count(key)) continue;
                            EditField ef;
                            ef.label = fb[i].label;
                            ef.key   = key;
                            ef.keyOrdinal = GetNextKeyOrdinal(key);
                            ef.pb    = pb;
                            ef.id    = fb[i].pid;
                            ef.type  = (ParamType2)(fb[i].isFloat ? TYPE_FLOAT : TYPE_INT);
                            g_edits.push_back(ef);
                        }
                        gh.count = (int)g_edits.size() - gh.startIdx;
                        if (gh.count > 0) {
                            // Kill interactive mouse
                            FPValue d;
                            int putBefore = theHold.GetGlobalPutCount();
                            fp->Invoke(epfn_exit_command_modes, d);
                            fp->Invoke(epfn_close_popup_dialog, d);
                            int putAfter = theHold.GetGlobalPutCount();

                            // Chamfer doesn't change putCount during interactive mode
                            bool isChamfer = (modeVal.i == epmode_chamfer_edge ||
                                              modeVal.i == epmode_chamfer_vertex);
                            bool hadPendingOp = (putBefore != putAfter) || isChamfer;

                            if (hadPendingOp) {
                                g_epolyOp = opFromMode;
                                g_epolyFP = fp;
                                FPParams beginPrms(1, TYPE_ENUM, opFromMode);
                                fp->Invoke(epfn_preview_begin, d, &beginPrms);
                                FPParams dragPrms(1, TYPE_BOOL, TRUE);
                                fp->Invoke(epfn_preview_set_dragging, d, &dragPrms);
                                fp->Invoke(epfn_preview_invalidate, d);
                                g_epolyPreview = true;
                                g_groups.push_back(gh);
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Spline detection — base Editable Spline only (not Edit Spline modifier)
    if (g_ctx == CTX_NONE) {
        SplineShape* ss = FindSplineShape(node);
        if (ss) {
            g_ctx = CTX_SPLINE;
            g_splineForButtons = ss;

            // Spline op values — stored locally, applied via Begin/Move/End
            GroupHeader gh;
            gh.title = L"SplineCmd";
            gh.startIdx = (int)g_edits.size();
            for (int si = 0; si < kSpCount; si++) {
                std::wstring key = L"SplineShape:" + std::wstring(kSpLabels[si]);
                if (!g_visEditMode && g_hidden.count(key)) continue;
                EditField ef;
                ef.label = kSpLabels[si];
                ef.key   = key;
                ef.pb    = nullptr;
                ef.id    = (ParamID)(kSpSentinel + si);
                ef.type  = (ParamType2)TYPE_FLOAT;
                g_edits.push_back(ef);
            }
            gh.count = (int)g_edits.size() - gh.startIdx;
            if (gh.count > 0) g_groups.push_back(gh);
        }
    }

    // ── Full modifier stack — everything, no skipping ───────────
    Object* walkObj = node->GetObjectRef();
    while (walkObj && walkObj->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
        IDerivedObject* d = static_cast<IDerivedObject*>(walkObj);
        for (int m = 0; m < d->NumModifiers(); m++) {
            Modifier* mod = d->GetModifier(m);
            if (!mod) continue;
            GroupHeader gh;
            MSTR cn; mod->GetClassName(cn, false);
            const MCHAR* p = cn.data();
            gh.title    = (p && p[0]) ? p : L"Modifier";
            gh.startIdx = (int)g_edits.size();
            gh.mod      = mod;
            int tot = 0;
            CollectAllParams(mod, gh.title, tot);
            gh.count = (int)g_edits.size() - gh.startIdx;
            if (gh.count > 0) g_groups.push_back(gh);
        }
        walkObj = d->GetObjRef();
    }

    // ── Base object — everything, including EPoly ───────────────
    if (walkObj) {
        GroupHeader gh;
        MSTR cn; walkObj->GetClassName(cn, false);
        const MCHAR* p = cn.data();
        gh.title    = (p && p[0]) ? p : L"Object";
        gh.startIdx = (int)g_edits.size();
        int tot = 0;
        CollectAllParams(walkObj, gh.title, tot);
        // Also collect EPoly-specific param block if present
        EPoly* ep = (EPoly*)walkObj->GetInterface(EPOLY_INTERFACE);
        if (ep) {
            IParamBlock2* epPB = ep->getParamBlock();
            if (epPB) {
                // Check if already collected
                bool found = false;
                for (auto& ef : g_edits)
                    if (ef.pb == epPB) { found = true; break; }
                if (!found) CollectParams(epPB, gh.title, tot);
            }
        }
        gh.count = (int)g_edits.size() - gh.startIdx;
        if (gh.count > 0) g_groups.push_back(gh);
    }

}


// MaxScript object path for PB1 params
static std::wstring MsObjPath(const EditField& ef) {
    return ef.msPath.empty() ? L"$.baseObject" : ef.msPath;
}

// Extract property name from key "ClassName:propName"
static std::wstring PropFromKey(const EditField& ef) {
    size_t sep = ef.key.find(L':');
    return (sep != std::wstring::npos) ? ef.key.substr(sep + 1) : ef.key;
}

static void FormatValue(const EditField& ef, TimeValue t, TCHAR* buf, int len) {
    // Spline op values — read from local storage (sentinel-marked IDs)
    if (!ef.pb && (int)ef.id >= kSpSentinel && (int)ef.id < kSpSentinel + kSpCount) {
        swprintf(buf, len, _T("%.4g"), g_splineVals[(int)ef.id - kSpSentinel]);
        return;
    }
    // MaxScript-based params (legacy PB1 objects)
    if (!ef.pb) {
        std::wstring prop = PropFromKey(ef);
        std::wstring path = ef.msPath.empty() ? L"$.baseObject" : ef.msPath;
        std::wstring script = L"try((getProperty " + path + L" #" + prop + L") as string)catch(\"--\")";
        FPValue result;
        BOOL ok = ExecuteMAXScriptScript(script.c_str(), MAXScript::ScriptSource::Dynamic,
            TRUE, &result);
        if (ok && result.type == TYPE_STRING && result.s) {
            // Convert "true"/"false" to 1/0
            if (_wcsicmp(result.s, L"true") == 0) swprintf(buf, len, _T("On"));
            else if (_wcsicmp(result.s, L"false") == 0) swprintf(buf, len, _T("Off"));
            else swprintf(buf, len, _T("%s"), result.s);
        } else
            swprintf(buf, len, _T("--"));
        return;
    }
    if (IsFloat(ef.type))      swprintf(buf, len, _T("%.4g"), ef.pb->GetFloat(ef.id, t));
    else if (ef.type==TYPE_BOOL) swprintf(buf, len, _T("%s"), ef.pb->GetInt(ef.id, t)?_T("On"):_T("Off"));
    else                       swprintf(buf, len, _T("%d"), ef.pb->GetInt(ef.id, t));
}


static void NotifyParamChanged() {
    Interface* ip = GetCOREInterface();
    if (!ip) return;
    if (ip->GetSelNodeCount() > 0) {
        INode* node = ip->GetSelNode(0);
        if (node) { node->InvalidateWS(); node->NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE); }
    }
    ip->RedrawViews(ip->GetTime());
}

// ── Mod search edit subclass ─────────────────────────────────────
static LRESULT CALLBACK ModSearchEditProc(HWND h, UINT m, WPARAM w, LPARAM l,
                                          UINT_PTR, DWORD_PTR) {
    if (m == WM_KEYDOWN) {
        if (w == VK_RETURN) {
            ApplyModSearchResult();
            ExitModSearch();
            GatherParams(); BuildLayout();
            return 0;
        }
        if (w == VK_ESCAPE) { ExitModSearch(); InvalidateRect(g_panel, nullptr, FALSE); return 0; }
        if (w == VK_DOWN) {
            if (g_modSearchSel < (int)g_modSearchResults.size() - 1) g_modSearchSel++;
            InvalidateRect(g_panel, nullptr, FALSE); return 0;
        }
        if (w == VK_UP) {
            if (g_modSearchSel > 0) g_modSearchSel--;
            InvalidateRect(g_panel, nullptr, FALSE); return 0;
        }
    }
    if (m == WM_CHAR && (w == VK_RETURN || w == VK_ESCAPE)) return 0;
    return DefSubclassProc(h, m, w, l);
}

// ── Edit subclass (for the single active edit) ──────────────────
static LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_KEYDOWN:
        if (wp == VK_RETURN) {
            KillActiveEdit(true);  // apply + destroy
            InvalidateRect(g_panel, nullptr, FALSE);
            return 0;
        }
        if (wp == VK_ESCAPE) {
            KillActiveEdit(false); // discard
            InvalidateRect(g_panel, nullptr, FALSE);
            return 0;
        }
        if (wp == VK_TAB) {
            // Move to next/prev param
            int dir = (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1;
            int next = g_editParam + dir;
            if (next >= 0 && next < (int)g_edits.size()) {
                KillActiveEdit(true);
                SpawnEditAt(next);
            }
            return 0;
        }
        break;
    case WM_CHAR:
        if (wp == VK_RETURN || wp == VK_ESCAPE) return 0;
        break;
    case WM_MOUSEWHEEL:
        PostMessage(g_panel, msg, wp, lp);
        return 0;
    }
    return CallWindowProc(g_origEdit, h, msg, wp, lp);
}

// ── Favorites edit subclass ──────────────────────────────────────
static LRESULT CALLBACK FavEditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    EditField* ef = (EditField*)GetProp(h, _T("WF"));
    switch (msg) {
    case WM_KEYDOWN:
        if (wp == VK_RETURN && ef) {
            TCHAR txt[256]; GetWindowText(h, txt, 256);
            if (!ef->pb && (int)ef->id >= kSpSentinel && (int)ef->id < kSpSentinel + kSpCount) {
                // Spline op favorite — apply via Begin/Move/End
                int idx = (int)ef->id - kSpSentinel;
                g_splineVals[idx] = (float)_wtof(txt);
                Interface* ip = GetCOREInterface();
                if (ip && ip->GetSelNodeCount() > 0 && g_splineForButtons && g_splineVals[idx] != 0.0f) {
                    TimeValue t = ip->GetTime();
                    SplineShape* ss = g_splineForButtons;
                    theHold.Begin();
                    ss->SetFCLimit();
                    int spId = idx + kSpSentinel;
                    if (spId == kSpFillet)       { ss->BeginFilletMove(t); ss->FilletMove(t, g_splineVals[idx]); ss->EndFilletMove(t, TRUE); }
                    else if (spId == kSpChamfer) { ss->BeginChamferMove(t); ss->ChamferMove(t, g_splineVals[idx]); ss->EndChamferMove(t, TRUE); }
                    else if (spId == kSpOutline) { ss->BeginOutlineMove(t); ss->OutlineMove(t, g_splineVals[idx]); ss->EndOutlineMove(t, TRUE); }
                    else if (spId == kSpWeld)    { ss->SetEndPointAutoWeldThreshold(g_splineVals[idx]); ss->DoVertWeld(); }
                    theHold.Accept(_T("Spline Op"));
                    ip->RedrawViews(t);
                }
            } else if (ef->pb) {
                Interface* ip = GetCOREInterface();
                TimeValue t = ip ? ip->GetTime() : 0;
                theHold.Suspend();
                if (IsFloat(ef->type))      ef->pb->SetValue(ef->id, t, (float)_wtof(txt));
                else if (ef->type==TYPE_BOOL) ef->pb->SetValue(ef->id, t, (_wcsicmp(txt,_T("On"))==0||_wcsicmp(txt,_T("1"))==0)?1:0);
                else                        ef->pb->SetValue(ef->id, t, _wtoi(txt));
                theHold.Resume();
            } else {
                std::wstring prop = PropFromKey(*ef);
                std::wstring val(txt);
                if (ef->type == (ParamType2)TYPE_BOOL) val = (_wcsicmp(txt,L"On")==0||_wcsicmp(txt,L"1")==0) ? L"true" : L"false";
                std::wstring s = L"try(setProperty " + MsObjPath(*ef) + L" #" + prop + L" " + val + L")catch()";
                ExecuteMAXScriptScript(s.c_str(), MAXScript::ScriptSource::Dynamic);
            }
            NotifyParamChanged();
            InvalidateRect(g_panel, nullptr, FALSE);
            return 0;
        }
        if (wp == VK_ESCAPE) { SetFocus(g_panel); return 0; }
        break;
    case WM_CHAR:
        if (wp == VK_RETURN || wp == VK_ESCAPE) return 0;
        break;
    case WM_MOUSEWHEEL: {
        if (!ef) break;
        float step = (float)GET_WHEEL_DELTA_WPARAM(wp) / 120.0f;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        Interface* ip = GetCOREInterface();
        TimeValue t = ip ? ip->GetTime() : 0;
        if (ef->pb) {
            theHold.Suspend();
            if (IsFloat(ef->type)) {
                float cur = ef->pb->GetFloat(ef->id, t);
                float a = cur<0?-cur:cur;
                float sc = a>100.f?10.f:a>10.f?1.f:a>1.f?0.1f:0.01f;
                if (shift) sc *= 10.0f; if (ctrl) sc *= 0.1f;
                ef->pb->SetValue(ef->id, t, cur + step * sc);
            } else if (ef->type == TYPE_BOOL) {
                ef->pb->SetValue(ef->id, t, ef->pb->GetInt(ef->id, t) ? 0 : 1);
            } else {
                int cur = ef->pb->GetInt(ef->id, t);
                int s = (int)step;
                if (shift) s *= 10; if (ctrl && s != 0) s = s>0?1:-1;
                if (s == 0) s = step>0?1:-1;
                ef->pb->SetValue(ef->id, t, cur + s);
            }
            theHold.Resume();
        } else if (!ef->pb && (int)ef->id >= kSpSentinel && (int)ef->id < kSpSentinel + kSpCount) {
            // Spline op favorite — wheel adjust
            int idx = (int)ef->id - kSpSentinel;
            float a = g_splineVals[idx] < 0 ? -g_splineVals[idx] : g_splineVals[idx];
            float sc = a>100.f?10.f:a>10.f?1.f:a>1.f?0.1f:0.01f;
            if (shift) sc *= 10.0f; if (ctrl) sc *= 0.1f;
            g_splineVals[idx] += step * sc;
        } else {
            // PB1 MaxScript path
            std::wstring prop = PropFromKey(*ef);
            if (ef->type == (ParamType2)TYPE_BOOL) {
                std::wstring s = L"try(setProperty " + MsObjPath(*ef) + L" #" + prop +
                    L" (not(getProperty " + MsObjPath(*ef) + L" #" + prop + L")))catch()";
                ExecuteMAXScriptScript(s.c_str(), MAXScript::ScriptSource::Dynamic);
            } else if (IsFloat(ef->type)) {
                std::wstring s = L"try(local v=getProperty " + MsObjPath(*ef) + L" #" + prop +
                    L";setProperty " + MsObjPath(*ef) + L" #" + prop + L" (v+" +
                    std::to_wstring(step) + L"*(if(abs v)>100 then 10 else if(abs v)>10 then 1 else if(abs v)>1 then 0.1 else 0.01)" +
                    (shift?L"*10":L"") + (ctrl?L"*0.1":L"") + L"))catch()";
                ExecuteMAXScriptScript(s.c_str(), MAXScript::ScriptSource::Dynamic);
            } else {
                int s = (int)step;
                if (shift) s *= 10; if (ctrl && s != 0) s = s>0?1:-1;
                if (s == 0) s = step>0?1:-1;
                std::wstring sc = L"try(local v=getProperty " + MsObjPath(*ef) + L" #" + prop +
                    L";setProperty " + MsObjPath(*ef) + L" #" + prop + L" (v+" + std::to_wstring(s) + L"))catch()";
                ExecuteMAXScriptScript(sc.c_str(), MAXScript::ScriptSource::Dynamic);
            }
        }
        NotifyParamChanged();
        TCHAR buf[64]; FormatValue(*ef, t, buf, 64);
        SetWindowText(h, buf);
        return 0;
    }
    }
    return CallWindowProc(g_origEdit, h, msg, wp, lp);
}

// Fav strip drag state (drag on label area, not on edit controls)
static bool  g_favDragging = false;
static int   g_favDragIdx  = -1;
static int   g_favDragStartX = 0;
static int   g_favDragAccum = 0;

// ── Tool name overlay near cursor ────────────────────────────────
static const TCHAR* GetCommandModeName(int mode) {
    switch (mode) {
    case epmode_extrude_vertex: case epmode_extrude_edge: case epmode_extrude_face: return _T("Extrude");
    case epmode_chamfer_vertex: case epmode_chamfer_edge: return _T("Chamfer");
    case epmode_bevel: return _T("Bevel");
    case epmode_inset_face: return _T("Inset");
    case epmode_outline: return _T("Outline");
    case epmode_cut_vertex: case epmode_cut_edge: case epmode_cut_face: return _T("Cut");
    case epmode_divide_edge: case epmode_divide_face: return _T("Divide");
    case epmode_bridge_border: case epmode_bridge_polygon: case epmode_bridge_edge: return _T("Bridge");
    case epmode_weld: return _T("Weld");
    case epmode_create_vertex: return _T("Create Vertex");
    case epmode_create_edge: return _T("Create Edge");
    case epmode_create_face: return _T("Create Face");
    default: return _T("Tool Active");
    }
}

static LRESULT CALLBACK ToolTipProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(kBg); FillRect(hdc, &rc, bg); DeleteObject(bg);
        HPEN bp = CreatePen(PS_SOLID, 1, kAccent);
        SelectObject(hdc, bp); SelectObject(hdc, (HBRUSH)GetStockObject(NULL_BRUSH));
        Rectangle(hdc, 0, 0, rc.right, rc.bottom); DeleteObject(bp);
        SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, kAccent);
        SelectObject(hdc, g_fontBold);
        TCHAR txt[64]; GetWindowText(hwnd, txt, 64);
        RECT tr = { 6, 3, rc.right - 6, rc.bottom };
        DrawText(hdc, txt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_ERASEBKGND: return 1;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static void UpdateToolTip() {
    if (!g_open || g_ctx != CTX_EPOLY || !g_epolyForButtons) {
        if (g_toolTip) ShowWindow(g_toolTip, SW_HIDE);
        return;
    }
    FPValue modeVal;
    g_epolyForButtons->Invoke(epfn_get_command_mode, modeVal);
    if (modeVal.i < 0) {
        if (g_toolTip) ShowWindow(g_toolTip, SW_HIDE);
        return;
    }
    const TCHAR* name = GetCommandModeName(modeVal.i);
    if (!g_toolTip) return;

    SetWindowText(g_toolTip, name);
    HDC hdc = GetDC(g_toolTip);
    SelectObject(hdc, g_fontBold);
    SIZE sz; GetTextExtentPoint32(hdc, name, (int)_tcslen(name), &sz);
    ReleaseDC(g_toolTip, hdc);

    POINT pt; GetCursorPos(&pt);
    int w = sz.cx + 16, h = sz.cy + 8;
    SetWindowPos(g_toolTip, HWND_TOPMOST, pt.x + 18, pt.y - 8, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_toolTip, nullptr, FALSE);
}

// ── Paint helper: draw a vertical button column ─────────────────
// Draw a horizontal button strip
static void DrawBtnStrip(HDC mem, const BtnDef* btns, int count,
                         int activeID, HFONT font, int w, int h) {
    RECT rc = {0, 0, w, h};
    HBRUSH bgBr = CreateSolidBrush(kBg);
    FillRect(mem, &rc, bgBr); DeleteObject(bgBr);
    HPEN penB = CreatePen(PS_SOLID, 1, kBorder);
    HPEN oldP = (HPEN)SelectObject(mem, penB);
    SelectObject(mem, GetStockObject(NULL_BRUSH));
    Rectangle(mem, 0, 0, w, h);
    SelectObject(mem, oldP); DeleteObject(penB);

    SetBkMode(mem, TRANSPARENT);
    SelectObject(mem, font);
    int bw = (w - 4 - (count - 1) * kBtnGap) / count;
    for (int i = 0; i < count; i++) {
        int bx = 2 + i * (bw + kBtnGap);
        RECT br = { bx, 2, bx + bw, h - 2 };
        bool active = (btns[i].id == activeID);
        bool hover  = (btns[i].id == g_hoverBtn);
        COLORREF bg = active ? kBtnAct : hover ? kBtnHov : kBtnBg;
        HBRUSH bb = CreateSolidBrush(bg); FillRect(mem, &br, bb); DeleteObject(bb);
        SetTextColor(mem, (active || hover) ? RGB(255,255,255) : kLabelClr);
        DrawText(mem, btns[i].label, -1, &br, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

// Hit test for horizontal button strip (local coords)
static int HitBtnStrip(const BtnDef* btns, int count, int stripW, POINT pt) {
    if (pt.y < 2 || pt.y >= kBtnH + 2) return -1;
    int bw = (stripW - 4 - (count - 1) * kBtnGap) / count;
    for (int i = 0; i < count; i++) {
        int bx = 2 + i * (bw + kBtnGap);
        if (pt.x >= bx && pt.x < bx + bw) return btns[i].id;
    }
    return -1;
}

// Strip dimensions for horizontal layout
static int BtnStripW(int count) { return 4 + count * (kBtnW + kBtnGap) - kBtnGap; }
static int BtnStripH() { return kBtnH + 4; }

// Position button strips relative to the panel
static bool IsModifyMode() {
    Interface* ip = GetCOREInterface();
    return ip && ip->GetCommandPanelTaskMode() == TASK_MODE_MODIFY;
}

static void PositionBtnStrips() {
    RECT pr; GetWindowRect(g_panel, &pr);
    int sh = BtnStripH();
    
    // Quick Mods strip (right side)
    if (!g_quickMods.empty() && g_open) {
        int rw = BtnStripW((int)g_quickMods.size());
        SetWindowPos(g_btnRight, HWND_TOPMOST, pr.right - rw, pr.top - sh - kSideGap, rw, sh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(g_btnRight, nullptr, FALSE);
    } else {
        if (g_btnRight) ShowWindow(g_btnRight, SW_HIDE);
    }

    if (!g_panel || !g_open || g_ctx == CTX_NONE || !g_showSubObj || !IsModifyMode()) {
        if (g_btnLeft) ShowWindow(g_btnLeft, SW_HIDE);
        return;
    }
    int leftCount = (g_ctx == CTX_EPOLY) ? 5 : 3;
    int lw = BtnStripW(leftCount);
    int topY = pr.top - sh - kSideGap;

    SetWindowPos(g_btnLeft, HWND_TOPMOST, pr.left, topY, lw, sh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_btnLeft, nullptr, FALSE);
}

// ── Paint helper: draw a button row ─────────────────────────────
static void DrawBtnRow(HDC mem, const BtnDef* btns, int count, int x, int y, int w,
                       int activeID, HFONT font) {
    int bw = (w - (count - 1) * kBtnGap) / count;
    SelectObject(mem, font);
    for (int i = 0; i < count; i++) {
        RECT br = { x + i * (bw + kBtnGap), y, x + i * (bw + kBtnGap) + bw, y + kBtnH };
        COLORREF bg = (btns[i].id == activeID) ? kBtnAct : kBtnBg;
        HBRUSH bb = CreateSolidBrush(bg); FillRect(mem, &br, bb); DeleteObject(bb);
        SetTextColor(mem, (btns[i].id == activeID) ? RGB(255,255,255) : kLabelClr);
        DrawText(mem, btns[i].label, -1, &br, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

// ── Paint ───────────────────────────────────────────────────────
static void PaintPanel(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc; GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HBITMAP oldBmp = (HBITMAP)SelectObject(mem, bmp);

    // Shared GDI objects — created once, properly cleaned up
    HPEN penBorder = CreatePen(PS_SOLID, 1, kBorder);
    HPEN penAccent = CreatePen(PS_SOLID, 1, kAccent);
    HPEN origPen   = (HPEN)SelectObject(mem, penBorder);
    HFONT origFont = (HFONT)SelectObject(mem, g_fontBold);
    HBRUSH origBr  = (HBRUSH)SelectObject(mem, GetStockObject(NULL_BRUSH));

    // Background
    { HBRUSH b = CreateSolidBrush(kBg); FillRect(mem, &rc, b); DeleteObject(b); }
    // Border
    Rectangle(mem, 0, 0, rc.right, rc.bottom);

    SetBkMode(mem, TRANSPARENT);
    int x = kPad, rEdge = rc.right - kPad;

    // Header
    int y = kPad + 2;
    // Header text is replaced by the search bar EDIT control

    // ModStack button
    {
        bool msOpen = ModStack::IsOpen();
        COLORREF mbg = msOpen ? kAccent : (g_hoverModStack ? kBtnHov : kBtnBg);
        HBRUSH mb = CreateSolidBrush(mbg); FillRect(mem, &g_modStackRect, mb); DeleteObject(mb);
        HPEN mp = CreatePen(PS_SOLID, 1, kBorder);
        HPEN mpo = (HPEN)SelectObject(mem, mp);
        SelectObject(mem, GetStockObject(NULL_BRUSH));
        Rectangle(mem, g_modStackRect.left, g_modStackRect.top, g_modStackRect.right, g_modStackRect.bottom);
        SelectObject(mem, mpo); DeleteObject(mp);
        SetTextColor(mem, msOpen ? RGB(255,255,255) : kValueClr);
        RECT mr = g_modStackRect;
        DrawText(mem, _T("M"), 1, &mr, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }

    // Visibility edit button (eye icon)
    {
        bool hasHidden = !g_hidden.empty();
        COLORREF vbg = g_visEditMode ? kAccent : (g_hoverVisBtn ? kBtnHov : kBtnBg);
        HBRUSH vb = CreateSolidBrush(vbg); FillRect(mem, &g_visBtnRect, vb); DeleteObject(vb);
        HPEN vp = CreatePen(PS_SOLID, 1, kBorder);
        HPEN vpo = (HPEN)SelectObject(mem, vp);
        SelectObject(mem, GetStockObject(NULL_BRUSH));
        Rectangle(mem, g_visBtnRect.left, g_visBtnRect.top, g_visBtnRect.right, g_visBtnRect.bottom);
        SelectObject(mem, vpo); DeleteObject(vp);
        // Show dot when there are hidden params (visual hint)
        SetTextColor(mem, g_visEditMode ? RGB(255,255,255) : (hasHidden ? kAccent : kValueClr));
        RECT vr = g_visBtnRect;
        DrawText(mem, hasHidden ? _T("\u25C9") : _T("\u25CB"), 1, &vr, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }

    // Close button
    if (g_hoverClose) {
        HBRUSH hov = CreateSolidBrush(kCloseHov); FillRect(mem, &g_closeRect, hov); DeleteObject(hov);
    }
    SetTextColor(mem, kValueClr);
    RECT cr = g_closeRect;
    DrawText(mem, _T("\u00D7"), 1, &cr, DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    y += kFontHdr + 4;
    MoveToEx(mem, x, y, nullptr); LineTo(mem, rEdge, y);
    y += 4;


    // Clip to content area
    HRGN clipRgn = CreateRectRgn(0, g_contentStartY, rc.right, rc.bottom - 1);
    SelectClipRgn(mem, clipRgn);

    // ── Modifier search mode ────────────────────────────────────
    if (g_modSearch) {
        SelectObject(mem, g_font);
        int sy = g_contentStartY - g_modSearchScrollY;
        for (int ri = 0; ri < (int)g_modSearchResults.size() && ri < 200; ri++) {
            int idx = g_modSearchResults[ri];
            if (idx < 0 || idx >= (int)g_modCache.size()) continue;
            auto& mc = g_modCache[idx];
            bool sel = (ri == g_modSearchSel);
            if (sel) {
                RECT sr = { 0, sy, rc.right, sy + kLineH };
                HBRUSH selBr = CreateSolidBrush(kAccent);
                FillRect(mem, &sr, selBr); DeleteObject(selBr);
            }
            SetTextColor(mem, sel ? RGB(255,255,255) : kLabelClr);
            TextOut(mem, x + 8, sy + 3, mc.label.c_str(), (int)mc.label.size());
            sy += kLineH;
        }
        // Skip normal content
        goto paint_done;
    }

    // Groups + params (scrolled)
    y = g_contentStartY - g_scrollY;
    HWND focused = GetFocus();
    for (size_t gi = 0; gi < g_groups.size(); gi++) {
        const auto& gh = g_groups[gi];
        bool collapsed = g_collapsed.count(gh.title) > 0;

        SelectObject(mem, g_fontBold);
        bool isOpGroup = (gi == 0 && g_epolyOp >= 0);
        bool modEnabled = gh.mod ? gh.mod->IsEnabled() : true;
        bool modActive = false;
        if (gh.mod) modActive = gh.mod->TestAFlag(A_MOD_BEING_EDITED);

        // Title — dim if disabled, accent if active
        COLORREF titleClr = isOpGroup ? kAccent
                          : !modEnabled ? RGB(100, 100, 100)
                          : modActive ? kAccent
                          : kGroupClr;
        SetTextColor(mem, titleClr);
        std::wstring hdr = (collapsed ? L"\x25B8 " : L"\x25BE ") + gh.title;
        if (isOpGroup) hdr += L"  [\x2717 CANCEL]";
        if (!modEnabled) hdr += L"  [OFF]";
        TextOut(mem, x, y, hdr.c_str(), (int)hdr.length());

        // Draw [●][▲][▼][×] buttons for modifier groups
        if (gh.mod) {
            int bw = 16, bh = kLineH - 4;
            int bx = rEdge - bw * 4 - 3;
            RECT enR   = { bx,            y + 2, bx + bw,       y + 2 + bh };
            RECT upR   = { bx + bw + 1,   y + 2, bx + bw*2+1,  y + 2 + bh };
            RECT dnR   = { bx + bw*2 + 2, y + 2, bx + bw*3+2,  y + 2 + bh };
            RECT delR  = { bx + bw*3 + 3, y + 2, bx + bw*4+3,  y + 2 + bh };

            HBRUSH bbg = CreateSolidBrush(kBtnBg);
            FillRect(mem, &enR, bbg); FillRect(mem, &upR, bbg); FillRect(mem, &dnR, bbg);
            DeleteObject(bbg);
            HBRUSH dbg = CreateSolidBrush(RGB(80, 50, 50));
            FillRect(mem, &delR, dbg); DeleteObject(dbg);

            // Enable toggle — filled = on, empty = off
            SetTextColor(mem, modEnabled ? kAccent : RGB(100, 100, 100));
            DrawText(mem, modEnabled ? _T("\u25CF") : _T("\u25CB"), 1, &enR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SetTextColor(mem, kLabelClr);
            DrawText(mem, _T("\u25B2"), 1, &upR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            DrawText(mem, _T("\u25BC"), 1, &dnR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SetTextColor(mem, RGB(200, 100, 100));
            DrawText(mem, _T("\u00D7"), 1, &delR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        y += kLineH;

        if (!collapsed) {
            Interface* ipv = GetCOREInterface();
            TimeValue tv = ipv ? ipv->GetTime() : 0;
            int editX = rEdge - kEditW;
            for (int fi = gh.startIdx; fi < gh.startIdx + gh.count; fi++) {
                auto& ef = g_edits[fi];
                bool isFav = g_favorites.count(ef.key) > 0;
                bool isHover = (fi == g_hoverParam);
                bool isEditing = (fi == g_editParam && g_editHwnd);
                bool isHidden = g_visEditMode && g_hidden.count(ef.key) > 0;

                // In vis edit mode: dim hidden params, show eye indicator
                COLORREF lblClr = isHidden ? RGB(80, 80, 80) : kLabelClr;
                COLORREF valClr = isHidden ? RGB(60, 60, 60) : kValueClr;

                // Label
                SelectObject(mem, g_font);
                if (g_visEditMode) {
                    // Eye icon: open for visible, closed for hidden
                    SetTextColor(mem, isHidden ? RGB(100, 50, 50) : RGB(50, 100, 50));
                    TextOut(mem, x, y + 3, isHidden ? _T("\u25CB") : _T("\u25C9"), 1);
                } else if (isFav) {
                    SetTextColor(mem, kAccent); TextOut(mem, x, y + 3, _T("\u2605"), 1);
                }
                SetTextColor(mem, lblClr);
                TextOut(mem, x + ((g_visEditMode || isFav) ? 14 : 8), y + 3, ef.label.c_str(), (int)ef.label.length());

                // Value box (painted, not a child window)
                RECT vr = { editX, y + 1, rEdge, y + kEditH + 1 };
                COLORREF vbg = isHidden ? kBg : (isEditing ? kEditFocus : isHover ? kEditFocus : kEditBg);
                HBRUSH vb = CreateSolidBrush(vbg); FillRect(mem, &vr, vb); DeleteObject(vb);
                SelectObject(mem, isHover || isEditing ? penAccent : penBorder);
                if (!isHidden) Rectangle(mem, vr.left - 1, vr.top - 1, vr.right + 1, vr.bottom + 1);

                // Draw value text (skip if active edit control is on top)
                if (!isEditing && !isHidden) {
                    TCHAR buf[64]; FormatValue(ef, tv, buf, 64);
                    SelectObject(mem, g_fontBold);
                    SetTextColor(mem, valClr);
                    DrawText(mem, buf, -1, &vr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                y += kLineH;
            }
        }
        if (gi + 1 < g_groups.size()) y += kGroupGap;
    }

paint_done:
    SelectClipRgn(mem, nullptr);
    DeleteObject(clipRgn);

    // Restore border pen for scroll indicator frame
    SelectObject(mem, penBorder);

    // Scroll indicator (thin bar on right if scrollable)
    if (g_contentH > g_viewH && g_viewH > 0) {
        int trackH = rc.bottom - g_contentStartY - 2;
        int thumbH = (g_viewH * trackH) / g_contentH;
        if (thumbH < 20) thumbH = 20;
        int thumbY = g_contentStartY + (g_scrollY * (trackH - thumbH)) / (g_contentH - g_viewH);
        RECT thumb = { rc.right - 4, thumbY, rc.right - 1, thumbY + thumbH };
        HBRUSH tb = CreateSolidBrush(kAccent); FillRect(mem, &thumb, tb); DeleteObject(tb);
    }

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);

    // Clean up — deselect all, then delete
    SelectObject(mem, origPen);
    SelectObject(mem, origFont);
    SelectObject(mem, origBr);
    DeleteObject(penBorder);
    DeleteObject(penAccent);
    SelectObject(mem, oldBmp); DeleteObject(bmp); DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

// ── Build / Rebuild layout ──────────────────────────────────────
// ── Single active edit control ───────────────────────────────────
static void KillActiveEdit(bool apply) {
    if (!g_editHwnd) return;
    if (apply && g_editParam >= 0 && g_editParam < (int)g_edits.size()) {
        TCHAR txt[256]; GetWindowText(g_editHwnd, txt, 256);
        auto& ef = g_edits[g_editParam];
        // Spline op values (sentinel-marked IDs)
        if (!ef.pb && (int)ef.id >= kSpSentinel && (int)ef.id < kSpSentinel + kSpCount) {
            int idx = (int)ef.id - kSpSentinel;
            {
                g_splineVals[idx] = (float)_wtof(txt);
                // Apply fillet/chamfer/outline
                Interface* ip = GetCOREInterface();
                if (ip && ip->GetSelNodeCount() > 0 && g_splineVals[idx] != 0.0f) {
                    SplineShape* ss = FindSplineShape(ip->GetSelNode(0));
                    if (ss) {
                        TimeValue t = ip->GetTime();
                        int spId = idx + kSpSentinel;
                        if (spId == kSpFillet || spId == kSpChamfer) ss->SetFCLimit();
                        theHold.Begin();
                        if (spId == kSpFillet)       { ss->BeginFilletMove(t); ss->FilletMove(t, g_splineVals[idx]); ss->EndFilletMove(t, TRUE); }
                        else if (spId == kSpChamfer) { ss->BeginChamferMove(t); ss->ChamferMove(t, g_splineVals[idx]); ss->EndChamferMove(t, TRUE); }
                        else if (spId == kSpOutline) { ss->BeginOutlineMove(t); ss->OutlineMove(t, g_splineVals[idx]); ss->EndOutlineMove(t, TRUE); }
                        else if (spId == kSpWeld)    { ss->SetEndPointAutoWeldThreshold(g_splineVals[idx]); ss->DoVertWeld(); }
                        theHold.Accept(_T("Spline Op"));
                        ss->NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE);
                        ip->RedrawViews(t);
                    }
                }
            }
        } else if (ef.pb) {
            Interface* ip = GetCOREInterface();
            TimeValue t = ip ? ip->GetTime() : 0;
            theHold.Suspend();
            if (g_epolyOp >= 0) {
                // EPoly takeover — use cached PB directly (FindParam can't resolve op params)
                if (IsFloat(ef.type))       ef.pb->SetValue(ef.id, t, (float)_wtof(txt));
                else if (ef.type==TYPE_BOOL) ef.pb->SetValue(ef.id, t, (_wcsicmp(txt,_T("On"))==0||_wcsicmp(txt,_T("1"))==0||_wcsicmp(txt,_T("true"))==0)?1:0);
                else                        ef.pb->SetValue(ef.id, t, _wtoi(txt));
            } else {
                // Normal params — apply to all selected nodes
                for (int ni = 0; ni < (ip ? ip->GetSelNodeCount() : 0); ni++) {
                    INode* nd = ip->GetSelNode(ni);
                    if (!nd) continue;
                    IParamBlock2* pb = nullptr; ParamID pid = 0; ParamType2 pt2 = (ParamType2)0;
                    if (!FindParam(nd, ef.key, pb, pid, pt2)) continue;
                    if (IsFloat(pt2))        pb->SetValue(pid, t, (float)_wtof(txt));
                    else if (pt2==TYPE_BOOL) pb->SetValue(pid, t, (_wcsicmp(txt,_T("On"))==0||_wcsicmp(txt,_T("1"))==0||_wcsicmp(txt,_T("true"))==0)?1:0);
                    else                     pb->SetValue(pid, t, _wtoi(txt));
                }
            }
            theHold.Resume();
            if (g_epolyPreview) EPolyRefresh();
            else                NotifyParamChanged();
        } else {
            // MaxScript-based params — apply to all selected
            std::wstring prop = PropFromKey(ef);
            std::wstring val(txt);
            if (ef.type == (ParamType2)TYPE_BOOL) val = (_wcsicmp(txt,L"On")==0||_wcsicmp(txt,L"1")==0||_wcsicmp(txt,L"true")==0) ? L"true" : L"false";
            std::wstring objPath = MsObjPath(ef);
            if (!objPath.empty() && objPath[0] == L'$') objPath = objPath.substr(1);
            std::wstring script = L"for obj in selection do try(setProperty (obj" + objPath + L") #" + prop + L" " + val + L")catch()";
            ExecuteMAXScriptScript(script.c_str(), MAXScript::ScriptSource::Dynamic);
            NotifyParamChanged();
        }
    }
    DestroyWindow(g_editHwnd);
    g_editHwnd = nullptr;
    g_editParam = -1;
    EnableAccelerators();
}

static void SpawnEditAt(int paramIdx) {
    KillActiveEdit(true);
    if (paramIdx < 0 || paramIdx >= (int)g_edits.size()) return;
    auto& ef = g_edits[paramIdx];

    RECT wr; GetWindowRect(g_panel, &wr);
    int panelW = wr.right - wr.left;
    int editX = panelW - kPad - kEditW;
    int screenY = ef.logY - g_scrollY;
    if (screenY + kEditH <= g_contentStartY || screenY >= g_contentStartY + g_viewH) return;

    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_CENTER;
    g_editHwnd = CreateWindowEx(0, _T("EDIT"), _T(""),
        style, editX, screenY + 1, kEditW, kEditH,
        g_panel, nullptr, hInstance, nullptr);
    SendMessage(g_editHwnd, WM_SETFONT, (WPARAM)g_fontBold, TRUE);
    if (!g_origEdit) g_origEdit = (WNDPROC)GetWindowLongPtr(g_editHwnd, GWLP_WNDPROC);
    SetWindowLongPtr(g_editHwnd, GWLP_WNDPROC, (LONG_PTR)EditProc);
    SetProp(g_editHwnd, _T("WF"), (HANDLE)&ef);
    g_editParam = paramIdx;

    // Fill with current value
    Interface* ip = GetCOREInterface();
    TimeValue t = ip ? ip->GetTime() : 0;
    TCHAR buf[64]; FormatValue(ef, t, buf, 64);
    SetWindowText(g_editHwnd, buf);
    SetFocus(g_editHwnd);
    SendMessage(g_editHwnd, EM_SETSEL, 0, -1);
    DisableAccelerators();
}

// Find which param index is at a screen Y coordinate
static int FindParamAtY(int clickY) {
    for (size_t i = 0; i < g_edits.size(); i++) {
        int screenY = g_edits[i].logY - g_scrollY;
        if (clickY >= screenY && clickY < screenY + kLineH) return (int)i;
    }
    return -1;
}

static void BuildLayout() {
    SendMessage(g_panel, WM_SETREDRAW, FALSE, 0);

    HDC hdc = GetDC(g_panel);
    SelectObject(hdc, g_font);
    int maxLbl = 0;
    for (auto& ef : g_edits) {
        SIZE sz; GetTextExtentPoint32(hdc, ef.label.c_str(), (int)ef.label.length(), &sz);
        if (sz.cx > maxLbl) maxLbl = sz.cx;
    }
    SelectObject(hdc, g_fontBold);
    int maxTitle = 0;
    for (auto& gh : g_groups) {
        std::wstring hdr = L"\u25BE " + gh.title;
        SIZE sz; GetTextExtentPoint32(hdc, hdr.c_str(), (int)hdr.length(), &sz);
        if (sz.cx > maxTitle) maxTitle = sz.cx;
    }
    SIZE hdrSz; GetTextExtentPoint32(hdc, g_nodeName.c_str(), (int)g_nodeName.length(), &hdrSz);
    if (hdrSz.cx + 30 > maxTitle) maxTitle = hdrSz.cx + 30;
    ReleaseDC(g_panel, hdc);

    int contentW = maxLbl + 36 + kEditW;
    if (contentW < maxTitle) contentW = maxTitle;
    int panelW = contentW + kPad * 2 + 8;
    if (panelW < kMinW) panelW = kMinW;
    if (panelW > 500) panelW = 500;  // cap to prevent layout breakage
    int editX = panelW - kPad - kEditW;

    g_closeRect    = { panelW - kPad - 18, kPad, panelW - kPad, kPad + 18 };
    g_modStackRect = { panelW - kPad - 18 - 2 - 18, kPad, panelW - kPad - 18 - 2, kPad + 18 };
    g_visBtnRect   = { panelW - kPad - 18 - 2 - 18 - 2 - 18, kPad, panelW - kPad - 18 - 2 - 18 - 2, kPad + 18 };

    // Fixed header area
    int y = 2 + kPad + kFontHdr + 4 + 1 + 4;
    g_contentStartY = y;

    // Content (logical Y, before scroll)
    int contentY = y;
    for (size_t gi = 0; gi < g_groups.size(); gi++) {
        contentY += kLineH;
        auto& gh = g_groups[gi];
        bool collapsed = g_collapsed.count(gh.title) > 0;
        if (!collapsed) {
            for (int fi = gh.startIdx; fi < gh.startIdx + gh.count; fi++) {
                g_edits[fi].logY = contentY;
                contentY += kLineH;
            }
        } else {
            // Mark collapsed params as off-screen so they never hit-test
            for (int fi = gh.startIdx; fi < gh.startIdx + gh.count; fi++)
                g_edits[fi].logY = -99999;
        }
        if (gi + 1 < g_groups.size()) contentY += kGroupGap;
    }
    g_contentH = contentY - g_contentStartY;

    // Panel height = header + min(content, max)
    HMONITOR hMon = MonitorFromWindow(g_panel, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) }; GetMonitorInfo(hMon, &mi);
    int screenH = mi.rcWork.bottom - mi.rcWork.top;
    int maxH = screenH - 40;
    if (maxH > kMaxPanelH) maxH = kMaxPanelH;

    int panelH = g_contentStartY + g_contentH + kPad;
    // Clamp to screen — account for panel Y position
    int availH = mi.rcWork.bottom - g_panelPos.y;
    if (availH < 120) availH = 120;
    if (maxH > availH) maxH = availH;
    if (panelH > maxH) panelH = maxH;
    if (panelH < 60) panelH = 60;
    g_viewH = panelH - g_contentStartY - kPad;

    // Clamp scroll
    int maxScroll = g_contentH - g_viewH;
    if (maxScroll < 0) maxScroll = 0;
    if (g_scrollY > maxScroll) g_scrollY = maxScroll;
    if (g_scrollY < 0) g_scrollY = 0;

    // No child EDIT windows — values are painted in PaintPanel.
    // A single EDIT spawns on click via SpawnEditAt().
    KillActiveEdit(false);
    g_hoverParam = -1;

    // Keep current position if panel was dragged (but not on fresh open)
    if (!g_freshOpen) {
        RECT cur; GetWindowRect(g_panel, &cur);
        if (cur.right - cur.left > 1) { g_panelPos.x = cur.left; g_panelPos.y = cur.top; }
    }
    g_freshOpen = false;

    SetWindowPos(g_panel, HWND_TOPMOST, g_panelPos.x, g_panelPos.y, panelW, panelH, SWP_NOACTIVATE);
    SendMessage(g_panel, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_panel, nullptr, FALSE);
    PositionBtnStrips();
}

// ── Button hit test ─────────────────────────────────────────────
static int HitBtnRow(const BtnDef* btns, int count, int rowY, int panelW, POINT pt) {
    int x = kPad, w = panelW - kPad * 2;
    int bw = (w - (count - 1) * kBtnGap) / count;
    if (pt.y < rowY || pt.y >= rowY + kBtnH) return -1;
    for (int i = 0; i < count; i++) {
        int bx = x + i * (bw + kBtnGap);
        if (pt.x >= bx && pt.x < bx + bw) return btns[i].id;
    }
    return -1;
}


// ── Find which param the mouse is over ──────────────────────────

// ── Find which group header the click is on ─────────────────────
static int FindGroupAtY(int clickY) {
    int y = g_contentStartY - g_scrollY;
    for (size_t gi = 0; gi < g_groups.size(); gi++) {
        if (clickY >= y && clickY < y + kLineH) return (int)gi;
        y += kLineH;
        bool collapsed = g_collapsed.count(g_groups[gi].title) > 0;
        if (!collapsed) y += g_groups[gi].count * kLineH;
        if (gi + 1 < g_groups.size()) y += kGroupGap;
    }
    return -1;
}

// ── Button strip window proc ────────────────────────────────────
static LRESULT CALLBACK BtnStripProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    bool isLeft = (hwnd == g_btnLeft);
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldBmp = (HBITMAP)SelectObject(mem, bmp);

        int curLevel = 0;
        Interface* ip = GetCOREInterface();
        if (ip) curLevel = ip->GetSubObjectLevel();

        // Only left strip (sub-object toggles)
        if (isLeft) {
            if (g_ctx == CTX_EPOLY)  DrawBtnStrip(mem, kEPolySubObj, 5, curLevel, g_font, rc.right, rc.bottom);
            if (g_ctx == CTX_SPLINE) DrawBtnStrip(mem, kSplineSubObj, 3, curLevel, g_font, rc.right, rc.bottom);
        } else {
            // Right strip (quick access modifiers)
            if (!g_quickMods.empty()) {
                std::vector<BtnDef> qBtns;
                for (int i = 0; i < (int)g_quickMods.size(); ++i) {
                    qBtns.push_back({g_quickMods[i].shortLabel.c_str(), i});
                }
                DrawBtnStrip(mem, qBtns.data(), (int)qBtns.size(), -1, g_font, rc.right, rc.bottom);
            }
        }

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;

    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (isLeft) {
            int hit = -1;
            if (g_ctx == CTX_EPOLY)  hit = HitBtnStrip(kEPolySubObj, 5, BtnStripW(5), pt);
            if (g_ctx == CTX_SPLINE) hit = HitBtnStrip(kSplineSubObj, 3, BtnStripW(3), pt);
            if (hit >= 0 && IsModifyMode()) {
                Interface* ip = GetCOREInterface();
                if (ip) ip->SetSubObjectLevel(ip->GetSubObjectLevel() == hit ? 0 : hit);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        } else {
            int count = (int)g_quickMods.size();
            if (count > 0) {
                int hit = -1;
                std::vector<BtnDef> qBtns;
                for (int i = 0; i < count; ++i) qBtns.push_back({g_quickMods[i].shortLabel.c_str(), i});
                hit = HitBtnStrip(qBtns.data(), count, BtnStripW(count), pt);
                if (hit >= 0 && hit < count) {
                    std::wstring name = g_quickMods[hit].internalName;
                    std::wstring s = L"try(addModifier $ (" + name + L"()))catch()";
                    ExecuteMAXScriptScript(s.c_str(), MAXScript::ScriptSource::Dynamic);
                    if (auto* ip = GetCOREInterface()) ip->RedrawViews(ip->GetTime());
                    return 0;
                }
            }
        }
        break;
    }
    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int hit = -1;
        if (isLeft) {
            if (g_ctx == CTX_EPOLY)  hit = HitBtnStrip(kEPolySubObj, 5, BtnStripW(5), pt);
            if (g_ctx == CTX_SPLINE) hit = HitBtnStrip(kSplineSubObj, 3, BtnStripW(3), pt);
        } else {
            int count = (int)g_quickMods.size();
            if (count > 0) {
                std::vector<BtnDef> qBtns;
                for (int i = 0; i < count; ++i) qBtns.push_back({g_quickMods[i].shortLabel.c_str(), i});
                hit = HitBtnStrip(qBtns.data(), count, BtnStripW(count), pt);
            }
        }
        if (hit != g_hoverBtn) { g_hoverBtn = hit; InvalidateRect(hwnd, nullptr, FALSE); }
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_hoverBtn != -1) { g_hoverBtn = -1; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// ── Favorites strip ─────────────────────────────────────────────
static void DestroyFavEdits() {
    for (auto& ef : g_favEdits)
        if (ef.hwnd) { DestroyWindow(ef.hwnd); ef.hwnd = nullptr; }
    g_favEdits.clear();
}

static const int kFavLabelH = 11;  // tiny label height
static const int kFavCellW = 72;   // brick cell width
static const int kFavCellH = kFavLabelH + kEditH + 4;  // label + edit + gap
static const int kFavGap   = 3;    // gap between bricks

static void BuildFavorites() {
    DestroyFavEdits();
    if (g_favorites.empty() || !g_favWnd) return;

    Interface* ip = GetCOREInterface();
    if (!ip || ip->GetSelNodeCount() == 0) return;
    INode* node = ip->GetSelNode(0);
    if (!node) return;

    // First pass: collect valid favorites into the vector (no HWND yet)
    for (auto& key : g_favorites) {
        if ((int)g_favEdits.size() >= kFavMaxParams) break;
        IParamBlock2* pb = nullptr; ParamID pid = 0; ParamType2 ptype = (ParamType2)0;
        if (FindParam(node, key, pb, pid, ptype)) {
            if (ptype & TYPE_TAB) continue;
            if (!IsFloat(ptype) && !IsInt(ptype) && ptype != TYPE_BOOL) continue;
            size_t sep = key.find(L':');
            std::wstring cls = (sep != std::wstring::npos) ? key.substr(0, sep) : L"";
            std::wstring raw = (sep != std::wstring::npos) ? key.substr(sep + 1) : key;
            // Try to find a pretty label from g_edits first (already formatted)
            std::wstring label;
            for (auto& ge : g_edits) {
                if (ge.key == key) { label = ge.label; break; }
            }
            if (label.empty()) label = PrettyLabel(raw, cls);
            EditField ef;
            ef.label = label;
            ef.key = key; ef.pb = pb; ef.id = pid; ef.type = ptype; ef.hwnd = nullptr;
            g_favEdits.push_back(std::move(ef));
        } else {
            // Spline op fallback: keys like "SplineShape:Weld"
            size_t sep = key.find(L':');
            if (sep != std::wstring::npos) {
                std::wstring cls = key.substr(0, sep);
                std::wstring prp = key.substr(sep + 1);
                if ((cls == L"SplineShape" || cls == L"SplineCmd") && g_splineForButtons) {
                    bool found = false;
                    for (int si = 0; si < kSpCount; si++) {
                        if (prp == kSpLabels[si]) {
                            EditField ef;
                            ef.label = kSpLabels[si];
                            ef.key = key; ef.pb = nullptr;
                            ef.id = (ParamID)(kSpSentinel + si);
                            ef.type = (ParamType2)TYPE_FLOAT; ef.hwnd = nullptr;
                            g_favEdits.push_back(std::move(ef));
                            found = true; break;
                        }
                    }
                    if (found) continue;
                }
            }
            // PB1 fallback: look up key in current g_edits (already built by GatherParams)
            // Match any PB1 entry (pb==nullptr) with matching key
            {
                bool found = false;
                for (auto& ge : g_edits) {
                    if (ge.key == key && !ge.pb) {
                        EditField ef;
                        ef.label = ge.label;
                        ef.key = key; ef.pb = nullptr; ef.id = ge.id;
                        ef.type = ge.type; ef.hwnd = nullptr;
                        ef.msPath = ge.msPath.empty() ? L"$.baseObject" : ge.msPath;
                        g_favEdits.push_back(std::move(ef));
                        found = true; break;
                    }
                }
                if (found) continue;
            }
        }
    }

    // Disambiguate duplicate labels by adding class prefix
    for (size_t i = 0; i < g_favEdits.size(); i++) {
        bool dup = false;
        for (size_t j = 0; j < g_favEdits.size(); j++)
            if (i != j && g_favEdits[i].label == g_favEdits[j].label) { dup = true; break; }
        if (dup) {
            size_t sep = g_favEdits[i].key.find(L':');
            if (sep != std::wstring::npos)
                g_favEdits[i].label = g_favEdits[i].key.substr(0, sep) + L" \u00B7 " + g_favEdits[i].label;
        }
    }

    // Second pass: create edit controls in brick layout (left→right, wrapping)
    // Compute strip width from panel
    RECT pr2; GetWindowRect(g_panel, &pr2);
    int stripW = (int)(pr2.right - pr2.left);
    if (stripW < 200) stripW = 400;
    int cols = std::max(1, (stripW - 8) / (kFavCellW + kFavGap));

    for (int i = 0; i < (int)g_favEdits.size(); i++) {
        auto& ef = g_favEdits[i];
        int col = i % cols;
        int row = i / cols;
        int cx = 4 + col * (kFavCellW + kFavGap);
        int cy = 4 + row * (kFavCellH + kFavGap) + kFavLabelH;
        DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_CENTER;
            ef.hwnd = CreateWindowEx(0, _T("EDIT"), _T(""),
            style, cx, cy, kFavCellW, kEditH, g_favWnd, nullptr, hInstance, nullptr);
        SendMessage(ef.hwnd, WM_SETFONT, (WPARAM)g_fontBold, TRUE);
        if (!g_origEdit) g_origEdit = (WNDPROC)GetWindowLongPtr(ef.hwnd, GWLP_WNDPROC);
        SetWindowLongPtr(ef.hwnd, GWLP_WNDPROC, (LONG_PTR)FavEditProc);
        SetProp(ef.hwnd, _T("WF"), (HANDLE)&ef);
    }

    // Refresh values
    TimeValue t = ip->GetTime();
    for (auto& ef : g_favEdits) {
        if (!ef.hwnd) continue;
        TCHAR buf[64]; FormatValue(ef, t, buf, 64);
        SetWindowText(ef.hwnd, buf);
    }
}

static void RefreshFavEdits() {
    Interface* ip = GetCOREInterface();
    if (!ip) return;
    TimeValue t = ip->GetTime();
    HWND focused = GetFocus();
    for (auto& ef : g_favEdits) {
        if (!ef.hwnd) continue;
        if (ef.hwnd == focused) continue;
        TCHAR buf[64]; FormatValue(ef, t, buf, 64);
        TCHAR old[64]; GetWindowText(ef.hwnd, old, 64);
        if (_tcscmp(old, buf) != 0) SetWindowText(ef.hwnd, buf);
    }
}

static void PositionFavStrip() {
    if (!g_favWnd || g_favEdits.empty()) {
        if (g_favWnd) ShowWindow(g_favWnd, SW_HIDE);
        return;
    }
    if (!g_open) { ShowWindow(g_favWnd, SW_HIDE); return; }

    RECT pr; GetWindowRect(g_panel, &pr);
    int panelW = (int)(pr.right - pr.left);
    if (panelW < 200) panelW = 400;
    int cols = std::max(1, (panelW - 8) / (kFavCellW + kFavGap));
    int rows = ((int)g_favEdits.size() + cols - 1) / cols;
    int w = 8 + cols * (kFavCellW + kFavGap);
    int h = 8 + rows * (kFavCellH + kFavGap);
    SetWindowPos(g_favWnd, HWND_TOPMOST,
        pr.left + (panelW - w) / 2, pr.bottom + kSideGap,
        w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_favWnd, nullptr, FALSE);
}

static HFONT g_fontTiny = nullptr;

static LRESULT CALLBACK FavStripProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);

        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldBmp = (HBITMAP)SelectObject(mem, bmp);

        // Background
        HBRUSH b = CreateSolidBrush(kBg); FillRect(mem, &rc, b); DeleteObject(b);
        HPEN p = CreatePen(PS_SOLID, 1, kBorder);
        HPEN op2 = (HPEN)SelectObject(mem, p);
        SelectObject(mem, GetStockObject(NULL_BRUSH));
        Rectangle(mem, 0, 0, rc.right, rc.bottom);
        SelectObject(mem, op2); DeleteObject(p);

        // Tiny labels — brick layout
        if (!g_fontTiny)
            g_fontTiny = CreateFont(-9, 0,0,0, FW_NORMAL, 0,0,0, DEFAULT_CHARSET,
                0,0, CLEARTYPE_QUALITY, 0, _T("Segoe UI"));
        SetBkMode(mem, TRANSPARENT);
        SelectObject(mem, g_fontTiny);
        SetTextColor(mem, kAccent);
        int cols = std::max(1, ((int)rc.right - 8) / (kFavCellW + kFavGap));
        for (int i = 0; i < (int)g_favEdits.size(); i++) {
            int col = i % cols;
            int row = i / cols;
            int cx = 4 + col * (kFavCellW + kFavGap);
            int cy = 4 + row * (kFavCellH + kFavGap);
            RECT lr = { cx, cy, cx + kFavCellW, cy + kFavLabelH };
            DrawText(mem, g_favEdits[i].label.c_str(), -1, &lr,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wp;
        HWND eh = (HWND)lp;
        SetTextColor(hdc, kValueClr);
        if (GetFocus() == eh) { SetBkColor(hdc, kEditFocus); return (LRESULT)g_brEditFoc; }
        SetBkColor(hdc, kEditBg);
        return (LRESULT)g_brEdit;
    }
    case WM_COMMAND:
        if (HIWORD(wp) == EN_SETFOCUS) DisableAccelerators();
        if (HIWORD(wp) == EN_KILLFOCUS) {
            EnableAccelerators();
        }
        break;
    case WM_TIMER:
        RefreshFavEdits();
        return 0;

    // ── Drag scrub on label area (above edit controls) ──────────
    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        RECT rc; GetClientRect(hwnd, &rc);
        int cols = std::max(1, ((int)rc.right - 8) / (kFavCellW + kFavGap));
        for (int i = 0; i < (int)g_favEdits.size(); i++) {
            int col = i % cols, row = i / cols;
            int cx = 4 + col * (kFavCellW + kFavGap);
            int cy = 4 + row * (kFavCellH + kFavGap);
            RECT lr = { cx, cy, cx + kFavCellW, cy + kFavLabelH };
            if (PtInRect(&lr, pt)) {
                g_favDragging = true;
                g_favDragIdx  = i;
                g_favDragStartX = pt.x;
                g_favDragAccum  = 0;
                SetCapture(hwnd);
                theHold.Begin();
                SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
                return 0;
            }
        }
        break;
    }
    case WM_MOUSEMOVE: {
        if (!g_favDragging || g_favDragIdx < 0 || g_favDragIdx >= (int)g_favEdits.size()) break;
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int dx = pt.x - g_favDragStartX;
        if (dx == 0) return 0;
        g_favDragStartX = pt.x;
        auto& ef = g_favEdits[g_favDragIdx];
        Interface* ip = GetCOREInterface();
        TimeValue t = ip ? ip->GetTime() : 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (ef.pb) {
            float cur = ef.pb->GetFloat(ef.id, t);
            if (IsFloat(ef.type)) {
                float a = cur < 0 ? -cur : cur;
                float sc = a > 0.001f ? a * 0.01f : 0.001f;
                if (shift) sc *= 10.0f; if (ctrl) sc *= 0.1f;
                ef.pb->SetValue(ef.id, t, cur + (float)dx * sc);
            } else if (ef.type == TYPE_BOOL) {
                ef.pb->SetValue(ef.id, t, ef.pb->GetInt(ef.id, t) ? 0 : 1);
            } else {
                g_favDragAccum += dx;
                int s = g_favDragAccum / 3;
                if (s != 0) { g_favDragAccum -= s * 3; if (shift) s *= 10;
                    ef.pb->SetValue(ef.id, t, ef.pb->GetInt(ef.id, t) + s); }
            }
        }
        if (ip) {
            for (int ni = 0; ni < ip->GetSelNodeCount(); ni++) {
                INode* nd = ip->GetSelNode(ni);
                if (nd) nd->NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE);
            }
            ip->RedrawViews(t);
        }
        RefreshFavEdits();
        // Tooltip
        POINT scr; GetCursorPos(&scr);
        TCHAR buf[64]; FormatValue(ef, t, buf, 64);
        std::wstring tip = ef.label + L": " + buf;
        ShowDragTip(scr, tip);
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!g_favDragging) break;
        g_favDragging = false;
        g_favDragIdx  = -1;
        ReleaseCapture();
        theHold.Accept(_T("Pin Drag"));
        HideDragTip();
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return 0;
    }
    case WM_SETCURSOR: {
        // Show resize cursor when hovering label areas
        if (!g_favDragging) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
            RECT rc; GetClientRect(hwnd, &rc);
            int cols = std::max(1, ((int)rc.right - 8) / (kFavCellW + kFavGap));
            for (int i = 0; i < (int)g_favEdits.size(); i++) {
                int col = i % cols, row = i / cols;
                int cx = 4 + col * (kFavCellW + kFavGap);
                int cy = 4 + row * (kFavCellH + kFavGap);
                RECT lr = { cx, cy, cx + kFavCellW, cy + kFavLabelH };
                if (PtInRect(&lr, pt)) { SetCursor(LoadCursor(nullptr, IDC_SIZEWE)); return TRUE; }
            }
        }
        break;
    }
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// ── Window proc ─────────────────────────────────────────────────
static LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT:      PaintPanel(hwnd); return 0;
    case WM_ERASEBKGND: return 1;

    case WM_USER + 101:
        if (g_open) ClosePanel();
        return 0;

    case WM_WINDOWPOSCHANGED: {
        if (g_open) {
            PositionBtnStrips(); PositionFavStrip();
            // Resize search bar to match panel width
            if (g_modSearchEdit) {
                RECT rc2; GetClientRect(hwnd, &rc2);
                SetWindowPos(g_modSearchEdit, nullptr, kPad, kPad - 1,
                    rc2.right - kPad * 2 - 62, kFontHdr + 2,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
        break;
    }

    case WM_NCHITTEST: {
        // No HTCAPTION — panel is dragged via middle-mouse only
        return HTCLIENT;
    }

    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        RECT wr; GetWindowRect(hwnd, &wr); int pw = wr.right - wr.left;

        if (PtInRect(&g_closeRect, pt)) { ClosePanel(); return 0; }
        if (PtInRect(&g_visBtnRect, pt)) {
            g_visEditMode = !g_visEditMode;
            GatherParams(); BuildLayout();
            BuildFavorites(); PositionFavStrip();
            return 0;
        }
        if (PtInRect(&g_modStackRect, pt)) { ModStack::Toggle(); InvalidateRect(hwnd, nullptr, FALSE); return 0; }

        // Mod search: click on result = apply
        if (g_modSearch && pt.y >= g_contentStartY) {
            int ri = (pt.y - g_contentStartY + g_modSearchScrollY) / kLineH;
            if (ri >= 0 && ri < (int)g_modSearchResults.size()) {
                g_modSearchSel = ri;
                ApplyModSearchResult();
                ExitModSearch();
                GatherParams(); BuildLayout();
                BuildFavorites(); PositionFavStrip();
            }
            return 0;
        }

        // Click anywhere kills active edit
        if (g_editHwnd) { KillActiveEdit(true); InvalidateRect(hwnd, nullptr, FALSE); }

        // Click/drag on value area (skip if on a group header row)
        if (FindGroupAtY(pt.y) < 0) {
            int idx = FindParamAtY(pt.y);
            RECT rc2; GetClientRect(hwnd, &rc2);
            int editX = rc2.right - kPad - kEditW;
            if (idx >= 0 && pt.x >= editX && pt.y >= g_contentStartY) {
                if (g_epolyOp >= 0 || g_visEditMode) {
                    // During EPoly takeover or vis edit — just spawn edit, no drag
                    SpawnEditAt(idx);
                } else {
                    // Start drag on value area — if user releases without moving, spawn edit instead
                    g_lmbDragging = true;
                    g_lmbDragIdx  = idx;
                    g_lmbDragStartX = pt.x;
                    g_lmbDragAccum  = 0;
                    g_lmbDragMoved  = false;
                    SetCapture(hwnd);
                    theHold.Begin();
                }
                return 0;
            }
        }

        // Vis edit mode: click on param toggles its visibility
        if (g_visEditMode) {
            int idx = FindParamAtY(pt.y);
            if (idx >= 0 && idx < (int)g_edits.size()) {
                const std::wstring& key = g_edits[idx].key;
                if (g_hidden.count(key)) g_hidden.erase(key);
                else g_hidden.insert(key);
                SaveSettings();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }

        // Click on group header
        { int gIdx = FindGroupAtY(pt.y);
        if (gIdx >= 0) {
            // [●][▲][▼][×] buttons for modifier groups
            if (g_groups[gIdx].mod) {
                int rEdge2 = pw - kPad;
                int bw = 16;
                int bx = rEdge2 - bw * 4 - 3;
                // Find this modifier's 1-based stack index
                auto findModIdx = [&](Modifier* mod) -> int {
                    Interface* ip2 = GetCOREInterface();
                    if (!ip2 || ip2->GetSelNodeCount() == 0) return -1;
                    INode* nd = ip2->GetSelNode(0);
                    Object* ob = nd ? nd->GetObjectRef() : nullptr;
                    while (ob && ob->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
                        IDerivedObject* dv = static_cast<IDerivedObject*>(ob);
                        for (int mi = 0; mi < dv->NumModifiers(); mi++)
                            if (dv->GetModifier(mi) == mod) return mi + 1;
                        ob = dv->GetObjRef();
                    }
                    return -1;
                };

                if (pt.x >= bx && pt.x < bx + bw) {
                    // ● Enable/disable toggle
                    Modifier* mod = g_groups[gIdx].mod;
                    if (mod->IsEnabled()) mod->DisableMod(); else mod->EnableMod();
                    Interface* ip3 = GetCOREInterface();
                    if (ip3 && ip3->GetSelNodeCount() > 0) {
                        INode* nd = ip3->GetSelNode(0);
                        if (nd) nd->NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE);
                        ip3->RedrawViews(ip3->GetTime());
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                if (pt.x >= bx + bw + 1 && pt.x < bx + bw*2 + 1) {
                    // ▲ Move up
                    int mi = findModIdx(g_groups[gIdx].mod);
                    if (mi > 1) {
                        // after delete, remaining = cnt-1. target = mi-1 from top.
                        // before: = remaining - target + 1. If target=1, no before (top).
                        std::wstring s;
                        if (mi == 2)
                            s = L"undo \"Move Up\" on (local m=$.modifiers[2];deleteModifier $ 2;addModifier $ m)";
                        else
                            s = L"undo \"Move Up\" on (local m=$.modifiers[" + std::to_wstring(mi) +
                                L"];deleteModifier $ " + std::to_wstring(mi) +
                                L";addModifier $ m before:($.modifiers.count-" + std::to_wstring(mi - 2) + L"))";
                        EPolyAccept();
                        ExecuteMAXScriptScript(s.c_str(), MAXScript::ScriptSource::Dynamic);
                        GatherParams(); BuildLayout();
                        BuildFavorites(); PositionFavStrip();
                        if (auto* ip2 = GetCOREInterface()) ip2->RedrawViews(ip2->GetTime());
                    }
                    return 0;
                }
                if (pt.x >= bx + bw*2 + 2 && pt.x < bx + bw*3 + 2) {
                    // ▼ Move down
                    int mi = findModIdx(g_groups[gIdx].mod);
                    Interface* ip2 = GetCOREInterface();
                    int total = 0;
                    if (ip2 && ip2->GetSelNodeCount() > 0) {
                        INode* nd = ip2->GetSelNode(0);
                        Object* ob = nd ? nd->GetObjectRef() : nullptr;
                        while (ob && ob->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
                            IDerivedObject* dv = static_cast<IDerivedObject*>(ob);
                            total += dv->NumModifiers();
                            ob = dv->GetObjRef();
                        }
                    }
                    if (mi > 0 && mi < total) {
                        // after delete, add before:remaining_count to go to bottom-relative position
                        std::wstring s = L"undo \"Move Down\" on (local m=$.modifiers[" +
                            std::to_wstring(mi) + L"];deleteModifier $ " +
                            std::to_wstring(mi) + L";addModifier $ m before:($.modifiers.count-" +
                            std::to_wstring(mi - 1) + L"))";
                        EPolyAccept();
                        ExecuteMAXScriptScript(s.c_str(), MAXScript::ScriptSource::Dynamic);
                        GatherParams(); BuildLayout();
                        BuildFavorites(); PositionFavStrip();
                        if (ip2) ip2->RedrawViews(ip2->GetTime());
                    }
                    return 0;
                }
                if (pt.x >= bx + bw*3 + 3) {
                    // × Delete
                    int mi = findModIdx(g_groups[gIdx].mod);
                    if (mi > 0) {
                        std::wstring s = L"deleteModifier $ " + std::to_wstring(mi);
                        EPolyAccept();
                        ExecuteMAXScriptScript(s.c_str(), MAXScript::ScriptSource::Dynamic);
                        GatherParams(); BuildLayout();
                        BuildFavorites(); PositionFavStrip();
                        if (auto* ip2 = GetCOREInterface()) ip2->RedrawViews(ip2->GetTime());
                    }
                    return 0;
                }
            }
            // EPoly op group — click CANCEL text to revert
            if (gIdx == 0 && g_epolyOp >= 0) {
                EPolyCancelDrop();
                return 0;
            }
            // Shift+click on modifier header = toggle enable/disable
            if ((GetKeyState(VK_SHIFT) & 0x8000) && g_groups[gIdx].mod) {
                Modifier* mod = g_groups[gIdx].mod;
                if (mod->IsEnabled()) mod->DisableMod(); else mod->EnableMod();
                Interface* ip3 = GetCOREInterface();
                if (ip3) {
                    INode* nd = ip3->GetSelNode(0);
                    if (nd) nd->NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE);
                    ip3->RedrawViews(ip3->GetTime());
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            // Normal collapse toggle — only if click is on the title text
            {
                const auto& title = g_groups[gIdx].title;
                bool collapsed2 = g_collapsed.count(title) > 0;
                std::wstring hdr = (collapsed2 ? L"\x25B8 " : L"\x25BE ") + title;
                if (g_groups[gIdx].mod && !g_groups[gIdx].mod->IsEnabled()) hdr += L"  [OFF]";
                HDC hdc2 = GetDC(hwnd);
                HFONT oldF2 = (HFONT)SelectObject(hdc2, g_fontBold);
                SIZE sz; GetTextExtentPoint32(hdc2, hdr.c_str(), (int)hdr.length(), &sz);
                SelectObject(hdc2, oldF2);
                ReleaseDC(hwnd, hdc2);
                if (pt.x <= kPad + sz.cx + 4) {
                    if (collapsed2) g_collapsed.erase(title);
                    else g_collapsed.insert(title);
                    SaveSettings();
                    BuildLayout();
                }
                return 0;
            }
        }}
        break;
    }

    case WM_MBUTTONDOWN: {
        // Middle-mouse drag to move panel
        g_mmDragging = true;
        GetCursorPos(&g_mmStart);
        GetWindowRect(hwnd, &g_mmPanelRect);
        SetCapture(hwnd);
        return 0;
    }
    case WM_MBUTTONUP: {
        if (g_mmDragging) { g_mmDragging = false; ReleaseCapture(); }
        return 0;
    }
    case WM_NCMBUTTONDOWN: {
        // Middle-mouse drag from non-client area
        g_mmDragging = true;
        GetCursorPos(&g_mmStart);
        GetWindowRect(hwnd, &g_mmPanelRect);
        SetCapture(hwnd);
        return 0;
    }

    case WM_RBUTTONDOWN: {
        if (g_modSearch) return 0;
        POINT rpt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };

        // Right-click on param → toggle favorite
        int idx = FindParamAtY(rpt.y);
        if (idx >= 0 && idx < (int)g_edits.size()) {
            const std::wstring& key = g_edits[idx].key;
            // Skip keys with empty class prefix (broken)
            if (key.empty() || key[0] == L':') break;
            if (g_favorites.count(key)) g_favorites.erase(key);
            else g_favorites.insert(key);
            SaveSettings();
            BuildFavorites();
            PositionFavStrip();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (g_lmbDragging) {
            int idx = g_lmbDragIdx;
            bool moved = g_lmbDragMoved;
            g_lmbDragging = false;
            g_lmbDragIdx  = -1;
            ReleaseCapture();
            if (moved) {
                theHold.Accept(_T("Param Drag"));
                HideDragTip();
                GatherParams(); BuildLayout();
                BuildFavorites(); PositionFavStrip();
            } else {
                // No drag — treat as click, spawn edit box
                theHold.Cancel();
                if (idx >= 0 && idx < (int)g_edits.size())
                    SpawnEditAt(idx);
            }
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        // Left-click param drag → scrub value
        if (g_lmbDragging && g_lmbDragIdx >= 0 && g_lmbDragIdx < (int)g_edits.size()) {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            int dx = pt.x - g_lmbDragStartX;
            // Deadzone: 3px before committing to drag (prevents accidental scrub on click)
            if (!g_lmbDragMoved && (dx > -3 && dx < 3)) return 0;
            if (!g_lmbDragMoved) g_lmbDragMoved = true;
            g_lmbDragStartX = pt.x;
            if (dx != 0) {
                Interface* ip = GetCOREInterface();
                TimeValue t = ip ? ip->GetTime() : 0;
                bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                EditField& ef = g_edits[g_lmbDragIdx];
                int selCount = ip ? ip->GetSelNodeCount() : 0;

                if (ef.pb) {
                    // PB2 — adjust on all selected nodes
                    for (int ni = 0; ni < selCount; ni++) {
                        INode* nd = ip->GetSelNode(ni);
                        if (!nd) continue;
                        IParamBlock2* pb = nullptr; ParamID pid = 0; ParamType2 pt2 = (ParamType2)0;
                        if (!FindParam(nd, ef.key, pb, pid, pt2)) continue;
                        if (IsFloat(pt2)) {
                            float cur = pb->GetFloat(pid, t);
                            float a = cur < 0 ? -cur : cur;
                            float sc = a > 0.001f ? a * 0.01f : 0.001f;
                            if (shift) sc *= 10.0f; if (ctrl) sc *= 0.1f;
                            pb->SetValue(pid, t, cur + (float)dx * sc);
                        } else if (pt2 == TYPE_BOOL) {
                            pb->SetValue(pid, t, pb->GetInt(pid, t) ? 0 : 1);
                        } else {
                            g_lmbDragAccum += dx;
                            int s = g_lmbDragAccum / 3;
                            if (s != 0) {
                                g_lmbDragAccum -= s * 3;
                                if (shift) s *= 10;
                                pb->SetValue(pid, t, pb->GetInt(pid, t) + s);
                            }
                        }
                    }
                } else if (!ef.msPath.empty()) {
                    // PB1 fallback via MaxScript
                    size_t sep = ef.key.find(L':');
                    if (sep != std::wstring::npos) {
                        std::wstring prop = ef.key.substr(sep + 1);
                        // Read current value to compute proportional step
                        std::wstring readS = L"try((getProperty " + ef.msPath + L" #" + prop + L") as float)catch(0.0)";
                        FPValue rv; ExecuteMAXScriptScript(readS.c_str(), MAXScript::ScriptSource::Dynamic, TRUE, &rv);
                        float cur = (rv.type == TYPE_FLOAT) ? rv.f : 0.0f;
                        float a = cur < 0 ? -cur : cur;
                        float sc = a > 0.001f ? a * 0.01f : 0.001f;
                        if (shift) sc *= 10.0f; if (ctrl) sc *= 0.1f;
                        float inc = (float)dx * sc;
                        std::wstring objPath = ef.msPath;
                        if (!objPath.empty() && objPath[0] == L'$') objPath = objPath.substr(1);
                        std::wstring s = L"for obj in selection do try(local o=obj" + objPath +
                            L";local v=getProperty o #" + prop +
                            L";setProperty o #" + prop + L" (v+" + std::to_wstring(inc) + L"))catch()";
                        ExecuteMAXScriptScript(s.c_str(), MAXScript::ScriptSource::Dynamic);
                    }
                }

                if (ip) {
                    for (int ni = 0; ni < selCount; ni++) {
                        INode* nd = ip->GetSelNode(ni);
                        if (nd) nd->NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE);
                    }
                    ip->RedrawViews(t);
                }

                // Refresh panel display
                InvalidateRect(hwnd, nullptr, FALSE);

                // Show drag tooltip with current value
                POINT scr; GetCursorPos(&scr);
                std::wstring tip = ef.label + L": ";
                if (ef.pb) {
                    IParamBlock2* pb = nullptr; ParamID pid = 0; ParamType2 pt2 = (ParamType2)0;
                    if (selCount > 0 && FindParam(ip->GetSelNode(0), ef.key, pb, pid, pt2)) {
                        if (IsFloat(pt2)) {
                            wchar_t buf[64]; swprintf(buf, 64, L"%.3f", pb->GetFloat(pid, t));
                            tip += buf;
                        } else {
                            tip += std::to_wstring(pb->GetInt(pid, t));
                        }
                    }
                } else if (!ef.msPath.empty()) {
                    size_t sep = ef.key.find(L':');
                    if (sep != std::wstring::npos) {
                        std::wstring prop = ef.key.substr(sep + 1);
                        std::wstring readS = L"try((getProperty " + ef.msPath + L" #" + prop + L") as string)catch(\"\")";
                        FPValue rv; ExecuteMAXScriptScript(readS.c_str(), MAXScript::ScriptSource::Dynamic, TRUE, &rv);
                        if (rv.type == TYPE_STRING && rv.s) tip += rv.s;
                    }
                }
                ShowDragTip(scr, tip);
            }
            return 0;
        }

        // Middle-mouse panel drag
        if (g_mmDragging) {
            POINT cur; GetCursorPos(&cur);
            SetWindowPos(hwnd, nullptr,
                g_mmPanelRect.left + (cur.x - g_mmStart.x),
                g_mmPanelRect.top + (cur.y - g_mmStart.y),
                0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        bool over = PtInRect(&g_closeRect, pt) != 0;
        if (over != g_hoverClose) { g_hoverClose = over; InvalidateRect(hwnd, &g_closeRect, FALSE); }
        bool overMs = PtInRect(&g_modStackRect, pt) != 0;
        if (overMs != g_hoverModStack) { g_hoverModStack = overMs; InvalidateRect(hwnd, &g_modStackRect, FALSE); }
        bool overVis = PtInRect(&g_visBtnRect, pt) != 0;
        if (overVis != g_hoverVisBtn) { g_hoverVisBtn = overVis; InvalidateRect(hwnd, &g_visBtnRect, FALSE); }
        // Track hovered param for highlight + wheel (not during search)
        if (!g_modSearch) {
            RECT rc2; GetClientRect(hwnd, &rc2);
            int newHover = -1;
            if (pt.x >= (int)(rc2.right - kPad - kEditW) && pt.y >= g_contentStartY)
                newHover = FindParamAtY(pt.y);
            if (newHover != g_hoverParam) { g_hoverParam = newHover; InvalidateRect(hwnd, nullptr, FALSE); }
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_hoverClose) { g_hoverClose = false; InvalidateRect(hwnd, &g_closeRect, FALSE); }
        if (g_hoverModStack) { g_hoverModStack = false; InvalidateRect(hwnd, &g_modStackRect, FALSE); }
        if (g_hoverVisBtn) { g_hoverVisBtn = false; InvalidateRect(hwnd, &g_visBtnRect, FALSE); }
        if (g_hoverParam >= 0) { g_hoverParam = -1; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wp;
        HWND eh = (HWND)lp;
        SetTextColor(hdc, kValueClr);
        if (GetFocus() == eh) { SetBkColor(hdc, kEditFocus); return (LRESULT)g_brEditFoc; }
        SetBkColor(hdc, kEditBg);
        return (LRESULT)g_brEdit;
    }

    case WM_COMMAND:
        if ((HWND)lp == g_modSearchEdit) {
            if (HIWORD(wp) == EN_CHANGE) UpdateModSearch();
            if (HIWORD(wp) == EN_SETFOCUS) DisableAccelerators();
            if (HIWORD(wp) == EN_KILLFOCUS) EnableAccelerators();
            break;
        }
        if (HIWORD(wp) == EN_KILLFOCUS && (HWND)lp == g_editHwnd) {
            KillActiveEdit(true);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;

    case WM_MOUSEWHEEL: {
        // In search mode, scroll results
        if (g_modSearch) {
            RECT rcS; GetClientRect(hwnd, &rcS);
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            g_modSearchScrollY -= delta / 120 * kLineH;
            int maxScr = (int)g_modSearchResults.size() * kLineH - (rcS.bottom - g_contentStartY);
            if (maxScr < 0) maxScr = 0;
            if (g_modSearchScrollY < 0) g_modSearchScrollY = 0;
            if (g_modSearchScrollY > maxScr) g_modSearchScrollY = maxScr;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        POINT mp; GetCursorPos(&mp); ScreenToClient(hwnd, &mp);
        RECT rcw; GetClientRect(hwnd, &rcw);
        int hoverIdx = FindParamAtY(mp.y);

        // Hover over a value → wheel adjusts the value
        if (hoverIdx >= 0 && mp.x >= (int)(rcw.right - kPad - kEditW) && !g_editHwnd) {
            auto& ef = g_edits[hoverIdx];
            float step = (float)GET_WHEEL_DELTA_WPARAM(wp) / 120.0f;
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            Interface* ip = GetCOREInterface();
            TimeValue t = ip ? ip->GetTime() : 0;

            // Spline op values (sentinel-marked)
            if (!ef.pb && (int)ef.id >= kSpSentinel && (int)ef.id < kSpSentinel + kSpCount) {
                int idx = (int)ef.id - kSpSentinel;
                {
                    float a = g_splineVals[idx] < 0 ? -g_splineVals[idx] : g_splineVals[idx];
                    float sc = a>100.f?10.f:a>10.f?1.f:a>1.f?0.1f:0.01f;
                    if (shift) sc *= 10.0f; if (ctrl) sc *= 0.1f;
                    g_splineVals[idx] += step * sc;
                }
            } else if (ef.pb && ip) {
                theHold.Suspend();
                if (g_epolyOp >= 0) {
                    // EPoly takeover — use cached PB directly
                    if (IsFloat(ef.type)) {
                        float cur = ef.pb->GetFloat(ef.id, t);
                        float a = cur<0?-cur:cur;
                        float sc = a>100.f?10.f:a>10.f?1.f:a>1.f?0.1f:0.01f;
                        if (shift) sc *= 10.0f; if (ctrl) sc *= 0.1f;
                        ef.pb->SetValue(ef.id, t, cur + step * sc);
                    } else if (ef.type == TYPE_BOOL) {
                        ef.pb->SetValue(ef.id, t, ef.pb->GetInt(ef.id, t) ? 0 : 1);
                    } else {
                        int cur = ef.pb->GetInt(ef.id, t);
                        int s = (int)step;
                        if (shift) s *= 10; if (ctrl && s != 0) s = s>0?1:-1;
                        if (s == 0) s = step>0?1:-1;
                        ef.pb->SetValue(ef.id, t, cur + s);
                    }
                } else {
                    // Normal params — apply to all selected nodes
                    for (int ni = 0; ni < ip->GetSelNodeCount(); ni++) {
                        INode* nd = ip->GetSelNode(ni);
                        if (!nd) continue;
                        IParamBlock2* pb = nullptr; ParamID pid = 0; ParamType2 pt2 = (ParamType2)0;
                        if (!FindParam(nd, ef.key, pb, pid, pt2)) continue;
                        if (IsFloat(pt2)) {
                            float cur = pb->GetFloat(pid, t);
                            float a = cur<0?-cur:cur;
                            float sc = a>100.f?10.f:a>10.f?1.f:a>1.f?0.1f:0.01f;
                            if (shift) sc *= 10.0f; if (ctrl) sc *= 0.1f;
                            pb->SetValue(pid, t, cur + step * sc);
                        } else if (pt2 == TYPE_BOOL) {
                            pb->SetValue(pid, t, pb->GetInt(pid, t) ? 0 : 1);
                        } else {
                            int cur = pb->GetInt(pid, t);
                            int s = (int)step;
                            if (shift) s *= 10; if (ctrl && s != 0) s = s>0?1:-1;
                            if (s == 0) s = step>0?1:-1;
                            pb->SetValue(pid, t, cur + s);
                        }
                    }
                }
                theHold.Resume();
                if (g_epolyPreview) EPolyRefresh();
                else                NotifyParamChanged();
            } else if (!ef.pb) {
                // MaxScript-based params (legacy PB1 objects) — apply to all selected
                std::wstring prop = PropFromKey(ef);
                std::wstring objPath = MsObjPath(ef);
                if (!objPath.empty() && objPath[0] == L'$') objPath = objPath.substr(1);
                if (ef.type == (ParamType2)TYPE_BOOL) {
                    std::wstring script = L"for obj in selection do try(local o=obj" + objPath +
                        L";setProperty o #" + prop + L" (not (getProperty o #" + prop + L")))catch()";
                    ExecuteMAXScriptScript(script.c_str(), MAXScript::ScriptSource::Dynamic);
                } else if (IsFloat(ef.type)) {
                    std::wstring script = L"for obj in selection do try(local o=obj" + objPath +
                        L";local v=getProperty o #" + prop +
                        L";setProperty o #" + prop + L" (v+" +
                        std::to_wstring(step) + L"*" +
                        L"(if (abs v)>100 then 10 else if (abs v)>10 then 1 else if (abs v)>1 then 0.1 else 0.01)" +
                        (shift ? L"*10" : L"") + (ctrl ? L"*0.1" : L"") +
                        L"))catch()";
                    ExecuteMAXScriptScript(script.c_str(), MAXScript::ScriptSource::Dynamic);
                } else {
                    int s = (int)step;
                    if (shift) s *= 10; if (ctrl && s != 0) s = s>0?1:-1;
                    if (s == 0) s = step>0?1:-1;
                    std::wstring script = L"for obj in selection do try(local o=obj" + objPath +
                        L";local v=getProperty o #" + prop +
                        L";setProperty o #" + prop + L" (v+" +
                        std::to_wstring(s) + L"))catch()";
                    ExecuteMAXScriptScript(script.c_str(), MAXScript::ScriptSource::Dynamic);
                }
                NotifyParamChanged();
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // Otherwise scroll the panel
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        g_scrollY -= delta / 120 * kLineH;
        int maxScroll = g_contentH - g_viewH;
        if (maxScroll < 0) maxScroll = 0;
        if (g_scrollY < 0) g_scrollY = 0;
        if (g_scrollY > maxScroll) g_scrollY = maxScroll;
        KillActiveEdit(true);  // kill edit on scroll
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_TIMER: {
        // Check if node changed
        Interface* ipt = GetCOREInterface();
        if (ipt) {
            if (ipt->GetSelNodeCount() == 0) { ClosePanel(); return 0; }
            INode* nd = ipt->GetSelNode(0);
            if (!nd) { ClosePanel(); return 0; }
            ULONG h = nd->GetHandle();
            const MCHAR* nn = nd->GetName();
            std::wstring cur = nn ? nn : L"";
            if (h != g_nodeHandle || cur != g_nodeName) { ClosePanel(); return 0; }
        }
        // Update button visibility based on modify mode
        PositionBtnStrips();
        // Values are painted — just repaint to show updated values
        InvalidateRect(hwnd, nullptr, FALSE);
        RefreshFavEdits();
        return 0;
    }

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            if (g_epolyPreview) {
                EPolyCancelDrop();
                GatherParams(); BuildLayout();
                BuildFavorites(); PositionFavStrip();
                InvalidateRect(hwnd, nullptr, FALSE);
            } else {
                ClosePanel();
            }
            return 0;
        }
        break;

    case WM_PP_TOGGLE:
        TogglePanel(); return 0;

    case WM_PP_SHADER:
        PowerShader::Toggle(); return 0;

    case WM_PP_MODSTACK:
        ModStack::Toggle(); return 0;

    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// ── Open / Close ────────────────────────────────────────────────
static void OpenPanel() {
    GatherParams();

    // Auto-collapse groups with 10+ params on first encounter
    static std::set<std::wstring> g_autoCollapseSeen;
    for (auto& gh : g_groups) {
        if (gh.count >= 10 && !g_autoCollapseSeen.count(gh.title)) {
            g_autoCollapseSeen.insert(gh.title);
            g_collapsed.insert(gh.title);
        }
    }

    POINT pt; GetCursorPos(&pt);
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) }; GetMonitorInfo(hMon, &mi);

    // Offset panel away from cursor when takeover is active so model stays visible
    int ox, oy;
    if (g_epolyOp >= 0) {
        ox = pt.x + 120;  oy = pt.y - 60;
    } else {
        ox = pt.x - kMinW / 2;  oy = pt.y;
    }
    if (ox + kMinW > mi.rcWork.right) ox = mi.rcWork.right - kMinW;
    if (ox < mi.rcWork.left) ox = mi.rcWork.left;
    if (oy < mi.rcWork.top)  oy = mi.rcWork.top;
    // Don't clamp bottom here — BuildLayout handles max height + scroll
    g_panelPos = { ox, oy };
    g_freshOpen = true;
    g_open = true;

    BuildLayout();
    PositionBtnStrips();
    BuildFavorites();
    PositionFavStrip();

    // Fade in panel with companions (button strips, fav strip)
    HWND fadeComps[] = { g_btnLeft, g_favWnd, g_btnRight };
    FadeIn(g_panel, fadeComps, 3);
    SetTimer(g_panel, 1, kRefreshMs, nullptr);
    if (g_favWnd && !g_favEdits.empty())
        SetTimer(g_favWnd, 1, kRefreshMs, nullptr);

    // Create search bar in header
    if (!g_modSearchEdit) {
        RECT rc; GetClientRect(g_panel, &rc);
        int searchX = kPad;
        int searchW = rc.right - kPad * 2 - 62;
        g_modSearchEdit = CreateWindowEx(0, _T("EDIT"), _T(""),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            searchX, kPad - 1, searchW, kFontHdr + 2,
            g_panel, nullptr, hInstance, nullptr);
        SendMessage(g_modSearchEdit, WM_SETFONT, (WPARAM)g_fontBold, TRUE);
        SendMessage(g_modSearchEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search modifiers...");
        SetWindowSubclass(g_modSearchEdit, ModSearchEditProc, 1, 0);
    } else {
        SetWindowText(g_modSearchEdit, _T(""));
        ShowWindow(g_modSearchEdit, SW_SHOW);
    }
    g_modSearch = false;

    // Only take focus if no EPoly tool is being taken over
    if (g_epolyOp < 0) SetFocus(g_panel);
}

static void ClosePanel() {
    if (!g_open) return;
    ExitModSearch();
    if (g_modSearchEdit) ShowWindow(g_modSearchEdit, SW_HIDE);
    EPolyAccept();  // commit the takeover preview
    // Always exit any active EPoly command mode on close
    {
        Interface* ipClose = GetCOREInterface();
        if (ipClose && ipClose->GetSelNodeCount() > 0) {
            EPoly* ep = FindEPoly(ipClose->GetSelNode(0));
            if (ep) {
                FPInterface* fp = (FPInterface*)ep;
                FPValue d;
                fp->Invoke(epfn_exit_command_modes, d);
                fp->Invoke(epfn_close_popup_dialog, d);
            }
        }
    }
    g_epolyOp = -1;
    g_epolyFP = nullptr;
    g_epolyPreview = false;

    KillTimer(g_panel, 1);
    KillActiveEdit(false);
    if (g_lmbDragging) { g_lmbDragging = false; g_lmbDragIdx = -1; ReleaseCapture(); theHold.Cancel(); HideDragTip(); }
    g_visEditMode = false;
    g_hoverParam = -1;
    g_edits.clear();
    g_groups.clear();
    if (g_btnLeft)  ShowWindow(g_btnLeft, SW_HIDE);
    if (g_btnRight) ShowWindow(g_btnRight, SW_HIDE);
    if (g_favWnd)   { KillTimer(g_favWnd, 1); DestroyFavEdits(); ShowWindow(g_favWnd, SW_HIDE); }
    FadeOut(g_panel);
    if (g_toolTip) ShowWindow(g_toolTip, SW_HIDE);
    g_open = false;
    g_hoverClose = false;
    g_hoverModStack = false;
    g_nodeHandle = 0;
    g_nodeName.clear();
    EnableAccelerators();
    Interface* ip = GetCOREInterface();
    if (ip) SetFocus(ip->GetMAXHWnd());
}

static void TogglePanel() {
    if (g_open) ClosePanel(); else OpenPanel();
}

// ── Action system ───────────────────────────────────────────────
class PPAction : public ActionItem {
public:
    int   GetId() override { return kToggleId; }
    BOOL  ExecuteAction() override { TogglePanel(); return TRUE; }
    void  GetButtonText(MSTR& t) override { t = PPARAM_NAME; }
    void  GetMenuText(MSTR& t) override { t = _T("Toggle PowerParams Panel"); }
    void  GetDescriptionText(MSTR& t) override { t = _T("Show/hide PowerParams floating parameter panel"); }
    void  GetCategoryText(MSTR& t) override { t = PPARAM_NAME; }
    BOOL  IsChecked() override { return g_open; }
    BOOL  IsItemVisible() override { return TRUE; }
    BOOL  IsEnabled() override { return TRUE; }
    void  DeleteThis() override {}
};
class PPActionCB : public ActionCallback {
public:
    BOOL ExecuteAction(int id) override {
        if (id == kToggleId) { TogglePanel(); return TRUE; }
        return FALSE;
    }
};

static PPAction   g_action;
static PPActionCB g_actionCB;

static ActionTable* MakeActionTable() {
    static ActionTable table(kTableId, kContextId, TSTR(PPARAM_NAME));
    static bool init = false;
    if (!init) { table.AppendOperation(&g_action); init = true; }
    return &table;
}

// ── GUP ─────────────────────────────────────────────────────────
class PowerParamsGUP : public GUP {
public:
    DWORD Start() override {
        LoadSettings();
        LoadModuleFlags();

        WNDCLASSEX wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc  = PanelProc;
        wc.hInstance     = hInstance;
        wc.lpszClassName = kWndClass;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassEx(&wc);

        g_font     = CreateFont(kFontPx,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,_T("Segoe UI"));
        g_fontBold = CreateFont(kFontHdr,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,_T("Segoe UI"));
        g_brEdit    = CreateSolidBrush(kEditBg);
        g_brEditFoc = CreateSolidBrush(kEditFocus);

        g_panel = CreateWindowEx(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_LAYERED,
            kWndClass, nullptr, WS_POPUP|WS_CLIPCHILDREN, 0,0,1,1, nullptr, nullptr, hInstance, nullptr);
        SetLayeredWindowAttributes(g_panel, 0, 255, LWA_ALPHA);

        // Button strip windows (separate floating windows for side buttons)
        WNDCLASSEX wcB = {};
        wcB.cbSize = sizeof(wcB);
        wcB.lpfnWndProc = BtnStripProc;
        wcB.hInstance = hInstance;
        wcB.lpszClassName = kBtnStripClass;
        wcB.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassEx(&wcB);
        g_btnLeft  = CreateWindowEx(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED,
            kBtnStripClass, nullptr, WS_POPUP, 0,0,1,1, nullptr, nullptr, hInstance, nullptr);
        SetLayeredWindowAttributes(g_btnLeft, 0, 255, LWA_ALPHA);
        g_btnRight = CreateWindowEx(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,
            kBtnStripClass, nullptr, WS_POPUP, 0,0,1,1, nullptr, nullptr, hInstance, nullptr);

        // Favorites strip window
        WNDCLASSEX wcF = {};
        wcF.cbSize = sizeof(wcF);
        wcF.lpfnWndProc = FavStripProc;
        wcF.hInstance = hInstance;
        wcF.lpszClassName = kFavClass;
        wcF.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassEx(&wcF);
        g_favWnd = CreateWindowEx(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED,
            kFavClass, nullptr, WS_POPUP | WS_CLIPCHILDREN, 0,0,1,1, nullptr, nullptr, hInstance, nullptr);
        SetLayeredWindowAttributes(g_favWnd, 0, 255, LWA_ALPHA);

        // Tool name tooltip window
        WNDCLASSEX wc2 = {};
        wc2.cbSize = sizeof(wc2);
        wc2.lpfnWndProc = ToolTipProc;
        wc2.hInstance = hInstance;
        wc2.lpszClassName = kToolTipClass;
        wc2.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassEx(&wc2);
        g_toolTip = CreateWindowEx(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,
            kToolTipClass, nullptr, WS_POPUP, 0,0,1,1, nullptr, nullptr, hInstance, nullptr);

        IActionManager* am = GetCOREInterface()->GetActionManager();
        if (am) am->ActivateActionTable(&g_actionCB, kTableId);
        g_mouseHook = SetWindowsHookEx(WH_MOUSE, MouseHookProc, nullptr, GetCurrentThreadId());
        g_kbHook    = SetWindowsHookEx(WH_KEYBOARD, KbHookProc, nullptr, GetCurrentThreadId());

        PowerShader::Init(hInstance, g_lightTheme);
        ModStack::Init(hInstance, g_lightTheme);

        // Extract MCR from resource to userMacros dir (auto-loaded by Max)
        {
            HRSRC hr = FindResource(hInstance, MAKEINTRESOURCE(101), RT_RCDATA);
            if (hr) {
                HGLOBAL hg = LoadResource(hInstance, hr);
                DWORD sz = SizeofResource(hInstance, hr);
                if (hg && sz) {
                    const char* data = static_cast<const char*>(LockResource(hg));
                    MSTR macroDir = GetCOREInterface()->GetDir(APP_USER_MACROS_DIR);
                    std::wstring outPath = std::wstring(macroDir.data()) + L"\\CloneTools-FlowState_Config.mcr";
                    // Always overwrite to keep in sync with plugin version
                    FILE* f = _wfopen(outPath.c_str(), L"wb");
                    if (f) { fwrite(data, 1, sz, f); fclose(f); }
                    // Force Max to load it now (GUPs load after macroscript scan)
                    std::wstring loadScript = L"macros.load \"" + std::wstring(macroDir.data()) + L"\" \"CloneTools-FlowState_Config.mcr\"";
                    ExecuteMAXScriptScript(loadScript.c_str(), MAXScript::ScriptSource::NotSpecified);
                }
            }
        }

        return GUPRESULT_KEEP;
    }

    void Stop() override {
        ModStack::Shutdown();
        PowerShader::Shutdown();
        if (g_kbHook)    { UnhookWindowsHookEx(g_kbHook);    g_kbHook = nullptr; }
        if (g_mouseHook) { UnhookWindowsHookEx(g_mouseHook); g_mouseHook = nullptr; }
        ClosePanel();
        if (g_btnLeft)  { DestroyWindow(g_btnLeft);  g_btnLeft = nullptr; }
        if (g_btnRight) { DestroyWindow(g_btnRight); g_btnRight = nullptr; }
        if (g_favWnd)   { DestroyFavEdits(); DestroyWindow(g_favWnd); g_favWnd = nullptr; }
        if (g_toolTip)  { DestroyWindow(g_toolTip); g_toolTip = nullptr; }
        if (g_panel)    { DestroyWindow(g_panel);  g_panel = nullptr; }
        if (g_font)     { DeleteObject(g_font);     g_font = nullptr; }
        if (g_fontBold) { DeleteObject(g_fontBold); g_fontBold = nullptr; }
        if (g_brEdit)   { DeleteObject(g_brEdit);   g_brEdit = nullptr; }
        if (g_brEditFoc){ DeleteObject(g_brEditFoc); g_brEditFoc = nullptr; }
        UnregisterClass(kWndClass, hInstance);
    }

    void      DeleteThis() override { delete this; }
    DWORD_PTR Control(DWORD) override { return 0; }
};

// ── ClassDesc ───────────────────────────────────────────────────
class PPClassDesc : public ClassDesc2 {
public:
    int          IsPublic() override              { return TRUE; }
    void*        Create(BOOL) override            { return new PowerParamsGUP(); }
    const TCHAR* ClassName() override             { return PPARAM_NAME; }
    const TCHAR* NonLocalizedClassName() override { return PPARAM_NAME; }
    SClass_ID    SuperClassID() override          { return GUP_CLASS_ID; }
    Class_ID     ClassID() override               { return PPARAM_CLASS_ID; }
    const TCHAR* Category() override              { return PPARAM_CATEGORY; }
    const TCHAR* InternalName() override          { return PPARAM_NAME; }
    HINSTANCE    HInstance() override              { return hInstance; }
    int           NumActionTables() override      { return 1; }
    ActionTable*  GetActionTable(int) override    { return MakeActionTable(); }
};

static PPClassDesc ppDesc;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, ULONG fdwReason, LPVOID) {
    if (fdwReason == DLL_PROCESS_ATTACH) { hInstance = hinstDLL; DisableThreadLibraryCalls(hinstDLL); }
    return TRUE;
}

__declspec(dllexport) const TCHAR* LibDescription()   { return PPARAM_NAME; }
__declspec(dllexport) int          LibNumberClasses()  { return 1; }
__declspec(dllexport) ClassDesc*   LibClassDesc(int i) { return i == 0 ? &ppDesc : nullptr; }
__declspec(dllexport) ULONG        LibVersion()        { return VERSION_3DSMAX; }
__declspec(dllexport) int          LibInitialize()     { return TRUE; }
__declspec(dllexport) int          LibShutdown()       { return TRUE; }
