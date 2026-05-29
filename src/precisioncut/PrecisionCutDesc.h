#pragma once
#include <max.h>
#include <iparamb2.h>
#include <iparamm2.h>
#include <modstack.h>
#include <maxtypes.h>

// ── Plugin identity ─────────────────────────────────────────────
#define PRECISIONCUT_CLASS_ID  Class_ID(0x5C4B7D89, 0x2E3F6A14)
#define PBLOCK_REF             0

// ── Parameter IDs ───────────────────────────────────────────────
enum PrecisionCutParams : ParamID {
    pb_spline_node = 0,
    pb_steps,
    pb_flip_normals,       // kept for file compat, replaced by pb_mode
    pb_weld_threshold,
    pb_mode,               // global default mode for new splines
    pb_keep_tris,
    pb_spline_mode,        // per-spline cut mode (parallel to pb_spline_node)
    pb_proj_mode,          // projection mode: 0=SplineZ, 1=View, 2=SurfaceNormal
    pb_depth,              // cut depth (0 = infinite)
    pb_solid,              // enable shell/thickness
    pb_thickness,          // shell wall thickness
    pb_bevel,              // enable auto-bevel
    pb_bevel_amount,       // chamfer amount
    pb_bevel_segments,     // chamfer segments
    pb_spline_steps,           // per-spline extraction steps
    pb_spline_weld_threshold,  // per-spline weld threshold
    pb_spline_keep_tris,       // per-spline keep triangulation
    pb_spline_proj_mode,       // per-spline projection mode
    pb_spline_flip_depth,      // per-spline depth flip
    pb_spline_depth,           // per-spline depth
    pb_spline_solid,           // per-spline solid mode
    pb_spline_thickness,       // per-spline thickness
    pb_spline_bevel,           // per-spline bevel toggle
    pb_spline_bevel_amount,    // per-spline bevel amount
    pb_spline_bevel_segments,  // per-spline bevel segments
};

// ── Projection mode constants ──────────────────────────────────
enum ProjectionMode : int {
    PROJ_SPLINE_Z = 0,
    PROJ_NORMAL   = 1,
};

extern HINSTANCE hInstance;
