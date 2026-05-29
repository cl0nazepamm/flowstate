#pragma once
#include <max.h>
#include <mnmesh.h>
#include <vector>
#include "SplineProjector.h"

// ── Post-cut options ────────────────────────────────────────────
struct CutOptions {
    float weldThresh  = 0.001f;
    bool  keepTris    = false;
    float depth       = 0.0f;    // 0 = infinite (auto from bbox)
    bool  flipDepth   = false;   // finite depth extends opposite projection axis
    bool  solid       = false;   // shell/thickness after cut
    float thickness   = 1.0f;    // shell wall thickness
    bool  bevel       = false;   // auto-chamfer boundary edges
    float bevelAmount = 0.5f;    // chamfer distance
    int   bevelSegs   = 1;       // chamfer segments
};

// ── Main mesh cutting engine ────────────────────────────────────
class MeshCutter {
public:
    // projDir = projection direction (spline Z, view, or surface normal).
    // Each polyline carries its own cutType (BOOLOP_CUT_*).
    static void Execute(MNMesh& mesh,
                        const std::vector<ProjectedPolyline>& polylines,
                        const Point3& projDir,
                        const CutOptions& opts);
};
