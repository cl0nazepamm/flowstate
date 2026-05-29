#pragma once
#include <max.h>
#include <iparamb2.h>
#include <iparamm2.h>
#include <iparamm.h>
#include <simpmod.h>
#include <simpobj.h>
#include <modstack.h>
#include <mnmesh.h>
#include <polyobj.h>
#include <triobj.h>
#include <shape.h>
#include <splshape.h>
#include <spline3d.h>
#include <linshape.h>
#include <istdplug.h>
#include <inode.h>
#include <object.h>
#include <ref.h>
#include <maxapi.h>
#include "PrecisionCutDesc.h"
#include "SplineProjector.h"
#include "MeshCutter.h"
#include <vector>

// ── Forward declarations ────────────────────────────────────────
class PrecisionCutMod;
class PrecisionCutClassDesc;
class SplinePickModeCallback;

struct SplineSettings {
    int   steps       = PSHAPE_BUILTIN_STEPS;
    float weldThresh  = 0.001f;
    BOOL  keepTris    = FALSE;
    int   mode        = BOOLOP_CUT_REMOVE_IN;
    int   projMode    = PROJ_SPLINE_Z;
    BOOL  flipDepth   = FALSE;
    float depth       = 0.0f;
    BOOL  solid       = FALSE;
    float thickness   = 1.0f;
    BOOL  bevel       = FALSE;
    float bevelAmount = 0.5f;
    int   bevelSegs   = 1;
};

// ── Class descriptor singleton ──────────────────────────────────
PrecisionCutClassDesc* GetPrecisionCutDesc();

// ── The Modifier ────────────────────────────────────────────────
class PrecisionCutMod : public Modifier {
public:
    IParamBlock2* pblock2;
    IParamMap2*   pmapParam;   // manual param map for UI
    static IObjParam* ip;
    static SplinePickModeCallback* pickCB;
    static bool inPickMode;

    PrecisionCutMod();
    ~PrecisionCutMod() override;

    // ── Modifier overrides ──────────────────────────────────────
    ChannelMask ChannelsUsed() override;
    ChannelMask ChannelsChanged() override;
    Class_ID InputType() override;
    void ModifyObject(TimeValue t, ModContext& mc, ObjectState* os, INode* node) override;

    // ── Animatable ──────────────────────────────────────────────
    void DeleteThis() override { delete this; }
    void GetClassName(TSTR& s, bool localized) const override { s = _T("Precision Cut"); }
    SClass_ID SuperClassID() override { return OSM_CLASS_ID; }
    Class_ID ClassID() override { return PRECISIONCUT_CLASS_ID; }
    const TCHAR* GetObjectName(bool localized) const override { return _T("Precision Cut"); }

    // ── Reference management ────────────────────────────────────
    int NumRefs() override { return 1; }
    RefTargetHandle GetReference(int i) override { return (i == 0) ? pblock2 : nullptr; }
    void SetReference(int i, RefTargetHandle rtarg) override;
    RefResult NotifyRefChanged(const Interval& iv, RefTargetHandle hTarg,
                               PartID& partID, RefMessage msg, BOOL propagate) override;

    // ── SubAnims / ParamBlocks ──────────────────────────────────
    int NumSubs() override { return 1; }
    Animatable* SubAnim(int i) override { return (i == 0) ? pblock2 : nullptr; }
    TSTR SubAnimName(int i, bool localized) override { return _T("Parameters"); }
    int NumParamBlocks() override { return 1; }
    IParamBlock2* GetParamBlock(int i) override { return (i == 0) ? pblock2 : nullptr; }
    IParamBlock2* GetParamBlockByID(BlockID id) override { return (pblock2 && pblock2->ID() == id) ? pblock2 : nullptr; }

    // ── Clone ───────────────────────────────────────────────────
    RefTargetHandle Clone(RemapDir& remap) override;

    // ── IO ──────────────────────────────────────────────────────
    IOResult Load(ILoad* iload) override { return Modifier::Load(iload); }
    IOResult Save(ISave* isave) override { return Modifier::Save(isave); }

    // ── Validity ────────────────────────────────────────────────
    Interval LocalValidity(TimeValue t) override;
    void NotifyInputChanged(const Interval& changeInt, PartID partID,
                            RefMessage message, ModContext* mc) override;

    // ── UI ──────────────────────────────────────────────────────
    void BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev) override;
    void EndEditParams(IObjParam* ip, ULONG flags, Animatable* next) override;
    CreateMouseCallBack* GetCreateMouseCallBack() override { return nullptr; }

    // ── Pick mode helpers ───────────────────────────────────────
    void EnterPickMode();
    void ExitPickMode();

    // ── Viewport projection ─────────────────────────────────────
    Point3 GetViewProjDir(TimeValue t) const;

    // ── Per-spline settings helpers ────────────────────────────
    void EnsureSplineSettingsCount(TimeValue t);
    void GetGlobalSettings(TimeValue t, SplineSettings& settings) const;
    void GetSplineSettings(TimeValue t, int index, SplineSettings& settings) const;
    void AppendSpline(INode* splineNode, TimeValue t);
    void DeleteSpline(int index);
    void SetSelectedSplineIndex(int index) { selectedSplineIndex_ = index; }
    int GetSelectedSplineIndex() const { return selectedSplineIndex_; }
    void SyncSelectedSplineToGlobals(TimeValue t);
    void SyncGlobalParamToSelectedSpline(TimeValue t, ParamID pid);

private:
    // Extract polylines in mesh object space + projection direction
    std::vector<ProjectedPolyline> ExtractPolylines(INode* splineNode, INode* meshNode,
                                                     TimeValue t, int steps, Point3& outProjDir,
                                                     int projMode, MNMesh* targetMesh);

    // Find the INode this modifier is applied to (walks reference chain)
    INode* FindOwnNode();

    int selectedSplineIndex_ = -1;
    bool syncingSelectedSpline_ = false;
};

// ── Class Descriptor ────────────────────────────────────────────
class PrecisionCutClassDesc : public ClassDesc2 {
public:
    int IsPublic() override { return TRUE; }
    void* Create(BOOL) override { return new PrecisionCutMod(); }
    const TCHAR* ClassName() override { return _T("Precision Cut"); }
    const TCHAR* NonLocalizedClassName() override { return _T("Precision Cut"); }
    SClass_ID SuperClassID() override { return OSM_CLASS_ID; }
    Class_ID ClassID() override { return PRECISIONCUT_CLASS_ID; }
    const TCHAR* Category() override { return _T("Clone Pipeline"); }
    const TCHAR* InternalName() override { return _T("Precision Cut"); }
    HINSTANCE HInstance() override { return hInstance; }
};
