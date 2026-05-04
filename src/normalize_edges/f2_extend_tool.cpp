// F2 Extend / Make Face -- function-published Editable Poly command.
//
// MAXScript surface:
//   F2Extend.extend()

#include <max.h>
#include <iparamb2.h>
#include <iFnPub.h>
#include <plugapi.h>
#include <maxapi.h>
#include <polyobj.h>
#include <mnmesh.h>
#include <hold.h>
#include <vector>
#include <limits>
#include <algorithm>
#include <cmath>

#define F2_EXTEND_INTERFACE_ID Interface_ID(0xC3D4E5F6, 0x45678901)

enum {
    f2_fn_extend,
};

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
    RefreshPolyObjectMesh(po);
    Interface* ip = GetCOREInterface();
    ip->RedrawViews(ip->GetTime());
}

class MNMeshRestore : public RestoreObj {
public:
    PolyObject* po;
    MNMesh before;
    MNMesh after;
    bool hasAfter = false;

    MNMeshRestore(PolyObject* p) : po(p), before(p->GetMesh()) {}

    void Restore(int isUndo) override {
        if (isUndo && !hasAfter) {
            after = po->GetMesh();
            hasAfter = true;
        }
        po->GetMesh() = before;
        RefreshPolyObjectMesh(po);
    }

    void Redo() override {
        po->GetMesh() = after;
        RefreshPolyObjectMesh(po);
    }

    void EndHold() override {}
};

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

static int FindEdgeByVerts(MNMesh& mesh, int a, int b) {
    for (int e = 0; e < mesh.ENum(); ++e) {
        const MNEdge* edge = mesh.E(e);
        if (!edge) continue;
        if ((edge->v1 == a && edge->v2 == b) ||
            (edge->v1 == b && edge->v2 == a)) {
            return e;
        }
    }
    return -1;
}

static int EdgeFaceCount(const MNEdge* edge) {
    if (!edge) return 0;
    int count = 0;
    if (edge->f1 >= 0) ++count;
    if (edge->f2 >= 0) ++count;
    return count;
}

static bool IsOpenEdge(const MNEdge* edge) {
    return EdgeFaceCount(edge) < 2;
}

static int FirstFaceForEdge(const MNEdge* edge) {
    if (!edge) return -1;
    return edge->f1 >= 0 ? edge->f1 : edge->f2;
}

static bool EdgeHasFace(const MNEdge* edge, int face) {
    if (!edge || face < 0) return false;
    return edge->f1 == face || edge->f2 == face;
}

static bool EdgesShareAnyFace(const MNEdge* a, const MNEdge* b) {
    if (!a || !b) return false;
    return (a->f1 >= 0 && EdgeHasFace(b, a->f1)) ||
           (a->f2 >= 0 && EdgeHasFace(b, a->f2));
}

static bool FaceHasSameVerts(const MNFace* face, const std::vector<int>& verts) {
    if (!face || face->deg != (int)verts.size()) return false;
    for (int v : verts) {
        bool found = false;
        for (int i = 0; i < face->deg; ++i) {
            if (face->vtx[i] == v) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

static bool FaceAlreadyExists(MNMesh& mesh, const std::vector<int>& verts) {
    for (int f = 0; f < mesh.FNum(); ++f) {
        if (FaceHasSameVerts(mesh.F(f), verts)) return true;
    }
    return false;
}

static bool ExistingEdgeCanAcceptFace(MNMesh& mesh, int a, int b) {
    const int edge = FindEdgeByVerts(mesh, a, b);
    if (edge < 0) return true;
    return IsOpenEdge(mesh.E(edge));
}

static float F2QuadEpsilonSq(const Point3 pts[4]) {
    float maxLenSq = 0.0f;
    for (int i = 0; i < 4; ++i) {
        for (int j = i + 1; j < 4; ++j) {
            maxLenSq = std::max(maxLenSq, (pts[i] - pts[j]).LengthSquared());
        }
    }
    return std::max(1e-10f, maxLenSq * 1e-8f);
}

static bool PointMatchesExistingVertex(MNMesh& mesh, const Point3& p, float epsSq) {
    for (int v = 0; v < mesh.VNum(); ++v) {
        if ((mesh.P(v) - p).LengthSquared() <= epsSq) return true;
    }
    return false;
}

static bool IsUsableF2Quad(const Point3 pts[4]) {
    const float epsSq = F2QuadEpsilonSq(pts);

    for (int i = 0; i < 4; ++i) {
        const Point3 edge = pts[(i + 1) % 4] - pts[i];
        if (edge.LengthSquared() <= epsSq) return false;
    }

    for (int i = 0; i < 4; ++i) {
        for (int j = i + 1; j < 4; ++j) {
            if ((pts[i] - pts[j]).LengthSquared() <= epsSq) return false;
        }
    }

    const Point3 n1 = CrossProd(pts[1] - pts[0], pts[2] - pts[0]);
    const Point3 n2 = CrossProd(pts[2] - pts[0], pts[3] - pts[0]);
    const float n1Sq = n1.LengthSquared();
    const float n2Sq = n2.LengthSquared();
    const float maxDiagSq = std::max((pts[2] - pts[0]).LengthSquared(),
                                     (pts[3] - pts[1]).LengthSquared());
    const float areaEpsSq = std::max(1e-16f, maxDiagSq * maxDiagSq * 1e-12f);
    if (n1Sq + n2Sq <= areaEpsSq) return false;

    if (n1Sq > areaEpsSq && n2Sq > areaEpsSq) {
        const float dot = DotProd(n1, n2);
        if (dot < -0.01f * std::sqrt(n1Sq * n2Sq)) return false;
    }

    const Point3 base = pts[1] - pts[0];
    const float baseLen = base.Length();
    if (baseLen <= 1e-8f) return false;

    const Point3 baseDir = base / baseLen;
    const float h2 = CrossProd(pts[2] - pts[0], baseDir).Length();
    const float h3 = CrossProd(pts[3] - pts[0], baseDir).Length();
    const float minHeight = std::max(1e-5f, baseLen * 1e-4f);
    if (h2 < minHeight || h3 < minHeight) return false;

    return true;
}

static bool CanCreateF2Quad(MNMesh& mesh, int a, int b, int a2, int b2,
                            const Point3& a2Pos, const Point3& b2Pos,
                            bool createA2, bool createB2) {
    const int existingVerts[4] = { a, b, a2, b2 };
    for (int i = 0; i < 4; ++i) {
        if (existingVerts[i] < 0 &&
            ((i == 2 && createA2) || (i == 3 && createB2))) {
            continue;
        }
        if (existingVerts[i] < 0 || existingVerts[i] >= mesh.VNum()) return false;
    }

    if (!createA2 && !createB2) {
        std::vector<int> verts = { a, b, b2, a2 };
        for (int i = 0; i < 4; ++i) {
            for (int j = i + 1; j < 4; ++j) {
                if (verts[i] == verts[j]) return false;
            }
        }
        if (FaceAlreadyExists(mesh, verts)) return false;
    }

    if (!ExistingEdgeCanAcceptFace(mesh, a, b)) return false;
    if (!createB2 && !ExistingEdgeCanAcceptFace(mesh, b, b2)) return false;
    if (!createA2 && !ExistingEdgeCanAcceptFace(mesh, a2, a)) return false;
    if (!createA2 && !createB2 && !ExistingEdgeCanAcceptFace(mesh, b2, a2)) {
        return false;
    }

    Point3 pts[4] = { mesh.P(a), mesh.P(b), b2Pos, a2Pos };
    const float epsSq = F2QuadEpsilonSq(pts);
    if (createA2 && PointMatchesExistingVertex(mesh, a2Pos, epsSq)) return false;
    if (createB2 && PointMatchesExistingVertex(mesh, b2Pos, epsSq)) return false;

    return IsUsableF2Quad(pts);
}

static std::vector<int> F2OpenLinkedEdges(MNMesh& mesh, int vertex,
                                          int selectedEdge,
                                          bool rejectSharedFace) {
    std::vector<int> result;
    if (vertex < 0 || vertex >= mesh.VNum() ||
        selectedEdge < 0 || selectedEdge >= mesh.ENum()) {
        return result;
    }

    const MNEdge* selected = mesh.E(selectedEdge);
    const Tab<int>& edges = mesh.vedg[vertex];
    for (int i = 0; i < edges.Count(); ++i) {
        const int e = edges[i];
        if (e < 0 || e == selectedEdge || e >= mesh.ENum()) continue;

        const MNEdge* edge = mesh.E(e);
        if (!IsOpenEdge(edge)) continue;
        if (rejectSharedFace && EdgesShareAnyFace(edge, selected)) continue;
        result.push_back(e);
    }
    return result;
}

struct F2EdgeChoice {
    int edgeA = -1;
    int edgeB = -1;
    int a2 = -1;
    int b2 = -1;
    bool createA2 = false;
    bool createB2 = false;
    Point3 a2Pos = Point3(0, 0, 0);
    Point3 b2Pos = Point3(0, 0, 0);
};

static bool ChooseF2EdgePair(MNMesh& mesh, int a, int b, int selectedEdge,
                             F2EdgeChoice& out) {
    std::vector<int> aEdges = F2OpenLinkedEdges(mesh, a, selectedEdge, true);
    std::vector<int> bEdges = F2OpenLinkedEdges(mesh, b, selectedEdge, true);
    const bool hasAEdges = !aEdges.empty();
    const bool hasBEdges = !bEdges.empty();
    if (aEdges.empty() && bEdges.empty()) return false;

    Point3 selectedDir = mesh.P(b) - mesh.P(a);
    const float selectedLen = selectedDir.Length();
    if (selectedLen < 1e-8f) return false;
    selectedDir /= selectedLen;

    float bestScore = -std::numeric_limits<float>::infinity();
    for (int ea : aEdges) {
        const int a2 = mesh.E(ea)->OtherVert(a);
        if (a2 < 0 || a2 >= mesh.VNum()) continue;

        Point3 da = mesh.P(a2) - mesh.P(a);
        const float lenA = da.Length();
        if (lenA < 1e-8f) continue;
        da /= lenA;

        for (int eb : bEdges) {
            if (ea == eb) continue;

            const int b2 = mesh.E(eb)->OtherVert(b);
            if (b2 < 0 || b2 >= mesh.VNum() || a2 == b2) continue;

            Point3 db = mesh.P(b2) - mesh.P(b);
            const float lenB = db.Length();
            if (lenB < 1e-8f) continue;
            db /= lenB;

            if (!CanCreateF2Quad(mesh, a, b, a2, b2,
                                 mesh.P(a2), mesh.P(b2), false, false)) {
                continue;
            }

            Point3 newTop = mesh.P(b2) - mesh.P(a2);
            const float newTopLen = newTop.Length();
            if (newTopLen < 1e-8f) continue;
            newTop /= newTopLen;

            const float railsParallel = DotProd(da, db);
            const float railsNotAlongSelected =
                1.0f - 0.5f * (std::fabs(DotProd(da, selectedDir)) +
                               std::fabs(DotProd(db, selectedDir)));
            const float oppositeParallel = std::fabs(DotProd(newTop, selectedDir));
            const float lengthMatch =
                -std::fabs(newTopLen - selectedLen) / std::max(selectedLen, 1e-6f);
            const float railLengthMatch =
                -std::fabs(lenA - lenB) / std::max(std::max(lenA, lenB), 1e-6f);
            const float score = railsParallel * 4.0f +
                                railsNotAlongSelected * 3.0f +
                                oppositeParallel * 2.0f +
                                lengthMatch +
                                railLengthMatch;

            if (score > bestScore) {
                bestScore = score;
                out.edgeA = ea;
                out.edgeB = eb;
                out.a2 = a2;
                out.b2 = b2;
            }
        }
    }

    if (out.edgeA >= 0 && out.edgeB >= 0) return true;
    if (hasAEdges && hasBEdges) return false;

    bestScore = -std::numeric_limits<float>::infinity();
    for (int ea : aEdges) {
        const int a2 = mesh.E(ea)->OtherVert(a);
        if (a2 < 0 || a2 >= mesh.VNum()) continue;

        Point3 rail = mesh.P(a2) - mesh.P(a);
        const float railLen = rail.Length();
        if (railLen < 1e-8f) continue;
        const Point3 railDir = rail / railLen;

        const Point3 b2Pos = mesh.P(b) + rail;
        if (!CanCreateF2Quad(mesh, a, b, a2, -1,
                             mesh.P(a2), b2Pos, false, true)) {
            continue;
        }

        const float railsNotAlongSelected =
            1.0f - std::fabs(DotProd(railDir, selectedDir));
        const float height = CrossProd(rail, selectedDir).Length();
        const float heightScore =
            std::min(height / std::max(selectedLen, 1e-6f), 4.0f);
        const float score = railsNotAlongSelected * 8.0f + heightScore;
        if (score > bestScore) {
            bestScore = score;
            out = F2EdgeChoice();
            out.edgeA = ea;
            out.a2 = a2;
            out.createB2 = true;
            out.b2Pos = b2Pos;
        }
    }

    for (int eb : bEdges) {
        const int b2 = mesh.E(eb)->OtherVert(b);
        if (b2 < 0 || b2 >= mesh.VNum()) continue;

        Point3 rail = mesh.P(b2) - mesh.P(b);
        const float railLen = rail.Length();
        if (railLen < 1e-8f) continue;
        const Point3 railDir = rail / railLen;

        const Point3 a2Pos = mesh.P(a) + rail;
        if (!CanCreateF2Quad(mesh, a, b, -1, b2,
                             a2Pos, mesh.P(b2), true, false)) {
            continue;
        }

        const float railsNotAlongSelected =
            1.0f - std::fabs(DotProd(railDir, selectedDir));
        const float height = CrossProd(rail, selectedDir).Length();
        const float heightScore =
            std::min(height / std::max(selectedLen, 1e-6f), 4.0f);
        const float score = railsNotAlongSelected * 8.0f + heightScore;
        if (score > bestScore) {
            bestScore = score;
            out = F2EdgeChoice();
            out.edgeB = eb;
            out.b2 = b2;
            out.createA2 = true;
            out.a2Pos = a2Pos;
        }
    }

    return (out.a2 >= 0 || out.createA2) && (out.b2 >= 0 || out.createB2);
}

static void CollectSelectedEdges(MNMesh& mesh, std::vector<int>& out) {
    BitArray esel;
    mesh.getEdgeSel(esel);
    for (int e = 0; e < esel.GetSize() && e < mesh.ENum(); ++e) {
        if (esel[e]) out.push_back(e);
    }
}

static void CollectSelectedVerts(MNMesh& mesh, std::vector<int>& out) {
    BitArray vsel;
    mesh.getVertexSel(vsel);
    for (int v = 0; v < vsel.GetSize() && v < mesh.VNum(); ++v) {
        if (vsel[v]) out.push_back(v);
    }
}

static bool DoF2EdgeExtend(PolyObject* po, const std::vector<int>& selEdges) {
    if (!po || selEdges.empty()) return false;
    if (selEdges.size() != 1) return false;

    MNMesh& mesh = po->GetMesh();
    mesh.FillInVertEdgesFaces();

    const int selectedEdge = selEdges[0];
    if (selectedEdge < 0 || selectedEdge >= mesh.ENum()) return false;

    const MNEdge* edge = mesh.E(selectedEdge);
    if (!IsOpenEdge(edge)) return false;

    const int a = edge->v1;
    const int b = edge->v2;
    if (a < 0 || b < 0 || a >= mesh.VNum() || b >= mesh.VNum()) return false;

    F2EdgeChoice choice;
    if (!ChooseF2EdgePair(mesh, a, b, selectedEdge, choice)) return false;

    int a2 = choice.a2;
    int b2 = choice.b2;
    const Point3 finalA2Pos = choice.createA2 ? choice.a2Pos : mesh.P(a2);
    const Point3 finalB2Pos = choice.createB2 ? choice.b2Pos : mesh.P(b2);
    if (!CanCreateF2Quad(mesh, a, b, a2, b2, finalA2Pos, finalB2Pos,
                         choice.createA2, choice.createB2)) {
        return false;
    }

    MNMesh rollback(mesh);
    auto restoreAndFail = [&]() {
        mesh = rollback;
        mesh.FillInMesh();
        mesh.InvalidateTopoCache();
        mesh.InvalidateGeomCache();
        return false;
    };

    if (choice.createA2) a2 = mesh.NewVert(choice.a2Pos);
    if (choice.createB2) b2 = mesh.NewVert(choice.b2Pos);
    if (a2 < 0 || b2 < 0 || a2 == b2) return restoreAndFail();

    DWORD smGroup = 0;
    MtlID material = 0;
    const int sourceFace = FirstFaceForEdge(edge);
    if (sourceFace >= 0) {
        const MNFace* face = mesh.F(sourceFace);
        if (face) {
            smGroup = face->smGroup;
            material = face->material;
        }
    }

    int newFace = -1;
    if (sourceFace >= 0) {
        const MNFace* face = mesh.F(sourceFace);
        const int edgePos = face ? face->EdgeIndex(selectedEdge) : -1;
        const bool faceUsesAB = edgePos >= 0 && face->vtx[edgePos] == a &&
            face->vtx[(edgePos + 1) % face->deg] == b;
        newFace = faceUsesAB
            ? mesh.NewQuad(b, a, a2, b2, smGroup, material)
            : mesh.NewQuad(a, b, b2, a2, smGroup, material);
    } else {
        newFace = mesh.NewQuad(a, b, b2, a2, smGroup, material);
    }
    if (newFace < 0) return restoreAndFail();

    mesh.FillInMesh();
    mesh.InvalidateTopoCache();
    mesh.InvalidateGeomCache();

    if (newFace < 0 || newFace >= mesh.FNum()) return restoreAndFail();
    const MNFace* madeFace = mesh.F(newFace);
    if (!madeFace || madeFace->deg != 4) return restoreAndFail();

    Point3 madePts[4] = { mesh.P(a), mesh.P(b), mesh.P(b2), mesh.P(a2) };
    if (!IsUsableF2Quad(madePts)) return restoreAndFail();

    const int newEdge = FindEdgeByVerts(mesh, a2, b2);
    if (newEdge < 0) return restoreAndFail();

    BitArray esel(mesh.ENum());
    esel.ClearAll();
    esel.Set(newEdge);
    mesh.EdgeSelect(esel);

    BitArray fsel(mesh.FNum());
    fsel.ClearAll();
    fsel.Set(newFace);
    mesh.FaceSelect(fsel);
    return true;
}

static Point3 FirstNonDegenerateNormal(MNMesh& mesh,
                                       const std::vector<int>& verts) {
    for (size_t i = 0; i < verts.size(); ++i) {
        for (size_t j = i + 1; j < verts.size(); ++j) {
            for (size_t k = j + 1; k < verts.size(); ++k) {
                Point3 n = CrossProd(mesh.P(verts[j]) - mesh.P(verts[i]),
                                     mesh.P(verts[k]) - mesh.P(verts[i]));
                const float len = n.Length();
                if (len > 1e-8f) return n / len;
            }
        }
    }
    return Point3(0, 0, 1);
}

static std::vector<int> SortVertsForFace(MNMesh& mesh,
                                         const std::vector<int>& verts) {
    Point3 center(0, 0, 0);
    for (int v : verts) center += mesh.P(v);
    center /= (float)verts.size();

    const Point3 normal = FirstNonDegenerateNormal(mesh, verts);
    Point3 xAxis = mesh.P(verts[0]) - center;
    if (xAxis.LengthSquared() < 1e-12f) xAxis = Point3(1, 0, 0);
    xAxis = Normalize(xAxis);

    Point3 yAxis = Normalize(CrossProd(normal, xAxis));
    if (yAxis.LengthSquared() < 1e-12f) yAxis = Point3(0, 1, 0);

    std::vector<int> ordered = verts;
    std::sort(ordered.begin(), ordered.end(), [&](int lhs, int rhs) {
        const Point3 dl = mesh.P(lhs) - center;
        const Point3 dr = mesh.P(rhs) - center;
        const float al = std::atan2(DotProd(dl, yAxis), DotProd(dl, xAxis));
        const float ar = std::atan2(DotProd(dr, yAxis), DotProd(dr, xAxis));
        return al < ar;
    });
    return ordered;
}

static bool DoF2VertexFill(PolyObject* po, const std::vector<int>& selVerts) {
    if (!po || selVerts.size() < 2) return false;

    MNMesh& mesh = po->GetMesh();
    mesh.FillInVertEdgesFaces();

    if (selVerts.size() == 2) {
        const int a = selVerts[0];
        const int b = selVerts[1];
        if (a < 0 || b < 0 || a >= mesh.VNum() || b >= mesh.VNum() || a == b) {
            return false;
        }
        if (FindEdgeByVerts(mesh, a, b) >= 0) return false;

        const int edge = mesh.SimpleNewEdge(a, b);
        mesh.FillInMesh();
        mesh.InvalidateTopoCache();
        mesh.InvalidateGeomCache();

        BitArray esel(mesh.ENum());
        esel.ClearAll();
        if (edge >= 0 && edge < mesh.ENum()) esel.Set(edge);
        mesh.EdgeSelect(esel);
        return true;
    }

    std::vector<int> ordered = SortVertsForFace(mesh, selVerts);
    const int face = mesh.NewFace(-1, (int)ordered.size(), ordered.data());
    if (face < 0) return false;

    mesh.FillInMesh();
    mesh.InvalidateTopoCache();
    mesh.InvalidateGeomCache();

    BitArray fsel(mesh.FNum());
    fsel.ClearAll();
    if (face < mesh.FNum()) fsel.Set(face);
    mesh.FaceSelect(fsel);
    return true;
}

static bool DoF2Extend(PolyObject* po) {
    if (!po) return false;

    MNMesh& mesh = po->GetMesh();

    std::vector<int> selEdges;
    CollectSelectedEdges(mesh, selEdges);
    if (!selEdges.empty()) return DoF2EdgeExtend(po, selEdges);

    std::vector<int> selVerts;
    CollectSelectedVerts(mesh, selVerts);
    return DoF2VertexFill(po, selVerts);
}

class F2ExtendTool : public FPStaticInterface {
public:
    DECLARE_DESCRIPTOR(F2ExtendTool)

    BOOL Extend();

    BEGIN_FUNCTION_MAP
        FN_0(f2_fn_extend, TYPE_BOOL, Extend)
    END_FUNCTION_MAP
};

static F2ExtendTool theF2ExtendTool(
    F2_EXTEND_INTERFACE_ID, _T("F2Extend"),
    -1, NULL, FP_CORE,

    f2_fn_extend, _T("extend"), -1, TYPE_BOOL, 0, 0,

    p_end
);

BOOL F2ExtendTool::Extend() {
    PolyObject* po = ActiveEPolyObject();
    if (!po) return FALSE;

    theHold.Begin();
    theHold.Put(new MNMeshRestore(po));
    if (!DoF2Extend(po)) {
        theHold.Cancel();
        return FALSE;
    }

    NotifyAndRedraw(po);
    theHold.Accept(_T("F2 Extend"));
    return TRUE;
}
