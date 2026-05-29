#include "MeshCutter.h"
#include <mnmesh.h>
#include <istdplug.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include <windows.h>

namespace {

// ── SEH guard: catch access violations from SDK internals ────────
// Returns true if the block ran without crashing.
#define SAFE_MESH_OP(expr)  \
    [&]() -> bool {         \
        __try { expr; return true; } \
        __except(EXCEPTION_EXECUTE_HANDLER) { return false; } \
    }()

static bool IsMeshValid(MNMesh& mesh) {
    int liveVerts = 0;
    for (int v = 0; v < mesh.VNum(); ++v) {
        MNVert* vert = mesh.V(v);
        if (vert && !vert->GetFlag(MN_DEAD))
            ++liveVerts;
    }

    int liveFaces = 0;
    for (int f = 0; f < mesh.FNum(); ++f) {
        MNFace* face = mesh.F(f);
        if (face && !face->GetFlag(MN_DEAD) && face->deg >= 3)
            ++liveFaces;
    }

    return liveVerts >= 3 && liveFaces >= 1;
}

static Point3 SafeNormalize(const Point3& v,
                            const Point3& fallback = Point3(0, 0, 1)) {
    const float lenSq = DotProd(v, v);
    if (lenSq <= 1.0e-20f)
        return fallback;
    return v / std::sqrt(lenSq);
}

static Point3 ResolveProjDir(const ProjectedPolyline& polyline,
                             const Point3& fallback) {
    return SafeNormalize(polyline.projDir, SafeNormalize(fallback));
}

static bool BuildCutterPrism(const ProjectedPolyline& polyline,
                             const Point3& projDir,
                             MNMesh& sourceMesh,
                             float weldThreshold,
                             float depthOverride,
                             bool flipDepth,
                             MNMesh& cutter) {
    if (!polyline.closed || polyline.pts.size() < 3)
        return false;

    const float tol = std::max(weldThreshold * 0.5f, 1.0e-5f);
    std::vector<Point3> loop;
    loop.reserve(polyline.pts.size());
    loop.push_back(polyline.pts.front());
    for (size_t i = 1; i < polyline.pts.size(); ++i) {
        if (Length(polyline.pts[i] - loop.back()) > tol)
            loop.push_back(polyline.pts[i]);
    }
    if (loop.size() >= 2 && Length(loop.front() - loop.back()) <= tol)
        loop.pop_back();
    if (loop.size() < 3)
        return false;

    const Point3 axis = SafeNormalize(projDir, Point3(0, 0, -1));
    const Point3 arb = (std::abs(axis.z) < 0.9f) ? Point3(0, 0, 1) : Point3(1, 0, 0);
    const Point3 uAxis = SafeNormalize(CrossProd(axis, arb));
    const Point3 vAxis = SafeNormalize(CrossProd(axis, uAxis));
    const Point3 origin = loop.front();

    std::vector<Point3> flat;
    flat.reserve(loop.size());
    double signedArea = 0.0;

    for (size_t i = 0; i < loop.size(); ++i) {
        const float depth = DotProd(loop[i] - origin, axis);
        flat.push_back(loop[i] - axis * depth);

        const size_t j = (i + 1) % loop.size();
        const float depthJ = DotProd(loop[j] - origin, axis);
        const Point3 flatJ = loop[j] - axis * depthJ;

        const Point3 di = flat.back() - origin;
        const Point3 dj = flatJ - origin;
        signedArea += DotProd(di, uAxis) * DotProd(dj, vAxis) -
                      DotProd(dj, uAxis) * DotProd(di, vAxis);
    }

    if (std::abs(signedArea) <= 1.0e-12)
        return false;

    if (signedArea < 0.0)
        std::reverse(flat.begin(), flat.end());

    const size_t n = flat.size();

    Box3 bbox;
    sourceMesh.BBox(bbox);
    const float diag = std::max(Length(bbox.pmax - bbox.pmin), 1.0f);
    const double margin = std::max(1.0e-3, static_cast<double>(weldThreshold) * 16.0);
    const double planeDepth = DotProd(flat.front(), axis);

    double srcMin = std::numeric_limits<double>::infinity();
    double srcMax = -std::numeric_limits<double>::infinity();
    for (int v = 0; v < sourceMesh.VNum(); ++v) {
        if (sourceMesh.V(v)->GetFlag(MN_DEAD)) continue;
        const double d = DotProd(sourceMesh.P(v), axis);
        srcMin = std::min(srcMin, d);
        srcMax = std::max(srcMax, d);
    }
    if (!std::isfinite(srcMin) || !std::isfinite(srcMax)) {
        srcMin = planeDepth - static_cast<double>(diag);
        srcMax = planeDepth + static_cast<double>(diag);
    }

    double minOffset, maxOffset;
    if (depthOverride > 0.0f) {
        if (flipDepth) {
            minOffset = -(static_cast<double>(depthOverride) + margin);
            maxOffset = margin;
        } else {
            minOffset = -margin;
            maxOffset = static_cast<double>(depthOverride) + margin;
        }
    } else {
        const double extra = std::max<double>(diag * 0.25, margin);
        minOffset = (srcMin - planeDepth) - extra;
        maxOffset = (srcMax - planeDepth) + extra;
    }

    cutter.ClearAndFree();
    cutter.setNumVerts(static_cast<int>(n * 2));
    cutter.setNumFaces(static_cast<int>(n + 2));

    for (size_t i = 0; i < n; ++i)
        cutter.P(static_cast<int>(i)) = flat[i] + axis * static_cast<float>(minOffset);
    for (size_t i = 0; i < n; ++i)
        cutter.P(static_cast<int>(n + i)) = flat[i] + axis * static_cast<float>(maxOffset);

    std::vector<int> cap(n);
    for (size_t i = 0; i < n; ++i) cap[i] = static_cast<int>(n - 1 - i);
    cutter.F(0)->MakePoly(static_cast<int>(n), cap.data());

    for (size_t i = 0; i < n; ++i) cap[i] = static_cast<int>(n + i);
    cutter.F(1)->MakePoly(static_cast<int>(n), cap.data());

    int quad[4];
    for (size_t i = 0; i < n; ++i) {
        const size_t j = (i + 1) % n;
        quad[0] = static_cast<int>(i);
        quad[1] = static_cast<int>(j);
        quad[2] = static_cast<int>(n + j);
        quad[3] = static_cast<int>(n + i);
        cutter.F(static_cast<int>(2 + i))->MakePoly(4, quad);
    }

    cutter.FillInMesh();
    return true;
}

static int FindClosestFace(MNMesh& mesh, const Point3& pt) {
    int bestFace = -1;
    float bestDist = std::numeric_limits<float>::max();
    for (int f = 0; f < mesh.FNum(); ++f) {
        MNFace* face = mesh.F(f);
        if (!face || face->GetFlag(MN_DEAD) || face->deg < 3) continue;
        Point3 centroid(0, 0, 0);
        for (int i = 0; i < face->deg; ++i)
            centroid += mesh.P(face->vtx[i]);
        centroid /= static_cast<float>(face->deg);
        float d = LengthSquared(pt - centroid);
        if (d < bestDist) { bestDist = d; bestFace = f; }
    }
    return bestFace;
}

static Point3 ComputeFaceNormal(MNMesh& mesh, int faceIdx) {
    MNFace* f = mesh.F(faceIdx);
    if (!f || f->deg < 3) return Point3(0, 0, 1);
    Point3 fn(0, 0, 0);
    for (int i = 0; i < f->deg; ++i) {
        const Point3& c = mesh.P(f->vtx[i]);
        const Point3& nx = mesh.P(f->vtx[(i + 1) % f->deg]);
        fn.x += (c.y - nx.y) * (c.z + nx.z);
        fn.y += (c.z - nx.z) * (c.x + nx.x);
        fn.z += (c.x - nx.x) * (c.y + nx.y);
    }
    return SafeNormalize(fn);
}

static void CleanMeshAfterBoolean(MNMesh& mesh, float weldThreshold, bool keepTris) {
    SAFE_MESH_OP({
        mesh.CollapseDeadStructs();
        mesh.EliminateCoincidentVerts(std::max(weldThreshold, 1.0e-5f));
        mesh.EliminateCollinearVerts();
        if (keepTris)
            mesh.Triangulate();
        else
            mesh.MakePolyMesh(0, TRUE);
        mesh.FillInMesh();
    });
}

static void CutOpenSpline(MNMesh& mesh,
                          const ProjectedPolyline& polyline,
                          const Point3& projDir,
                          float weldThreshold) {
    if (polyline.pts.size() < 2) return;

    const Point3 dir = SafeNormalize(projDir);
    const Point3 dirNeg = -dir;

    struct Hit { Point3 pt; int face; bool valid; };

    auto projectRay = [&](const Point3& src, const Point3& d) -> Hit {
        Ray ray(src, d);
        float t = 0.0f;
        Point3 normal;
        int face = -1;
        Tab<float> bary;
        bool ok = false;
        SAFE_MESH_OP(ok = mesh.IntersectRay(ray, t, normal, face, bary));
        if (ok && face >= 0 && t >= 0.0f)
            return {src + d * t, face, true};
        return {{}, -1, false};
    };

    auto projectOnSurface = [&](const Point3& pt) -> Hit {
        int face = FindClosestFace(mesh, pt);
        if (face >= 0) return {pt, face, true};
        return {{}, -1, false};
    };

    const int nPts = static_cast<int>(polyline.pts.size());
    const int nSegs = polyline.closed ? nPts : (nPts - 1);

    for (int seg = 0; seg < nSegs; ++seg) {
        if (!IsMeshValid(mesh)) break;

        const int iA = seg;
        const int iB = (seg + 1) % nPts;

        // Re-project BOTH endpoints every iteration to get fresh face
        // indices (CutFace + FillInMesh invalidate all prior indices).
        Hit hA, hB;
        if (polyline.onSurface) {
            hA = projectOnSurface(polyline.pts[iA]);
            hB = projectOnSurface(polyline.pts[iB]);
        } else {
            hA = projectRay(polyline.pts[iA], dir);
            if (!hA.valid) hA = projectRay(polyline.pts[iA], dirNeg);
            hB = projectRay(polyline.pts[iB], dir);
            if (!hB.valid) hB = projectRay(polyline.pts[iB], dirNeg);
        }

        if (!hA.valid || !hB.valid) continue;
        if (Length(hB.pt - hA.pt) <= weldThreshold) continue;

        // CutFace requires BOTH points on the SAME face.
        // If they're on different faces, skip — don't crash.
        if (hA.face != hB.face) continue;

        const int face = hA.face;
        if (face < 0 || face >= mesh.FNum()) continue;
        MNFace* f = mesh.F(face);
        if (!f || f->GetFlag(MN_DEAD) || f->deg < 3) continue;

        Point3 fn = ComputeFaceNormal(mesh, face);
        bool ok = SAFE_MESH_OP(mesh.CutFace(face, hA.pt, hB.pt, fn, false, TRIANGULATION_LEGACY));
        if (!ok) break;  // mesh is corrupted, stop entirely
        if (!SAFE_MESH_OP(mesh.FillInMesh())) break;
    }
}
} // namespace

// ═════════════════════════════════════════════════════════════════
void MeshCutter::Execute(MNMesh& mesh,
                         const std::vector<ProjectedPolyline>& polylines,
                         const Point3& projDir,
                         const CutOptions& opts) {
    if (polylines.empty()) return;

    if (!SAFE_MESH_OP(mesh.FillInMesh()))
        return;

    bool didClosedCut = false;

    size_t i = 0;
    while (i < polylines.size()) {
        const int currentCutMode = polylines[i].cutType;
        size_t j = i;
        MNMesh combinedCutter;
        bool hasCombinedCutter = false;

        while (j < polylines.size() && polylines[j].cutType == currentCutMode) {
            const ProjectedPolyline& polyline = polylines[j];
            if (polyline.pts.size() >= 3 && polyline.closed) {
                MNMesh prism;
                if (BuildCutterPrism(polyline, ResolveProjDir(polyline, projDir), mesh,
                                     opts.weldThresh, polyline.depth, polyline.flipDepth, prism)) {
                    if (hasCombinedCutter) {
                        SAFE_MESH_OP(combinedCutter += prism);
                    } else {
                        combinedCutter = prism;
                        hasCombinedCutter = true;
                    }
                }
            }
            ++j;
        }

        if (hasCombinedCutter) {
            SAFE_MESH_OP(mesh.CollapseDeadStructs());
            SAFE_MESH_OP(mesh.FillInMesh());
            SAFE_MESH_OP(combinedCutter.CollapseDeadStructs());
            SAFE_MESH_OP(combinedCutter.FillInMesh());
            if (!IsMeshValid(mesh) || !IsMeshValid(combinedCutter)) break;

            bool booleanSuccess = false;
            const bool noCrash = SAFE_MESH_OP({
                mesh.PrepForBoolean();
                combinedCutter.PrepForBoolean();
                booleanSuccess = mesh.BooleanCut(combinedCutter, currentCutMode);
            });

            if (noCrash) {
                CleanMeshAfterBoolean(mesh, opts.weldThresh, opts.keepTris);
                didClosedCut = didClosedCut || booleanSuccess;
            }

            if (!IsMeshValid(mesh)) break;
        }

        i = j;
    }

    // Open splines
    for (const ProjectedPolyline& polyline : polylines) {
        if (polyline.pts.size() < 2 || polyline.closed) continue;
        if (!IsMeshValid(mesh)) break;
        CutOpenSpline(mesh, polyline, ResolveProjDir(polyline, projDir), opts.weldThresh);
    }

    if (didClosedCut)
        CleanMeshAfterBoolean(mesh, opts.weldThresh, opts.keepTris);

    SAFE_MESH_OP(mesh.EliminateCoincidentVerts(std::max(opts.weldThresh, 1.0e-6f)));
    SAFE_MESH_OP(mesh.EliminateCollinearVerts());
    SAFE_MESH_OP(mesh.CollapseDeadStructs());
    SAFE_MESH_OP(mesh.FillInMesh());
}
