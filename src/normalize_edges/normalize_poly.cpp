#include <max.h>
#include <iparamb2.h>
#include <iparamm2.h>
#include <plugapi.h>
#include <maxapi.h>
#include <polyobj.h>
#include <mnmesh.h>
#include "resource.h"
#include <vector>

#define NORMALIZE_POLY_CLASS_ID Class_ID(0xA1B2C3D4, 0x12345678)
#define NORMALIZE_POLY_NAME     _T("Normalize Poly")

extern HINSTANCE hInstance;

enum { pblock_main };
enum { pb_threshold, pb_select_only };

class NormalizePolyClassDesc : public ClassDesc2 {
public:
    int IsPublic() override                          { return TRUE; }
    void* Create(BOOL loading) override;
    const TCHAR* ClassName() override                { return NORMALIZE_POLY_NAME; }
    const TCHAR* NonLocalizedClassName() override    { return NORMALIZE_POLY_NAME; }
    SClass_ID SuperClassID() override                { return OSM_CLASS_ID; }
    Class_ID ClassID() override                      { return NORMALIZE_POLY_CLASS_ID; }
    const TCHAR* Category() override                 { return _T("Clone Tools"); }
    const TCHAR* InternalName() override             { return _T("NormalizePoly"); }
    HINSTANCE HInstance() override                   { return hInstance; }
};

static NormalizePolyClassDesc descInst;

ClassDesc* GetNormalizePolyDesc() {
    return &descInst;
}

static ParamBlockDesc2 paramblockDesc(
    pblock_main, _T("params"), 0, &descInst,
    P_AUTO_CONSTRUCT | P_AUTO_UI,
    0,
    IDD_NORMALIZE_POLY, IDS_PARAMS, 0, 0, NULL,

    pb_threshold, _T("threshold"), TYPE_FLOAT, P_ANIMATABLE, IDS_THRESHOLD,
        p_default,  10.0f,
        p_range,    0.0f, 100.0f,
        p_ui,       TYPE_SPINNER, EDITTYPE_FLOAT,
                    IDC_THRESHOLD, IDC_THRESHOLD_SPIN, 0.1f,
        p_end,

    pb_select_only, _T("selectOnly"), TYPE_BOOL, 0, IDS_SELECT_ONLY,
        p_default,  TRUE,
        p_ui,       TYPE_SINGLECHECKBOX, IDC_SELECT_ONLY,
        p_end,

    p_end
);

class NormalizePolyMod : public Modifier {
public:
    IParamBlock2* pblock = nullptr;

    NormalizePolyMod() { descInst.MakeAutoParamBlocks(this); }

    // Animatable
    void DeleteThis() override                       { delete this; }
    Class_ID ClassID() override                      { return NORMALIZE_POLY_CLASS_ID; }
    SClass_ID SuperClassID() override                { return OSM_CLASS_ID; }
    void GetClassName(MSTR& s, bool) const override  { s = NORMALIZE_POLY_NAME; }
    MSTR GetName(bool) const override                { return NORMALIZE_POLY_NAME; }
    int NumSubs() override                           { return 1; }
    Animatable* SubAnim(int) override                { return pblock; }
    MSTR SubAnimName(int, bool) override             { return _T("Parameters"); }
    int NumParamBlocks() override                    { return 1; }
    IParamBlock2* GetParamBlock(int) override        { return pblock; }
    IParamBlock2* GetParamBlockByID(BlockID id) override {
        return (pblock && pblock->ID() == id) ? pblock : nullptr;
    }

    // ReferenceMaker
    int NumRefs() override                           { return 1; }
    RefTargetHandle GetReference(int) override       { return pblock; }
    void SetReference(int, RefTargetHandle rtarg) override {
        pblock = (IParamBlock2*)rtarg;
    }
    RefResult NotifyRefChanged(const Interval&, RefTargetHandle, PartID&,
                               RefMessage, BOOL) override {
        return REF_SUCCEED;
    }

    // ReferenceTarget
    RefTargetHandle Clone(RemapDir& remap) override {
        NormalizePolyMod* clone = new NormalizePolyMod();
        clone->ReplaceReference(0, remap.CloneRef(pblock));
        BaseClone(this, clone, remap);
        return clone;
    }

    // Modifier
    ChannelMask ChannelsUsed() override {
        return GEOM_CHANNEL | TOPO_CHANNEL | SELECT_CHANNEL
             | TEXMAP_CHANNEL | VERTCOLOR_CHANNEL;
    }
    ChannelMask ChannelsChanged() override {
        return GEOM_CHANNEL | TOPO_CHANNEL | SELECT_CHANNEL
             | TEXMAP_CHANNEL | VERTCOLOR_CHANNEL;
    }
    Class_ID InputType() override                    { return polyObjectClassID; }
    BOOL DependOnTopology(ModContext&) override      { return TRUE; }
    void ModifyObject(TimeValue t, ModContext& mc, ObjectState* os, INode* node) override;

    // UI
    CreateMouseCallBack* GetCreateMouseCallBack()    { return nullptr; }
    void BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev) override {
        descInst.BeginEditParams(ip, this, flags, prev);
    }
    void EndEditParams(IObjParam* ip, ULONG flags, Animatable* next) override {
        descInst.EndEditParams(ip, this, flags, next);
    }
};

void* NormalizePolyClassDesc::Create(BOOL) { return new NormalizePolyMod(); }

// ── Core algorithm ──────────────────────────────────────────────────
// For every vertex with exactly 2 incident edges, compute
//   weight = | normalize(P(v0) - P(v)) + normalize(P(v1) - P(v)) |
//     ~0   → vertex sits on a straight line (redundant)
//     √2   → 90° bend
//     ~2   → folded back on itself
// Threshold matches Shiva VertexCleaner: slider 0..100 → cutoff 0..1.573.
void NormalizePolyMod::ModifyObject(TimeValue t, ModContext& /*mc*/,
                                    ObjectState* os, INode* /*node*/)
{
    // InputType() == polyObjectClassID guarantees os->obj is a PolyObject;
    // the modifier stack handles the upstream conversion (e.g. from Edit Mesh).
    if (!os->obj) return;
    PolyObject* po = (PolyObject*)os->obj;

    MNMesh& mesh = po->GetMesh();
    const int numV = mesh.VNum();
    if (numV < 3) return;

    mesh.FillInVertEdgesFaces();

    const float threshold  = pblock->GetFloat(pb_threshold,   t) * 0.01573f;
    const BOOL  selectOnly = pblock->GetInt  (pb_select_only, t);

    std::vector<unsigned char> mark(numV, 0);

    #pragma omp parallel for schedule(static)
    for (int v = 0; v < numV; ++v) {
        const Tab<int>& edges = mesh.vedg[v];
        if (edges.Count() != 2) continue;

        const MNEdge* e0 = mesh.E(edges[0]);
        const MNEdge* e1 = mesh.E(edges[1]);
        const int v0 = e0->OtherVert(v);
        const int v1 = e1->OtherVert(v);

        const Point3& p = mesh.P(v);
        Point3 d0 = mesh.P(v0) - p;
        Point3 d1 = mesh.P(v1) - p;

        const float l0 = d0.Length();
        const float l1 = d1.Length();
        if (l0 <= 1e-8f || l1 <= 1e-8f) continue;
        d0 /= l0;
        d1 /= l1;

        if ((d0 + d1).Length() <= threshold) mark[v] = 1;
    }

    if (selectOnly) {
        BitArray sel(numV);
        sel.ClearAll();
        for (int v = 0; v < numV; ++v) {
            if (mark[v]) sel.Set(v);
        }
        mesh.VertexSelect(sel);
    } else {
        std::vector<int> kill;
        kill.reserve(numV / 8);
        for (int v = 0; v < numV; ++v) {
            if (mark[v]) kill.push_back(v);
        }
        if (!kill.empty()) {
            mesh.RemoveVertices(kill.data(), (int)kill.size());
            mesh.CollapseDeadStructs();
        }
    }
}
