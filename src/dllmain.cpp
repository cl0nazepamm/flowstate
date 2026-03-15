#include <max.h>
#include <gup.h>
#include <iparamb2.h>
#include <plugapi.h>
#include <maxapi.h>
#include <modstack.h>
#include <object.h>
#include <actiontable.h>
#include <custcont.h>
#include <iepoly.h>
#include <maxscript/maxscript.h>
#include <hold.h>
#include <windowsx.h>
#include <string>
#include <vector>

#define WALKER_CLASS_ID  Class_ID(0x7A1CE200, 0x3B4D5E6F)
#define WALKER_NAME      _T("Walker")
#define WALKER_CATEGORY  _T("MCP")

extern HINSTANCE hInstance;
HINSTANCE hInstance = nullptr;

// ── Action IDs ──────────────────────────────────────────────────
static const ActionTableId   kTableId   = 0x7A1CE201;
static const ActionContextId kContextId = 0x7A1CE202;
static const int             kToggleId  = 1;

// ── Config ──────────────────────────────────────────────────────
static const TCHAR* kWndClass   = _T("WalkerPanel");
static const int kPad       = 12;
static const int kFontPx    = 12;
static const int kFontHdr   = 14;
static const int kLineH     = 22;
static const int kHeaderH   = 42;
static const int kGroupGap  = 8;
static const int kEditW     = 90;
static const int kEditH     = 18;
static const int kMaxParams = 24;
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

// ── EPoly operation → param mapping ─────────────────────────────
struct OpParam {
    const TCHAR* name;
    ParamID pid;
    bool isFloat;
};

// Connect
static const OpParam kConnectParams[] = {
    { _T("Segments"), (ParamID)ep_connect_edge_segments, false },
    { _T("Pinch"),    (ParamID)ep_connect_edge_pinch,    true  },
    { _T("Slide"),    (ParamID)ep_connect_edge_slide,    true  },
};
// Bridge
static const OpParam kBridgeParams[] = {
    { _T("Segments"), (ParamID)ep_bridge_segments,  false },
    { _T("Taper"),    (ParamID)ep_bridge_taper,     true  },
    { _T("Bias"),     (ParamID)ep_bridge_bias,      true  },
    { _T("Twist 1"),  (ParamID)ep_bridge_twist_1,   true  },
    { _T("Twist 2"),  (ParamID)ep_bridge_twist_2,   true  },
};
// Extrude face
static const OpParam kExtrudeFaceParams[] = {
    { _T("Height"), (ParamID)ep_face_extrude_height, true },
};
// Extrude edge
static const OpParam kExtrudeEdgeParams[] = {
    { _T("Height"), (ParamID)ep_edge_extrude_height, true },
    { _T("Width"),  (ParamID)ep_edge_extrude_width,  true },
};
// Extrude vertex
static const OpParam kExtrudeVertParams[] = {
    { _T("Width"),  (ParamID)ep_vertex_extrude_width,  true },
    { _T("Height"), (ParamID)ep_vertex_extrude_height, true },
};
// Bevel
static const OpParam kBevelParams[] = {
    { _T("Height"),  (ParamID)ep_bevel_height,  true  },
    { _T("Outline"), (ParamID)ep_bevel_outline, true  },
    { _T("Type"),    (ParamID)ep_bevel_type,    false },
};
// Chamfer (edge)
static const OpParam kChamferEdgeParams[] = {
    { _T("Amount"),   (ParamID)ep_edge_chamfer,          true  },
    { _T("Segments"), (ParamID)ep_edge_chamfer_segments, false },
};
// Chamfer (vertex)
static const OpParam kChamferVertParams[] = {
    { _T("Amount"), (ParamID)ep_vertex_chamfer, true },
};
// Inset
static const OpParam kInsetParams[] = {
    { _T("Amount"), (ParamID)ep_inset,      true  },
    { _T("Type"),   (ParamID)ep_inset_type, false },
};
// Outline
static const OpParam kOutlineParams[] = {
    { _T("Amount"), (ParamID)ep_outline, true },
};

static const OpParam* LookupOp(int op, int selLevel, int& count, std::wstring& title) {
    switch (op) {
    case epop_connect_edges:
        title = L"Connect"; count = 3; return kConnectParams;
    case epop_connect_vertices:
        title = L"Connect Verts"; count = 0; return nullptr;
    case epop_bridge_border:
    case epop_bridge_polygon:
    case epop_bridge_edge:
        title = L"Bridge"; count = 5; return kBridgeParams;
    case epop_extrude:
        if (selLevel == EP_SL_EDGE) {
            title = L"Extrude Edge"; count = 2; return kExtrudeEdgeParams;
        } else if (selLevel == EP_SL_VERTEX) {
            title = L"Extrude Vertex"; count = 2; return kExtrudeVertParams;
        } else {
            title = L"Extrude Face"; count = 1; return kExtrudeFaceParams;
        }
    case epop_bevel:
        title = L"Bevel"; count = 3; return kBevelParams;
    case epop_chamfer:
        if (selLevel == EP_SL_VERTEX) {
            title = L"Chamfer Vertex"; count = 1; return kChamferVertParams;
        } else {
            title = L"Chamfer Edge"; count = 2; return kChamferEdgeParams;
        }
    case epop_inset:
        title = L"Inset"; count = 2; return kInsetParams;
    case epop_outline:
        title = L"Outline"; count = 1; return kOutlineParams;
    default:
        count = 0; return nullptr;
    }
}

// ── Data ────────────────────────────────────────────────────────
struct EditField {
    HWND         hwnd = nullptr;
    std::wstring label;
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

static const UINT WM_WALKER_TOGGLE = WM_USER + 100;

// EPoly undo+reapply state
static int          g_epolyOp  = -1;       // last detected operation ID
static FPInterface* g_epolyFP  = nullptr;  // EPoly FP interface for re-executing

static std::vector<EditField>   g_edits;
static std::vector<GroupHeader> g_groups;
static std::wstring             g_nodeName;

// Forward declarations
static void TogglePanel();
static void ClosePanel();
static void RefreshEdits(bool forceAll = false);
static void ApplyEdit(HWND h);

// ── Mouse side-button hook ──────────────────────────────────────
static LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wp, LPARAM lp) {
    if (nCode >= 0 && wp == WM_XBUTTONDOWN) {
        MSLLHOOKSTRUCT* ms = (MSLLHOOKSTRUCT*)lp;
        WORD xbutton = HIWORD(ms->mouseData);
        if (xbutton == XBUTTON1 || xbutton == XBUTTON2) {
            HWND fg = GetForegroundWindow();
            Interface* ip = GetCOREInterface();
            if (ip && (fg == ip->GetMAXHWnd() || IsChild(ip->GetMAXHWnd(), fg) || fg == g_panel)) {
                PostMessage(g_panel, WM_WALKER_TOGGLE, 0, 0);
                return 1;
            }
        }
    }
    return CallNextHookEx(g_mouseHook, nCode, wp, lp);
}

// ── Param helpers ───────────────────────────────────────────────
static bool IsFloat(ParamType2 t) {
    return t==TYPE_FLOAT||t==TYPE_ANGLE||t==TYPE_PCNT_FRAC||t==TYPE_WORLD||t==TYPE_COLOR_CHANNEL;
}
static bool IsInt(ParamType2 t) {
    return t==TYPE_INT||t==TYPE_TIMEVALUE||t==TYPE_RADIOBTN_INDEX||t==TYPE_INDEX;
}

// ── Generic param collection (fallback) ─────────────────────────
static void CollectParams(IParamBlock2* pb, int& total) {
    if (!pb) return;
    int n = pb->NumParams();
    for (int i = 0; i < n && total < kMaxParams; i++) {
        ParamID pid = pb->IndextoID(i);
        const ParamDef& d = pb->GetParamDef(pid);
        if (d.type & TYPE_TAB) continue;
        if (!IsFloat(d.type) && !IsInt(d.type) && d.type != TYPE_BOOL) continue;

        EditField ef;
        ef.label = d.int_name ? d.int_name : L"?";
        ef.pb    = pb;
        ef.id    = pid;
        ef.type  = d.type;
        g_edits.push_back(ef);
        total++;
    }
}

// ── Try EPoly last-operation params ─────────────────────────────
static bool TryEPolyParams(Animatable* owner) {
    if (!owner) return false;

    FPInterface* fp = (FPInterface*)owner->GetInterface(EPOLY_INTERFACE);
    if (!fp) return false;

    // Call via FP dispatch — these virtuals are private
    FPValue lastOpVal, selLevelVal;
    fp->Invoke(epfn_get_last_operation, lastOpVal);
    fp->Invoke(epfn_get_epoly_sel_level, selLevelVal);
    int lastOp   = lastOpVal.i;
    int selLevel = selLevelVal.i;

    int paramCount = 0;
    std::wstring opTitle;
    const OpParam* params = LookupOp(lastOp, selLevel, paramCount, opTitle);
    if (!params || paramCount == 0) return false;

    // Find the param block containing these IDs
    IParamBlock2* pb = nullptr;
    for (int b = 0; b < owner->NumParamBlocks(); b++) {
        IParamBlock2* candidate = owner->GetParamBlock(b);
        if (candidate && candidate->IDtoIndex(params[0].pid) >= 0) {
            pb = candidate;
            break;
        }
    }
    if (!pb) return false;

    GroupHeader gh;
    gh.title    = opTitle;
    gh.startIdx = (int)g_edits.size();

    for (int i = 0; i < paramCount; i++) {
        if (pb->IDtoIndex(params[i].pid) < 0) continue;
        EditField ef;
        ef.label = params[i].name;
        ef.pb    = pb;
        ef.id    = params[i].pid;
        ef.type  = (ParamType2)(params[i].isFloat ? TYPE_FLOAT : TYPE_INT);
        g_edits.push_back(ef);
    }

    gh.count = (int)g_edits.size() - gh.startIdx;
    if (gh.count > 0) {
        // Only arm undo+reapply if we successfully gathered operation params
        g_epolyOp = lastOp;
        g_epolyFP = fp;
        g_groups.push_back(gh);
        return true;
    }
    return false;
}

// ── Gather params ───────────────────────────────────────────────
static void GatherParams() {
    g_groups.clear();
    g_edits.clear();
    g_nodeName.clear();
    g_epolyOp = -1;
    g_epolyFP = nullptr;

    Interface* ip = GetCOREInterface();
    if (!ip || ip->GetSelNodeCount() == 0) return;
    INode* node = ip->GetSelNode(0);
    if (!node) return;
    const MCHAR* nn = node->GetName();
    g_nodeName = nn ? nn : L"";

    Object* obj = node->GetObjectRef();
    if (!obj) return;

    // ── Priority: EPoly last-operation params ───────────────────
    // Check modifiers first (Edit Poly modifier)
    Object* walk = obj;
    while (walk && walk->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
        IDerivedObject* d = static_cast<IDerivedObject*>(walk);
        for (int m = 0; m < d->NumModifiers(); m++) {
            if (TryEPolyParams(d->GetModifier(m))) return;
        }
        walk = d->GetObjRef();
    }
    // Check base object (Editable Poly)
    if (walk && TryEPolyParams(walk)) return;

    // ── Fallback: generic all-params ────────────────────────────
    obj = node->GetObjectRef();
    while (obj && obj->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
        IDerivedObject* d = static_cast<IDerivedObject*>(obj);
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
                CollectParams(mod->GetParamBlock(b), tot);
            gh.count = (int)g_edits.size() - gh.startIdx;
            if (gh.count > 0) g_groups.push_back(gh);
        }
        obj = d->GetObjRef();
    }
    if (obj) {
        GroupHeader gh;
        MSTR cn; obj->GetClassName(cn, false);
        const MCHAR* p = cn.data();
        gh.title    = p ? p : L"Object";
        gh.startIdx = (int)g_edits.size();
        int tot = 0;
        for (int b = 0; b < obj->NumParamBlocks(); b++)
            CollectParams(obj->GetParamBlock(b), tot);
        gh.count = (int)g_edits.size() - gh.startIdx;
        if (gh.count > 0) g_groups.push_back(gh);
    }
}

// ── Value formatting ────────────────────────────────────────────
static void FormatValue(const EditField& ef, TimeValue t, TCHAR* buf, int len) {
    if (IsFloat(ef.type)) {
        swprintf(buf, len, _T("%.4g"), ef.pb->GetFloat(ef.id, t));
    } else if (ef.type == TYPE_BOOL) {
        swprintf(buf, len, _T("%s"), ef.pb->GetInt(ef.id, t) ? _T("On") : _T("Off"));
    } else {
        swprintf(buf, len, _T("%d"), ef.pb->GetInt(ef.id, t));
    }
}

// forceAll=true: update every field including focused (used after wheel/toggle)
// forceAll=false: skip focused field (used by timer, so we don't clobber typing)
static void RefreshEdits(bool forceAll) {
    Interface* ip = GetCOREInterface();
    if (!ip) return;
    if (ip->GetSelNodeCount() == 0) { ClosePanel(); return; }
    INode* node = ip->GetSelNode(0);
    const MCHAR* nn = node ? node->GetName() : nullptr;
    std::wstring cur = nn ? nn : L"";
    if (cur != g_nodeName) { ClosePanel(); return; }

    TimeValue t = ip->GetTime();
    HWND focused = GetFocus();
    for (auto& ef : g_edits) {
        if (!forceAll && ef.hwnd == focused) continue;
        TCHAR buf[64];
        FormatValue(ef, t, buf, 64);
        SetWindowText(ef.hwnd, buf);
    }
}

// ── EPoly: undo last op, re-execute with current params ─────────
static void EPolyReapply() {
    if (g_epolyOp < 0 || !g_epolyFP) return;
    Interface* ip = GetCOREInterface();
    if (!ip) return;

    // Undo the last operation (original or our previous re-apply)
    ExecuteMAXScriptScript(_T("max undo"), MAXScript::ScriptSource::NotSpecified, TRUE);

    // Re-execute with current param block values
    FPParams prms(1, TYPE_ENUM, g_epolyOp);
    FPValue result;
    g_epolyFP->Invoke(epfn_button_op, result, &prms);

    ip->RedrawViews(ip->GetTime());
}

// ── Notify scene after param change ─────────────────────────────
static void NotifyParamChanged() {
    Interface* ip = GetCOREInterface();
    if (!ip) return;
    TimeValue t = ip->GetTime();

    // Invalidate the selected node so geometry recalculates
    if (ip->GetSelNodeCount() > 0) {
        INode* node = ip->GetSelNode(0);
        if (node) {
            node->InvalidateWS();
            node->NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE);
        }
    }
    ip->RedrawViews(t);
}

// ── Apply edited value ──────────────────────────────────────────
static void ApplyEdit(HWND h) {
    Interface* ip = GetCOREInterface();
    if (!ip) return;
    TimeValue t = ip->GetTime();

    for (auto& ef : g_edits) {
        if (ef.hwnd != h) continue;
        TCHAR txt[256];
        GetWindowText(h, txt, 256);

        theHold.Suspend();
        if (IsFloat(ef.type)) {
            ef.pb->SetValue(ef.id, t, (float)_wtof(txt));
        } else if (ef.type == TYPE_BOOL) {
            int v = (_wcsicmp(txt, _T("On")) == 0 || _wtoi(txt) != 0) ? 1 : 0;
            ef.pb->SetValue(ef.id, t, v);
        } else {
            ef.pb->SetValue(ef.id, t, _wtoi(txt));
        }
        theHold.Resume();
        if (g_epolyOp >= 0) EPolyReapply();
        else                 NotifyParamChanged();
        break;
    }
}

// ── Edit subclass (Enter/Esc/Tab/Wheel + Ctrl/Shift) ────────────
static LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    EditField* ef = (EditField*)GetProp(h, _T("WF"));

    switch (msg) {
    case WM_KEYDOWN:
        if (wp == VK_RETURN)  { ApplyEdit(h); RefreshEdits(true); return 0; }
        if (wp == VK_ESCAPE)  { ClosePanel(); return 0; }
        if (wp == VK_TAB)     { SetFocus(GetNextDlgTabItem(g_panel, h, GetKeyState(VK_SHIFT) < 0)); return 0; }
        break;
    case WM_CHAR:
        if (wp == VK_RETURN || wp == VK_ESCAPE) return 0;
        break;

    case WM_MOUSEWHEEL: {
        if (!ef || !ef->pb) break;
        Interface* ip = GetCOREInterface();
        TimeValue t = ip->GetTime();
        float step = (float)GET_WHEEL_DELTA_WPARAM(wp) / 120.0f;
        bool shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
        bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

        // Suspend undo for param-only changes (EPolyReapply handles its own undo)
        theHold.Suspend();
        if (IsFloat(ef->type)) {
            float cur = ef->pb->GetFloat(ef->id, t);
            float a = cur < 0 ? -cur : cur;
            float sc = a > 100.f ? 10.f : a > 10.f ? 1.f : a > 1.f ? 0.1f : 0.01f;
            if (shift) sc *= 10.0f;
            if (ctrl)  sc *= 0.1f;
            ef->pb->SetValue(ef->id, t, cur + step * sc);
        } else if (ef->type == TYPE_BOOL) {
            ef->pb->SetValue(ef->id, t, ef->pb->GetInt(ef->id, t) ? 0 : 1);
        } else {
            int cur = ef->pb->GetInt(ef->id, t);
            int intStep = (int)step;
            if (shift) intStep *= 10;
            if (ctrl && intStep != 0) { intStep = intStep > 0 ? 1 : -1; }
            if (intStep == 0) intStep = step > 0 ? 1 : -1;
            ef->pb->SetValue(ef->id, t, cur + intStep);
        }
        theHold.Resume();

        if (g_epolyOp >= 0) EPolyReapply();
        else                 NotifyParamChanged();
        RefreshEdits(true);
        return 0;
    }

    case WM_LBUTTONDOWN:
        if (ef && ef->type == TYPE_BOOL) {
            Interface* ip = GetCOREInterface();
            TimeValue t = ip->GetTime();
            theHold.Suspend();
            ef->pb->SetValue(ef->id, t, ef->pb->GetInt(ef->id, t) ? 0 : 1);
            theHold.Resume();
            if (g_epolyOp >= 0) EPolyReapply();
            else                 NotifyParamChanged();
            RefreshEdits(true);
            return 0;
        }
        break;
    }
    return CallWindowProc(g_origEdit, h, msg, wp, lp);
}

// ── Panel paint ─────────────────────────────────────────────────
static void PaintPanel(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc; GetClientRect(hwnd, &rc);

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HBITMAP old = (HBITMAP)SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(mem, &rc, bg); DeleteObject(bg);

    RECT bar = { 0, 0, rc.right, 3 };
    HBRUSH ab = CreateSolidBrush(kAccent);
    FillRect(mem, &bar, ab); DeleteObject(ab);

    HPEN bp = CreatePen(PS_SOLID, 1, kBorder);
    SelectObject(mem, bp);
    SelectObject(mem, (HBRUSH)GetStockObject(NULL_BRUSH));
    Rectangle(mem, 0, 0, rc.right, rc.bottom);
    DeleteObject(bp);

    SetBkMode(mem, TRANSPARENT);
    int y = kPad + 3, x = kPad;
    int rEdge = rc.right - kPad;

    SelectObject(mem, g_fontBold);
    SetTextColor(mem, kAccent);
    TextOut(mem, x, y, g_nodeName.c_str(), (int)g_nodeName.length());

    if (g_hoverClose) {
        HBRUSH hov = CreateSolidBrush(kCloseHov);
        FillRect(mem, &g_closeRect, hov); DeleteObject(hov);
    }
    SetTextColor(mem, kValueClr);
    RECT cr = g_closeRect;
    DrawText(mem, _T("\u00D7"), 1, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    y += kFontHdr + 6;
    HPEN sep = CreatePen(PS_SOLID, 1, kBorder);
    SelectObject(mem, sep);
    MoveToEx(mem, x, y, nullptr); LineTo(mem, rEdge, y);
    DeleteObject(sep);
    y += 5;

    HWND focused = GetFocus();
    for (size_t gi = 0; gi < g_groups.size(); gi++) {
        const auto& gh = g_groups[gi];
        SelectObject(mem, g_fontBold);
        SetTextColor(mem, kGroupClr);
        TextOut(mem, x, y, gh.title.c_str(), (int)gh.title.length());
        y += kLineH;

        SelectObject(mem, g_font);
        for (int fi = gh.startIdx; fi < gh.startIdx + gh.count; fi++) {
            auto& ef = g_edits[fi];
            SetTextColor(mem, kLabelClr);
            TextOut(mem, x + 8, y + 2, ef.label.c_str(), (int)ef.label.length());

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
        if (gi + 1 < g_groups.size()) y += kGroupGap;
    }

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

// ── Window proc ─────────────────────────────────────────────────
static LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT:
        PaintPanel(hwnd); return 0;
    case WM_ERASEBKGND:
        return 1;

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
        break;
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
        if (GetFocus() == eh) {
            SetBkColor(hdc, kEditFocus);
            return (LRESULT)g_brEditFoc;
        }
        SetBkColor(hdc, kEditBg);
        return (LRESULT)g_brEdit;
    }

    case WM_COMMAND:
        if (HIWORD(wp) == EN_SETFOCUS) {
            DisableAccelerators();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (HIWORD(wp) == EN_KILLFOCUS) {
            EnableAccelerators();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;

    case WM_TIMER:
        RefreshEdits(); return 0;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { ClosePanel(); return 0; }
        break;

    case WM_WALKER_TOGGLE:
        TogglePanel(); return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// ── Open / Close ────────────────────────────────────────────────
static void OpenPanel() {
    GatherParams();
    if (g_groups.empty()) return;

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
        SIZE sz; GetTextExtentPoint32(hdc, gh.title.c_str(), (int)gh.title.length(), &sz);
        if (sz.cx > maxTitle) maxTitle = sz.cx;
    }
    SIZE hdrSz; GetTextExtentPoint32(hdc, g_nodeName.c_str(), (int)g_nodeName.length(), &hdrSz);
    if (hdrSz.cx + 30 > maxTitle) maxTitle = hdrSz.cx + 30;
    ReleaseDC(g_panel, hdc);

    int contentW = maxLbl + 28 + kEditW;
    if (contentW < maxTitle) contentW = maxTitle;
    int panelW = contentW + kPad * 2 + 4;
    if (panelW < kMinW) panelW = kMinW;
    int editX = panelW - kPad - kEditW;

    g_closeRect = { panelW - kPad - 18, kPad, panelW - kPad, kPad + 18 };

    int y = 3 + kPad + kFontHdr + 6 + 1 + 5;
    for (size_t gi = 0; gi < g_groups.size(); gi++) {
        y += kLineH;
        auto& gh = g_groups[gi];
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
        if (gi + 1 < g_groups.size()) y += kGroupGap;
    }
    int panelH = y + kPad;

    RefreshEdits();

    POINT pt; GetCursorPos(&pt);
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) }; GetMonitorInfo(hMon, &mi);
    int ox = pt.x - panelW / 2, oy = pt.y - 20;
    if (ox + panelW > mi.rcWork.right)  ox = mi.rcWork.right - panelW;
    if (oy + panelH > mi.rcWork.bottom) oy = mi.rcWork.bottom - panelH;
    if (ox < mi.rcWork.left) ox = mi.rcWork.left;
    if (oy < mi.rcWork.top)  oy = mi.rcWork.top;

    SetWindowPos(g_panel, HWND_TOPMOST, ox, oy, panelW, panelH, SWP_SHOWWINDOW);
    SetTimer(g_panel, 1, kRefreshMs, nullptr);
    if (!g_edits.empty()) SetFocus(g_edits[0].hwnd);
    g_open = true;
}

static void ClosePanel() {
    if (!g_open) return;
    KillTimer(g_panel, 1);
    for (auto& ef : g_edits) {
        if (ef.hwnd) { RemoveProp(ef.hwnd, _T("WF")); DestroyWindow(ef.hwnd); ef.hwnd = nullptr; }
    }
    g_edits.clear();
    g_groups.clear();
    ShowWindow(g_panel, SW_HIDE);
    g_open = false;
    g_hoverClose = false;
    g_epolyOp = -1;
    g_epolyFP = nullptr;

    EnableAccelerators();

    Interface* ip = GetCOREInterface();
    if (ip) SetFocus(ip->GetMAXHWnd());
}

static void TogglePanel() {
    if (g_open) ClosePanel(); else OpenPanel();
}

// ── Action system ───────────────────────────────────────────────
class WalkerAction : public ActionItem {
public:
    int   GetId() override { return kToggleId; }
    BOOL  ExecuteAction() override { TogglePanel(); return TRUE; }
    void  GetButtonText(MSTR& t) override { t = WALKER_NAME; }
    void  GetMenuText(MSTR& t) override { t = _T("Toggle Walker Panel"); }
    void  GetDescriptionText(MSTR& t) override { t = _T("Show/hide Walker floating parameter panel"); }
    void  GetCategoryText(MSTR& t) override { t = WALKER_NAME; }
    BOOL  IsChecked() override { return g_open; }
    BOOL  IsItemVisible() override { return TRUE; }
    BOOL  IsEnabled() override { return TRUE; }
    void  DeleteThis() override {}
};

class WalkerActionCB : public ActionCallback {
public:
    BOOL ExecuteAction(int id) override {
        if (id == kToggleId) { TogglePanel(); return TRUE; }
        return FALSE;
    }
};

static WalkerAction   g_action;
static WalkerActionCB g_actionCB;

static ActionTable* MakeActionTable() {
    static ActionTable table(kTableId, kContextId, TSTR(WALKER_NAME));
    static bool init = false;
    if (!init) { table.AppendOperation(&g_action); init = true; }
    return &table;
}

// ── GUP ─────────────────────────────────────────────────────────
class WalkerGUP : public GUP {
public:
    DWORD Start() override {
        WNDCLASSEX wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc  = PanelProc;
        wc.hInstance     = hInstance;
        wc.lpszClassName = kWndClass;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassEx(&wc);

        g_font     = CreateFont(kFontPx, 0,0,0, FW_NORMAL, 0,0,0, DEFAULT_CHARSET,0,0, CLEARTYPE_QUALITY, 0, _T("Segoe UI"));
        g_fontBold = CreateFont(kFontHdr, 0,0,0, FW_SEMIBOLD, 0,0,0, DEFAULT_CHARSET,0,0, CLEARTYPE_QUALITY, 0, _T("Segoe UI"));
        g_brEdit    = CreateSolidBrush(kEditBg);
        g_brEditFoc = CreateSolidBrush(kEditFocus);

        g_panel = CreateWindowEx(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            kWndClass, nullptr, WS_POPUP,
            0, 0, 1, 1, nullptr, nullptr, hInstance, nullptr);

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
class WalkerClassDesc : public ClassDesc2 {
public:
    int          IsPublic() override              { return TRUE; }
    void*        Create(BOOL) override            { return new WalkerGUP(); }
    const TCHAR* ClassName() override             { return WALKER_NAME; }
    const TCHAR* NonLocalizedClassName() override { return WALKER_NAME; }
    SClass_ID    SuperClassID() override          { return GUP_CLASS_ID; }
    Class_ID     ClassID() override               { return WALKER_CLASS_ID; }
    const TCHAR* Category() override              { return WALKER_CATEGORY; }
    const TCHAR* InternalName() override          { return WALKER_NAME; }
    HINSTANCE    HInstance() override              { return hInstance; }

    int           NumActionTables() override      { return 1; }
    ActionTable*  GetActionTable(int) override    { return MakeActionTable(); }
};

static WalkerClassDesc walkerDesc;

// ── DLL boilerplate ─────────────────────────────────────────────
BOOL WINAPI DllMain(HINSTANCE hinstDLL, ULONG fdwReason, LPVOID) {
    if (fdwReason == DLL_PROCESS_ATTACH) { hInstance = hinstDLL; DisableThreadLibraryCalls(hinstDLL); }
    return TRUE;
}

__declspec(dllexport) const TCHAR* LibDescription()   { return WALKER_NAME; }
__declspec(dllexport) int          LibNumberClasses()  { return 1; }
__declspec(dllexport) ClassDesc*   LibClassDesc(int i) { return i == 0 ? &walkerDesc : nullptr; }
__declspec(dllexport) ULONG        LibVersion()        { return VERSION_3DSMAX; }
__declspec(dllexport) int          LibInitialize()     { return TRUE; }
__declspec(dllexport) int          LibShutdown()       { return TRUE; }
