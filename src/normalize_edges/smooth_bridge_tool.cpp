// Smooth Bridge — function-published Edit Poly command.
//
// Detects two connected edge rings in the active Editable Poly's edge
// selection and bridges them with G1 Hermite curves (tangent matched to
// the surrounding mesh; near-circular at tension≈1).
//
// MAXScript surface:
//   SmoothBridge.bridge          segments tension     -- one-shot
//   SmoothBridge.beginPreview                         -- snapshot mesh
//   SmoothBridge.preview         segments tension     -- restore + apply
//   SmoothBridge.commitPreview   segments tension     -- finalize w/ undo
//   SmoothBridge.cancelPreview                        -- restore original

#include <max.h>
#include <iparamb2.h>
#include <iFnPub.h>
#include <plugapi.h>
#include <maxapi.h>
#include <polyobj.h>
#include <mnmesh.h>
#include <hold.h>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <limits>
#include <algorithm>
#include <cmath>

#define SMOOTH_BRIDGE_INTERFACE_ID Interface_ID(0xC3D4E5F6, 0x34567890)

enum {
    sb_fn_bridge,
    sb_fn_beginPreview,
    sb_fn_preview,
    sb_fn_commitPreview,
    sb_fn_cancelPreview,
};

static void RefreshPolyObjectMesh(PolyObject* po);

// ── Undo restore: snapshot the entire MNMesh ────────────────────
class MNMeshRestore : public RestoreObj {
public:
    PolyObject* po;
    MNMesh before;
    MNMesh after;
    bool hasAfter = false;

    MNMeshRestore(PolyObject* p) : po(p), before(p->GetMesh()) {}

    void Restore(int isUndo) override {
        if (isUndo && !hasAfter) { after = po->GetMesh(); hasAfter = true; }
        po->GetMesh() = before;
        RefreshPolyObjectMesh(po);
    }
    void Redo() override {
        po->GetMesh() = after;
        RefreshPolyObjectMesh(po);
    }
    void EndHold() override {}
};

// ── Geometry helpers ────────────────────────────────────────────

// Outward tangent at v for the bridge curve.
//
// Two cases, picked automatically:
//
// 1. Endpoint of an open loop (v has exactly one loop edge), and the
//    loop has a clear continuation in the mesh — i.e. a non-loop edge
//    whose direction roughly opposes the loop edge at v. That edge is
//    the next segment of the curve past the selection (e.g. the next
//    arc edge along a curved wall top). Use it: tangent points "the
//    way the curve was already going" past v. This is what you want
//    when smoothly extending a curve across a gap.
//
// 2. Interior loop vertex, or no continuation found: fall back to the
//    in-face perpendicular — the non-loop edge that lives in the same
//    face as the loop edge at v, i.e. the perpendicular extension of
//    the surface across the loop. This is what you want when the two
//    rings sit on parallel strips with no shared curve direction.
//
// Returns the unit direction pointing away from the mesh interior; zero
// if no qualifying edge exists.
static Point3 BoundaryTangent(MNMesh& mesh, int v,
                              const std::vector<unsigned char>& isLoopEdge) {
    const Tab<int>& edges = mesh.vedg[v];
    const Point3& P = mesh.P(v);

    // Count loop edges at v; remember the (unique) one if there's just one.
    int loopCount = 0;
    int singleLoopEdge = -1;
    for (int i = 0; i < edges.Count(); ++i) {
        const int e = edges[i];
        if (e < 0 || (size_t)e >= isLoopEdge.size()) continue;
        if (isLoopEdge[e]) { ++loopCount; singleLoopEdge = e; }
    }

    // ── Case 1: open-loop endpoint — prefer arc continuation ─────────
    if (loopCount == 1 && singleLoopEdge >= 0) {
        const MNEdge* le = mesh.E(singleLoopEdge);
        const int otherLoopV = le->OtherVert(v);
        Point3 loopDir = mesh.P(otherLoopV) - P;
        const float ld = loopDir.Length();
        if (ld > 1e-8f) {
            loopDir /= ld;

            Point3 bestTangent(0, 0, 0);
            float bestDot = -0.3f;   // ≈ 107° — must meaningfully oppose the loop

            for (int i = 0; i < edges.Count(); ++i) {
                const int e = edges[i];
                if (e < 0 || (size_t)e >= isLoopEdge.size() || isLoopEdge[e]) continue;
                const int other = mesh.E(e)->OtherVert(v);
                Point3 dir = mesh.P(other) - P;
                const float len = dir.Length();
                if (len < 1e-8f) continue;
                Point3 dirN = dir / len;
                const float d = DotProd(dirN, loopDir);
                if (d < bestDot) {
                    bestDot = d;
                    bestTangent = -dirN;   // continue past v in the curve's existing direction
                }
            }
            if (bestTangent.LengthSquared() > 1e-12f) return bestTangent;
        }
    }

    // ── Case 2: in-face perpendicular fallback ───────────────────────
    const Tab<int>& faces = mesh.vfac[v];
    Point3 sum(0, 0, 0);
    int n = 0;
    for (int fi = 0; fi < faces.Count(); ++fi) {
        const int f = faces[fi];
        if (f < 0) continue;
        const MNFace* face = mesh.F(f);
        if (!face || face->deg < 3) continue;

        int vIdx = -1;
        for (int k = 0; k < face->deg; ++k) {
            if (face->vtx[k] == v) { vIdx = k; break; }
        }
        if (vIdx < 0) continue;

        const int e1 = face->edg[vIdx];
        const int e2 = face->edg[(vIdx + face->deg - 1) % face->deg];

        auto isLoop = [&](int e) {
            return e >= 0 && (size_t)e < isLoopEdge.size() && isLoopEdge[e];
        };
        const bool e1Loop = isLoop(e1);
        const bool e2Loop = isLoop(e2);

        if (!e1Loop && !e2Loop) continue;

        const int candidates[2] = { e1, e2 };
        for (int e : candidates) {
            if (e < 0 || (size_t)e >= isLoopEdge.size() || isLoopEdge[e]) continue;
            const int other = mesh.E(e)->OtherVert(v);
            Point3 dir = P - mesh.P(other);
            const float len = dir.Length();
            if (len > 1e-8f) { sum += dir / len; ++n; }
        }
    }

    if (n == 0) return Point3(0, 0, 0);
    const float ls = sum.Length();
    return (ls > 1e-8f) ? (sum / ls) : Point3(0, 0, 0);
}

static Point3 Hermite(const Point3& p0, const Point3& p1,
                      const Point3& t0, const Point3& t1, float u) {
    const float u2 = u * u;
    const float u3 = u2 * u;
    const float h00 =  2*u3 - 3*u2 + 1;
    const float h10 =      u3 - 2*u2 + u;
    const float h01 = -2*u3 + 3*u2;
    const float h11 =      u3 -   u2;
    return p0*h00 + t0*h10 + p1*h01 + t1*h11;
}

// ── Edge ring detection ─────────────────────────────────────────
struct EdgeRing {
    std::vector<int> verts;   // ordered vertex sequence
    bool closed = false;
};

// Walk the selected edges, partitioning them into connected rings.
// Each ring is a polyline (open) or polygon (closed). Endpoint
// vertices for closed rings appear once (no duplicate at end).
static std::vector<EdgeRing> FindEdgeRings(MNMesh& mesh,
                                           const std::vector<int>& selEdges) {
    // Build vertex → incident-selected-edges map
    std::unordered_map<int, std::vector<int>> vertEdges;
    for (int e : selEdges) {
        const MNEdge* edge = mesh.E(e);
        vertEdges[edge->v1].push_back(e);
        vertEdges[edge->v2].push_back(e);
    }

    std::unordered_set<int> usedEdges;
    std::vector<EdgeRing> rings;

    while ((int)usedEdges.size() < (int)selEdges.size()) {
        // Pick a start vertex: prefer a degree-1 endpoint (open ring start)
        int startV = -1;
        for (auto& kv : vertEdges) {
            int unused = 0;
            for (int e : kv.second) if (!usedEdges.count(e)) ++unused;
            if (unused == 1) { startV = kv.first; break; }
        }
        // No endpoint → closed ring; pick any vertex with unused edges
        if (startV < 0) {
            for (auto& kv : vertEdges) {
                for (int e : kv.second) if (!usedEdges.count(e)) { startV = kv.first; break; }
                if (startV >= 0) break;
            }
        }
        if (startV < 0) break;

        EdgeRing ring;
        ring.verts.push_back(startV);

        int curr = startV;
        bool closed = false;
        while (true) {
            int nextE = -1;
            for (int e : vertEdges[curr]) {
                if (!usedEdges.count(e)) { nextE = e; break; }
            }
            if (nextE < 0) break;            // dead end (shouldn't hit on well-formed ring)
            usedEdges.insert(nextE);
            int next = mesh.E(nextE)->OtherVert(curr);
            if (next == startV) {            // closed back to start
                closed = true;
                break;
            }
            ring.verts.push_back(next);
            curr = next;
        }
        ring.closed = closed;
        rings.push_back(std::move(ring));
    }
    return rings;
}

// Pick the (reverse, offset) pairing that minimizes total chord² between
// rings. For open rings only offset 0 is meaningful. Returns false if
// the rings can't be bridged (mismatched topology).
static bool MatchRings(const MNMesh& mesh, const EdgeRing& r1, const EdgeRing& r2,
                       bool& outReverse, int& outOffset) {
    const int N = (int)r1.verts.size();
    if ((int)r2.verts.size() != N) return false;
    if (r1.closed != r2.closed)   return false;

    const int maxOffset = r1.closed ? N : 1;
    float bestScore = std::numeric_limits<float>::infinity();
    bool  bestRev   = false;
    int   bestOff   = 0;

    for (int rev = 0; rev <= 1; ++rev) {
        for (int off = 0; off < maxOffset; ++off) {
            float score = 0.0f;
            for (int i = 0; i < N; ++i) {
                int j;
                if (r1.closed) {
                    j = rev ? ((off - i) % N + N) % N
                            : ((off + i) % N);
                } else {
                    j = rev ? (N - 1 - i) : i;
                }
                Point3 d = mesh.P(r1.verts[i]) - mesh.P(r2.verts[j]);
                score += d.LengthSquared();
            }
            if (score < bestScore) {
                bestScore = score;
                bestRev   = (rev != 0);
                bestOff   = off;
            }
        }
    }

    outReverse = bestRev;
    outOffset  = bestOff;
    return true;
}

// ── Bridge core (no undo handling — caller decides) ─────────────
//
// smoothA / smoothB select continuity at each ring:
//   true  → G1 (tangent matches surrounding surface, smooth blend)
//   false → G0 (curve follows the chord; sharp angle if the surface
//                tangent there isn't already aligned with the chord)
// Hermite with both tangents = chord vector reduces to linear lerp,
// so G0/G0 is an exact straight line.
static bool DoSmoothBridge(PolyObject* po, int segments, float tension,
                           bool smoothA, bool smoothB) {
    if (!po) return false;
    if (segments < 1)    segments = 1;
    if (tension <= 0.0f) tension  = 1.0f;

    MNMesh& mesh = po->GetMesh();
    if (mesh.ENum() < 2) return false;

    BitArray esel;
    mesh.getEdgeSel(esel);

    std::vector<int> selEdges;
    std::vector<unsigned char> isLoopEdge(mesh.ENum(), 0);
    for (int e = 0; e < esel.GetSize() && e < mesh.ENum(); ++e) {
        if (esel[e]) {
            selEdges.push_back(e);
            isLoopEdge[e] = 1;
        }
    }
    if (selEdges.size() < 2) return false;

    mesh.FillInVertEdgesFaces();

    auto rings = FindEdgeRings(mesh, selEdges);
    if (rings.size() != 2) return false;

    bool reverse;
    int  offset;
    if (!MatchRings(mesh, rings[0], rings[1], reverse, offset)) return false;

    const int N    = (int)rings[0].verts.size();
    const int cols = segments + 1;

    // Pair each vertex on ring1 with its match on ring2
    std::vector<std::pair<int,int>> pairs;
    pairs.reserve(N);
    for (int i = 0; i < N; ++i) {
        int j;
        if (rings[0].closed) {
            j = reverse ? ((offset - i) % N + N) % N
                        : ((offset + i) % N);
        } else {
            j = reverse ? (N - 1 - i) : i;
        }
        pairs.push_back({rings[0].verts[i], rings[1].verts[j]});
    }

    // Build a (rows × cols) vertex grid: rows = pairs, cols = curve samples.
    // Endpoints reuse existing verts; intermediates are newly added.
    std::vector<std::vector<int>> grid(N, std::vector<int>(cols, -1));
    for (int i = 0; i < N; ++i) {
        grid[i][0]      = pairs[i].first;
        grid[i][cols-1] = pairs[i].second;

        const Point3 PA = mesh.P(pairs[i].first);
        const Point3 PB = mesh.P(pairs[i].second);
        const Point3 chord    = PB - PA;
        const float  chordLen = chord.Length();

        Point3 tA;
        if (smoothA) {
            Point3 TA = BoundaryTangent(mesh, pairs[i].first, isLoopEdge);
            if (TA.LengthSquared() < 1e-12f) TA = Normalize(chord);
            tA = TA * (chordLen * tension);
        } else {
            tA = chord;        // G0: chord vector → linear approach from A
        }

        Point3 tB;
        if (smoothB) {
            Point3 TB = BoundaryTangent(mesh, pairs[i].second, isLoopEdge);
            if (TB.LengthSquared() < 1e-12f) TB = Normalize(-chord);
            tB = -TB * (chordLen * tension);
        } else {
            tB = chord;        // G0: chord vector → linear arrival at B
        }

        for (int j = 1; j < cols - 1; ++j) {
            const float u = (float)j / (float)segments;
            grid[i][j] = mesh.NewVert(Hermite(PA, PB, tA, tB, u));
        }
    }

    // Stitch quads. For closed rings we wrap; for open rings we stop at N-1.
    // Winding chosen so the bridge surface normals match the convention of
    // the source faces adjacent to the loop edges (consistently oriented mesh).
    const int rowSpan = rings[0].closed ? N : N - 1;
    for (int i = 0; i < rowSpan; ++i) {
        const int i_next = (i + 1) % N;
        for (int j = 0; j < segments; ++j) {
            mesh.NewQuad(grid[i][j],       grid[i][j+1],
                         grid[i_next][j+1], grid[i_next][j]);
        }
    }

    mesh.FillInMesh();
    mesh.InvalidateTopoCache();
    mesh.InvalidateGeomCache();
    return true;
}

static void RefreshPolyObjectMesh(PolyObject* po) {
    if (!po) return;
    MNMesh& mesh = po->GetMesh();
    mesh.FillInMesh();
    mesh.InvalidateTopoCache();
    mesh.InvalidateGeomCache();
    po->InvalidateChannels(GEOM_CHANNEL | TOPO_CHANNEL | SELECT_CHANNEL);
    po->NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE);
}

static void NotifyAndRedraw(PolyObject* po) {
    po->InvalidateChannels(GEOM_CHANNEL | TOPO_CHANNEL | SELECT_CHANNEL);
    po->NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE);
    Interface* ip = GetCOREInterface();
    ip->RedrawViews(ip->GetTime());
}

// Resolve the active Editable Poly object, or nullptr.
static PolyObject* ActiveEPolyObject() {
    Interface* ip = GetCOREInterface();
    INode* node = ip->GetSelNode(0);
    if (!node) return nullptr;
    Object* obj = node->GetObjectRef();
    if (!obj) return nullptr;

    Object* baseObj = obj->FindBaseObject();
    if (!baseObj) baseObj = obj;
    if (baseObj->ClassID() != EPOLYOBJ_CLASS_ID) return nullptr;
    return (PolyObject*)baseObj;
}

// ── FP-published static interface ───────────────────────────────
class SmoothBridgeTool : public FPStaticInterface {
public:
    DECLARE_DESCRIPTOR(SmoothBridgeTool)

    void Bridge       (int segments, float tension, BOOL smoothA, BOOL smoothB);
    void BeginPreview ();
    void Preview      (int segments, float tension, BOOL smoothA, BOOL smoothB);
    void CommitPreview(int segments, float tension, BOOL smoothA, BOOL smoothB);
    void CancelPreview();

    BEGIN_FUNCTION_MAP
        VFN_4(sb_fn_bridge,        Bridge,        TYPE_INT, TYPE_FLOAT, TYPE_BOOL, TYPE_BOOL)
        VFN_0(sb_fn_beginPreview,  BeginPreview)
        VFN_4(sb_fn_preview,       Preview,       TYPE_INT, TYPE_FLOAT, TYPE_BOOL, TYPE_BOOL)
        VFN_4(sb_fn_commitPreview, CommitPreview, TYPE_INT, TYPE_FLOAT, TYPE_BOOL, TYPE_BOOL)
        VFN_0(sb_fn_cancelPreview, CancelPreview)
    END_FUNCTION_MAP

private:
    PolyObject* mPreviewPo   = nullptr;
    MNMesh*     mPreviewSnap = nullptr;
};

static SmoothBridgeTool theBridgeTool(
    SMOOTH_BRIDGE_INTERFACE_ID, _T("SmoothBridge"),
    -1, NULL, FP_CORE,

    sb_fn_bridge,        _T("bridge"),         -1, TYPE_VOID, 0, 4,
        _T("segments"), -1, TYPE_INT,
        _T("tension"),  -1, TYPE_FLOAT,
        _T("smoothA"),  -1, TYPE_BOOL,
        _T("smoothB"),  -1, TYPE_BOOL,
    sb_fn_beginPreview,  _T("beginPreview"),   -1, TYPE_VOID, 0, 0,
    sb_fn_preview,       _T("preview"),        -1, TYPE_VOID, 0, 4,
        _T("segments"), -1, TYPE_INT,
        _T("tension"),  -1, TYPE_FLOAT,
        _T("smoothA"),  -1, TYPE_BOOL,
        _T("smoothB"),  -1, TYPE_BOOL,
    sb_fn_commitPreview, _T("commitPreview"),  -1, TYPE_VOID, 0, 4,
        _T("segments"), -1, TYPE_INT,
        _T("tension"),  -1, TYPE_FLOAT,
        _T("smoothA"),  -1, TYPE_BOOL,
        _T("smoothB"),  -1, TYPE_BOOL,
    sb_fn_cancelPreview, _T("cancelPreview"),  -1, TYPE_VOID, 0, 0,

    p_end
);

void SmoothBridgeTool::Bridge(int segments, float tension,
                              BOOL smoothA, BOOL smoothB) {
    PolyObject* po = ActiveEPolyObject();
    if (!po) return;

    theHold.Begin();
    theHold.Put(new MNMeshRestore(po));
    if (!DoSmoothBridge(po, segments, tension, smoothA != FALSE, smoothB != FALSE)) {
        theHold.Cancel();
        return;
    }
    NotifyAndRedraw(po);
    theHold.Accept(_T("Smooth Bridge"));
}

void SmoothBridgeTool::BeginPreview() {
    if (mPreviewSnap) CancelPreview();
    PolyObject* po = ActiveEPolyObject();
    if (!po) return;
    mPreviewPo   = po;
    mPreviewSnap = new MNMesh(po->GetMesh());
}

void SmoothBridgeTool::Preview(int segments, float tension,
                               BOOL smoothA, BOOL smoothB) {
    if (!mPreviewPo || !mPreviewSnap) return;
    mPreviewPo->GetMesh() = *mPreviewSnap;     // restore to snapshot
    DoSmoothBridge(mPreviewPo, segments, tension, smoothA != FALSE, smoothB != FALSE);
    NotifyAndRedraw(mPreviewPo);
}

void SmoothBridgeTool::CommitPreview(int segments, float tension,
                                     BOOL smoothA, BOOL smoothB) {
    if (!mPreviewPo || !mPreviewSnap) return;

    // Rewind to the snapshot, then run the bridge inside a Hold so the
    // operation lands as a single undoable step.
    mPreviewPo->GetMesh() = *mPreviewSnap;
    NotifyAndRedraw(mPreviewPo);

    theHold.Begin();
    theHold.Put(new MNMeshRestore(mPreviewPo));
    DoSmoothBridge(mPreviewPo, segments, tension, smoothA != FALSE, smoothB != FALSE);
    NotifyAndRedraw(mPreviewPo);
    theHold.Accept(_T("Smooth Bridge"));

    delete mPreviewSnap;
    mPreviewSnap = nullptr;
    mPreviewPo   = nullptr;
}

void SmoothBridgeTool::CancelPreview() {
    if (!mPreviewPo || !mPreviewSnap) return;
    mPreviewPo->GetMesh() = *mPreviewSnap;
    NotifyAndRedraw(mPreviewPo);
    delete mPreviewSnap;
    mPreviewSnap = nullptr;
    mPreviewPo   = nullptr;
}
