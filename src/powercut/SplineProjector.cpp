#include "SplineProjector.h"
#include <mnmesh.h>
#include <float.h>

// ═════════════════════════════════════════════════════════════════
// Build a local 2D orthonormal frame on a mesh face
// ═════════════════════════════════════════════════════════════════
FaceFrame BuildFaceFrame(MNMesh& mesh, int faceIdx) {
    FaceFrame ff;
    MNFace* face = mesh.F(faceIdx);

    // Compute face normal via Newell's method (handles ngons)
    ff.normal = Point3(0, 0, 0);
    int deg = face->deg;
    for (int i = 0; i < deg; i++) {
        const Point3& cur = mesh.P(face->vtx[i]);
        const Point3& nxt = mesh.P(face->vtx[(i + 1) % deg]);
        ff.normal.x += (cur.y - nxt.y) * (cur.z + nxt.z);
        ff.normal.y += (cur.z - nxt.z) * (cur.x + nxt.x);
        ff.normal.z += (cur.x - nxt.x) * (cur.y + nxt.y);
    }
    float len = Length(ff.normal);
    if (len > 1e-12f)
        ff.normal /= len;
    else
        ff.normal = Point3(0, 0, 1);

    ff.origin = mesh.P(face->vtx[0]);

    // Build orthonormal U/V axes on face plane
    Point3 edge0 = mesh.P(face->vtx[1]) - ff.origin;
    float elen = Length(edge0);
    if (elen > 1e-12f)
        ff.uAxis = edge0 / elen;
    else
        ff.uAxis = Point3(1, 0, 0);

    ff.vAxis = CrossProd(ff.normal, ff.uAxis);
    float vlen = Length(ff.vAxis);
    if (vlen > 1e-12f)
        ff.vAxis /= vlen;
    else
        ff.vAxis = Point3(0, 1, 0);

    return ff;
}

// ═════════════════════════════════════════════════════════════════
// 2D segment-segment intersection
// Returns true if segments intersect, with parametric values tA, tB
// ═════════════════════════════════════════════════════════════════
bool Intersect2DSegments(const Point2D& a0, const Point2D& a1,
                         const Point2D& b0, const Point2D& b1,
                         double& tA, double& tB, double eps) {
    Point2D da = a1 - a0;
    Point2D db = b1 - b0;
    double denom = da.cross(db);

    if (std::abs(denom) < eps)
        return false;  // parallel

    Point2D ab = b0 - a0;
    tA = ab.cross(db) / denom;
    tB = ab.cross(da) / denom;

    return (tA >= -eps && tA <= 1.0 + eps && tB >= -eps && tB <= 1.0 + eps);
}

// ═════════════════════════════════════════════════════════════════
// Point-in-polygon by winding number (2D)
// Returns winding number: 0 means outside, nonzero means inside
// ═════════════════════════════════════════════════════════════════
int WindingNumber2D(const Point2D& pt, const std::vector<Point2D>& polygon) {
    int wn = 0;
    int n = (int)polygon.size();
    for (int i = 0; i < n; i++) {
        const Point2D& p0 = polygon[i];
        const Point2D& p1 = polygon[(i + 1) % n];

        if (p0.y <= pt.y) {
            if (p1.y > pt.y) {
                // upward crossing
                double cross = (p1 - p0).cross(pt - p0);
                if (cross > 0)
                    ++wn;
            }
        } else {
            if (p1.y <= pt.y) {
                // downward crossing
                double cross = (p1 - p0).cross(pt - p0);
                if (cross < 0)
                    --wn;
            }
        }
    }
    return wn;
}

// ═════════════════════════════════════════════════════════════════
// Ray vs single MNMesh face intersection (fan triangulation)
// ═════════════════════════════════════════════════════════════════
bool RayHitsFace(MNMesh& mesh, int faceIdx, const Point3& rayOrig,
                 const Point3& rayDir, float& tHit, Point3& hitPt) {
    MNFace* face = mesh.F(faceIdx);
    if (!face || face->GetFlag(MN_DEAD))
        return false;

    int deg = face->deg;
    if (deg < 3)
        return false;

    // Fan triangulate from vertex 0
    const Point3& v0 = mesh.P(face->vtx[0]);
    float bestT = FLT_MAX;
    bool hit = false;

    for (int i = 1; i < deg - 1; i++) {
        const Point3& v1 = mesh.P(face->vtx[i]);
        const Point3& v2 = mesh.P(face->vtx[i + 1]);

        // Moller-Trumbore
        Point3 e1 = v1 - v0;
        Point3 e2 = v2 - v0;
        Point3 pvec = CrossProd(rayDir, e2);
        float det = DotProd(e1, pvec);

        if (std::abs(det) < 1e-10f)
            continue;

        float invDet = 1.0f / det;
        Point3 tvec = rayOrig - v0;
        float u = DotProd(tvec, pvec) * invDet;
        if (u < -1e-6f || u > 1.0f + 1e-6f)
            continue;

        Point3 qvec = CrossProd(tvec, e1);
        float v = DotProd(rayDir, qvec) * invDet;
        if (v < -1e-6f || u + v > 1.0f + 1e-6f)
            continue;

        float t = DotProd(e2, qvec) * invDet;
        if (t > 1e-6f && t < bestT) {
            bestT = t;
            hit = true;
        }
    }

    if (hit) {
        tHit = bestT;
        hitPt = rayOrig + rayDir * bestT;
    }
    return hit;
}
