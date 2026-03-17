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
static const int kPad       = 12;
static const int kFontPx    = 12;
static const int kFontHdr   = 14;
static const int kLineH     = 22;
static const int kHeaderH   = 42;
static const int kGroupGap  = 8;
static const int kEditW     = 90;
static const int kEditH     = 18;
static const int kMaxParams = 40;
static const int kMinW      = 240;
static const int kRefreshMs = 500;

static const COLORREF kBg        = RGB(28, 28, 32);
static const COLORREF kBorder    = RGB(55, 55, 65);
static const COLORREF kAccent    = RGB(100, 170, 255);
static const COLORREF kGroupClr  = RGB(130, 145, 170);
static const COLORREF kLabelClr  = RGB(160, 160, 170);
static const COLORREF kValueClr  = RGB(255, 255, 255);
static const COLORREF kEditBg    = RGB(38, 38, 44);
static const COLORREF kEditFocus = RGB(50, 50, 58);
static const COLORREF kCloseHov  = RGB(200, 60, 60);
static const COLORREF kPinClr    = RGB(255, 200, 60);

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
static const FallbackOpParam kChamferEdgeParams[] = { {_T("Amount"),(ParamID)ep_edge_chamfer,true}, {_T("Segments"),(ParamID)ep_edge_chamfer_segments,false} };
static const FallbackOpParam kChamferVertParams[] = { {_T("Amount"),(ParamID)ep_vertex_chamfer,true} };
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
        if (selLevel==EP_SL_VERTEX) { title=L"Chamfer Vertex"; count=1; return kChamferVertParams; }
        title=L"Chamfer Edge"; count=2; return kChamferEdgeParams;
    case epop_inset:            title=L"Inset";    count=2; return kInsetParams;
    case epop_outline:          title=L"Outline";  count=1; return kOutlineParams;
    default: count=0; return nullptr;
    }
}

// ── Data ────────────────────────────────────────────────────────
struct EditField {
    HWND         hwnd = nullptr;
    std::wstring label;
    std::wstring key;        // persistent ID "Group:Label"
    int          keyOrdinal = 0; // nth occurrence for duplicated keys
    IParamBlock2* pb  = nullptr;
    ParamID      id   = 0;
    ParamType2   type = (ParamType2)0;
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

static int          g_epolyOp          = -1;
static FPInterface* g_epolyFP          = nullptr;
static bool         g_epolyPreview     = false;
static bool         g_tryEPoly         = true;
static int          g_lastDetectedOp   = -2;  // remembered while op UI is live; reset when op UI disappears
static ULONG        g_nodeHandle       = 0;

static std::vector<EditField>   g_edits;
static std::vector<GroupHeader> g_groups;
static std::wstring             g_nodeName;
static POINT                    g_panelPos = {0,0}; // for relayout

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
static void TogglePanel(bool fresh = false);
static void ClosePanel(bool accept = true);
static void RefreshEdits(bool forceAll = false);
static void ApplyEdit(HWND h);
static void BuildLayout();

// ── Mouse hook — XButton2=panel, XButton1=pin ───────────────────
static LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wp, LPARAM lp) {
    if (nCode >= 0 && wp == WM_XBUTTONDOWN) {
        MSLLHOOKSTRUCT* ms = (MSLLHOOKSTRUCT*)lp;
        WORD xbutton = HIWORD(ms->mouseData);
        HWND fg = GetForegroundWindow();
        Interface* ip = GetCOREInterface();
        bool isMax = ip && (fg == ip->GetMAXHWnd() || IsChild(ip->GetMAXHWnd(), fg) || fg == g_panel);
        if (!isMax) goto pass;

        if (xbutton == XBUTTON2) {
            PostMessage(g_panel, WM_PP_TOGGLE, 1, 0);
            return 1;
        }
        if (xbutton == XBUTTON1 && g_open) {
            // Check if cursor is over PowerParams panel
            POINT cpt; GetCursorPos(&cpt);
            RECT pr; GetWindowRect(g_panel, &pr);
            if (PtInRect(&pr, cpt))
                PostMessage(g_panel, WM_PP_PIN, 0, 0);      // pin/unpin
            else
                PostMessage(g_panel, WM_PP_ADDPARAM, 0, 0);  // grab spinner
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
    HWND hit = WindowFromPoint(pt);
    if (!hit) return L"";

    std::vector<HWND> chain;
    for (HWND w = hit; w && chain.size() < 24; w = GetParent(w))
        chain.push_back(w);

    Interface* ip = GetCOREInterface();
    if (!ip || ip->GetSelNodeCount() == 0) return L"";
    INode* node = ip->GetSelNode(0);
    if (!node) return L"";

    auto controlMatchesHit = [&](HWND mapHwnd, int ctrlID) -> bool {
        if (!mapHwnd || ctrlID <= 0) return false;
        HWND ctrl = GetDlgItem(mapHwnd, ctrlID);
        if (!ctrl) return false;
        for (HWND w : chain)
            if (w == ctrl || IsChild(ctrl, w) || IsChild(w, ctrl)) return true;
        return false;
    };

    // Search all param blocks on the selected node's modifier stack + base object.
    auto tryPB = [&](IParamBlock2* pb, const MCHAR* className) -> std::wstring {
        if (!pb) return L"";
        IParamMap2* map = pb->GetMap();
        if (!map) return L"";
        HWND mapHwnd = map->GetHWnd();
        if (!mapHwnd) return L"";

        for (int i = 0; i < pb->NumParams(); i++) {
            ParamID pid = pb->IndextoID(i);
            const ParamDef& def = pb->GetParamDef(pid);
            for (int c = 0; c < def.ctrl_count; c++) {
                if (controlMatchesHit(mapHwnd, def.ctrl_IDs[c])) {
                    std::wstring key = std::wstring(className) + L":" + (def.int_name ? def.int_name : L"?");
                    return key;
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
        // EPoly param block
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

// ── EPoly detection ─────────────────────────────────────────────
static bool TryEPolyParams(Animatable* owner) {
    if (!owner) return false;
    EPoly* ep = (EPoly*)owner->GetInterface(EPOLY_INTERFACE);
    if (!ep) return false;

    IParamBlock2* pb = ep->getParamBlock();
    if (!pb) return false;

    FPInterface* fp = (FPInterface*)ep;
    FPValue lastOpVal, selLevelVal;
    fp->Invoke(epfn_get_last_operation, lastOpVal);
    fp->Invoke(epfn_get_epoly_sel_level, selLevelVal);
    int lastOp   = lastOpVal.i;
    int selLevel = selLevelVal.i;

    // Forget/reject repeating the same destructive operation until a new one appears.
    if (lastOp == g_lastDetectedOp) return false;

    MSTR classNameM;
    owner->GetClassName(classNameM, false);
    std::wstring className = classNameM.data() ? classNameM.data() : L"EPoly";

    int fallbackCount = 0;
    std::wstring opTitle;
    const FallbackOpParam* fallback = LookupFallbackParams(lastOp, selLevel, fallbackCount, opTitle);
    if (opTitle.empty()) opTitle = className;

    GroupHeader gh;
    gh.title    = opTitle;
    gh.startIdx = (int)g_edits.size();

    // Try visible-control scan first (only works if command panel shows this rollup)
    IParamMap2* map = pb->GetMap();
    HWND mapHwnd = map ? map->GetHWnd() : nullptr;
    if (mapHwnd) {
        for (int i = 0; i < pb->NumParams() && (int)g_edits.size() < kMaxParams; i++) {
            ParamID pid = pb->IndextoID(i);
            const ParamDef& d = pb->GetParamDef(pid);
            if (d.type & TYPE_TAB) continue;
            if (!IsFloat(d.type) && !IsInt(d.type) && d.type != TYPE_BOOL) continue;
            if (!d.int_name || !d.int_name[0]) continue;

            bool visibleInMap = false;
            for (int c = 0; c < d.ctrl_count; c++) {
                if (d.ctrl_IDs[c] <= 0) continue;
                HWND ctrl = GetDlgItem(mapHwnd, d.ctrl_IDs[c]);
                if (ctrl && IsWindowVisible(ctrl)) { visibleInMap = true; break; }
            }
            if (!visibleInMap) continue;

            std::wstring key = opTitle + L":" + d.int_name;
            if (g_hidden.count(key)) continue;

            EditField ef;
            ef.label = MakeParamLabel(d.int_name);
            ef.key   = key;
            ef.keyOrdinal = GetNextKeyOrdinal(key);
            ef.pb    = pb;
            ef.id    = pid;
            ef.type  = d.type;
            g_edits.push_back(ef);
        }
    }

    // Fallback: hardcoded param IDs (works even if command panel is closed)
    if ((int)g_edits.size() == gh.startIdx && fallback && fallbackCount > 0) {
        gh.title = opTitle;
        for (int i = 0; i < fallbackCount && (int)g_edits.size() < kMaxParams; i++) {
            if (pb->IDtoIndex(fallback[i].pid) < 0) continue;
            std::wstring key = gh.title + L":" + fallback[i].label;
            if (g_hidden.count(key)) continue;

            EditField ef;
            ef.label = fallback[i].label;
            ef.key   = key;
            ef.keyOrdinal = GetNextKeyOrdinal(key);
            ef.pb    = pb;
            ef.id    = fallback[i].pid;
            ef.type  = (ParamType2)(fallback[i].isFloat ? TYPE_FLOAT : TYPE_INT);
            g_edits.push_back(ef);
        }
    }

    gh.count = (int)g_edits.size() - gh.startIdx;
    if (gh.count > 0) {
        g_epolyOp = lastOp;
        g_epolyFP = fp;
        g_lastDetectedOp = lastOp;
        g_groups.push_back(gh);
        return true;
    }
    return false;
}

static bool TryCollectLiveEPoly(Object* obj) {
    Object* walk = obj;
    while (walk && walk->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
        IDerivedObject* d = static_cast<IDerivedObject*>(walk);
        for (int m = 0; m < d->NumModifiers(); m++) {
            if (TryEPolyParams(d->GetModifier(m))) return true;
        }
        walk = d->GetObjRef();
    }
    return walk ? TryEPolyParams(walk) : false;
}

static bool HasLiveEPolyMap(Object* obj) {
    Object* walk = obj;
    while (walk && walk->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
        IDerivedObject* d = static_cast<IDerivedObject*>(walk);
        for (int m = 0; m < d->NumModifiers(); m++) {
            Animatable* owner = d->GetModifier(m);
            EPoly* ep = owner ? (EPoly*)owner->GetInterface(EPOLY_INTERFACE) : nullptr;
            if (!ep) continue;
            IParamBlock2* pb = ep->getParamBlock();
            IParamMap2* map = pb ? pb->GetMap() : nullptr;
            if (map && map->GetHWnd()) return true;
        }
        walk = d->GetObjRef();
    }
    if (!walk) return false;
    EPoly* ep = (EPoly*)walk->GetInterface(EPOLY_INTERFACE);
    if (!ep) return false;
    IParamBlock2* pb = ep->getParamBlock();
    IParamMap2* map = pb ? pb->GetMap() : nullptr;
    return map && map->GetHWnd();
}

// ── Gather params ───────────────────────────────────────────────
static void GatherParams() {
    g_groups.clear();
    g_edits.clear();
    g_nodeName.clear();
    g_nodeHandle = 0;
    g_epolyOp = -1;
    g_epolyFP = nullptr;

    Interface* ip = GetCOREInterface();
    if (!ip || ip->GetSelNodeCount() == 0) return;
    INode* node = ip->GetSelNode(0);
    if (!node) return;
    const MCHAR* nn = node->GetName();
    g_nodeName = nn ? nn : L"";
    g_nodeHandle = node->GetHandle();

    Object* obj = node->GetObjectRef();
    if (!obj) return;

    // ── One-shot EPoly operation detection ──────────────────────
    if (g_tryEPoly) TryCollectLiveEPoly(obj);

    // ── Generic: modifiers + base object (always runs) ──────────
    // Collect modifiers
    Object* walkObj = node->GetObjectRef();
    while (walkObj && walkObj->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
        IDerivedObject* d = static_cast<IDerivedObject*>(walkObj);
        for (int m = 0; m < d->NumModifiers(); m++) {
            Modifier* mod = d->GetModifier(m);
            if (!mod) continue;
            // Skip Edit Poly modifier in generic — handled by EPoly path or XButton1
            if (mod->GetInterface(EPOLY_INTERFACE)) continue;
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
    // Collect base object (skip Editable Poly — too many params, use XButton1)
    if (walkObj && !walkObj->GetInterface(EPOLY_INTERFACE)) {
        GroupHeader gh;
        MSTR cn; walkObj->GetClassName(cn, false);
        const MCHAR* p = cn.data();
        gh.title    = p ? p : L"Object";
        gh.startIdx = (int)g_edits.size();
        int tot = 0;
        for (int b = 0; b < walkObj->NumParamBlocks(); b++)
            CollectParams(walkObj->GetParamBlock(b), gh.title, tot);
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

// ── EPoly preview ───────────────────────────────────────────────
static void EPolyPreviewBegin() {
    if (g_epolyOp < 0 || !g_epolyFP || g_epolyPreview) return;
    ExecuteMAXScriptScript(_T("max undo"), MAXScript::ScriptSource::NotSpecified, TRUE);
    FPParams prms(1, TYPE_ENUM, g_epolyOp);
    FPValue result;
    g_epolyFP->Invoke(epfn_preview_begin, result, &prms);
    g_epolyPreview = true;
    FPValue dummy;
    g_epolyFP->Invoke(epfn_preview_invalidate, dummy);
    Interface* ip = GetCOREInterface();
    if (ip) ip->RedrawViews(ip->GetTime());
}
static void EPolyPreviewUpdate() {
    if (!g_epolyPreview || !g_epolyFP) return;
    FPValue dummy;
    g_epolyFP->Invoke(epfn_preview_invalidate, dummy);
    Interface* ip = GetCOREInterface();
    if (ip) ip->RedrawViews(ip->GetTime());
}
static void EPolyPreviewAccept() {
    if (!g_epolyPreview || !g_epolyFP) return;
    FPValue dummy;
    g_epolyFP->Invoke(epfn_preview_accept, dummy);
    g_epolyPreview = false;
}
static void EPolyPreviewCancel() {
    if (!g_epolyPreview || !g_epolyFP) return;
    FPValue dummy;
    g_epolyFP->Invoke(epfn_preview_cancel, dummy);
    g_epolyPreview = false;
}

// ── Value formatting ────────────────────────────────────────────
static void FormatValue(const EditField& ef, TimeValue t, TCHAR* buf, int len) {
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
        if (!ef.hwnd || !ef.pb) continue;
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
        if (!ef.pb) return;
        TCHAR txt[256];
        GetWindowText(h, txt, 256);
        if (g_epolyOp >= 0 && !g_epolyPreview) EPolyPreviewBegin();
        theHold.Suspend();
        if (IsFloat(ef.type))       ef.pb->SetValue(ef.id, t, (float)_wtof(txt));
        else if (ef.type==TYPE_BOOL) ef.pb->SetValue(ef.id, t, (_wcsicmp(txt,_T("On"))==0||_wtoi(txt)!=0)?1:0);
        else                        ef.pb->SetValue(ef.id, t, _wtoi(txt));
        theHold.Resume();
        if (g_epolyPreview) EPolyPreviewUpdate();
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
        if (wp == VK_ESCAPE)  { ClosePanel(false); return 0; }
        if (wp == VK_TAB)     { SetFocus(GetNextDlgTabItem(g_panel, h, GetKeyState(VK_SHIFT)<0)); return 0; }
        break;
    case WM_CHAR:
        if (wp == VK_RETURN || wp == VK_ESCAPE) return 0;
        break;
    case WM_MOUSEWHEEL: {
        if (!ef || !ef->pb) break;
        Interface* ip = GetCOREInterface();
        if (!ip) break;
        TimeValue t = ip->GetTime();
        float step = (float)GET_WHEEL_DELTA_WPARAM(wp) / 120.0f;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (g_epolyOp >= 0 && !g_epolyPreview) EPolyPreviewBegin();
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
        if (g_epolyPreview)  EPolyPreviewUpdate();
        else                 NotifyParamChanged();
        RefreshEdits(true);
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (ef && ef->type == TYPE_BOOL && ef->pb) {
            Interface* ip = GetCOREInterface();
            if (!ip) break;
            TimeValue t = ip->GetTime();
            if (g_epolyOp >= 0 && !g_epolyPreview) EPolyPreviewBegin();
            theHold.Suspend();
            ef->pb->SetValue(ef->id, t, ef->pb->GetInt(ef->id, t) ? 0 : 1);
            theHold.Resume();
            if (g_epolyPreview)  EPolyPreviewUpdate();
            else                 NotifyParamChanged();
            RefreshEdits(true);
            return 0;
        }
        break;
    }
    return CallWindowProc(g_origEdit, h, msg, wp, lp);
}

// ── Paint ───────────────────────────────────────────────────────
static void PaintPanel(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc; GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HBITMAP old = (HBITMAP)SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(kBg); FillRect(mem, &rc, bg); DeleteObject(bg);
    RECT bar = {0,0,rc.right,3};
    HBRUSH ab = CreateSolidBrush(kAccent); FillRect(mem, &bar, ab); DeleteObject(ab);
    HPEN bp = CreatePen(PS_SOLID, 1, kBorder);
    SelectObject(mem, bp); SelectObject(mem, (HBRUSH)GetStockObject(NULL_BRUSH));
    Rectangle(mem, 0, 0, rc.right, rc.bottom); DeleteObject(bp);

    SetBkMode(mem, TRANSPARENT);
    int y = kPad + 3, x = kPad, rEdge = rc.right - kPad;

    SelectObject(mem, g_fontBold);
    SetTextColor(mem, kAccent);
    const std::wstring& hdrText = g_nodeName.empty() ? std::wstring(L"PowerParams") : g_nodeName;
    TextOut(mem, x, y, hdrText.c_str(), (int)hdrText.length());

    if (g_hoverClose) {
        HBRUSH hov = CreateSolidBrush(kCloseHov); FillRect(mem, &g_closeRect, hov); DeleteObject(hov);
    }
    SetTextColor(mem, kValueClr);
    RECT cr = g_closeRect;
    DrawText(mem, _T("\u00D7"), 1, &cr, DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    y += kFontHdr + 6;
    HPEN sep = CreatePen(PS_SOLID, 1, kBorder);
    SelectObject(mem, sep); MoveToEx(mem, x, y, nullptr); LineTo(mem, rEdge, y); DeleteObject(sep);
    y += 5;

    HWND focused = GetFocus();
    for (size_t gi = 0; gi < g_groups.size(); gi++) {
        const auto& gh = g_groups[gi];
        bool collapsed = g_collapsed.count(gh.title) > 0;

        // Group header with collapse indicator
        SelectObject(mem, g_fontBold);
        SetTextColor(mem, kGroupClr);
        std::wstring hdr = (collapsed ? L"\x25B8 " : L"\x25BE ") + gh.title;
        TextOut(mem, x, y, hdr.c_str(), (int)hdr.length());
        y += kLineH;

        if (!collapsed) {
            SelectObject(mem, g_font);
            for (int fi = gh.startIdx; fi < gh.startIdx + gh.count; fi++) {
                auto& ef = g_edits[fi];
                bool isPinned = g_pinned.count(ef.key) > 0;

                // Pin indicator
                if (isPinned) {
                    SetTextColor(mem, kPinClr);
                    TextOut(mem, x, y + 2, _T("\u2605"), 1);
                }

                SetTextColor(mem, kLabelClr);
                TextOut(mem, x + (isPinned ? 16 : 8), y + 2, ef.label.c_str(), (int)ef.label.length());

                if (ef.hwnd) {
                    RECT er; GetWindowRect(ef.hwnd, &er);
                    MapWindowPoints(HWND_DESKTOP, hwnd, (LPPOINT)&er, 2);
                    InflateRect(&er, 1, 1);
                    COLORREF bc = (focused == ef.hwnd) ? kAccent : kBorder;
                    HPEN ep = CreatePen(PS_SOLID, 1, bc);
                    SelectObject(mem, ep);
                    Rectangle(mem, er.left, er.top, er.right, er.bottom);
                    DeleteObject(ep);
                }
                y += kLineH;
            }
        }
        if (gi + 1 < g_groups.size()) y += kGroupGap;
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
        std::wstring hdr = L"\u25BE " + gh.title;
        SIZE sz; GetTextExtentPoint32(hdc, hdr.c_str(), (int)hdr.length(), &sz);
        if (sz.cx > maxTitle) maxTitle = sz.cx;
    }
    SIZE hdrSz; GetTextExtentPoint32(hdc, g_nodeName.c_str(), (int)g_nodeName.length(), &hdrSz);
    if (hdrSz.cx + 30 > maxTitle) maxTitle = hdrSz.cx + 30;
    ReleaseDC(g_panel, hdc);

    int contentW = maxLbl + 36 + kEditW;
    if (contentW < maxTitle) contentW = maxTitle;
    int panelW = contentW + kPad * 2 + 4;
    if (panelW < kMinW) panelW = kMinW;
    int editX = panelW - kPad - kEditW;

    g_closeRect = { panelW - kPad - 18, kPad, panelW - kPad, kPad + 18 };

    int y = 3 + kPad + kFontHdr + 6 + 1 + 5;
    for (size_t gi = 0; gi < g_groups.size(); gi++) {
        y += kLineH; // group header
        auto& gh = g_groups[gi];
        bool collapsed = g_collapsed.count(gh.title) > 0;

        if (!collapsed) {
            for (int fi = gh.startIdx; fi < gh.startIdx + gh.count; fi++) {
                auto& ef = g_edits[fi];
                DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL;
                if (ef.type == TYPE_BOOL) style |= ES_CENTER | ES_READONLY;
                else style |= ES_RIGHT;

                ef.hwnd = CreateWindowEx(0, _T("EDIT"), _T(""),
                    style, editX, y + 1, kEditW, kEditH,
                    g_panel, nullptr, hInstance, nullptr);
                SendMessage(ef.hwnd, WM_SETFONT, (WPARAM)g_font, TRUE);
                if (!g_origEdit) g_origEdit = (WNDPROC)GetWindowLongPtr(ef.hwnd, GWLP_WNDPROC);
                SetWindowLongPtr(ef.hwnd, GWLP_WNDPROC, (LONG_PTR)EditProc);
                SetProp(ef.hwnd, _T("WF"), (HANDLE)&ef);
                y += kLineH;
            }
        }
        if (gi + 1 < g_groups.size()) y += kGroupGap;
    }
    int panelH = y + kPad;
    if (panelH < 60) panelH = 60;

    RefreshEdits(true);
    SetWindowPos(g_panel, HWND_TOPMOST, g_panelPos.x, g_panelPos.y, panelW, panelH, SWP_NOACTIVATE);
    InvalidateRect(g_panel, nullptr, FALSE);
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
    int y = 3 + kPad + kFontHdr + 6 + 1 + 5;
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

    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        if (PtInRect(&g_closeRect, pt)) return HTCLIENT;
        if (pt.y < kHeaderH) return HTCAPTION;
        return HTCLIENT;
    }

    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
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

        // Click on group header = toggle collapse
        int gIdx = FindGroupAtY(pt.y);
        if (gIdx >= 0) {
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

    case WM_TIMER: RefreshEdits(); return 0;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { ClosePanel(false); return 0; }
        break;

    case WM_PP_TOGGLE:
        TogglePanel(wp != 0); return 0;

    case WM_PP_PIN: {
        int idx = FindParamAtCursor();
        if (idx >= 0) {
            const auto& key = g_edits[idx].key;
            if (g_pinned.count(key)) g_pinned.erase(key);
            else g_pinned.insert(key);
            SaveSettings();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_PP_ADDPARAM: {
        // XButton1 outside panel: detect spinner in Max UI, pin it
        std::wstring key = DetectSpinnerKey();
        if (!key.empty()) {
            g_pinned.insert(key);
            SaveSettings();
            // Re-gather to pick up the new pinned param
            GatherParams();
            BuildLayout();
        }
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// ── Open / Close ────────────────────────────────────────────────
static void OpenPanel() {
    GatherParams();
    // Always open — even if empty, user can add params via XButton1

    POINT pt; GetCursorPos(&pt);
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) }; GetMonitorInfo(hMon, &mi);

    int estH = 3 + kPad*2 + kFontHdr + 10 + (int)g_edits.size() * kLineH + (int)g_groups.size() * (kLineH + kGroupGap);
    if (estH < 60) estH = 60;
    int ox = pt.x - kMinW / 2, oy = pt.y - 20;
    if (ox + kMinW > mi.rcWork.right) ox = mi.rcWork.right - kMinW;
    if (oy + estH > mi.rcWork.bottom) oy = mi.rcWork.bottom - estH;
    if (ox < mi.rcWork.left) ox = mi.rcWork.left;
    if (oy < mi.rcWork.top)  oy = mi.rcWork.top;
    g_panelPos = { ox, oy };

    ShowWindow(g_panel, SW_SHOWNA);
    BuildLayout();
    SetTimer(g_panel, 1, kRefreshMs, nullptr);

    // Focus first visible edit
    for (auto& ef : g_edits) { if (ef.hwnd) { SetFocus(ef.hwnd); break; } }
    g_open = true;
}

static void ClosePanel(bool accept) {
    if (!g_open) return;
    KillTimer(g_panel, 1);
    if (accept) EPolyPreviewAccept(); else EPolyPreviewCancel();
    DestroyEdits();
    g_edits.clear();
    g_groups.clear();
    ShowWindow(g_panel, SW_HIDE);
    g_open = false;
    g_hoverClose = false;
    g_epolyOp = -1;
    g_epolyFP = nullptr;
    g_epolyPreview = false;
    g_tryEPoly = false;
    g_nodeHandle = 0;
    g_nodeName.clear();
    EnableAccelerators();
    Interface* ip = GetCOREInterface();
    if (ip) SetFocus(ip->GetMAXHWnd());
}

static void TogglePanel(bool fresh) {
    if (g_open) { ClosePanel(); return; }
    if (fresh) g_tryEPoly = true;
    OpenPanel();
}

// ── Action system ───────────────────────────────────────────────
class PPAction : public ActionItem {
public:
    int   GetId() override { return kToggleId; }
    BOOL  ExecuteAction() override { TogglePanel(true); return TRUE; }
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
        if (id == kToggleId) { TogglePanel(true); return TRUE; }
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

        IActionManager* am = GetCOREInterface()->GetActionManager();
        if (am) am->ActivateActionTable(&g_actionCB, kTableId);
        g_mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, hInstance, 0);

        return GUPRESULT_KEEP;
    }

    void Stop() override {
        if (g_mouseHook) { UnhookWindowsHookEx(g_mouseHook); g_mouseHook = nullptr; }
        ClosePanel();
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
