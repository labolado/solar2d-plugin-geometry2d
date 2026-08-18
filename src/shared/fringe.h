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
*   (u, v) — NanoVG's fringe-gradient coordinates, kept for shader users
*   a      — the per-vertex alpha, precomputed for mesh:setFillVertexColor
*            (Solar2D meshes bake the AA gradient into vertex alpha; no
*            custom shader needed)
*
* Stroke (banded adaptation of NanoVG's strip):
*   The strip spans the stroke half-width + `fringe` pixels of AA skirt on
*   each side, as two aligned vertex rows — the outer skirt edge (a = 0)
*   and the core edge at the true stroke half-width (a = 1) — interleaved
*   into one strip. Linear vertex-color interpolation then reproduces the
*   exact AA ramp: solid core, linear fade over the skirt. (NanoVG itself
*   computes this ramp per-pixel in its fragment shader, which is why its
*   strip vertices cannot be vertex-color baked.)
*   u = 0 / 1 at the skirt edges, core rows at uc = fringe/(2·(w/2+fringe));
*   v = 0 at butt/square cap faces, 1 elsewhere.
*
* Fill (stencil-free adaptation):
*   A one-sided skirt expanded OUTWARD from the fill region by `fringe`
*   pixels (NanoVG emits a two-sided strip that relies on a stencil
*   buffer to clip the interior half; a mesh has no stencil, so only
*   the outward half is emitted here). The skirt always extends away
*   from each ring's OWN interior as given by its winding (signed
*   shoelace area), so outer rings and holes both work without a hole
*   flag. a = 1 on the path outline (opaque), 0 at the outer skirt
*   edge (transparent). u = 0 / 1 likewise; v is always 1.
*
* Coordinates are Solar2D display coordinates — y axis pointing DOWN
* (screen space), the same space display.newMesh renders in. Positive
* shoelace area = the ring is listed clockwise on screen (outer polygon);
* negative = counterclockwise on screen (hole).
*/

#pragma once

#include <utility>
#include <vector>

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

// Fill fringe: one-sided outward AA skirt along the outline of each ring.
//   rings: closed polygons in Solar2D display coordinates (y down). The
//          skirt extends away from each ring's own interior as determined
//          by its winding — outer polygons and holes are handled alike,
//          no hole flag needed.
//   fringe: skirt width in pixels (>= 0; 0 produces no output).
//   join: corner style for the outer edge.
//   miterLimit: miter validity threshold (NanoVG default 2.4).
//   tessTol: tessellation tolerance for round joins (NanoVG default 0.25).
// Output: flat triangle list (3 vertices per triangle).
void ExpandFill(const std::vector<std::vector<std::pair<float, float>>> &rings,
                float fringe, LineJoin join, float miterLimit, float tessTol,
                std::vector<Vertex> &out);

// Stroke fringe: banded strip covering the stroke body plus the AA skirt on
// both sides (see header notes for the alpha/uv layout).
//   polylines: one or more polylines; `closed` applies to all of them.
//   width: full stroke width in pixels.
//   fringe: AA skirt width on each side (>= 0; 0 = solid strip, alpha 1).
//   cap: end-cap style for open polylines.
// Output: flat triangle list (3 vertices per triangle).
void ExpandStroke(const std::vector<std::vector<std::pair<float, float>>> &polylines,
                  bool closed, float width, float fringe,
                  LineCap cap, LineJoin join, float miterLimit, float tessTol,
                  std::vector<Vertex> &out);

} // namespace Fringe
