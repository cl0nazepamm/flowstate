#include "PowerCutMod.h"
#include "resource.h"
#include <polyobj.h>
#include <triobj.h>
#include <shape.h>
#include <splshape.h>
#include <linshape.h>
#include <polyshp.h>
#include <istdplug.h>
#include <hold.h>

static const TCHAR* GetString(int id) {
    static TCHAR buf[256];
    if (LoadString(hInstance, id, buf, 256))
        return buf;
    return _T("");
}

static const TCHAR* ModeName(int m) {
    switch (m) {
    case BOOLOP_CUT_REFINE:     return _T("Cut");
    case BOOLOP_CUT_SEPARATE:   return _T("Split");
    case BOOLOP_CUT_REMOVE_IN:  return _T("Subtract");
    case BOOLOP_CUT_REMOVE_OUT: return _T("Intersect");
    default:                    return _T("Subtract");
    }
}

static bool IsSelectedSplineMirrorParam(ParamID pid) {
    switch (pid) {
    case pb_steps:
    case pb_weld_threshold:
    case pb_keep_tris:
    case pb_mode:
    case pb_proj_mode:
    case pb_flip_normals:
    case pb_depth:
    case pb_solid:
    case pb_thickness:
    case pb_bevel:
    case pb_bevel_amount:
    case pb_bevel_segments:
        return true;
    default:
        return false;
    }
}

// ═════════════════════════════════════════════════════════════════
//  PARAMETER BLOCK DESCRIPTOR
// ═════════════════════════════════════════════════════════════════
static ParamBlockDesc2 powercut_param_blk(
    0, _T("PowerCutParams"), IDS_PARAMS, GetPowerCutDesc(),
    P_AUTO_CONSTRUCT, PBLOCK_REF,

    pb_spline_node, _T("splineNodes"), TYPE_INODE_TAB, 0, P_VARIABLE_SIZE, IDS_SPLINE_NODE,
        p_end,

    pb_steps, _T("steps"), TYPE_INT, P_ANIMATABLE, IDS_STEPS,
        p_default, PSHAPE_BUILTIN_STEPS,
        p_range, PSHAPE_BUILTIN_STEPS, 100,
        p_ui, TYPE_SPINNER, EDITTYPE_INT, IDC_STEPS_EDIT, IDC_STEPS_SPIN, 1.0f,
        p_end,

    pb_flip_normals, _T("flipNormals"), TYPE_BOOL, 0, IDS_FLIP,
        p_default, FALSE,
        p_end,

    pb_weld_threshold, _T("weldThreshold"), TYPE_FLOAT, P_ANIMATABLE, IDS_WELD_THRESH,
        p_default, 0.001f, p_range, 0.0001f, 10.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_WELD_EDIT, IDC_WELD_SPIN, 0.001f,
        p_end,

    pb_mode, _T("mode"), TYPE_INT, 0, IDS_MODE,
        p_default, 2,
        p_end,

    pb_keep_tris, _T("keepTriangulation"), TYPE_BOOL, 0, IDS_KEEP_TRIS,
        p_default, FALSE,
        p_ui, TYPE_SINGLECHECKBOX, IDC_KEEP_TRIS,
        p_end,

    pb_spline_mode, _T("splineModes"), TYPE_INT_TAB, 0, P_VARIABLE_SIZE, IDS_MODE,
        p_end,

    pb_proj_mode, _T("projMode"), TYPE_INT, 0, IDS_PROJ_MODE,
        p_default, PROJ_SPLINE_Z,
        p_end,

    pb_depth, _T("depth"), TYPE_FLOAT, P_ANIMATABLE, IDS_DEPTH,
        p_default, 0.0f, p_range, 0.0f, 1000000.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_DEPTH_EDIT, IDC_DEPTH_SPIN, 0.1f,
        p_end,

    pb_solid, _T("solid"), TYPE_BOOL, 0, IDS_SOLID,
        p_default, FALSE,
        p_ui, TYPE_SINGLECHECKBOX, IDC_SOLID_CHECK,
        p_end,

    pb_thickness, _T("thickness"), TYPE_FLOAT, P_ANIMATABLE, IDS_THICKNESS,
        p_default, 1.0f, p_range, 0.001f, 1000000.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_THICK_EDIT, IDC_THICK_SPIN, 0.1f,
        p_end,

    pb_bevel, _T("bevel"), TYPE_BOOL, 0, IDS_BEVEL,
        p_default, FALSE,
        p_ui, TYPE_SINGLECHECKBOX, IDC_BEVEL_CHECK,
        p_end,

    pb_bevel_amount, _T("bevelAmount"), TYPE_FLOAT, P_ANIMATABLE, IDS_BEVEL_AMOUNT,
        p_default, 0.5f, p_range, 0.0f, 1000000.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_BEVEL_AMT_EDIT, IDC_BEVEL_AMT_SPIN, 0.01f,
        p_end,

    pb_bevel_segments, _T("bevelSegments"), TYPE_INT, P_ANIMATABLE, IDS_BEVEL_SEGMENTS,
        p_default, 1, p_range, 1, 20,
        p_ui, TYPE_SPINNER, EDITTYPE_INT, IDC_BEVEL_SEG_EDIT, IDC_BEVEL_SEG_SPIN, 1.0f,
        p_end,

    pb_spline_steps, _T("splineSteps"), TYPE_INT_TAB, 0, P_VARIABLE_SIZE, IDS_STEPS,
        p_end,

    pb_spline_weld_threshold, _T("splineWeldThreshold"), TYPE_FLOAT_TAB, 0, P_VARIABLE_SIZE, IDS_WELD_THRESH,
        p_end,

    pb_spline_keep_tris, _T("splineKeepTris"), TYPE_BOOL_TAB, 0, P_VARIABLE_SIZE, IDS_KEEP_TRIS,
        p_end,

    pb_spline_proj_mode, _T("splineProjModes"), TYPE_INT_TAB, 0, P_VARIABLE_SIZE, IDS_PROJ_MODE,
        p_end,

    pb_spline_flip_depth, _T("splineFlipDepth"), TYPE_BOOL_TAB, 0, P_VARIABLE_SIZE, IDS_FLIP,
        p_end,

    pb_spline_depth, _T("splineDepth"), TYPE_FLOAT_TAB, 0, P_VARIABLE_SIZE, IDS_DEPTH,
        p_end,

    pb_spline_solid, _T("splineSolid"), TYPE_BOOL_TAB, 0, P_VARIABLE_SIZE, IDS_SOLID,
        p_end,

    pb_spline_thickness, _T("splineThickness"), TYPE_FLOAT_TAB, 0, P_VARIABLE_SIZE, IDS_THICKNESS,
        p_end,

    pb_spline_bevel, _T("splineBevel"), TYPE_BOOL_TAB, 0, P_VARIABLE_SIZE, IDS_BEVEL,
        p_end,

    pb_spline_bevel_amount, _T("splineBevelAmount"), TYPE_FLOAT_TAB, 0, P_VARIABLE_SIZE, IDS_BEVEL_AMOUNT,
        p_end,

    pb_spline_bevel_segments, _T("splineBevelSegments"), TYPE_INT_TAB, 0, P_VARIABLE_SIZE, IDS_BEVEL_SEGMENTS,
        p_end,

    p_end
);

// ═════════════════════════════════════════════════════════════════
//  VIEWPORT PICK MODE — click splines directly in viewport
// ═════════════════════════════════════════════════════════════════
class SplinePickModeCallback : public PickModeCallback, public PickNodeCallback {
public:
    PowerCutMod* mod;
    HWND hPanel;

    SplinePickModeCallback() : mod(nullptr), hPanel(nullptr) {}

    // ── PickModeCallback ─────────────────────────────────────
    BOOL HitTest(IObjParam* ip, HWND hwnd, ViewExp* vpt, IPoint2 m, int flags) override {
        return ip->PickNode(hwnd, m, this) ? TRUE : FALSE;
    }

    BOOL Pick(IObjParam* ip, ViewExp* vpt) override {
        INode* node = vpt->GetClosestHit();
        if (!node || !mod || !mod->pblock2) return FALSE;

        ObjectState os = node->EvalWorldState(ip->GetTime());
        if (!os.obj || !os.obj->IsShapeObject()) return FALSE;

        theHold.Begin();
        mod->AppendSpline(node, ip->GetTime());
        theHold.Accept(_T("Add Spline"));

        if (hPanel) {
            SendMessage(hPanel, WM_USER + 0x100, 0, 0);
        }
        return FALSE;  // FALSE = stay in pick mode for more picks
    }

    void EnterMode(IObjParam* ip) override {
        if (hPanel) {
            SetDlgItemText(hPanel, IDC_PICK_SPLINE, _T("Pick..."));
        }
    }

    void ExitMode(IObjParam* ip) override {
        PowerCutMod::inPickMode = false;
        if (hPanel) {
            SetDlgItemText(hPanel, IDC_PICK_SPLINE, _T("Add"));
        }
    }

    BOOL RightClick(IObjParam* ip, ViewExp* vpt) override {
        return TRUE;  // TRUE = exit pick mode on right-click
    }

    // ── PickNodeCallback ─────────────────────────────────────
    BOOL Filter(INode* node) override {
        if (!node) return FALSE;
        ObjectState os = node->EvalWorldState(GetCOREInterface()->GetTime());
        return (os.obj && os.obj->IsShapeObject()) ? TRUE : FALSE;
    }

    PickNodeCallback* GetFilter() override { return this; }

    BOOL AllowMultiSelect() override { return FALSE; }
};

// ═════════════════════════════════════════════════════════════════
//  DIALOG PROC — spline list + per-spline mode radio buttons
// ═════════════════════════════════════════════════════════════════
class PowerCutDlgProc : public ParamMap2UserDlgProc {
public:
    PowerCutMod* mod;
    PowerCutDlgProc(PowerCutMod* m) : mod(m) {}

    INT_PTR DlgProc(TimeValue t, IParamMap2* map, HWND hWnd, UINT msg,
                     WPARAM wParam, LPARAM lParam) override {
        switch (msg) {
        case WM_INITDIALOG:
            RefillList(hWnd, t);
            if (mod)
                mod->SyncSelectedSplineToGlobals(t);
            UpdateRadioButtons(hWnd);
            UpdateProjectionRadios(hWnd);
            UpdateFlipButton(hWnd);
            UpdateDepthUI(hWnd);
            return TRUE;

        case WM_USER + 0x100:
            // Sent by pick mode callback when a spline is added
            RefillList(hWnd, GetCOREInterface()->GetTime());
            {
                HWND hList = GetDlgItem(hWnd, IDC_SPLINE_LIST);
                int count = (int)SendMessage(hList, LB_GETCOUNT, 0, 0);
                if (count > 0) {
                    SendMessage(hList, LB_SETCURSEL, count - 1, 0);
                    if (mod) {
                        mod->SetSelectedSplineIndex(count - 1);
                        mod->SyncSelectedSplineToGlobals(GetCOREInterface()->GetTime());
                    }
                }
            }
            UpdateRadioButtons(hWnd);
            UpdateProjectionRadios(hWnd);
            UpdateFlipButton(hWnd);
            UpdateDepthUI(hWnd);
            return TRUE;

        case WM_COMMAND: {
            const WORD id = LOWORD(wParam);
            const WORD notify = HIWORD(wParam);

            // ── Add splines (pick mode toggle / H-key fallback) ──
            if (id == IDC_PICK_SPLINE) {
                if (mod) {
                    if (PowerCutMod::inPickMode) {
                        mod->ExitPickMode();
                    } else {
                        // If Ctrl+click or H-key: open name dialog as fallback
                        bool useFallback = (GetKeyState(VK_MENU) & 0x8000) != 0;
                        if (useFallback) {
                            SplinePickFilter filter;
                            GetCOREInterface()->DoHitByNameDialog(&filter);
                            for (int i = 0; i < filter.picked.Count(); ++i) {
                                INode* node = filter.picked[i];
                                mod->AppendSpline(node, GetCOREInterface()->GetTime());
                            }
                            RefillList(hWnd, GetCOREInterface()->GetTime());
                            HWND hList = GetDlgItem(hWnd, IDC_SPLINE_LIST);
                            int count = (int)SendMessage(hList, LB_GETCOUNT, 0, 0);
                            if (count > 0) {
                                SendMessage(hList, LB_SETCURSEL, count - 1, 0);
                                mod->SetSelectedSplineIndex(count - 1);
                                mod->SyncSelectedSplineToGlobals(GetCOREInterface()->GetTime());
                            }
                            UpdateRadioButtons(hWnd);
                            UpdateProjectionRadios(hWnd);
                            UpdateFlipButton(hWnd);
                            UpdateDepthUI(hWnd);
                        } else {
                            // Set the panel HWND on the callback so it can refresh
                            if (PowerCutMod::pickCB) {
                                PowerCutMod::pickCB->hPanel = hWnd;
                            }
                            mod->EnterPickMode();
                        }
                    }
                }
                return TRUE;
            }

            // ── Remove selected ─────────────────────────────
            if (id == IDC_DEL_SPLINE) {
                if (mod && mod->pblock2) {
                    HWND hList = GetDlgItem(hWnd, IDC_SPLINE_LIST);
                    int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
                    mod->DeleteSpline(sel);
                    RefillList(hWnd, GetCOREInterface()->GetTime());
                    int count = (int)SendMessage(hList, LB_GETCOUNT, 0, 0);
                    if (sel >= count) sel = count - 1;
                    mod->SetSelectedSplineIndex(sel);
                    if (sel >= 0) {
                        SendMessage(hList, LB_SETCURSEL, sel, 0);
                        mod->SyncSelectedSplineToGlobals(GetCOREInterface()->GetTime());
                    }
                    UpdateRadioButtons(hWnd);
                    UpdateProjectionRadios(hWnd);
                    UpdateFlipButton(hWnd);
                    UpdateDepthUI(hWnd);
                }
                return TRUE;
            }

            // ── List selection changed ──────────────────────
            if (id == IDC_SPLINE_LIST && notify == LBN_SELCHANGE) {
                if (mod) {
                    HWND hList = GetDlgItem(hWnd, IDC_SPLINE_LIST);
                    int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
                    mod->SetSelectedSplineIndex(sel);
                    mod->SyncSelectedSplineToGlobals(GetCOREInterface()->GetTime());
                }
                UpdateRadioButtons(hWnd);
                UpdateProjectionRadios(hWnd);
                UpdateFlipButton(hWnd);
                UpdateDepthUI(hWnd);
                return TRUE;
            }

            // ── Mode radio buttons ──────────────────────────
            if (id == IDC_MODE_CUT || id == IDC_MODE_SPLIT ||
                id == IDC_MODE_SUBTRACT || id == IDC_MODE_INTERSECT) {
                SetSelectedMode(hWnd, id);
                return TRUE;
            }

            // ── Projection mode radio buttons ───────────────
            if (id == IDC_PROJ_SPLINE_Z || id == IDC_PROJ_NORMAL) {
                if (mod && mod->pblock2) {
                    int projMode = (id == IDC_PROJ_NORMAL) ? PROJ_NORMAL : PROJ_SPLINE_Z;
                    mod->pblock2->SetValue(pb_proj_mode, 0, projMode);
                    UpdateProjectionRadios(hWnd);
                }
                return TRUE;
            }

            // ── Flip depth toggle button ─────────────────────
            if (id == IDC_FLIP_CHECK) {
                if (mod && mod->pblock2) {
                    Interval iv;
                    BOOL flipDepth = FALSE;
                    mod->pblock2->GetValue(pb_flip_normals, 0, flipDepth, iv);
                    mod->pblock2->SetValue(pb_flip_normals, 0, flipDepth ? FALSE : TRUE);
                    UpdateFlipButton(hWnd);
                }
                return TRUE;
            }

            // ── Depth infinite checkbox ─────────────────────
            if (id == IDC_DEPTH_INFINITE) {
                if (mod && mod->pblock2) {
                    BOOL checked = IsDlgButtonChecked(hWnd, IDC_DEPTH_INFINITE);
                    if (checked) {
                        mod->pblock2->SetValue(pb_depth, 0, 0.0f);
                    } else {
                        // Unchecking infinite: set a sensible default depth
                        Interval iv;
                        float cur = 0.0f;
                        mod->pblock2->GetValue(pb_depth, 0, cur, iv);
                        if (cur <= 0.0f)
                            mod->pblock2->SetValue(pb_depth, 0, 10.0f);
                    }
                    UpdateDepthUI(hWnd);
                }
                return TRUE;
            }
            break;
        }
        }
        return FALSE;
    }

    void DeleteThis() override { delete this; }

    void RefillList(HWND hWnd, TimeValue t) {
        HWND hList = GetDlgItem(hWnd, IDC_SPLINE_LIST);
        SendMessage(hList, LB_RESETCONTENT, 0, 0);
        if (!mod || !mod->pblock2) return;

        mod->EnsureSplineSettingsCount(t);

        const int count = mod->pblock2->Count(pb_spline_node);
        for (int i = 0; i < count; ++i) {
            INode* node = nullptr;
            Interval iv;
            mod->pblock2->GetValue(pb_spline_node, t, node, iv, i);

            SplineSettings settings;
            mod->GetSplineSettings(t, i, settings);

            TCHAR buf[256];
            _stprintf_s(buf, _T("%s  [%s]"),
                        node ? node->GetName() : _T("<deleted>"),
                        ModeName(settings.mode));
            SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)buf);
        }

        int sel = mod->GetSelectedSplineIndex();
        if (sel < 0 && count > 0)
            sel = 0;
        if (sel >= count)
            sel = count - 1;
        mod->SetSelectedSplineIndex(sel);
        if (sel >= 0)
            SendMessage(hList, LB_SETCURSEL, sel, 0);
    }

    void UpdateRadioButtons(HWND hWnd) {
        int sel = mod ? mod->GetSelectedSplineIndex() : -1;
        SplineSettings settings;
        if (mod && mod->pblock2 && sel >= 0)
            mod->GetSplineSettings(0, sel, settings);

        CheckRadioButton(hWnd, IDC_MODE_CUT, IDC_MODE_INTERSECT,
                          IDC_MODE_CUT + settings.mode);

        BOOL enable = (sel >= 0) ? TRUE : FALSE;
        EnableWindow(GetDlgItem(hWnd, IDC_MODE_CUT), enable);
        EnableWindow(GetDlgItem(hWnd, IDC_MODE_SPLIT), enable);
        EnableWindow(GetDlgItem(hWnd, IDC_MODE_SUBTRACT), enable);
        EnableWindow(GetDlgItem(hWnd, IDC_MODE_INTERSECT), enable);
    }

    void UpdateProjectionRadios(HWND hWnd) {
        int projMode = PROJ_SPLINE_Z;
        if (mod && mod->pblock2) {
            Interval iv;
            mod->pblock2->GetValue(pb_proj_mode, 0, projMode, iv);
        }
        CheckDlgButton(hWnd, IDC_PROJ_SPLINE_Z, (projMode == PROJ_SPLINE_Z) ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hWnd, IDC_PROJ_NORMAL, (projMode == PROJ_NORMAL) ? BST_CHECKED : BST_UNCHECKED);
    }

    void UpdateFlipButton(HWND hWnd) {
        if (!mod || !mod->pblock2) return;

        Interval iv;
        BOOL flipDepth = FALSE;
        mod->pblock2->GetValue(pb_flip_normals, 0, flipDepth, iv);
        SetDlgItemText(hWnd, IDC_FLIP_CHECK,
                       flipDepth ? _T("Flip Depth: On") : _T("Flip Depth: Off"));

        BOOL enable = (mod->GetSelectedSplineIndex() >= 0) ? TRUE : FALSE;
        EnableWindow(GetDlgItem(hWnd, IDC_FLIP_CHECK), enable);
    }

    void UpdateDepthUI(HWND hWnd) {
        if (!mod || !mod->pblock2) return;
        Interval iv;
        float depth = 0.0f;
        mod->pblock2->GetValue(pb_depth, 0, depth, iv);
        BOOL isInfinite = (depth <= 0.0f) ? TRUE : FALSE;
        CheckDlgButton(hWnd, IDC_DEPTH_INFINITE, isInfinite ? BST_CHECKED : BST_UNCHECKED);
        // Disable spinner when infinite
        HWND hEdit = GetDlgItem(hWnd, IDC_DEPTH_EDIT);
        HWND hSpin = GetDlgItem(hWnd, IDC_DEPTH_SPIN);
        if (hEdit) EnableWindow(hEdit, !isInfinite);
        if (hSpin) EnableWindow(hSpin, !isInfinite);
    }

    void SetSelectedMode(HWND hWnd, WORD radioID) {
        HWND hList = GetDlgItem(hWnd, IDC_SPLINE_LIST);
        int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
        if (!mod || !mod->pblock2 || sel < 0 ||
            sel >= mod->pblock2->Count(pb_spline_node))
            return;

        int mode = static_cast<int>(radioID - IDC_MODE_CUT);
        mod->EnsureSplineSettingsCount(GetCOREInterface()->GetTime());
        mod->pblock2->SetValue(pb_spline_mode, 0, mode, sel);
        mod->SyncSelectedSplineToGlobals(GetCOREInterface()->GetTime());
        RefillList(hWnd, GetCOREInterface()->GetTime());

        // Re-select the same item after refill.
        SendMessage(hList, LB_SETCURSEL, sel, 0);
    }

    class SplinePickFilter : public HitByNameDlgCallback {
    public:
        INodeTab picked;
        const MCHAR* dialogTitle() override { return _T("Add Spline Objects"); }
        const MCHAR* buttonText() override { return _T("Add"); }
        BOOL singleSelect() override { return FALSE; }
        BOOL filter(INode* node) override {
            if (!node) return FALSE;
            ObjectState os = node->EvalWorldState(GetCOREInterface()->GetTime());
            return (os.obj && os.obj->IsShapeObject()) ? TRUE : FALSE;
        }
        void proc(INodeTab& nodeTab) override {
            picked = nodeTab;
        }
    };
};

// ═════════════════════════════════════════════════════════════════
//  CONSTRUCTOR / DESTRUCTOR
// ═════════════════════════════════════════════════════════════════
IObjParam* PowerCutMod::ip = nullptr;
SplinePickModeCallback* PowerCutMod::pickCB = nullptr;
bool PowerCutMod::inPickMode = false;

PowerCutMod::PowerCutMod() : pblock2(nullptr), pmapParam(nullptr) {
    GetPowerCutDesc()->MakeAutoParamBlocks(this);
}

PowerCutMod::~PowerCutMod() {}

void PowerCutMod::GetGlobalSettings(TimeValue t, SplineSettings& settings) const {
    if (!pblock2)
        return;

    Interval iv = FOREVER;
    pblock2->GetValue(pb_steps, t, settings.steps, iv);
    pblock2->GetValue(pb_weld_threshold, t, settings.weldThresh, iv);
    pblock2->GetValue(pb_keep_tris, t, settings.keepTris, iv);
    pblock2->GetValue(pb_mode, t, settings.mode, iv);
    pblock2->GetValue(pb_proj_mode, t, settings.projMode, iv);
    pblock2->GetValue(pb_flip_normals, t, settings.flipDepth, iv);
    pblock2->GetValue(pb_depth, t, settings.depth, iv);
    pblock2->GetValue(pb_solid, t, settings.solid, iv);
    pblock2->GetValue(pb_thickness, t, settings.thickness, iv);
    pblock2->GetValue(pb_bevel, t, settings.bevel, iv);
    pblock2->GetValue(pb_bevel_amount, t, settings.bevelAmount, iv);
    pblock2->GetValue(pb_bevel_segments, t, settings.bevelSegs, iv);
}

void PowerCutMod::GetSplineSettings(TimeValue t, int index, SplineSettings& settings) const {
    GetGlobalSettings(t, settings);
    if (!pblock2 || index < 0)
        return;

    Interval iv = FOREVER;
    if (index < pblock2->Count(pb_spline_steps))
        pblock2->GetValue(pb_spline_steps, t, settings.steps, iv, index);
    if (index < pblock2->Count(pb_spline_weld_threshold))
        pblock2->GetValue(pb_spline_weld_threshold, t, settings.weldThresh, iv, index);
    if (index < pblock2->Count(pb_spline_keep_tris))
        pblock2->GetValue(pb_spline_keep_tris, t, settings.keepTris, iv, index);
    if (index < pblock2->Count(pb_spline_mode))
        pblock2->GetValue(pb_spline_mode, t, settings.mode, iv, index);
    if (index < pblock2->Count(pb_spline_proj_mode))
        pblock2->GetValue(pb_spline_proj_mode, t, settings.projMode, iv, index);
    if (index < pblock2->Count(pb_spline_flip_depth))
        pblock2->GetValue(pb_spline_flip_depth, t, settings.flipDepth, iv, index);
    if (index < pblock2->Count(pb_spline_depth))
        pblock2->GetValue(pb_spline_depth, t, settings.depth, iv, index);
    if (index < pblock2->Count(pb_spline_solid))
        pblock2->GetValue(pb_spline_solid, t, settings.solid, iv, index);
    if (index < pblock2->Count(pb_spline_thickness))
        pblock2->GetValue(pb_spline_thickness, t, settings.thickness, iv, index);
    if (index < pblock2->Count(pb_spline_bevel))
        pblock2->GetValue(pb_spline_bevel, t, settings.bevel, iv, index);
    if (index < pblock2->Count(pb_spline_bevel_amount))
        pblock2->GetValue(pb_spline_bevel_amount, t, settings.bevelAmount, iv, index);
    if (index < pblock2->Count(pb_spline_bevel_segments))
        pblock2->GetValue(pb_spline_bevel_segments, t, settings.bevelSegs, iv, index);
}

void PowerCutMod::EnsureSplineSettingsCount(TimeValue t) {
    if (!pblock2)
        return;

    const int nodeCount = pblock2->Count(pb_spline_node);
    if (nodeCount <= 0)
        return;

    SplineSettings defaults;
    GetGlobalSettings(t, defaults);

    while (pblock2->Count(pb_spline_steps) < nodeCount)
        pblock2->Append(pb_spline_steps, 1, &defaults.steps);
    while (pblock2->Count(pb_spline_weld_threshold) < nodeCount)
        pblock2->Append(pb_spline_weld_threshold, 1, &defaults.weldThresh);
    while (pblock2->Count(pb_spline_keep_tris) < nodeCount)
        pblock2->Append(pb_spline_keep_tris, 1, &defaults.keepTris);
    while (pblock2->Count(pb_spline_mode) < nodeCount)
        pblock2->Append(pb_spline_mode, 1, &defaults.mode);
    while (pblock2->Count(pb_spline_proj_mode) < nodeCount)
        pblock2->Append(pb_spline_proj_mode, 1, &defaults.projMode);
    while (pblock2->Count(pb_spline_flip_depth) < nodeCount)
        pblock2->Append(pb_spline_flip_depth, 1, &defaults.flipDepth);
    while (pblock2->Count(pb_spline_depth) < nodeCount)
        pblock2->Append(pb_spline_depth, 1, &defaults.depth);
    while (pblock2->Count(pb_spline_solid) < nodeCount)
        pblock2->Append(pb_spline_solid, 1, &defaults.solid);
    while (pblock2->Count(pb_spline_thickness) < nodeCount)
        pblock2->Append(pb_spline_thickness, 1, &defaults.thickness);
    while (pblock2->Count(pb_spline_bevel) < nodeCount)
        pblock2->Append(pb_spline_bevel, 1, &defaults.bevel);
    while (pblock2->Count(pb_spline_bevel_amount) < nodeCount)
        pblock2->Append(pb_spline_bevel_amount, 1, &defaults.bevelAmount);
    while (pblock2->Count(pb_spline_bevel_segments) < nodeCount)
        pblock2->Append(pb_spline_bevel_segments, 1, &defaults.bevelSegs);
}

void PowerCutMod::AppendSpline(INode* splineNode, TimeValue t) {
    if (!pblock2 || !splineNode)
        return;

    SplineSettings settings;
    GetGlobalSettings(t, settings);

    pblock2->Append(pb_spline_node, 1, &splineNode);
    pblock2->Append(pb_spline_steps, 1, &settings.steps);
    pblock2->Append(pb_spline_weld_threshold, 1, &settings.weldThresh);
    pblock2->Append(pb_spline_keep_tris, 1, &settings.keepTris);
    pblock2->Append(pb_spline_mode, 1, &settings.mode);
    pblock2->Append(pb_spline_proj_mode, 1, &settings.projMode);
    pblock2->Append(pb_spline_flip_depth, 1, &settings.flipDepth);
    pblock2->Append(pb_spline_depth, 1, &settings.depth);
    pblock2->Append(pb_spline_solid, 1, &settings.solid);
    pblock2->Append(pb_spline_thickness, 1, &settings.thickness);
    pblock2->Append(pb_spline_bevel, 1, &settings.bevel);
    pblock2->Append(pb_spline_bevel_amount, 1, &settings.bevelAmount);
    pblock2->Append(pb_spline_bevel_segments, 1, &settings.bevelSegs);
}

void PowerCutMod::DeleteSpline(int index) {
    if (!pblock2 || index < 0 || index >= pblock2->Count(pb_spline_node))
        return;

    pblock2->Delete(pb_spline_node, index, 1);
    if (index < pblock2->Count(pb_spline_steps))
        pblock2->Delete(pb_spline_steps, index, 1);
    if (index < pblock2->Count(pb_spline_weld_threshold))
        pblock2->Delete(pb_spline_weld_threshold, index, 1);
    if (index < pblock2->Count(pb_spline_keep_tris))
        pblock2->Delete(pb_spline_keep_tris, index, 1);
    if (index < pblock2->Count(pb_spline_mode))
        pblock2->Delete(pb_spline_mode, index, 1);
    if (index < pblock2->Count(pb_spline_proj_mode))
        pblock2->Delete(pb_spline_proj_mode, index, 1);
    if (index < pblock2->Count(pb_spline_flip_depth))
        pblock2->Delete(pb_spline_flip_depth, index, 1);
    if (index < pblock2->Count(pb_spline_depth))
        pblock2->Delete(pb_spline_depth, index, 1);
    if (index < pblock2->Count(pb_spline_solid))
        pblock2->Delete(pb_spline_solid, index, 1);
    if (index < pblock2->Count(pb_spline_thickness))
        pblock2->Delete(pb_spline_thickness, index, 1);
    if (index < pblock2->Count(pb_spline_bevel))
        pblock2->Delete(pb_spline_bevel, index, 1);
    if (index < pblock2->Count(pb_spline_bevel_amount))
        pblock2->Delete(pb_spline_bevel_amount, index, 1);
    if (index < pblock2->Count(pb_spline_bevel_segments))
        pblock2->Delete(pb_spline_bevel_segments, index, 1);
}

void PowerCutMod::SyncSelectedSplineToGlobals(TimeValue t) {
    if (!pblock2)
        return;

    const int sel = selectedSplineIndex_;
    if (sel < 0 || sel >= pblock2->Count(pb_spline_node))
        return;

    EnsureSplineSettingsCount(t);

    SplineSettings settings;
    GetSplineSettings(t, sel, settings);

    syncingSelectedSpline_ = true;
    pblock2->SetValue(pb_steps, t, settings.steps);
    pblock2->SetValue(pb_weld_threshold, t, settings.weldThresh);
    pblock2->SetValue(pb_keep_tris, t, settings.keepTris);
    pblock2->SetValue(pb_mode, t, settings.mode);
    pblock2->SetValue(pb_proj_mode, t, settings.projMode);
    pblock2->SetValue(pb_flip_normals, t, settings.flipDepth);
    pblock2->SetValue(pb_depth, t, settings.depth);
    pblock2->SetValue(pb_solid, t, settings.solid);
    pblock2->SetValue(pb_thickness, t, settings.thickness);
    pblock2->SetValue(pb_bevel, t, settings.bevel);
    pblock2->SetValue(pb_bevel_amount, t, settings.bevelAmount);
    pblock2->SetValue(pb_bevel_segments, t, settings.bevelSegs);
    syncingSelectedSpline_ = false;
}

void PowerCutMod::SyncGlobalParamToSelectedSpline(TimeValue t, ParamID pid) {
    if (!pblock2 || syncingSelectedSpline_)
        return;

    const int sel = selectedSplineIndex_;
    if (sel < 0 || sel >= pblock2->Count(pb_spline_node))
        return;

    EnsureSplineSettingsCount(t);

    Interval iv = FOREVER;
    switch (pid) {
    case pb_steps: {
        int value = PSHAPE_BUILTIN_STEPS;
        pblock2->GetValue(pb_steps, t, value, iv);
        pblock2->SetValue(pb_spline_steps, t, value, sel);
        break;
    }
    case pb_weld_threshold: {
        float value = 0.001f;
        pblock2->GetValue(pb_weld_threshold, t, value, iv);
        pblock2->SetValue(pb_spline_weld_threshold, t, value, sel);
        break;
    }
    case pb_keep_tris: {
        BOOL value = FALSE;
        pblock2->GetValue(pb_keep_tris, t, value, iv);
        pblock2->SetValue(pb_spline_keep_tris, t, value, sel);
        break;
    }
    case pb_mode: {
        int value = BOOLOP_CUT_REMOVE_IN;
        pblock2->GetValue(pb_mode, t, value, iv);
        pblock2->SetValue(pb_spline_mode, t, value, sel);
        break;
    }
    case pb_proj_mode: {
        int value = PROJ_SPLINE_Z;
        pblock2->GetValue(pb_proj_mode, t, value, iv);
        pblock2->SetValue(pb_spline_proj_mode, t, value, sel);
        break;
    }
    case pb_flip_normals: {
        BOOL value = FALSE;
        pblock2->GetValue(pb_flip_normals, t, value, iv);
        pblock2->SetValue(pb_spline_flip_depth, t, value, sel);
        break;
    }
    case pb_depth: {
        float value = 0.0f;
        pblock2->GetValue(pb_depth, t, value, iv);
        pblock2->SetValue(pb_spline_depth, t, value, sel);
        break;
    }
    case pb_solid: {
        BOOL value = FALSE;
        pblock2->GetValue(pb_solid, t, value, iv);
        pblock2->SetValue(pb_spline_solid, t, value, sel);
        break;
    }
    case pb_thickness: {
        float value = 1.0f;
        pblock2->GetValue(pb_thickness, t, value, iv);
        pblock2->SetValue(pb_spline_thickness, t, value, sel);
        break;
    }
    case pb_bevel: {
        BOOL value = FALSE;
        pblock2->GetValue(pb_bevel, t, value, iv);
        pblock2->SetValue(pb_spline_bevel, t, value, sel);
        break;
    }
    case pb_bevel_amount: {
        float value = 0.5f;
        pblock2->GetValue(pb_bevel_amount, t, value, iv);
        pblock2->SetValue(pb_spline_bevel_amount, t, value, sel);
        break;
    }
    case pb_bevel_segments: {
        int value = 1;
        pblock2->GetValue(pb_bevel_segments, t, value, iv);
        pblock2->SetValue(pb_spline_bevel_segments, t, value, sel);
        break;
    }
    default:
        break;
    }
}

// ═════════════════════════════════════════════════════════════════
//  CHANNELS
// ═════════════════════════════════════════════════════════════════
ChannelMask PowerCutMod::ChannelsUsed()    { return GEOM_CHANNEL | TOPO_CHANNEL; }
ChannelMask PowerCutMod::ChannelsChanged() { return GEOM_CHANNEL | TOPO_CHANNEL; }
Class_ID PowerCutMod::InputType() { return defObjectClassID; }

// ═════════════════════════════════════════════════════════════════
//  REFERENCE MANAGEMENT
// ═════════════════════════════════════════════════════════════════
void PowerCutMod::SetReference(int i, RefTargetHandle rtarg) {
    if (i == PBLOCK_REF)
        pblock2 = (IParamBlock2*)rtarg;
}

RefResult PowerCutMod::NotifyRefChanged(const Interval& iv,
                                             RefTargetHandle hTarg,
                                             PartID& partID,
                                             RefMessage msg,
                                             BOOL propagate) {
    if (msg == REFMSG_CHANGE && hTarg == pblock2) {
        ParamID pid = pblock2->LastNotifyParamID();

        // Mirror global UI param → per-spline storage.
        // Guard against reentrancy: SyncGlobalParamToSelectedSpline
        // modifies the pblock which would fire NotifyRefChanged again.
        if (!syncingSelectedSpline_ && IsSelectedSplineMirrorParam(pid))
            SyncGlobalParamToSelectedSpline(GetCOREInterface()->GetTime(), pid);

        NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE);
    }
    return REF_SUCCEED;
}

// ═════════════════════════════════════════════════════════════════
//  CLONE
// ═════════════════════════════════════════════════════════════════
RefTargetHandle PowerCutMod::Clone(RemapDir& remap) {
    PowerCutMod* mod = new PowerCutMod();
    mod->ReplaceReference(PBLOCK_REF, remap.CloneRef(pblock2));
    BaseClone(this, mod, remap);
    return mod;
}

// ═════════════════════════════════════════════════════════════════
//  VALIDITY — depend on every spline node's transform
// ═════════════════════════════════════════════════════════════════
Interval PowerCutMod::LocalValidity(TimeValue t) {
    Interval iv = FOREVER;
    if (!pblock2) return iv;

    const int count = pblock2->Count(pb_spline_node);
    for (int n = 0; n < count; ++n) {
        INode* node = nullptr;
        Interval nodeIv;
        pblock2->GetValue(pb_spline_node, t, node, nodeIv, n);
        if (node) {
            Interval tmValid = FOREVER;
            node->GetNodeTM(t, &tmValid);
            iv &= tmValid;
            node->EvalWorldState(t);
        }
    }

    pblock2->GetValidity(t, iv);
    return iv;
}

void PowerCutMod::NotifyInputChanged(const Interval&, PartID, RefMessage, ModContext*) {}

// ═════════════════════════════════════════════════════════════════
//  UI
// ═════════════════════════════════════════════════════════════════
void PowerCutMod::BeginEditParams(IObjParam* objParam, ULONG flags, Animatable* prev) {
    ip = objParam;
    if (!pickCB) pickCB = new SplinePickModeCallback();
    pickCB->mod = this;

    pmapParam = CreateCPParamMap2(pblock2, objParam, hInstance,
        MAKEINTRESOURCE(IDD_PANEL), GetString(IDS_PARAMS), 0,
        new PowerCutDlgProc(this));
}

void PowerCutMod::EndEditParams(IObjParam* objParam, ULONG flags, Animatable* next) {
    ExitPickMode();
    if (pmapParam) { DestroyCPParamMap2(pmapParam); pmapParam = nullptr; }
    if (pickCB) pickCB->mod = nullptr;
    ip = nullptr;
}

// ═════════════════════════════════════════════════════════════════
//  PICK MODE
// ═════════════════════════════════════════════════════════════════
void PowerCutMod::EnterPickMode() {
    if (!ip || !pickCB) return;
    pickCB->mod = this;
    ip->SetPickMode(pickCB);
    inPickMode = true;
}

void PowerCutMod::ExitPickMode() {
    if (!ip || !inPickMode) return;
    ip->ClearPickMode();
    inPickMode = false;
}

// ═════════════════════════════════════════════════════════════════
//  FIND OWN NODE — walk dependents to find the INode we're applied to
// ═════════════════════════════════════════════════════════════════
class FindNodeDEP : public DependentEnumProc {
public:
    INode* node = nullptr;
    int proc(ReferenceMaker* rmaker) override {
        if (rmaker->SuperClassID() == BASENODE_CLASS_ID) {
            node = static_cast<INode*>(rmaker);
            return DEP_ENUM_HALT;
        }
        return DEP_ENUM_CONTINUE;
    }
};

INode* PowerCutMod::FindOwnNode() {
    FindNodeDEP dep;
    DoEnumDependents(&dep);
    return dep.node;
}

// ═════════════════════════════════════════════════════════════════
//  EXTRACT POLYLINES — spline node-local -> world -> mesh node-local
// ═════════════════════════════════════════════════════════════════
std::vector<ProjectedPolyline> PowerCutMod::ExtractPolylines(
    INode* splineNode, INode* meshNode, TimeValue t, int steps,
    Point3& outProjDir, int projMode, MNMesh* targetMesh) {

    std::vector<ProjectedPolyline> result;
    outProjDir = Point3(0, 0, -1);
    if (!splineNode) return result;

    ObjectState os = splineNode->EvalWorldState(t);
    Object* obj = os.obj;
    if (!obj || !obj->IsShapeObject()) return result;
    ShapeObject* shapeObj = static_cast<ShapeObject*>(obj);

    Matrix3 splineTM = splineNode->GetNodeTM(t);

    if (!meshNode)
        meshNode = FindOwnNode();

    Matrix3 meshTMInv;
    if (meshNode)
        meshTMInv = Inverse(meshNode->GetNodeTM(t));

    Matrix3 toMeshSpace = splineTM * meshTMInv;

    Point3 splineZ = Normalize(splineTM.GetRow(2));
    const Point3 splineZInMesh = Normalize(VectorTransform(meshTMInv, splineZ));

    switch (projMode) {
    case PROJ_NORMAL:
        outProjDir = splineZInMesh;
        break;
    case PROJ_SPLINE_Z:
    default:
        outProjDir = splineZInMesh;
        break;
    }

    struct RayProjHit {
        bool valid;
        float t;
        Point3 pt;
        int face;
    };

    auto projectPointToMesh = [&](const Point3& src, const Point3& axis) -> RayProjHit {
        if (!targetMesh)
            return {false, 0.0f, src, -1};

        const Point3 rayDir = Normalize(axis);
        auto cast = [&](const Point3& dir) -> RayProjHit {
            Ray ray(src, dir);
            float tHit = 0.0f;
            Point3 normal;
            int face = -1;
            Tab<float> bary;
            if (targetMesh->IntersectRay(ray, tHit, normal, face, bary) &&
                face >= 0 && tHit >= 0.0f) {
                return {true, tHit, src + dir * tHit, face};
            }
            return {false, 0.0f, src, -1};
        };

        const RayProjHit fwd = cast(rayDir);
        const RayProjHit back = cast(-rayDir);

        if (fwd.valid && (!back.valid || fwd.t <= back.t))
            return fwd;
        if (back.valid)
            return back;
        return {false, 0.0f, src, -1};
    };

    PolyShape pshape;
    shapeObj->MakePolyShape(t, pshape, steps, FALSE);
    const int curveCount = shapeObj->NumberOfCurves(t);

    for (int i = 0; i < pshape.numLines; i++) {
        PolyLine& pl = pshape.lines[i];
        if (pl.numPts < 2) continue;

        bool curveClosed = false;
        if (i < curveCount)
            curveClosed = (shapeObj->CurveClosed(t, i) != 0);

        const bool polyLineClosed = (pl.IsClosed() != 0);
        bool endpointClosed = false;
        if (pl.numPts >= 3) {
            const Point3& a = pl.pts[0].p;
            const Point3& b = pl.pts[pl.numPts - 1].p;
            endpointClosed = (Length(b - a) <= 1.0e-3f);
        }

        ProjectedPolyline pp;
        pp.closed = curveClosed || polyLineClosed || endpointClosed;
        pp.cutType = BOOLOP_CUT_REMOVE_IN; // will be overridden by caller
        pp.projDir = outProjDir;
        pp.depth = 0.0f;
        pp.flipDepth = false;
        // For open splines in Normal mode, points go onto the surface
        // (CutFace needs on-surface points).  For closed splines, keep
        // original positions — the prism boolean cuts through the mesh
        // at the right place without offsetting.
        pp.onSurface = (targetMesh && projMode == PROJ_NORMAL && !pp.closed);
        pp.pts.reserve(pl.numPts);

        Point3 normalAccum(0.0f, 0.0f, 0.0f);
        int normalHits = 0;

        for (int j = 0; j < pl.numPts; j++) {
            Point3 pt = pl.pts[j].p * toMeshSpace;

            if (targetMesh && projMode == PROJ_NORMAL) {
                const RayProjHit hit = projectPointToMesh(pt, outProjDir);
                if (hit.valid) {
                    // Only snap to surface for open splines (CutFace path).
                    // Closed splines keep original position so the prism
                    // intersects the mesh at the spline, not offset from it.
                    if (!pp.closed)
                        pt = hit.pt;
                    if (hit.face >= 0) {
                        Point3 faceNormal = targetMesh->GetFaceNormal(hit.face, true);
                        if (Length(faceNormal) > 1.0e-6f)
                            normalAccum += Normalize(faceNormal);
                        else
                            normalAccum += outProjDir;
                        ++normalHits;
                    }
                }
            }
            pp.pts.push_back(pt);
        }

        if (projMode == PROJ_NORMAL && normalHits > 0) {
            if (Length(normalAccum) > 1.0e-6f)
                pp.projDir = Normalize(normalAccum);
            else
                pp.projDir = outProjDir;
        }

        result.push_back(std::move(pp));
    }

    return result;
}

// ═════════════════════════════════════════════════════════════════
//  MODIFY OBJECT
// ═════════════════════════════════════════════════════════════════
void PowerCutMod::ModifyObject(TimeValue t, ModContext& mc,
                                    ObjectState* os, INode* node) {
    if (!os || !os->obj || !pblock2) return;

    const int nodeCount = pblock2->Count(pb_spline_node);
    if (nodeCount == 0) return;

    Interval iv = FOREVER;

    // Convert to poly.
    Object* obj = os->obj;
    MNMesh* pmesh = nullptr;

    if (obj->IsSubClassOf(polyObjectClassID)) {
        pmesh = &static_cast<PolyObject*>(obj)->GetMesh();
    } else if (obj->CanConvertToType(polyObjectClassID)) {
        PolyObject* poly = static_cast<PolyObject*>(obj->ConvertToType(t, polyObjectClassID));
        if (poly) {
            if (poly != obj) os->obj = poly;
            pmesh = &poly->GetMesh();
        }
    }
    if (!pmesh || pmesh->VNum() < 3 || pmesh->FNum() < 1) return;

    // Collect ALL polylines from ALL spline nodes first, each carrying
    // its own cutType / projDir / depth / flipDepth.  Then do ONE
    // Execute call so mode-grouping works across spline objects.
    std::vector<ProjectedPolyline> allPolylines;
    Point3 firstDir(0, 0, -1);

    float minWeld = 0.001f;
    bool anyKeepTris = false;

    for (int n = 0; n < nodeCount; ++n) {
        INode* splineNode = nullptr;
        Interval nodeIv;
        pblock2->GetValue(pb_spline_node, t, splineNode, nodeIv, n);
        if (!splineNode) continue;

        SplineSettings settings;
        GetSplineSettings(t, n, settings);

        if (settings.steps == 0) settings.steps = 1;
        if (settings.steps < PSHAPE_BUILTIN_STEPS)
            settings.steps = PSHAPE_BUILTIN_STEPS;

        Point3 dir;
        auto polys = ExtractPolylines(splineNode, node, t, settings.steps, dir,
                                      settings.projMode,
                                      (settings.projMode == PROJ_NORMAL) ? pmesh : nullptr);

        for (auto& pp : polys) {
            pp.cutType   = settings.mode;
            pp.depth     = settings.depth;
            pp.flipDepth = settings.flipDepth != 0;
        }

        if (!polys.empty()) {
            if (allPolylines.empty())
                firstDir = dir;
            minWeld = std::min(minWeld, settings.weldThresh);
            if (settings.keepTris) anyKeepTris = true;
            allPolylines.insert(allPolylines.end(),
                                std::make_move_iterator(polys.begin()),
                                std::make_move_iterator(polys.end()));
        }
    }

    if (allPolylines.empty()) return;

    CutOptions opts;
    opts.weldThresh = minWeld;
    opts.keepTris   = anyKeepTris;

    MeshCutter::Execute(*pmesh, allPolylines, firstDir, opts);

    pmesh->InvalidateGeomCache();
    os->obj->UpdateValidity(GEOM_CHAN_NUM, iv);
    os->obj->UpdateValidity(TOPO_CHAN_NUM, iv);
}
