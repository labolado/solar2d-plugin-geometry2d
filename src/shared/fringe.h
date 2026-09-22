/*
* fringe.h — edge antialiasing "fringe" (skirt) generation, extracted from
* NanoVG (https://github.com/memononen/nanovg).
*
* This is a standalone adaptation of NanoVG's nvg__expandFill /
* nvg__expandStroke outline expansion logic (nanovg.c), reworked to run
* on flat polylines without an NVGcontext. The input polygons are already
* flattened (no bezier tessellation — feed triangulated/point-only paths),
* so only the join/cap/fringe machinery was extracted.
*
* Output is a flat triangle list. Each vertex carries:
*   a — the per-vertex alpha, precomputed for mesh:setFillVertexColor
*       (Solar2D meshes bake the AA gradient into vertex alpha; no
*       custom shader needed)
*   (u, v) are NanoVG's internal fringe-gradient coordinates — they are
*   consumed while building the strip (v drives the cap fade, baked into a)
*   and are NOT exposed to Lua.
*
* Stroke (banded adaptation of NanoVG's strip):
*   The strip spans the stroke half-width + `fringe` pixels of AA skirt on
*   each side, as two aligned vertex rows — the outer skirt edge (a = 0)
*   and the core edge at the true stroke half-width (a = 1) — interleaved
*   into one strip. Linear vertex-color interpolation then reproduces the
*   exact AA ramp: solid core, linear fade over the skirt. (NanoVG itself
*   computes this ramp per-pixel in its fragment shader, which is why its
*   strip vertices cannot be vertex-color baked.)
*
* Fill (stencil-free adaptation):
*   A one-sided skirt expanded from each ring's boundary toward the
*   NON-FILL side by `fringe` pixels (NanoVG emits a two-sided strip that
*   relies on a stencil buffer to clip the interior half; a mesh has no
*   stencil, so only the non-fill half is emitted here):
*     outer ring — the skirt goes OUTSIDE the polygon
*     hole ring  — the skirt goes INTO the hole (toward its interior)
*   a = 1 on the ring outline (opaque), 0 at the skirt edge (transparent).
*   Which side is the fill cannot be derived from a single ring's winding
*   alone, so it is resolved from the ring's `hole` semantics (see
*   FillRing): an explicit flag wins; otherwise the winding convention
*   applies.
*
* Coordinates are Solar2D display coordinates — y axis pointing DOWN
* (screen space), the same space display.newMesh renders in. Winding
* convention (when the hole flag is auto): positive shoelace area = the
* ring is listed clockwise on screen (outer polygon); negative =
* counterclockwise on screen (hole).
*/

#pragma once

#include <utility>
#include <vector>
#include <cstddef>

namespace Fringe {

enum LineCap {
    CAP_BUTT,
    CAP_SQUARE,
    CAP_ROUND,
};

enum LineJoin {
    JOIN_MITER,
    JOIN_BEVEL,
    JOIN_ROUND,
};

struct Vertex {
    float x, y;
    float u, v;
    float a;   // per-vertex alpha (for mesh:setFillVertexColor)
};

// A fill ring with hole semantics. The skirt side (into the hole vs.
// outside the polygon) is resolved as:
//   hole = 1  → hole ring: skirt toward the ring's interior
//   hole = 0  → outer ring: skirt away from the ring's interior
//   hole = -1 → auto: positive shoelace (CW on screen) = outer,
//              negative (CCW on screen) = hole
struct FillRing {
    std::vector<std::pair<float, float>> points;
    int hole = -1;
};

// Fill fringe: one-sided AA skirt toward the non-fill side of each ring.
//   rings: closed polygons in Solar2D display coordinates (y down).
//   fringe: skirt width in pixels (>= 0; 0 produces no output).
//   join: corner style for the skirt edge.
//   miterLimit: miter validity threshold (NanoVG default 2.4).
//   tessTol: tessellation tolerance for round joins (NanoVG default 0.25).
// Output: flat triangle list (3 vertices per triangle).
// False means subdivision cannot be represented safely; discard the output.
bool ExpandFill(const std::vector<FillRing> &rings,
                float fringe, LineJoin join, float miterLimit, float tessTol,
                std::vector<Vertex> &out);

// Stroke fringe: banded strip covering the stroke body plus the AA skirt on
// both sides (see header notes for the alpha/uv layout).
//   polylines: one or more polylines; `closed` applies to all of them.
//   width: full stroke width in pixels.
//   fringe: AA skirt width on each side (>= 0; 0 = solid strip, alpha 1).
//   cap: end-cap style for open polylines.
// Output: flat triangle list (3 vertices per triangle).
bool ExpandStroke(const std::vector<std::vector<std::pair<float, float>>> &polylines,
                  bool closed, float width, float fringe,
                  LineCap cap, LineJoin join, float miterLimit, float tessTol,
                  std::vector<Vertex> &out, std::size_t maxVertices = 1000000);

} // namespace Fringe
