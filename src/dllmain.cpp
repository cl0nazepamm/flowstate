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
#include <string>
#include <vector>
#include <set>

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
static const int kEditH     = 20;
static const int kMaxParams = 50;
static const int kMinW      = 260;
static const int kRefreshMs = 500;
static const int kBtnH      = 22;
static const int kBtnGap    = 3;
static const int kMaxPanelH = 700;

// 3ds Max dark theme colors
static const COLORREF kBg        = RGB(56, 56, 56);
static const COLORREF kBorder    = RGB(42, 42, 42);
static const COLORREF kAccent    = RGB(38, 148, 168);   // teal
static const COLORREF kGroupClr  = RGB(190, 190, 190);
static const COLORREF kLabelClr  = RGB(195, 195, 195);
static const COLORREF kValueClr  = RGB(230, 230, 230);
static const COLORREF kEditBg    = RGB(42, 42, 42);
static const COLORREF kEditFocus = RGB(48, 58, 65);
static const COLORREF kCloseHov  = RGB(200, 60, 60);
static const COLORREF kPinClr    = RGB(255, 200, 60);
static const COLORREF kBtnBg     = RGB(68, 68, 68);
static const COLORREF kBtnHov    = RGB(80, 80, 80);
static const COLORREF kBtnAct    = RGB(38, 148, 168);   // active = teal

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
static const FallbackOpParam kExtrudeFaceParams[] = { {_T("Height"),(ParamID)ep_face_extrude_height,true} };
static const FallbackOpParam kExtrudeEdgeParams[] = { {_T("Height"),(ParamID)ep_edge_extrude_height,true}, {_T("Width"),(ParamID)ep_edge_extrude_width,true} };
static const FallbackOpParam kExtrudeVertParams[] = { {_T("Width"),(ParamID)ep_vertex_extrude_width,true}, {_T("Height"),(ParamID)ep_vertex_extrude_height,true} };
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
        if (selLevel==EP_SL_EDGE)   { title=L"Extrude Edge";   count=2; return kExtrudeEdgeParams; }
        if (selLevel==EP_SL_VERTEX) { title=L"Extrude Vertex"; count=2; return kExtrudeVertParams; }
        title=L"Extrude Face"; count=1; return kExtrudeFaceParams;
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
    HWND         hwnd = nullptr;
    std::wstring label;
    std::wstring key;
    int          keyOrdinal = 0;
    IParamBlock2* pb  = nullptr;
    ParamID      id   = 0;
    ParamType2   type = (ParamType2)0;
    int          logY = 0;   // logical Y before scroll
};

struct GroupHeader {
    std::wstring title;
    int startIdx = 0;
    int count    = 0;
};

// ── Globals ─────────────────────────────────────────────────────
static HWND     g_panel      = nullptr;
static HFONT    g_font       = nullptr;
static HFONT    g_fontBold   = nullptr;
static HBRUSH   g_brEdit     = nullptr;
static HBRUSH   g_brEditFoc  = nullptr;
static WNDPROC  g_origEdit   = nullptr;
static HHOOK    g_mouseHook  = nullptr;
static bool     g_open       = false;
static bool     g_hoverClose = false;
static RECT     g_closeRect  = {};

static const UINT WM_PP_TOGGLE   = WM_USER + 100;
static const UINT WM_PP_PIN      = WM_USER + 102;
static const UINT WM_PP_ADDPARAM = WM_USER + 103;

static ULONG        g_nodeHandle       = 0;

// EPoly preview state
static int          g_epolyOp       = -1;
static FPInterface* g_epolyFP       = nullptr;
static int          g_epolySelLevel = -1;
static bool         g_epolyPreview  = false;
static int          g_epolyPutSnap  = -1;     // frozen putCount snapshot
static bool         g_epolyToolWasLive = false; // tool was active when panel opened
static bool         g_epolyWasCancelled = false; // we cancelled Max's preview — skip undo in EPolyBegin
static int          g_lastKnownOp  = -1;      // last op we showed — detect changes

static bool     g_suppressClose = false;

// Context-aware tool system
enum ObjContext { CTX_NONE, CTX_EPOLY, CTX_SPLINE };
static ObjContext g_ctx = CTX_NONE;
static FPInterface* g_epolyForButtons = nullptr;
static SplineShape* g_splineForButtons = nullptr;

struct BtnDef { const TCHAR* label; int id; };

// EPoly buttons
static const BtnDef kEPolySubObj[] = {
    {_T("Vert"),1}, {_T("Edge"),2}, {_T("Bord"),3}, {_T("Face"),4}, {_T("Elem"),5}
};
static const BtnDef kEPolyOps[] = {
    {_T("Extrude"),epop_extrude}, {_T("Connect"),epop_connect_edges},
    {_T("Bridge"),epop_bridge_edge}, {_T("Chamfer"),epop_chamfer},
    {_T("Bevel"),epop_bevel}, {_T("Inset"),epop_inset},
    {_T("Outline"),epop_outline}, {_T("Remove"),epop_remove}
};

// Spline buttons
static const BtnDef kSplineSubObj[] = {
    {_T("Vert"),SS_VERTEX}, {_T("Seg"),SS_SEGMENT}, {_T("Spline"),SS_SPLINE}
};
static const BtnDef kSplineOps[] = {
    {_T("Refine"),ScmRefine}, {_T("Fillet"),ScmFillet},
    {_T("Chamfer"),ScmChamfer}, {_T("Outline"),ScmOutline},
    {_T("Connect"),ScmConnect}, {_T("Trim"),ScmTrim},
    {_T("Extend"),ScmExtend}, {_T("Boolean"),ScmUnion}
};
static int g_subObjY = 0;   // Y position of sub-object button row
static int g_opBtnY  = 0;   // Y position of operation button row
static int g_contentStartY = 0;  // where scrollable content begins

// Tool tip overlay (shows active EPoly tool name near cursor)
static HWND g_toolTip = nullptr;
static const TCHAR* kToolTipClass = _T("PPToolTip");

// Scroll
static int g_scrollY   = 0;
static int g_contentH  = 0;  // total content height
static int g_viewH     = 0;  // visible content area height


static std::vector<EditField>   g_edits;
static std::vector<GroupHeader> g_groups;
static std::wstring             g_nodeName;
static POINT                    g_panelPos = {0,0};
static bool                     g_freshOpen = false; // true during OpenPanel, prevents BuildLayout from overriding cursor pos

// ── Persistent settings ─────────────────────────────────────────
static std::set<std::wstring> g_collapsed;
static std::set<std::wstring> g_pinned;
static std::set<std::wstring> g_hidden;

static std::wstring GetCfgPath() {
    Interface* ip = GetCOREInterface();
    if (!ip) return L"";
    MSTR dirStr = ip->GetDir(APP_PLUGCFG_DIR);
    const MCHAR* dir = dirStr.data();
    if (!dir || !dir[0]) return L"";
    return std::wstring(dir) + L"\\PowerParams.cfg";
}

static void SaveSettings() {
    std::wstring path = GetCfgPath();
    if (path.empty()) return;
    FILE* f = _wfopen(path.c_str(), L"w");
    if (!f) return;
    for (auto& s : g_collapsed) fwprintf(f, L"C:%s\n", s.c_str());
    for (auto& s : g_pinned)    fwprintf(f, L"P:%s\n", s.c_str());
    for (auto& s : g_hidden)    fwprintf(f, L"H:%s\n", s.c_str());
    fclose(f);
}

static void LoadSettings() {
    g_collapsed.clear(); g_pinned.clear(); g_hidden.clear();
    std::wstring path = GetCfgPath();
    if (path.empty()) return;
    FILE* f = _wfopen(path.c_str(), L"r");
    if (!f) return;
    wchar_t line[512];
    while (fgetws(line, 512, f)) {
        size_t len = wcslen(line);
        while (len > 0 && (line[len-1]==L'\n'||line[len-1]==L'\r')) line[--len]=0;
        if (len < 3 || line[1] != L':') continue;
        std::wstring val(line + 2);
        switch (line[0]) {
        case L'C': g_collapsed.insert(val); break;
        case L'P': g_pinned.insert(val);    break;
        case L'H': g_hidden.insert(val);    break;
        }
    }
    fclose(f);
}

// ── Forward declarations ────────────────────────────────────────
static void TogglePanel();
static void ClosePanel();
static void RefreshEdits(bool forceAll = false);
static void ApplyEdit(HWND h);
static void BuildLayout();

// ── Mouse hook — XButton2=panel, XButton1=pin ───────────────────
static LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wp, LPARAM lp) {
    // Click outside panel = instant close
    if (nCode >= 0 && g_open && !g_suppressClose && !g_epolyPreview && (wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN || wp == WM_MBUTTONDOWN)) {
        MSLLHOOKSTRUCT* ms = (MSLLHOOKSTRUCT*)lp;
        RECT pr; GetWindowRect(g_panel, &pr);
        if (!PtInRect(&pr, ms->pt)) {
            PostMessage(g_panel, WM_USER + 101, 0, 0);
        }
    }
    if (nCode >= 0 && wp == WM_XBUTTONDOWN) {
        MSLLHOOKSTRUCT* ms = (MSLLHOOKSTRUCT*)lp;
        WORD xbutton = HIWORD(ms->mouseData);
        HWND fg = GetForegroundWindow();
        Interface* ip = GetCOREInterface();
        bool isMax = ip && (fg == ip->GetMAXHWnd() || IsChild(ip->GetMAXHWnd(), fg) || fg == g_panel);
        if (!isMax) goto pass;

        if (xbutton == XBUTTON2) {
            PostMessage(g_panel, WM_PP_TOGGLE, 0, 0);
            return 1;
        }
        if (xbutton == XBUTTON1 && g_open) {
            PostMessage(g_panel, WM_PP_ADDPARAM, 0, 0);
            return 1;
        }
    }
pass:
    return CallNextHookEx(g_mouseHook, nCode, wp, lp);
}

// ── Param helpers ───────────────────────────────────────────────
static bool IsFloat(ParamType2 t) { return t==TYPE_FLOAT||t==TYPE_ANGLE||t==TYPE_PCNT_FRAC||t==TYPE_WORLD||t==TYPE_COLOR_CHANNEL; }
static bool IsInt(ParamType2 t)   { return t==TYPE_INT||t==TYPE_TIMEVALUE||t==TYPE_RADIOBTN_INDEX||t==TYPE_INDEX; }

// ── Detect spinner under cursor → return its persistent key ─────
static std::wstring DetectSpinnerKey() {
    POINT pt; GetCursorPos(&pt);
    HWND hwnd = WindowFromPoint(pt);
    if (!hwnd) return L"";

    // Must be a Max spinner or edit control
    TCHAR cls[64]; GetClassName(hwnd, cls, 64);
    if (_tcscmp(cls, SPINNERWINDOWCLASS) != 0 && _tcscmp(cls, CUSTEDITWINDOWCLASS) != 0)
        return L"";

    int ctrlID = GetDlgCtrlID(hwnd);
    if (ctrlID <= 0) return L"";

    Interface* ip = GetCOREInterface();
    if (!ip || ip->GetSelNodeCount() == 0) return L"";
    INode* node = ip->GetSelNode(0);
    if (!node) return L"";

    // Match ctrl ID against all param blocks — no param map dependency
    auto tryPB = [&](IParamBlock2* pb, const MCHAR* className) -> std::wstring {
        if (!pb) return L"";
        for (int i = 0; i < pb->NumParams(); i++) {
            ParamID pid = pb->IndextoID(i);
            const ParamDef& def = pb->GetParamDef(pid);
            if (!def.int_name) continue;
            for (int c = 0; c < def.ctrl_count; c++) {
                if (def.ctrl_IDs[c] == ctrlID) {
                    return std::wstring(className) + L":" + def.int_name;
                }
            }
        }
        return L"";
    };

    Object* obj = node->GetObjectRef();
    while (obj && obj->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
        IDerivedObject* d = static_cast<IDerivedObject*>(obj);
        for (int m = 0; m < d->NumModifiers(); m++) {
            Modifier* mod = d->GetModifier(m);
            if (!mod) continue;
            MSTR cn; mod->GetClassName(cn, false);
            for (int b = 0; b < mod->NumParamBlocks(); b++) {
                std::wstring key = tryPB(mod->GetParamBlock(b), cn.data());
                if (!key.empty()) return key;
            }
        }
        obj = d->GetObjRef();
    }
    if (obj) {
        MSTR cn; obj->GetClassName(cn, false);
        for (int b = 0; b < obj->NumParamBlocks(); b++) {
            std::wstring key = tryPB(obj->GetParamBlock(b), cn.data());
            if (!key.empty()) return key;
        }
        EPoly* ep = (EPoly*)obj->GetInterface(EPOLY_INTERFACE);
        if (ep) {
            std::wstring key = tryPB(ep->getParamBlock(), cn.data());
            if (!key.empty()) return key;
        }
    }
    return L"";
}

// ── Find a param by persistent key on current selection ─────────
static bool FindParamByKey(INode* node, const std::wstring& key,
                           IParamBlock2*& outPB, ParamID& outID, ParamType2& outType,
                           int keyOrdinal = 0) {
    size_t sep = key.find(L':');
    if (sep == std::wstring::npos) return false;
    std::wstring wantClass = key.substr(0, sep);
    std::wstring wantParam = key.substr(sep + 1);
    int seen = 0;

    auto searchPB = [&](IParamBlock2* pb) -> bool {
        if (!pb) return false;
        for (int i = 0; i < pb->NumParams(); i++) {
            ParamID pid = pb->IndextoID(i);
            const ParamDef& def = pb->GetParamDef(pid);
            if (def.int_name && std::wstring(def.int_name) == wantParam) {
                if (seen == keyOrdinal) {
                    outPB = pb; outID = pid; outType = def.type;
                    return true;
                }
                ++seen;
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
                if (searchPB(mod->GetParamBlock(b))) return true;
        }
        obj = d->GetObjRef();
    }
    if (obj) {
        MSTR cn; obj->GetClassName(cn, false);
        if (std::wstring(cn.data()) == wantClass) {
            for (int b = 0; b < obj->NumParamBlocks(); b++)
                if (searchPB(obj->GetParamBlock(b))) return true;
            EPoly* ep = (EPoly*)obj->GetInterface(EPOLY_INTERFACE);
            if (ep && searchPB(ep->getParamBlock())) return true;
        }
    }
    return false;
}

static bool ResolveEditBinding(INode* node, EditField& ef) {
    IParamBlock2* pb = nullptr;
    ParamID pid = 0;
    ParamType2 ptype = (ParamType2)0;
    if (!FindParamByKey(node, ef.key, pb, pid, ptype, ef.keyOrdinal)) return false;
    ef.pb = pb;
    ef.id = pid;
    ef.type = ptype;
    return true;
}

static int GetNextKeyOrdinal(const std::wstring& key) {
    int ord = 0;
    for (const auto& ef : g_edits)
        if (ef.key == key) ++ord;
    return ord;
}

static void CollectParams(IParamBlock2* pb, const std::wstring& groupTitle, int& total) {
    if (!pb) return;
    int n = pb->NumParams();
    for (int i = 0; i < n && total < kMaxParams; i++) {
        ParamID pid = pb->IndextoID(i);
        const ParamDef& d = pb->GetParamDef(pid);
        if (d.type & TYPE_TAB) continue;
        if (!IsFloat(d.type) && !IsInt(d.type) && d.type != TYPE_BOOL) continue;

        std::wstring label = d.int_name ? d.int_name : L"?";
        std::wstring key = groupTitle + L":" + label;

        if (g_hidden.count(key)) continue;  // skip hidden

        EditField ef;
        ef.label = label;
        ef.key   = key;
        ef.keyOrdinal = GetNextKeyOrdinal(key);
        ef.pb    = pb;
        ef.id    = pid;
        ef.type  = d.type;
        g_edits.push_back(ef);
        total++;
    }
}


// ── Find EPoly interface on selected node ────────────────────────
static EPoly* FindEPoly(INode* node) {
    if (!node) return nullptr;
    Object* obj = node->GetObjectRef();
    while (obj && obj->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
        IDerivedObject* d = static_cast<IDerivedObject*>(obj);
        for (int m = 0; m < d->NumModifiers(); m++) {
            EPoly* ep = (EPoly*)d->GetModifier(m)->GetInterface(EPOLY_INTERFACE);
            if (ep) return ep;
        }
        obj = d->GetObjRef();
    }
    if (obj) return (EPoly*)obj->GetInterface(EPOLY_INTERFACE);
    return nullptr;
}

// ── EPoly preview helpers ───────────────────────────────────────
static bool g_epolySkipPutCheck = false;  // skip putCount check for button-triggered ops

static void EPolyBegin() {
    if (g_epolyOp < 0 || !g_epolyFP || g_epolyPreview) return;
    if (g_epolyPutSnap < 0) return;

    // Frozen snapshot check — skip for button-triggered ops (we know it's fresh)
    if (!g_epolySkipPutCheck) {
        int now = theHold.GetGlobalPutCount();
        if (now != g_epolyPutSnap) {
            g_epolyPutSnap = -1;
            return;
        }
    }
    g_epolySkipPutCheck = false;

    if (!g_epolyWasCancelled)
        ExecuteMAXScriptScript(_T("max undo"), MAXScript::ScriptSource::NotSpecified, TRUE);
    g_epolyWasCancelled = false;

    FPParams prms(1, TYPE_ENUM, g_epolyOp);
    FPValue r;
    g_epolyFP->Invoke(epfn_preview_begin, r, &prms);
    g_epolyPreview = true;
    FPValue d;
    g_epolyFP->Invoke(epfn_preview_invalidate, d);
    if (auto* ip = GetCOREInterface()) ip->RedrawViews(ip->GetTime());
    InvalidateRect(g_panel, nullptr, FALSE);
}

static void EPolyRefresh() {
    if (!g_epolyPreview || !g_epolyFP) return;
    FPValue d;
    g_epolyFP->Invoke(epfn_preview_invalidate, d);
    if (auto* ip = GetCOREInterface()) ip->RedrawViews(ip->GetTime());
}

static void EPolyAccept() {
    if (!g_epolyPreview || !g_epolyFP) return;
    FPValue d;
    g_epolyFP->Invoke(epfn_preview_accept, d);
    g_epolyPreview = false;
    g_epolyPutSnap = -1;  // allow fresh snapshot for next operation
}

static void EPolyCancel() {
    if (!g_epolyPreview || !g_epolyFP) return;
    FPValue d;
    g_epolyFP->Invoke(epfn_preview_cancel, d);
    g_epolyPreview = false;
    g_epolyPutSnap = -1;
}

// Remove the op group from panel
static void RemoveOpGroup() {
    g_epolyOp = -1;
    g_epolyFP = nullptr;
    g_epolySelLevel = -1;
    g_epolyPutSnap = -1;
    if (!g_groups.empty()) {
        int cnt = g_groups[0].count;
        int start = g_groups[0].startIdx;
        for (int i = start; i < start + cnt; i++) {
            if (g_edits[i].hwnd) { RemoveProp(g_edits[i].hwnd, _T("WF")); DestroyWindow(g_edits[i].hwnd); }
        }
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
    g_nodeName.clear();
    g_nodeHandle = 0;
    g_epolyOp = -1;
    g_epolyFP = nullptr;
    g_epolySelLevel = -1;
    g_epolyPreview = false;
    g_ctx = CTX_NONE;
    g_epolyForButtons = nullptr;
    g_splineForButtons = nullptr;
    // g_epolyToolWasLive is set by ExitActiveEPolyTool before GatherParams runs
    g_scrollY = 0;
    g_epolyPutSnap = -1;

    Interface* ip = GetCOREInterface();
    if (!ip || ip->GetSelNodeCount() == 0) return;
    INode* node = ip->GetSelNode(0);
    if (!node) return;
    const MCHAR* nn = node->GetName();
    g_nodeName = nn ? nn : L"";
    g_nodeHandle = node->GetHandle();

    Object* obj = node->GetObjectRef();
    if (!obj) return;

    // ── EPoly detection (robust) ────────────────────────────────
    // Rules:
    // - Show last-op params ONLY if nothing changed since the operation
    // - putCount snapshot is frozen on first detection, never re-snapshotted
    // - If putCount differs (selection change, any edit) → skip, generic only
    // - If same op was already handled (putSnap matches last handled) → skip
    {
        EPoly* ep = FindEPoly(node);
        if (ep) {
            g_ctx = CTX_EPOLY;
            g_epolyForButtons = (FPInterface*)ep;
            IParamBlock2* pb = ep->getParamBlock();
            FPInterface* fp = (FPInterface*)ep;

            if (pb && fp) {
                FPValue opVal, slVal;
                fp->Invoke(epfn_get_last_operation, opVal);
                fp->Invoke(epfn_get_epoly_sel_level, slVal);
                int lastOp = opVal.i, selLv = slVal.i;

                // Decide if we should show operation params
                bool showOp = false;
                int curPut = theHold.GetGlobalPutCount();

                if (g_epolyPutSnap < 0) {
                    // First detection — show if:
                    // 1. Tool was LIVE (caddy open, command mode active), OR
                    // 2. Operation CHANGED since last time (new op, even if tool exited)
                    bool fresh = g_epolyToolWasLive || (lastOp != g_lastKnownOp);
                    if (fresh) {
                        g_epolyPutSnap = curPut;
                        showOp = true;
                    }
                } else if (curPut == g_epolyPutSnap) {
                    // Re-open, nothing changed — show
                    showOp = true;
                }
                // else: putCount differs — selection/topology changed, skip

                if (showOp && lastOp >= 0) {
                    int cnt = 0; std::wstring title;
                    const FallbackOpParam* fb = LookupFallbackParams(lastOp, selLv, cnt, title);
                    if (fb && cnt > 0) {
                        MSTR cn; ((Animatable*)ep)->GetClassName(cn, false);
                        std::wstring cls = cn.data() ? cn.data() : L"EPoly";

                        GroupHeader gh;
                        gh.title = title;
                        gh.startIdx = (int)g_edits.size();
                        for (int i = 0; i < cnt; i++) {
                            std::wstring key = cls + L":" + fb[i].label;
                            if (g_hidden.count(key)) continue;
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
                            g_epolyOp = lastOp;
                            g_epolyFP = fp;
                            g_lastKnownOp = lastOp;
                            g_epolySelLevel = selLv;
                            // Enter preview immediately — live feedback from the start
                            FPValue pv;
                            fp->Invoke(epfn_preview_on, pv);
                            if (pv.i != 0) {
                                g_epolyPreview = true;
                            } else {
                                EPolyBegin();  // undo + preview_begin
                            }
                            g_groups.push_back(gh);
                        }
                    }
                }
            }
        }
    }

    // ── Spline detection + param collection ────────────────────
    if (g_ctx == CTX_NONE) {
        Object* walk = obj;
        while (walk && walk->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
            IDerivedObject* d = static_cast<IDerivedObject*>(walk);
            walk = d->GetObjRef();
        }
        if (walk && walk->ClassID() == splineShapeClassID) {
            g_ctx = CTX_SPLINE;
            SplineShape* ss = static_cast<SplineShape*>(walk);
            g_splineForButtons = ss;

            // Spline operation params — read live spinner values
            struct SpinDef { const TCHAR* label; int idx; };
            static const SpinDef kSpinDefs[] = {
                {_T("Weld"), 0}, {_T("CrossInsert"), 1},
                {_T("Fillet"), 2}, {_T("Chamfer"), 3}, {_T("Outline"), 4},
            };

            GroupHeader gh;
            gh.title = L"Spline Ops";
            gh.startIdx = (int)g_edits.size();

            for (auto& sd : kSpinDefs) {
                std::wstring key = L"SplineShape:" + std::wstring(sd.label);
                if (g_hidden.count(key)) continue;

                EditField ef;
                ef.label = sd.label;
                ef.key   = key;
                ef.pb    = nullptr;
                ef.id    = (ParamID)sd.idx;
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
            gh.title    = p ? p : L"Modifier";
            gh.startIdx = (int)g_edits.size();
            int tot = 0;
            for (int b = 0; b < mod->NumParamBlocks(); b++)
                CollectParams(mod->GetParamBlock(b), gh.title, tot);
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
        gh.title    = p ? p : L"Object";
        gh.startIdx = (int)g_edits.size();
        int tot = 0;
        for (int b = 0; b < walkObj->NumParamBlocks(); b++)
            CollectParams(walkObj->GetParamBlock(b), gh.title, tot);
        // Also collect EPoly-specific param block if present
        EPoly* ep = (EPoly*)walkObj->GetInterface(EPOLY_INTERFACE);
        if (ep) {
            IParamBlock2* epPB = ep->getParamBlock();
            bool alreadyCollected = false;
            for (int b = 0; b < walkObj->NumParamBlocks(); b++) {
                if (walkObj->GetParamBlock(b) == epPB) { alreadyCollected = true; break; }
            }
            if (!alreadyCollected) CollectParams(epPB, gh.title, tot);
        }
        gh.count = (int)g_edits.size() - gh.startIdx;
        if (gh.count > 0) g_groups.push_back(gh);
    }

    // ── Collect pinned params not already in panel ──────────────
    if (!g_pinned.empty() && node) {
        // Build set of keys already collected
        std::set<std::wstring> existing;
        for (auto& ef : g_edits) existing.insert(ef.key);

        GroupHeader pgh;
        pgh.title    = L"\u2605 Pinned";
        pgh.startIdx = (int)g_edits.size();

        for (auto& key : g_pinned) {
            if (existing.count(key)) continue;  // already shown
            if (g_hidden.count(key)) continue;

            IParamBlock2* pb = nullptr;
            ParamID pid = 0;
            ParamType2 ptype = (ParamType2)0;
            if (FindParamByKey(node, key, pb, pid, ptype)) {
                if (ptype & TYPE_TAB) continue;
                if (!IsFloat(ptype) && !IsInt(ptype) && ptype != TYPE_BOOL) continue;

                size_t sep = key.find(L':');
                EditField ef;
                ef.label = (sep != std::wstring::npos) ? key.substr(sep + 1) : key;
                ef.key   = key;
                ef.keyOrdinal = GetNextKeyOrdinal(key);
                ef.pb    = pb;
                ef.id    = pid;
                ef.type  = ptype;
                g_edits.push_back(ef);
            }
        }

        pgh.count = (int)g_edits.size() - pgh.startIdx;
        if (pgh.count > 0) g_groups.push_back(pgh);
    }
}


// ── Value formatting ────────────────────────────────────────────
// Spline params: use ISplineOps::GetUIParam
// The splineUIParam enum is empty in the header but the implementation
// may respond. We use indices based on the order in the UI.
static bool FormatSplineValue(const EditField& ef, TCHAR* buf, int len) {
    if (!g_splineForButtons) { swprintf(buf, len, _T("--")); return true; }
    ISplineOps* ops = (ISplineOps*)g_splineForButtons->GetInterface(I_SPLINEOPS);
    if (!ops) { swprintf(buf, len, _T("--")); return true; }
    float val = 0.0f;
    ops->GetUIParam((splineUIParam)(int)ef.id, val);
    swprintf(buf, len, _T("%.4g"), val);
    return true;
}

static bool ApplySplineValue(const EditField& ef, float val) {
    if (!g_splineForButtons) return false;
    ISplineOps* ops = (ISplineOps*)g_splineForButtons->GetInterface(I_SPLINEOPS);
    if (!ops) return false;
    ops->SetUIParam((splineUIParam)(int)ef.id, val);
    return true;
}

static void FormatValue(const EditField& ef, TimeValue t, TCHAR* buf, int len) {
    // Spline virtual params (no param block)
    if (!ef.pb && g_ctx == CTX_SPLINE) {
        if (FormatSplineValue(ef, buf, len)) return;
        swprintf(buf, len, _T("--"));
        return;
    }
    if (!ef.pb) { swprintf(buf, len, _T("--")); return; }
    if (IsFloat(ef.type))      swprintf(buf, len, _T("%.4g"), ef.pb->GetFloat(ef.id, t));
    else if (ef.type==TYPE_BOOL) swprintf(buf, len, _T("%s"), ef.pb->GetInt(ef.id, t)?_T("On"):_T("Off"));
    else                       swprintf(buf, len, _T("%d"), ef.pb->GetInt(ef.id, t));
}

static void RefreshEdits(bool forceAll) {
    Interface* ip = GetCOREInterface();
    if (!ip) return;
    if (ip->GetSelNodeCount() == 0) { ClosePanel(); return; }
    INode* node = ip->GetSelNode(0);
    if (!node) { ClosePanel(); return; }
    const MCHAR* nn = node ? node->GetName() : nullptr;
    std::wstring cur = nn ? nn : L"";
    ULONG handle = node->GetHandle();

    // Hard-close when context changes.
    if (handle != g_nodeHandle || cur != g_nodeName) { ClosePanel(); return; }

    TimeValue t = ip->GetTime();
    HWND focused = GetFocus();
    for (auto& ef : g_edits) {
        if (!ef.hwnd) continue;
        if (!ef.pb && g_ctx != CTX_SPLINE) continue;
        if (!forceAll && ef.hwnd == focused) continue;
        TCHAR buf[64];
        FormatValue(ef, t, buf, 64);
        SetWindowText(ef.hwnd, buf);
    }
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

static void ApplyEdit(HWND h) {
    Interface* ip = GetCOREInterface();
    if (!ip) return;
    if (ip->GetSelNodeCount() == 0) return;
    INode* node = ip->GetSelNode(0);
    if (!node) return;
    TimeValue t = ip->GetTime();
    for (auto& ef : g_edits) {
        if (ef.hwnd != h) continue;
        TCHAR txt[256];
        GetWindowText(h, txt, 256);

        // Spline virtual params
        if (!ef.pb && g_ctx == CTX_SPLINE) {
            ApplySplineValue(ef, (float)_wtof(txt));
            NotifyParamChanged();
            break;
        }

        if (!ef.pb) return;
        theHold.Suspend();
        if (IsFloat(ef.type))       ef.pb->SetValue(ef.id, t, (float)_wtof(txt));
        else if (ef.type==TYPE_BOOL) ef.pb->SetValue(ef.id, t, (_wcsicmp(txt,_T("On"))==0||_wtoi(txt)!=0)?1:0);
        else                        ef.pb->SetValue(ef.id, t, _wtoi(txt));
        theHold.Resume();
        if (g_epolyPreview) EPolyRefresh();
        else                NotifyParamChanged();
        break;
    }
}

// ── Edit subclass ───────────────────────────────────────────────
static LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    EditField* ef = (EditField*)GetProp(h, _T("WF"));
    switch (msg) {
    case WM_KEYDOWN:
        if (wp == VK_RETURN)  { ApplyEdit(h); RefreshEdits(true); return 0; }
        if (wp == VK_ESCAPE)  { EPolyCancel(); ClosePanel(); return 0; }
        if (wp == VK_TAB)     { SetFocus(GetNextDlgTabItem(g_panel, h, GetKeyState(VK_SHIFT)<0)); return 0; }
        break;
    case WM_CHAR:
        if (wp == VK_RETURN || wp == VK_ESCAPE) return 0;
        break;
    case WM_MOUSEWHEEL: {
        // If mouse is not over this edit, forward to panel for scrolling
        POINT mp; GetCursorPos(&mp);
        RECT er; GetWindowRect(h, &er);
        if (!PtInRect(&er, mp)) return SendMessage(g_panel, msg, wp, lp);
        if (!ef) break;
        // Spline virtual params — wheel adjusts via ISplineOps
        if (!ef->pb && g_ctx == CTX_SPLINE) {
            ISplineOps* ops = g_splineForButtons ? (ISplineOps*)g_splineForButtons->GetInterface(I_SPLINEOPS) : nullptr;
            if (ops) {
                float step = (float)GET_WHEEL_DELTA_WPARAM(wp) / 120.0f;
                bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                float cur = 0; ops->GetUIParam((splineUIParam)(int)ef->id, cur);
                float a = cur<0?-cur:cur;
                float sc = a>100.f?10.f:a>10.f?1.f:a>1.f?0.1f:0.01f;
                if (shift) sc *= 10.0f;
                if (ctrl)  sc *= 0.1f;
                ops->SetUIParam((splineUIParam)(int)ef->id, cur + step * sc);
                NotifyParamChanged();
                RefreshEdits(true);
            }
            return 0;
        }
        if (!ef->pb) break;
        Interface* ip = GetCOREInterface();
        if (!ip) break;
        TimeValue t = ip->GetTime();
        float step = (float)GET_WHEEL_DELTA_WPARAM(wp) / 120.0f;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        theHold.Suspend();
        if (IsFloat(ef->type)) {
            float cur = ef->pb->GetFloat(ef->id, t);
            float a = cur<0?-cur:cur;
            float sc = a>100.f?10.f:a>10.f?1.f:a>1.f?0.1f:0.01f;
            if (shift) sc *= 10.0f;
            if (ctrl)  sc *= 0.1f;
            ef->pb->SetValue(ef->id, t, cur + step * sc);
        } else if (ef->type == TYPE_BOOL) {
            ef->pb->SetValue(ef->id, t, ef->pb->GetInt(ef->id, t) ? 0 : 1);
        } else {
            int cur = ef->pb->GetInt(ef->id, t);
            int s = (int)step;
            if (shift) s *= 10;
            if (ctrl && s != 0) s = s>0?1:-1;
            if (s == 0) s = step>0?1:-1;
            ef->pb->SetValue(ef->id, t, cur + s);
        }
        theHold.Resume();
        if (g_epolyPreview) EPolyRefresh();
        else                NotifyParamChanged();
        RefreshEdits(true);
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (ef && ef->type == TYPE_BOOL && ef->pb) {
            Interface* ip = GetCOREInterface();
            if (!ip) break;
            TimeValue t = ip->GetTime();
            if (g_epolyOp >= 0 && !g_epolyPreview) EPolyBegin();
            theHold.Suspend();
            ef->pb->SetValue(ef->id, t, ef->pb->GetInt(ef->id, t) ? 0 : 1);
            theHold.Resume();
            if (g_epolyPreview) EPolyRefresh();
            else                NotifyParamChanged();
            RefreshEdits(true);
            return 0;
        }
        break;
    }
    return CallWindowProc(g_origEdit, h, msg, wp, lp);
}

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
    HBITMAP old = (HBITMAP)SelectObject(mem, bmp);

    // Background
    HBRUSH bg = CreateSolidBrush(kBg); FillRect(mem, &rc, bg); DeleteObject(bg);
    // Accent bar
    RECT bar = {0,0,rc.right,2};
    HBRUSH ab = CreateSolidBrush(kAccent); FillRect(mem, &bar, ab); DeleteObject(ab);
    // Border
    HPEN bp = CreatePen(PS_SOLID, 1, kBorder);
    SelectObject(mem, bp); SelectObject(mem, (HBRUSH)GetStockObject(NULL_BRUSH));
    Rectangle(mem, 0, 0, rc.right, rc.bottom); DeleteObject(bp);

    SetBkMode(mem, TRANSPARENT);
    int x = kPad, rEdge = rc.right - kPad;

    // Header
    int y = kPad + 2;
    SelectObject(mem, g_fontBold);
    SetTextColor(mem, kAccent);
    const std::wstring& hdrText = g_nodeName.empty() ? std::wstring(L"PowerParams") : g_nodeName;
    TextOut(mem, x, y, hdrText.c_str(), (int)hdrText.length());

    // Close button
    if (g_hoverClose) {
        HBRUSH hov = CreateSolidBrush(kCloseHov); FillRect(mem, &g_closeRect, hov); DeleteObject(hov);
    }
    SetTextColor(mem, kValueClr);
    RECT cr = g_closeRect;
    DrawText(mem, _T("\u00D7"), 1, &cr, DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    y += kFontHdr + 4;
    HPEN sep = CreatePen(PS_SOLID, 1, kBorder);
    SelectObject(mem, sep); MoveToEx(mem, x, y, nullptr); LineTo(mem, rEdge, y); DeleteObject(sep);
    y += 4;

    // Context-aware buttons
    if (g_ctx != CTX_NONE) {
        int curLevel = 0;
        Interface* ip = GetCOREInterface();
        if (ip) curLevel = ip->GetSubObjectLevel();

        if (g_ctx == CTX_EPOLY) {
            DrawBtnRow(mem, kEPolySubObj, 5, x, y, rEdge - x, curLevel, g_font);
            y += kBtnH + kBtnGap;
            DrawBtnRow(mem, kEPolyOps, 8, x, y, rEdge - x, -1, g_font);
        } else if (g_ctx == CTX_SPLINE) {
            DrawBtnRow(mem, kSplineSubObj, 3, x, y, rEdge - x, curLevel, g_font);
            y += kBtnH + kBtnGap;
            DrawBtnRow(mem, kSplineOps, 8, x, y, rEdge - x, -1, g_font);
        }
        y += kBtnH + 4;

        // Separator
        HPEN s2 = CreatePen(PS_SOLID, 1, kBorder);
        SelectObject(mem, s2); MoveToEx(mem, x, y, nullptr); LineTo(mem, rEdge, y); DeleteObject(s2);
        y += 4;
    }

    // Clip to content area for scrollable groups
    HRGN clipRgn = CreateRectRgn(0, g_contentStartY, rc.right, rc.bottom - 1);
    SelectClipRgn(mem, clipRgn);

    // Groups + params (scrolled)
    y = g_contentStartY - g_scrollY;
    HWND focused = GetFocus();
    for (size_t gi = 0; gi < g_groups.size(); gi++) {
        const auto& gh = g_groups[gi];
        bool collapsed = g_collapsed.count(gh.title) > 0;

        SelectObject(mem, g_fontBold);
        bool isOpGroup = (gi == 0 && g_epolyOp >= 0);
        SetTextColor(mem, isOpGroup ? kAccent : kGroupClr);
        std::wstring hdr = (collapsed ? L"\x25B8 " : L"\x25BE ") + gh.title;
        if (isOpGroup) hdr += L"  [\x2713 EXIT]  [\x2717 CANCEL]";
        TextOut(mem, x, y, hdr.c_str(), (int)hdr.length());
        y += kLineH;

        if (!collapsed) {
            SelectObject(mem, g_font);
            for (int fi = gh.startIdx; fi < gh.startIdx + gh.count; fi++) {
                auto& ef = g_edits[fi];
                bool isPinned = g_pinned.count(ef.key) > 0;
                if (isPinned) { SetTextColor(mem, kPinClr); TextOut(mem, x, y + 3, _T("\u2605"), 1); }
                SetTextColor(mem, kLabelClr);
                TextOut(mem, x + (isPinned ? 16 : 8), y + 3, ef.label.c_str(), (int)ef.label.length());

                if (ef.hwnd) {
                    RECT er; GetWindowRect(ef.hwnd, &er);
                    MapWindowPoints(HWND_DESKTOP, hwnd, (LPPOINT)&er, 2);
                    InflateRect(&er, 1, 1);
                    COLORREF bc = (focused == ef.hwnd) ? kAccent : kBorder;
                    HPEN ep2 = CreatePen(PS_SOLID, 1, bc);
                    SelectObject(mem, ep2); Rectangle(mem, er.left, er.top, er.right, er.bottom);
                    DeleteObject(ep2);
                }
                y += kLineH;
            }
        }
        if (gi + 1 < g_groups.size()) y += kGroupGap;
    }

    SelectClipRgn(mem, nullptr);
    DeleteObject(clipRgn);

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
    SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

// ── Build / Rebuild layout ──────────────────────────────────────
static void DestroyEdits() {
    for (auto& ef : g_edits) {
        if (ef.hwnd) { RemoveProp(ef.hwnd, _T("WF")); DestroyWindow(ef.hwnd); ef.hwnd = nullptr; }
    }
}

// ── Apply scroll offset to all edit controls ────────────────────
static void ApplyScroll(int panelW) {
    int editX = panelW - kPad - kEditW;
    for (auto& ef : g_edits) {
        if (!ef.hwnd) continue;
        int screenY = ef.logY - g_scrollY;
        bool vis = (screenY + kEditH > g_contentStartY && screenY < g_contentStartY + g_viewH);
        SetWindowPos(ef.hwnd, nullptr, editX, screenY + 1, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(ef.hwnd, vis ? SW_SHOW : SW_HIDE);
    }
}

static void BuildLayout() {
    DestroyEdits();

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
        std::wstring hdr = L"\u25BE " + gh.title + L"  [\x2713 EXIT]  [\x2717 CANCEL]";
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
    int editX = panelW - kPad - kEditW;

    g_closeRect = { panelW - kPad - 18, kPad, panelW - kPad, kPad + 18 };

    // Fixed header area
    int y = 2 + kPad + kFontHdr + 4 + 1 + 4;
    if (g_ctx != CTX_NONE) {
        g_subObjY = y;
        y += kBtnH + kBtnGap;
        g_opBtnY = y;
        y += kBtnH + 4 + 1 + 4;
    }
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
    if (panelH > maxH) panelH = maxH;
    if (panelH < 60) panelH = 60;
    g_viewH = panelH - g_contentStartY - kPad;

    // Clamp scroll
    int maxScroll = g_contentH - g_viewH;
    if (maxScroll < 0) maxScroll = 0;
    if (g_scrollY > maxScroll) g_scrollY = maxScroll;
    if (g_scrollY < 0) g_scrollY = 0;

    // Create edit controls at scrolled positions
    for (size_t gi = 0; gi < g_groups.size(); gi++) {
        auto& gh = g_groups[gi];
        bool collapsed = g_collapsed.count(gh.title) > 0;
        if (collapsed) continue;
        for (int fi = gh.startIdx; fi < gh.startIdx + gh.count; fi++) {
            auto& ef = g_edits[fi];
            int screenY = ef.logY - g_scrollY;
            bool vis = (screenY + kEditH > g_contentStartY && screenY < g_contentStartY + g_viewH);

            DWORD style = WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL;
            if (vis) style |= WS_VISIBLE;
            if (ef.type == TYPE_BOOL) style |= ES_CENTER | ES_READONLY;
            else style |= ES_RIGHT;

            ef.hwnd = CreateWindowEx(0, _T("EDIT"), _T(""),
                style, editX, screenY + 1, kEditW, kEditH,
                g_panel, nullptr, hInstance, nullptr);
            SendMessage(ef.hwnd, WM_SETFONT, (WPARAM)g_font, TRUE);
            if (!g_origEdit) g_origEdit = (WNDPROC)GetWindowLongPtr(ef.hwnd, GWLP_WNDPROC);
            SetWindowLongPtr(ef.hwnd, GWLP_WNDPROC, (LONG_PTR)EditProc);
            SetProp(ef.hwnd, _T("WF"), (HANDLE)&ef);
        }
    }

    // Keep current position if panel was dragged (but not on fresh open)
    if (!g_freshOpen) {
        RECT cur; GetWindowRect(g_panel, &cur);
        if (cur.right - cur.left > 1) { g_panelPos.x = cur.left; g_panelPos.y = cur.top; }
    }
    g_freshOpen = false;

    RefreshEdits(true);
    SetWindowPos(g_panel, HWND_TOPMOST, g_panelPos.x, g_panelPos.y, panelW, panelH, SWP_NOACTIVATE);
    InvalidateRect(g_panel, nullptr, FALSE);
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
static int FindParamAtCursor() {
    POINT pt; GetCursorPos(&pt);
    ScreenToClient(g_panel, &pt);
    for (size_t i = 0; i < g_edits.size(); i++) {
        if (!g_edits[i].hwnd) continue;
        RECT er; GetWindowRect(g_edits[i].hwnd, &er);
        MapWindowPoints(HWND_DESKTOP, g_panel, (LPPOINT)&er, 2);
        er.left = kPad; // extend hit area to full row
        if (pt.y >= er.top - 2 && pt.y <= er.bottom + 2) return (int)i;
    }
    return -1;
}

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

// ── Window proc ─────────────────────────────────────────────────
static LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT:      PaintPanel(hwnd); return 0;
    case WM_ERASEBKGND: return 1;

    case WM_USER + 101:
        if (g_open) ClosePanel();
        return 0;

    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        if (PtInRect(&g_closeRect, pt)) return HTCLIENT;
        if (pt.y < kHeaderH) return HTCAPTION;
        return HTCLIENT;
    }

    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        RECT wr; GetWindowRect(hwnd, &wr); int pw = wr.right - wr.left;

        // Context-aware button clicks
        if (g_ctx == CTX_EPOLY) {
            int subHit = HitBtnRow(kEPolySubObj, 5, g_subObjY, pw, pt);
            if (subHit >= 0) {
                Interface* ip = GetCOREInterface();
                if (ip) ip->SetSubObjectLevel(ip->GetSubObjectLevel() == subHit ? 0 : subHit);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            int opHit = HitBtnRow(kEPolyOps, 8, g_opBtnY, pw, pt);
            if (opHit >= 0 && g_epolyForButtons) {
                // Accept current preview before running new op
                EPolyAccept();

                // Execute the operation
                FPParams prms(1, TYPE_ENUM, opHit);
                FPValue r;
                g_epolyForButtons->Invoke(epfn_button_op, r, &prms);

                // Reset detection state so re-gather picks up the new op
                g_epolyOp = -1;
                g_epolyFP = nullptr;
                g_epolyPreview = false;
                g_epolyPutSnap = -1;
                g_lastKnownOp = -1;
                g_epolyToolWasLive = true;
                g_epolyWasCancelled = false;
                g_epolySkipPutCheck = true;  // we JUST ran it, skip putCount verify

                // Re-gather + rebuild to show new op params + enter preview
                GatherParams();
                BuildLayout();
                if (auto* ip = GetCOREInterface()) ip->RedrawViews(ip->GetTime());
                return 0;
            }
        } else if (g_ctx == CTX_SPLINE) {
            int subHit = HitBtnRow(kSplineSubObj, 3, g_subObjY, pw, pt);
            if (subHit >= 0) {
                Interface* ip = GetCOREInterface();
                if (ip) ip->SetSubObjectLevel(ip->GetSubObjectLevel() == subHit ? 0 : subHit);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            int opHit = HitBtnRow(kSplineOps, 8, g_opBtnY, pw, pt);
            if (opHit >= 0 && g_splineForButtons) {
                g_splineForButtons->StartCommandMode((splineCommandMode)opHit);
                if (auto* ip = GetCOREInterface()) ip->RedrawViews(ip->GetTime());
                return 0;
            }
        }
        if (PtInRect(&g_closeRect, pt)) { ClosePanel(); return 0; }

        // Ctrl+click on param = hide it
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            int idx = FindParamAtCursor();
            if (idx >= 0) {
                g_hidden.insert(g_edits[idx].key);
                SaveSettings();
                // Remove from edits and rebuild
                g_edits.erase(g_edits.begin() + idx);
                // Recalculate group indices
                int pos = 0;
                for (auto& gh : g_groups) { gh.startIdx = pos; pos += gh.count; }
                // Find which group lost a param
                for (auto& gh : g_groups) {
                    int end = gh.startIdx + gh.count;
                    if (idx >= gh.startIdx && idx < end) { gh.count--; break; }
                }
                // Fix startIdx for groups after the removed param
                for (size_t gi = 0; gi < g_groups.size(); gi++) {
                    if (g_groups[gi].startIdx > idx) g_groups[gi].startIdx--;
                }
                BuildLayout();
                return 0;
            }
        }

        // Shift+click on group header = unhide all params for that group
        if (GetKeyState(VK_SHIFT) & 0x8000) {
            int gIdx = FindGroupAtY(pt.y);
            if (gIdx >= 0) {
                const auto& title = g_groups[gIdx].title;
                std::wstring prefix = title + L":";
                // Remove all hidden entries starting with this group
                for (auto it = g_hidden.begin(); it != g_hidden.end(); ) {
                    if (it->compare(0, prefix.size(), prefix) == 0) it = g_hidden.erase(it);
                    else ++it;
                }
                SaveSettings();
                // Re-gather and rebuild to get the params back
                GatherParams();
                BuildLayout();
                return 0;
            }
        }

        // Click on group header
        int gIdx = FindGroupAtY(pt.y);
        if (gIdx >= 0) {
            // EPoly op group — EXIT (left half) or CANCEL (right half)
            if (gIdx == 0 && g_epolyOp >= 0) {
                // Detect which button: measure "EXIT" text width
                HDC hdc = GetDC(hwnd);
                SelectObject(hdc, g_fontBold);
                std::wstring exitTxt = L"  [\x2713 EXIT]";
                std::wstring hdrBase = L"\x25BE " + g_groups[0].title;
                SIZE baseSz, exitSz;
                GetTextExtentPoint32(hdc, hdrBase.c_str(), (int)hdrBase.length(), &baseSz);
                GetTextExtentPoint32(hdc, exitTxt.c_str(), (int)exitTxt.length(), &exitSz);
                ReleaseDC(hwnd, hdc);
                int exitEnd = kPad + baseSz.cx + exitSz.cx;
                if (pt.x < exitEnd)
                    EPolyDrop();      // EXIT = accept
                else
                    EPolyCancelDrop(); // CANCEL = revert
                return 0;
            }
            // Normal collapse toggle
            const auto& title = g_groups[gIdx].title;
            if (g_collapsed.count(title)) g_collapsed.erase(title);
            else g_collapsed.insert(title);
            SaveSettings();
            BuildLayout();
            return 0;
        }
        break;
    }

    case WM_RBUTTONDOWN: {
        // Right-click = clear ALL hidden params, re-gather everything
        if (!g_hidden.empty()) {
            g_hidden.clear();
            SaveSettings();
            GatherParams();
            BuildLayout();
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        bool over = PtInRect(&g_closeRect, pt) != 0;
        if (over != g_hoverClose) { g_hoverClose = over; InvalidateRect(hwnd, &g_closeRect, FALSE); }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_hoverClose) { g_hoverClose = false; InvalidateRect(hwnd, &g_closeRect, FALSE); }
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
        if (HIWORD(wp) == EN_SETFOCUS)  { DisableAccelerators(); InvalidateRect(hwnd, nullptr, FALSE); }
        if (HIWORD(wp) == EN_KILLFOCUS) { if (g_open) ApplyEdit((HWND)lp); EnableAccelerators(); InvalidateRect(hwnd, nullptr, FALSE); }
        break;

    case WM_MOUSEWHEEL: {
        // Panel-level scroll (forwarded from edits when mouse not over them)
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        g_scrollY -= delta / 120 * kLineH;
        int maxScroll = g_contentH - g_viewH;
        if (maxScroll < 0) maxScroll = 0;
        if (g_scrollY < 0) g_scrollY = 0;
        if (g_scrollY > maxScroll) g_scrollY = maxScroll;
        RECT wr; GetWindowRect(hwnd, &wr);
        ApplyScroll(wr.right - wr.left);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_TIMER: RefreshEdits(); return 0;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { ClosePanel(); return 0; }
        break;

    case WM_PP_TOGGLE:
        TogglePanel(); return 0;

    case WM_PP_ADDPARAM: {
        // Suppress close during detection (HWND_BOTTOM causes WA_INACTIVE)
        g_suppressClose = true;
        SetWindowPos(g_panel, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        std::wstring key = DetectSpinnerKey();
        SetWindowPos(g_panel, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        g_suppressClose = false;

        if (!key.empty() && !g_pinned.count(key)) {
            g_pinned.insert(key);
            SaveSettings();
            GatherParams();
            BuildLayout();
        }
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// ── Open / Close ────────────────────────────────────────────────
// Exit any active EPoly tool (Chamfer caddy, Extrude, etc.)
static void ExitActiveEPolyTool() {
    Interface* ip = GetCOREInterface();
    if (!ip || ip->GetSelNodeCount() == 0) return;

    EPoly* ep = FindEPoly(ip->GetSelNode(0));
    if (!ep) return;

    FPInterface* fp = (FPInterface*)ep;
    g_epolyWasCancelled = false;

    // Check if a tool/preview is active BEFORE we exit it
    FPValue modeVal, prevVal;
    fp->Invoke(epfn_get_command_mode, modeVal);
    fp->Invoke(epfn_preview_on, prevVal);
    g_epolyToolWasLive = (modeVal.i >= 0 || prevVal.i != 0);

    if (modeVal.i < 0 && prevVal.i == 0) return;  // nothing active

    FPValue d;
    if (prevVal.i != 0) {
        // Save sub-object selection before cancel
        ExecuteMAXScriptScript(
            _T("global __ppSel = #{}\n")
            _T("global __ppLvl = subObjectLevel\n")
            _T("try(if __ppLvl==1 do __ppSel=polyOp.getVertSelection $)catch()\n")
            _T("try(if __ppLvl==2 do __ppSel=polyOp.getEdgeSelection $)catch()\n")
            _T("try(if __ppLvl==4 or __ppLvl==5 do __ppSel=polyOp.getFaceSelection $)catch()\n"),
            MAXScript::ScriptSource::NotSpecified, TRUE);

        // Cancel preview — reverts mesh, param values stay in PB
        fp->Invoke(epfn_preview_cancel, d);
        g_epolyWasCancelled = true;

        // Restore selection after cancel
        ExecuteMAXScriptScript(
            _T("try(\n")
            _T("  subObjectLevel = __ppLvl\n")
            _T("  if __ppLvl==1 do polyOp.setVertSelection $ __ppSel\n")
            _T("  if __ppLvl==2 do polyOp.setEdgeSelection $ __ppSel\n")
            _T("  if __ppLvl==4 or __ppLvl==5 do polyOp.setFaceSelection $ __ppSel\n")
            _T(")catch()\n"),
            MAXScript::ScriptSource::NotSpecified, TRUE);
    }
    // Exit command mode + close caddy
    fp->Invoke(epfn_exit_command_modes, d);
    fp->Invoke(epfn_close_popup_dialog, d);
}

static void OpenPanel() {
    ExitActiveEPolyTool();
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

    int ox = pt.x - kMinW / 2, oy = pt.y;
    if (ox + kMinW > mi.rcWork.right) ox = mi.rcWork.right - kMinW;
    if (ox < mi.rcWork.left) ox = mi.rcWork.left;
    if (oy < mi.rcWork.top)  oy = mi.rcWork.top;
    // Don't clamp bottom here — BuildLayout handles max height + scroll
    g_panelPos = { ox, oy };
    g_freshOpen = true;

    ShowWindow(g_panel, SW_SHOWNA);
    BuildLayout();
    SetTimer(g_panel, 1, kRefreshMs, nullptr);

    // Focus first visible edit
    for (auto& ef : g_edits) { if (ef.hwnd) { SetFocus(ef.hwnd); break; } }
    g_open = true;
}

static void ClosePanel() {
    if (!g_open) return;
    EPolyAccept();  // commit preview if active
    // Don't reset g_epolyPutSnap here — keeps the frozen snapshot
    // so next open can check if anything changed
    g_epolyOp = -1;
    g_epolyFP = nullptr;
    g_epolySelLevel = -1;
    g_epolyPreview = false;

    KillTimer(g_panel, 1);
    DestroyEdits();
    g_edits.clear();
    g_groups.clear();
    ShowWindow(g_panel, SW_HIDE);
    if (g_toolTip) ShowWindow(g_toolTip, SW_HIDE);
    g_open = false;
    g_hoverClose = false;
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

        g_panel = CreateWindowEx(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,
            kWndClass, nullptr, WS_POPUP, 0,0,1,1, nullptr, nullptr, hInstance, nullptr);

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
        g_mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, hInstance, 0);

        return GUPRESULT_KEEP;
    }

    void Stop() override {
        if (g_mouseHook) { UnhookWindowsHookEx(g_mouseHook); g_mouseHook = nullptr; }
        ClosePanel();
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
