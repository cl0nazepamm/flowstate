#pragma once
#include <max.h>
#include <mnmesh.h>
#include <vector>
#include <cmath>
#include <algorithm>

// ── Projected polyline from a single spline ─────────────────────
struct ProjectedPolyline {
    std::vector<Point3>  pts;       // world-space polyline points
    bool                 closed;    // true = hole punch, false = cut
    int                  cutType;   // BOOLOP_CUT_* mode for this polyline
    Point3               projDir;   // per-polyline projection direction in mesh space
    float                depth;     // per-polyline depth override (0 = infinite)
    bool                 flipDepth; // per-polyline depth flip
    bool                 onSurface; // true = pts are already projected onto mesh surface
};

// ── Per-face hit: intersection segment on a face ────────────────
struct FaceHit {
    int   faceIdx;
    int   segStart;     // polyline segment index that enters
    float tEnter;       // parametric entry on segment edge
    float tExit;        // parametric exit  on segment edge
    Point3 pEnter;      // entry point in face local coords
    Point3 pExit;       // exit point  in face local coords
};

// ── 2D point for polygon operations ─────────────────────────────
struct Point2D {
    double x, y;
    Point2D() : x(0), y(0) {}
    Point2D(double _x, double _y) : x(_x), y(_y) {}
    Point2D operator-(const Point2D& o) const { return {x - o.x, y - o.y}; }
    Point2D operator+(const Point2D& o) const { return {x + o.x, y + o.y}; }
    Point2D operator*(double s) const { return {x * s, y * s}; }
    double cross(const Point2D& o) const { return x * o.y - y * o.x; }
    double dot(const Point2D& o) const { return x * o.x + y * o.y; }
    double length() const { return std::sqrt(x * x + y * y); }
};

// ── Utility: project 3D point onto face plane ───────────────────
// Returns 2D coords in face-local frame (u,v axes on face plane)
inline Point2D ProjectToFacePlane(const Point3& pt, const Point3& origin,
                                   const Point3& uAxis, const Point3& vAxis) {
    Point3 d = pt - origin;
    return Point2D(DotProd(d, uAxis), DotProd(d, vAxis));
}

// ── Build a local 2D frame for a face ───────────────────────────
struct FaceFrame {
    Point3 origin;
    Point3 normal;
    Point3 uAxis;
    Point3 vAxis;
};

FaceFrame BuildFaceFrame(MNMesh& mesh, int faceIdx);

// ── 2D segment-segment intersection ─────────────────────────────
bool Intersect2DSegments(const Point2D& a0, const Point2D& a1,
                         const Point2D& b0, const Point2D& b1,
                         double& tA, double& tB, double eps = 1e-10);

// ── Point-in-polygon 2D (winding number) ────────────────────────
int WindingNumber2D(const Point2D& pt, const std::vector<Point2D>& polygon);

// ── Ray-face intersection (3D ray vs single MNMesh face) ────────
bool RayHitsFace(MNMesh& mesh, int faceIdx, const Point3& rayOrig,
                 const Point3& rayDir, float& tHit, Point3& hitPt);
