#include <max.h>
#include <iparamb2.h>
#include <iparamm2.h>
#include <plugapi.h>
#include <triobj.h>

#include "loop_subdivision.h"
#include "loop_subdivision_resource.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

#define CLONE_LOOP_CLASS_ID Class_ID(0x4f6b2d13, 0x7a91c5e4)
#define CLONE_LOOP_NAME     _T("Loop Subdivision")
#define CLONE_LOOP_CATEGORY _T("Clone Tools")

extern HINSTANCE hInstance;

namespace {

constexpr int kParamRef = 0;
constexpr int kMaxIterations = 5;
constexpr int kMaxFaces = 2 * 1024 * 1024;
constexpr int kMaxVerts = 2 * 1024 * 1024;

enum { pblock_main };
enum { pb_iterations };

struct EdgeKey {
    int a = 0;
    int b = 0;

    EdgeKey() = default;
    EdgeKey(int v0, int v1) : a(std::min(v0, v1)), b(std::max(v0, v1)) {}

    bool operator==(const EdgeKey& rhs) const {
        return a == rhs.a && b == rhs.b;
    }
};

struct EdgeKeyHash {
    size_t operator()(const EdgeKey& key) const {
        const uint64_t packed =
            (static_cast<uint64_t>(static_cast<uint32_t>(key.a)) << 32) |
             static_cast<uint32_t>(key.b);
        return static_cast<size_t>(packed ^ (packed >> 33));
    }
};

struct MapEdgeKey {
    int a = 0;
    int b = 0;

    MapEdgeKey() = default;
    MapEdgeKey(int v0, int v1) : a(std::min(v0, v1)), b(std::max(v0, v1)) {}

    bool operator==(const MapEdgeKey& rhs) const {
        return a == rhs.a && b == rhs.b;
    }
};

struct MapEdgeKeyHash {
    size_t operator()(const MapEdgeKey& key) const {
        const uint64_t packed =
            (static_cast<uint64_t>(static_cast<uint32_t>(key.a)) << 32) |
             static_cast<uint32_t>(key.b);
        return static_cast<size_t>(packed ^ (packed >> 33));
    }
};

struct EdgeInfo {
    int v0 = 0;
    int v1 = 0;
    int opposite[2] = { -1, -1 };
    int faceCount = 0;
    int oddIndex = -1;
};

struct FaceOut {
    int v[3] = { 0, 0, 0 };
    DWORD smGroup = 0;
    MtlID matID = 0;
    bool hidden = false;
};

class CloneLoopModifier;

class CloneLoopClassDesc : public ClassDesc2 {
public:
    int IsPublic() override { return TRUE; }
    void* Create(BOOL) override;
    const TCHAR* ClassName() override { return CLONE_LOOP_NAME; }
    const TCHAR* NonLocalizedClassName() override { return CLONE_LOOP_NAME; }
    SClass_ID SuperClassID() override { return OSM_CLASS_ID; }
    Class_ID ClassID() override { return CLONE_LOOP_CLASS_ID; }
    const TCHAR* Category() override { return CLONE_LOOP_CATEGORY; }
    const TCHAR* InternalName() override { return _T("CloneLoop"); }
    HINSTANCE HInstance() override { return hInstance; }
};

static CloneLoopClassDesc gDesc;

static ParamBlockDesc2 gParamBlock(
    pblock_main, _T("params"), IDS_LOOP_SUBDIVISION_PARAMS, &gDesc,
    P_AUTO_CONSTRUCT | P_AUTO_UI,
    kParamRef,
    IDD_LOOP_SUBDIVISION_PANEL, IDS_LOOP_SUBDIVISION_PARAMS, 0, 0, nullptr,

    pb_iterations, _T("iterations"), TYPE_INT, P_ANIMATABLE, IDS_LOOP_SUBDIVISION_ITERATIONS,
        p_default, 0,
        p_range, 0, kMaxIterations,
        p_ui, TYPE_SPINNER, EDITTYPE_POS_INT,
              IDC_LOOP_SUBDIVISION_ITERATIONS, IDC_LOOP_SUBDIVISION_SPIN, 1.0f,
        p_end,

    p_end
);

static int ClampIterations(int value) {
    return std::max(0, std::min(value, kMaxIterations));
}

static float LoopBeta(int valence) {
    if (valence <= 2) {
        return 0.0f;
    }

    const double n = static_cast<double>(valence);
    const double c = std::cos(2.0 * 3.14159265358979323846 / n);
    const double term = 0.375 + 0.25 * c;
    return static_cast<float>((0.625 - term * term) / n);
}

static bool BuildEdgeData(
    const Mesh& mesh,
    std::unordered_map<EdgeKey, EdgeInfo, EdgeKeyHash>& edges,
    std::vector<std::vector<int>>& neighbors,
    std::vector<std::vector<int>>& boundaryNeighbors,
    std::vector<unsigned char>& nonManifoldVerts)
{
    const int faceCount = mesh.getNumFaces();
    const int vertCount = mesh.getNumVerts();

    edges.clear();
    edges.reserve(static_cast<size_t>(faceCount) * 3);
    neighbors.assign(vertCount, {});
    boundaryNeighbors.assign(vertCount, {});
    nonManifoldVerts.assign(vertCount, 0);

    auto addNeighbor = [&neighbors](int v, int n) {
        std::vector<int>& list = neighbors[v];
        if (std::find(list.begin(), list.end(), n) == list.end()) {
            list.push_back(n);
        }
    };

    auto addEdge = [&edges, &addNeighbor](int a, int b, int opposite) {
        addNeighbor(a, b);
        addNeighbor(b, a);

        const EdgeKey key(a, b);
        auto [it, inserted] = edges.emplace(key, EdgeInfo{});
        EdgeInfo& edge = it->second;
        if (inserted) {
            edge.v0 = key.a;
            edge.v1 = key.b;
        }

        if (edge.faceCount < 2) {
            edge.opposite[edge.faceCount] = opposite;
        }
        ++edge.faceCount;
    };

    for (int f = 0; f < faceCount; ++f) {
        Face& face = mesh.faces[f];
        const int a = static_cast<int>(face.v[0]);
        const int b = static_cast<int>(face.v[1]);
        const int c = static_cast<int>(face.v[2]);

        if (a < 0 || a >= vertCount || b < 0 || b >= vertCount || c < 0 || c >= vertCount) {
            return false;
        }
        if (a == b || b == c || c == a) {
            return false;
        }

        addEdge(a, b, c);
        addEdge(b, c, a);
        addEdge(c, a, b);
    }

    for (const auto& item : edges) {
        const EdgeInfo& edge = item.second;
        if (edge.faceCount == 1) {
            boundaryNeighbors[edge.v0].push_back(edge.v1);
            boundaryNeighbors[edge.v1].push_back(edge.v0);
        } else if (edge.faceCount > 2) {
            nonManifoldVerts[edge.v0] = 1;
            nonManifoldVerts[edge.v1] = 1;
        }
    }

    return true;
}

static void SetOutputFace(Mesh& mesh, int index, const FaceOut& src) {
    Face& face = mesh.faces[index];
    face.setVerts(src.v[0], src.v[1], src.v[2]);
    face.setEdgeVisFlags(1, 1, 1);
    face.setSmGroup(src.smGroup);
    face.setMatID(src.matID);
    face.SetHide(src.hidden ? TRUE : FALSE);
}

static bool SubdivideMapChannel(
    Mesh& source,
    Mesh& dest,
    int mapChannel)
{
    if (!source.mapSupport(mapChannel)) {
        return true;
    }

    const int srcMapVerts = source.getNumMapVerts(mapChannel);
    const int srcMapFaces = source.Map(mapChannel).fnum;
    if (srcMapVerts <= 0 || srcMapFaces != source.getNumFaces()) {
        return true;
    }

    const UVVert* srcVerts = source.mapVerts(mapChannel);
    const TVFace* srcFaces = source.mapFaces(mapChannel);
    if (!srcVerts || !srcFaces) {
        return true;
    }

    const int64_t maxMapVerts =
        static_cast<int64_t>(srcMapVerts) + static_cast<int64_t>(source.getNumFaces()) * 3;
    if (maxMapVerts > kMaxVerts || maxMapVerts > std::numeric_limits<int>::max()) {
        return false;
    }

    std::vector<UVVert> outVerts;
    outVerts.reserve(static_cast<size_t>(srcMapVerts) + static_cast<size_t>(source.getNumFaces()) * 3);
    for (int i = 0; i < srcMapVerts; ++i) {
        outVerts.push_back(srcVerts[i]);
    }

    std::unordered_map<MapEdgeKey, int, MapEdgeKeyHash> edgeMap;
    edgeMap.reserve(static_cast<size_t>(source.getNumFaces()) * 3);

    auto midpoint = [&](int a, int b) -> int {
        const MapEdgeKey key(a, b);
        auto found = edgeMap.find(key);
        if (found != edgeMap.end()) {
            return found->second;
        }

        const int idx = static_cast<int>(outVerts.size());
        outVerts.push_back((srcVerts[a] + srcVerts[b]) * 0.5f);
        edgeMap.emplace(key, idx);
        return idx;
    };

    std::vector<TVFace> outFaces(static_cast<size_t>(source.getNumFaces()) * 4);
    for (int f = 0; f < source.getNumFaces(); ++f) {
        const TVFace& face = srcFaces[f];
        const int a = static_cast<int>(face.t[0]);
        const int b = static_cast<int>(face.t[1]);
        const int c = static_cast<int>(face.t[2]);

        if (a < 0 || a >= srcMapVerts || b < 0 || b >= srcMapVerts || c < 0 || c >= srcMapVerts) {
            // A malformed optional map channel must not cancel otherwise
            // valid geometry subdivision.  Leave this channel unsupported.
            return true;
        }

        const int ab = midpoint(a, b);
        const int bc = midpoint(b, c);
        const int ca = midpoint(c, a);
        const int base = f * 4;

        outFaces[base + 0].setTVerts(a, ab, ca);
        outFaces[base + 1].setTVerts(b, bc, ab);
        outFaces[base + 2].setTVerts(c, ca, bc);
        outFaces[base + 3].setTVerts(ab, bc, ca);
    }

    dest.setMapSupport(mapChannel, TRUE);
    dest.setNumMapVerts(mapChannel, static_cast<int>(outVerts.size()), FALSE);
    dest.setNumMapFaces(mapChannel, static_cast<int>(outFaces.size()), FALSE);

    UVVert* dstVerts = dest.mapVerts(mapChannel);
    TVFace* dstFaces = dest.mapFaces(mapChannel);
    if (!dstVerts || !dstFaces) {
        return false;
    }

    for (int i = 0; i < static_cast<int>(outVerts.size()); ++i) {
        dstVerts[i] = outVerts[i];
    }

    for (int i = 0; i < static_cast<int>(outFaces.size()); ++i) {
        dstFaces[i] = outFaces[i];
    }

    return true;
}

static bool LoopSubdivideOnce(Mesh& mesh) {
    int srcVerts = mesh.getNumVerts();
    int srcFaces = mesh.getNumFaces();
    if (srcVerts < 3 || srcFaces < 1) {
        return true;
    }

    // Reject topology that cannot fit before paying for the O(F) adjacency
    // build. Face cleanup is intentionally not attempted for oversized input.
    const int64_t initialFaceEstimate = static_cast<int64_t>(srcFaces) * 4;
    if (srcVerts > kMaxVerts || initialFaceEstimate > kMaxFaces ||
        initialFaceEstimate > std::numeric_limits<int>::max()) {
        return false;
    }

    std::unordered_map<EdgeKey, EdgeInfo, EdgeKeyHash> edges;
    std::vector<std::vector<int>> neighbors;
    std::vector<std::vector<int>> boundaryNeighbors;
    std::vector<unsigned char> nonManifoldVerts;
    std::unique_ptr<Mesh> cleaned;
    Mesh* source = &mesh;
    if (!BuildEdgeData(mesh, edges, neighbors, boundaryNeighbors, nonManifoldVerts)) {
        // Valid meshes pay no extra scan.  Only retry malformed converter
        // output after Max removes illegal and repeated-index faces for us.
        // Keep the copy private until the complete output mesh is ready.
        cleaned = std::make_unique<Mesh>(mesh);
        const bool removedIllegal = cleaned->RemoveIllegalFaces() != FALSE;
        const bool removedDegenerate = cleaned->RemoveDegenerateFaces() != FALSE;
        if (!removedIllegal && !removedDegenerate) {
            return false;
        }

        srcVerts = cleaned->getNumVerts();
        srcFaces = cleaned->getNumFaces();
        if (srcVerts < 3 || srcFaces < 1 ||
            !BuildEdgeData(*cleaned, edges, neighbors, boundaryNeighbors, nonManifoldVerts)) {
            return false;
        }
        source = cleaned.get();
    }

    const int64_t nextVertEstimate =
        static_cast<int64_t>(srcVerts) + static_cast<int64_t>(edges.size());
    const int64_t nextFaceEstimate = static_cast<int64_t>(srcFaces) * 4;
    if (nextVertEstimate > kMaxVerts || nextFaceEstimate > kMaxFaces ||
        nextVertEstimate > std::numeric_limits<int>::max() ||
        nextFaceEstimate > std::numeric_limits<int>::max()) {
        return false;
    }

    const int nextVerts = static_cast<int>(nextVertEstimate);
    Mesh& input = *source;

    std::vector<Point3> outVerts(static_cast<size_t>(nextVerts));
    for (int v = 0; v < srcVerts; ++v) {
        const Point3& p = input.verts[v];
        const std::vector<int>& boundary = boundaryNeighbors[v];

        if (boundary.size() == 2) {
            outVerts[v] = p * 0.75f +
                          (input.verts[boundary[0]] + input.verts[boundary[1]]) * 0.125f;
        } else if (boundary.empty() && !nonManifoldVerts[v] && neighbors[v].size() > 2) {
            const float beta = LoopBeta(static_cast<int>(neighbors[v].size()));
            Point3 sum(0.0f, 0.0f, 0.0f);
            for (const int n : neighbors[v]) {
                sum += input.verts[n];
            }
            outVerts[v] = p * (1.0f - beta * static_cast<float>(neighbors[v].size())) + sum * beta;
        } else {
            outVerts[v] = p;
        }
    }

    int oddIndex = srcVerts;
    for (auto& item : edges) {
        EdgeInfo& edge = item.second;
        edge.oddIndex = oddIndex;

        const Point3& p0 = input.verts[edge.v0];
        const Point3& p1 = input.verts[edge.v1];
        if (edge.faceCount == 2 && edge.opposite[0] >= 0 && edge.opposite[1] >= 0) {
            outVerts[oddIndex] =
                (p0 + p1) * 0.375f +
                (input.verts[edge.opposite[0]] + input.verts[edge.opposite[1]]) * 0.125f;
        } else {
            outVerts[oddIndex] = (p0 + p1) * 0.5f;
        }
        ++oddIndex;
    }

    std::vector<FaceOut> outFaces(static_cast<size_t>(srcFaces) * 4);
    for (int f = 0; f < srcFaces; ++f) {
        Face& face = input.faces[f];
        const int a = static_cast<int>(face.v[0]);
        const int b = static_cast<int>(face.v[1]);
        const int c = static_cast<int>(face.v[2]);

        const int ab = edges[EdgeKey(a, b)].oddIndex;
        const int bc = edges[EdgeKey(b, c)].oddIndex;
        const int ca = edges[EdgeKey(c, a)].oddIndex;
        const int base = f * 4;

        const DWORD sm = face.getSmGroup();
        const MtlID mt = face.getMatID();
        const bool hidden = face.Hidden() != FALSE;

        outFaces[base + 0] = { { a, ab, ca }, sm, mt, hidden };
        outFaces[base + 1] = { { b, bc, ab }, sm, mt, hidden };
        outFaces[base + 2] = { { c, ca, bc }, sm, mt, hidden };
        outFaces[base + 3] = { { ab, bc, ca }, sm, mt, hidden };

    }

    Mesh result;
    result.setNumVerts(nextVerts, FALSE);
    result.setNumFaces(static_cast<int>(outFaces.size()), FALSE);

    for (int v = 0; v < nextVerts; ++v) {
        result.setVert(v, outVerts[v]);
    }
    for (int f = 0; f < static_cast<int>(outFaces.size()); ++f) {
        SetOutputFace(result, f, outFaces[f]);
    }

    const int mapCount = input.getNumMaps();
    result.setNumMaps(mapCount, FALSE);
    for (int mp = -NUM_HIDDENMAPS; mp < mapCount; ++mp) {
        if (!SubdivideMapChannel(input, result, mp)) {
            return false;
        }
    }

    mesh = result;
    return true;
}

static TriObject* GetTriObjectForSubdivision(TimeValue t, ObjectState* os) {
    Object* const input = os ? os->obj : nullptr;
    if (!input) {
        return nullptr;
    }

    // Preserve the zero-conversion hot path used by Editable Mesh and any
    // primitive or modifier stack that already evaluates to a TriObject.
    if (input->IsSubClassOf(triObjectClassID)) {
        return static_cast<TriObject*>(input);
    }

    // Editable Poly, patches, NURBS, body objects, and third-party geometry
    // use Max's native one-step TriObject conversion.  For PolyObject this is
    // the SDK-owned MNMesh bridge, which preserves its conversion semantics.
    if (!input->CanConvertToType(triObjectClassID)) {
        return nullptr;
    }

    Object* const converted = input->ConvertToType(t, triObjectClassID);
    if (!converted || !converted->IsSubClassOf(triObjectClassID)) {
        if (converted && converted != input) {
            converted->DeleteThis();
        }
        return nullptr;
    }

    if (converted != input) {
        os->obj = converted;
        os->obj->UnlockObject();
    }
    return static_cast<TriObject*>(converted);
}

static void UpdateSubdivisionValidity(Object* object, const Interval& valid) {
    if (!object) {
        return;
    }

    object->UpdateValidity(GEOM_CHAN_NUM, valid);
    object->UpdateValidity(TOPO_CHAN_NUM, valid);
    object->UpdateValidity(TEXMAP_CHAN_NUM, valid);
    object->UpdateValidity(VERT_COLOR_CHAN_NUM, valid);
    object->UpdateValidity(SELECT_CHAN_NUM, valid);
    object->UpdateValidity(SUBSEL_TYPE_CHAN_NUM, valid);
    object->UpdateValidity(DISP_ATTRIB_CHAN_NUM, valid);
    object->UpdateValidity(GFX_DATA_CHAN_NUM, valid);
}

class CloneLoopModifier : public Modifier {
public:
    IParamBlock2* pblock = nullptr;

    CloneLoopModifier() {
        gDesc.MakeAutoParamBlocks(this);
    }

    void DeleteThis() override { delete this; }
    Class_ID ClassID() override { return CLONE_LOOP_CLASS_ID; }
    SClass_ID SuperClassID() override { return OSM_CLASS_ID; }
    void GetClassName(MSTR& s, bool) const override { s = CLONE_LOOP_NAME; }
    MSTR GetName(bool) const override { return CLONE_LOOP_NAME; }
    const TCHAR* GetObjectName(bool) const override { return CLONE_LOOP_NAME; }

    int NumSubs() override { return 1; }
    Animatable* SubAnim(int i) override { return i == 0 ? pblock : nullptr; }
    MSTR SubAnimName(int i, bool) override { return i == 0 ? _T("Parameters") : _T(""); }

    int NumParamBlocks() override { return 1; }
    IParamBlock2* GetParamBlock(int i) override { return i == 0 ? pblock : nullptr; }
    IParamBlock2* GetParamBlockByID(BlockID id) override {
        return (pblock && pblock->ID() == id) ? pblock : nullptr;
    }

    int NumRefs() override { return 1; }
    RefTargetHandle GetReference(int i) override { return i == 0 ? pblock : nullptr; }
    void SetReference(int i, RefTargetHandle rtarg) override {
        if (i == 0) {
            pblock = static_cast<IParamBlock2*>(rtarg);
        }
    }

    RefResult NotifyRefChanged(const Interval&, RefTargetHandle, PartID&, RefMessage, BOOL) override {
        return REF_SUCCEED;
    }

    RefTargetHandle Clone(RemapDir& remap) override {
        CloneLoopModifier* clone = new CloneLoopModifier();
        clone->ReplaceReference(kParamRef, remap.CloneRef(pblock));
        BaseClone(this, clone, remap);
        return clone;
    }

    ChannelMask ChannelsUsed() override {
        return GEOM_CHANNEL | TOPO_CHANNEL | TEXMAP_CHANNEL | VERTCOLOR_CHANNEL |
               SELECT_CHANNEL | SUBSEL_TYPE_CHANNEL | DISP_ATTRIB_CHANNEL;
    }

    ChannelMask ChannelsChanged() override {
        return GEOM_CHANNEL | TOPO_CHANNEL | TEXMAP_CHANNEL | VERTCOLOR_CHANNEL |
               SELECT_CHANNEL | SUBSEL_TYPE_CHANNEL | DISP_ATTRIB_CHANNEL;
    }

    Class_ID InputType() override {
        // Match Max's own broad topology modifiers: accept every deformable
        // primitive/mesh and perform the fastest supported conversion below.
        return defObjectClassID;
    }

    BOOL DependOnTopology(ModContext&) override {
        return TRUE;
    }

    Interval LocalValidity(TimeValue t) override {
        Interval valid = FOREVER;
        if (pblock) {
            pblock->GetValidity(t, valid);
        }
        return valid;
    }

    void ModifyObject(TimeValue t, ModContext&, ObjectState* os, INode*) override {
        if (!os || !os->obj || !pblock) {
            return;
        }

        // UpdateValidity intersects this modifier's validity with each input
        // channel independently, preserving Max's per-channel cache ranges.
        const Interval valid = LocalValidity(t);

        const int iterations = ClampIterations(pblock->GetInt(pb_iterations, t));
        if (iterations == 0) {
            UpdateSubdivisionValidity(os->obj, valid);
            return;
        }

        TriObject* const tri = GetTriObjectForSubdivision(t, os);
        if (!tri) {
            UpdateSubdivisionValidity(os->obj, valid);
            return;
        }

        Mesh& mesh = tri->GetMesh();

        bool changed = false;
        for (int i = 0; i < iterations; ++i) {
            if (!LoopSubdivideOnce(mesh)) {
                break;
            }
            changed = true;
        }

        if (changed) {
            mesh.InvalidateGeomCache();
            mesh.InvalidateTopologyCache();
            mesh.buildNormals();
            mesh.buildBoundingBox();
        }

        UpdateSubdivisionValidity(tri, valid);
    }

    void BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev) override {
        gDesc.BeginEditParams(ip, this, flags, prev);
    }

    void EndEditParams(IObjParam* ip, ULONG flags, Animatable* next) override {
        gDesc.EndEditParams(ip, this, flags, next);
    }

    CreateMouseCallBack* GetCreateMouseCallBack() override {
        return nullptr;
    }

    IOResult Save(ISave* isave) override {
        return Modifier::Save(isave);
    }

    IOResult Load(ILoad* iload) override {
        return Modifier::Load(iload);
    }
};

void* CloneLoopClassDesc::Create(BOOL) {
    return new CloneLoopModifier();
}

} // namespace

ClassDesc* GetLoopSubdivisionDesc() {
    return &gDesc;
}
