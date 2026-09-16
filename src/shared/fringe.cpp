/*
* fringe.cpp — see fringe.h. Direct adaptation of NanoVG's nvg__expandFill /
* nvg__expandStroke (nanovg.c), reworked for standalone flat-polyline use in
* Solar2D display coordinates (y down).
*/

#include "fringe.h"

#include <cmath>
#include <array>

namespace Fringe {

namespace {

const float PI = 3.14159265f;

// NanoVG's NVGpointFlags
enum {
    PT_CORNER     = 0x01,
    PT_LEFT       = 0x02,
    PT_BEVEL      = 0x04,
    PT_INNERBEVEL = 0x08,
};

struct Point {
    float x, y;
    float dx, dy;     // normalized direction of the edge starting here
    float len;        // length of that edge
    float dmx, dmy;   // miter direction (scaled)
    unsigned char flags;
};

inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline int maxi(int a, int b) { return a > b ? a : b; }
inline int clampi(int a, int mn, int mx) { return a < mn ? mn : (a > mx ? mx : a); }

// nvg__normalize
static float normalize(float *x, float *y)
{
    float d = sqrtf((*x) * (*x) + (*y) * (*y));
    if (d > 1e-6f) {
        float id = 1.0f / d;
        *x *= id;
        *y *= id;
    }
    return d;
}

// nvg__curveDivs
static int curveDivs(float r, float arc, float tol)
{
    float da = acosf(r / (r + tol)) * 2.0f;
    return maxi(2, (int)ceilf(arc / da));
}

// Signed polygon area (standard shoelace). In Solar2D display coordinates
// (y down) positive = clockwise on screen (outer polygon), negative =
// counterclockwise on screen (hole). The sign is all the skirt needs — the
// shoelace formula itself is indifferent to the axis orientation.
// NOTE: NanoVG's nvg__polyArea negates this (it flips the cross product
// terms), because NanoVG flips its own y at the GL layer; the standard
// cross product is used here to match Solar2D screen coordinates directly.
static float polyArea(Point *pts, int npts)
{
    float area = 0;
    for (int i = 0; i < npts; i++) {
        Point &a = pts[i];
        Point &b = pts[(i + 1) % npts];
        area += a.x * b.y - a.y * b.x;
    }
    return area * 0.5f;
}

static void pushV(std::vector<Vertex> &out, float x, float y, float u, float v, float a)
{
    Vertex vtx;
    vtx.x = x;
    vtx.y = y;
    vtx.u = u;
    vtx.v = v;
    vtx.a = a;
    out.push_back(vtx);
}

// ---------------------------------------------------------------------------
// nvg__calculateJoins
// ---------------------------------------------------------------------------
static void calculateJoins(std::vector<Point> &pts, float w, LineJoin lineJoin, float miterLimit)
{
    float iw = 0.0f;

    if (w > 0.0f) iw = 1.0f / w;

    size_t n = pts.size();
    Point *p0 = &pts[n - 1];
    Point *p1 = &pts[0];

    for (size_t j = 0; j < n; j++) {
        float dlx0, dly0, dlx1, dly1, dmr2, cross, limit;
        dlx0 = p0->dy;
        dly0 = -p0->dx;
        dlx1 = p1->dy;
        dly1 = -p1->dx;
        // Calculate extrusions
        p1->dmx = (dlx0 + dlx1) * 0.5f;
        p1->dmy = (dly0 + dly1) * 0.5f;
        dmr2 = p1->dmx * p1->dmx + p1->dmy * p1->dmy;
        if (dmr2 > 0.000001f) {
            float scale = 1.0f / dmr2;
            if (scale > 600.0f) {
                scale = 600.0f;
            }
            p1->dmx *= scale;
            p1->dmy *= scale;
        }

        // Clear flags, but keep the corner.
        p1->flags = (p1->flags & PT_CORNER) ? PT_CORNER : 0;

        // Keep track of left turns.
        cross = p1->dx * p0->dy - p0->dx * p1->dy;
        if (cross > 0.0f) {
            p1->flags |= PT_LEFT;
        }

        // Calculate if we should use bevel or miter for inner join.
        limit = maxf(1.01f, minf(p0->len, p1->len) * iw);
        if ((dmr2 * limit * limit) < 1.0f)
            p1->flags |= PT_INNERBEVEL;

        // Check to see if the corner needs to be beveled.
        if (p1->flags & PT_CORNER) {
            if ((dmr2 * miterLimit * miterLimit) < 1.0f || lineJoin == JOIN_BEVEL || lineJoin == JOIN_ROUND) {
                p1->flags |= PT_BEVEL;
            }
        }

        p0 = p1++;
    }
}

// ---------------------------------------------------------------------------
// nvg__chooseBevel
// ---------------------------------------------------------------------------
static void chooseBevel(bool bevel, Point *p0, Point *p1, float w,
                        float *x0, float *y0, float *x1, float *y1)
{
    if (bevel) {
        *x0 = p1->x + p0->dy * w;
        *y0 = p1->y - p0->dx * w;
        *x1 = p1->x + p1->dy * w;
        *y1 = p1->y - p1->dx * w;
    } else {
        *x0 = p1->x + p1->dmx * w;
        *y0 = p1->y + p1->dmy * w;
        *x1 = p1->x + p1->dmx * w;
        *y1 = p1->y + p1->dmy * w;
    }
}

// ---------------------------------------------------------------------------
// nvg__roundJoin — emits (lu-side, ru-side) vertex pairs forming a strip.
// All vertices get alpha `a` (the run's row alpha; cap fades multiply later).
// ---------------------------------------------------------------------------
static void roundJoin(std::vector<Vertex> &out, Point *p0, Point *p1,
                      float lw, float rw, float lu, float ru, int ncap, float a)
{
    int i, n;
    float dlx0 = p0->dy;
    float dly0 = -p0->dx;
    float dlx1 = p1->dy;
    float dly1 = -p1->dx;

    if (p1->flags & PT_LEFT) {
        float lx0, ly0, lx1, ly1, a0, a1;
        chooseBevel(p1->flags & PT_INNERBEVEL, p0, p1, lw, &lx0, &ly0, &lx1, &ly1);
        a0 = atan2f(-dly0, -dlx0);
        a1 = atan2f(-dly1, -dlx1);
        if (a1 > a0) a1 -= PI * 2;

        pushV(out, lx0, ly0, lu, 1, a);
        pushV(out, p1->x - dlx0 * rw, p1->y - dly0 * rw, ru, 1, a);

        n = clampi((int)ceilf(((a0 - a1) / PI) * ncap), 2, ncap);
        for (i = 0; i < n; i++) {
            float u = i / (float)(n - 1);
            float ang = a0 + u * (a1 - a0);
            float rx = p1->x + cosf(ang) * rw;
            float ry = p1->y + sinf(ang) * rw;
            pushV(out, p1->x, p1->y, 0.5f, 1, a);
            pushV(out, rx, ry, ru, 1, a);
        }

        pushV(out, lx1, ly1, lu, 1, a);
        pushV(out, p1->x - dlx1 * rw, p1->y - dly1 * rw, ru, 1, a);

    } else {
        float rx0, ry0, rx1, ry1, a0, a1;
        chooseBevel(p1->flags & PT_INNERBEVEL, p0, p1, -rw, &rx0, &ry0, &rx1, &ry1);
        a0 = atan2f(dly0, dlx0);
        a1 = atan2f(dly1, dlx1);
        if (a1 < a0) a1 += PI * 2;

        pushV(out, p1->x + dlx0 * rw, p1->y + dly0 * rw, lu, 1, a);
        pushV(out, rx0, ry0, ru, 1, a);

        n = clampi((int)ceilf(((a1 - a0) / PI) * ncap), 2, ncap);
        for (i = 0; i < n; i++) {
            float u = i / (float)(n - 1);
            float ang = a0 + u * (a1 - a0);
            float lx = p1->x + cosf(ang) * lw;
            float ly = p1->y + sinf(ang) * lw;
            pushV(out, lx, ly, lu, 1, a);
            pushV(out, p1->x, p1->y, 0.5f, 1, a);
        }

        pushV(out, p1->x + dlx1 * rw, p1->y + dly1 * rw, lu, 1, a);
        pushV(out, rx1, ry1, ru, 1, a);
    }
}

// ---------------------------------------------------------------------------
// nvg__bevelJoin — emits (lu-side, ru-side) vertex pairs forming a strip
// ---------------------------------------------------------------------------
static void bevelJoin(std::vector<Vertex> &out, Point *p0, Point *p1,
                      float lw, float rw, float lu, float ru, float a)
{
    float rx0, ry0, rx1, ry1;
    float lx0, ly0, lx1, ly1;
    float dlx0 = p0->dy;
    float dly0 = -p0->dx;
    float dlx1 = p1->dy;
    float dly1 = -p1->dx;

    if (p1->flags & PT_LEFT) {
        chooseBevel(p1->flags & PT_INNERBEVEL, p0, p1, lw, &lx0, &ly0, &lx1, &ly1);

        pushV(out, lx0, ly0, lu, 1, a);
        pushV(out, p1->x - dlx0 * rw, p1->y - dly0 * rw, ru, 1, a);

        if (p1->flags & PT_BEVEL) {
            pushV(out, lx0, ly0, lu, 1, a);
            pushV(out, p1->x - dlx0 * rw, p1->y - dly0 * rw, ru, 1, a);

            pushV(out, lx1, ly1, lu, 1, a);
            pushV(out, p1->x - dlx1 * rw, p1->y - dly1 * rw, ru, 1, a);
        } else {
            rx0 = p1->x - p1->dmx * rw;
            ry0 = p1->y - p1->dmy * rw;

            pushV(out, p1->x, p1->y, 0.5f, 1, a);
            pushV(out, p1->x - dlx0 * rw, p1->y - dly0 * rw, ru, 1, a);

            pushV(out, rx0, ry0, ru, 1, a);
            pushV(out, rx0, ry0, ru, 1, a);

            pushV(out, p1->x, p1->y, 0.5f, 1, a);
            pushV(out, p1->x - dlx1 * rw, p1->y - dly1 * rw, ru, 1, a);
        }

        pushV(out, lx1, ly1, lu, 1, a);
        pushV(out, p1->x - dlx1 * rw, p1->y - dly1 * rw, ru, 1, a);

    } else {
        chooseBevel(p1->flags & PT_INNERBEVEL, p0, p1, -rw, &rx0, &ry0, &rx1, &ry1);

        pushV(out, p1->x + dlx0 * lw, p1->y + dly0 * lw, lu, 1, a);
        pushV(out, rx0, ry0, ru, 1, a);

        if (p1->flags & PT_BEVEL) {
            pushV(out, p1->x + dlx0 * lw, p1->y + dly0 * lw, lu, 1, a);
            pushV(out, rx0, ry0, ru, 1, a);

            pushV(out, p1->x + dlx1 * lw, p1->y + dly1 * lw, lu, 1, a);
            pushV(out, rx1, ry1, ru, 1, a);
        } else {
            lx0 = p1->x + p1->dmx * lw;
            ly0 = p1->y + p1->dmy * lw;

            pushV(out, p1->x + dlx0 * lw, p1->y + dly0 * lw, lu, 1, a);
            pushV(out, p1->x, p1->y, 0.5f, 1, a);

            pushV(out, lx0, ly0, lu, 1, a);
            pushV(out, lx0, ly0, lu, 1, a);

            pushV(out, p1->x + dlx1 * lw, p1->y + dly1 * lw, lu, 1, a);
            pushV(out, p1->x, p1->y, 0.5f, 1, a);
        }

        pushV(out, p1->x + dlx1 * lw, p1->y + dly1 * lw, lu, 1, a);
        pushV(out, rx1, ry1, ru, 1, a);
    }
}

// ---------------------------------------------------------------------------
// nvg__buttCapStart / nvg__buttCapEnd / nvg__roundCapStart / nvg__roundCapEnd
// ---------------------------------------------------------------------------
static void buttCapStart(std::vector<Vertex> &out, Point *p,
                         float dx, float dy, float w, float d,
                         float aa, float u0, float u1, float a)
{
    float px = p->x - dx * d;
    float py = p->y - dy * d;
    float dlx = dy;
    float dly = -dx;
    pushV(out, px + dlx * w - dx * aa, py + dly * w - dy * aa, u0, 0, a);
    pushV(out, px - dlx * w - dx * aa, py - dly * w - dy * aa, u1, 0, a);
    pushV(out, px + dlx * w, py + dly * w, u0, 1, a);
    pushV(out, px - dlx * w, py - dly * w, u1, 1, a);
}

static void buttCapEnd(std::vector<Vertex> &out, Point *p,
                       float dx, float dy, float w, float d,
                       float aa, float u0, float u1, float a)
{
    float px = p->x + dx * d;
    float py = p->y + dy * d;
    float dlx = dy;
    float dly = -dx;
    pushV(out, px + dlx * w, py + dly * w, u0, 1, a);
    pushV(out, px - dlx * w, py - dly * w, u1, 1, a);
    pushV(out, px + dlx * w + dx * aa, py + dly * w + dy * aa, u0, 0, a);
    pushV(out, px - dlx * w + dx * aa, py - dly * w + dy * aa, u1, 0, a);
}

static void roundCapStart(std::vector<Vertex> &out, Point *p,
                          float dx, float dy, float w, int ncap, float u0, float u1, float a)
{
    int i;
    float px = p->x;
    float py = p->y;
    float dlx = dy;
    float dly = -dx;
    for (i = 0; i < ncap; i++) {
        float t = i / (float)(ncap - 1) * PI;
        float ax = cosf(t) * w, ay = sinf(t) * w;
        pushV(out, px - dlx * ax - dx * ay, py - dly * ax - dy * ay, u0, 1, a);
        pushV(out, px, py, 0.5f, 1, a);
    }
    pushV(out, px + dlx * w, py + dly * w, u0, 1, a);
    pushV(out, px - dlx * w, py - dly * w, u1, 1, a);
}

static void roundCapEnd(std::vector<Vertex> &out, Point *p,
                        float dx, float dy, float w, int ncap, float u0, float u1, float a)
{
    int i;
    float px = p->x;
    float py = p->y;
    float dlx = dy;
    float dly = -dx;
    pushV(out, px + dlx * w, py + dly * w, u0, 1, a);
    pushV(out, px - dlx * w, py - dly * w, u1, 1, a);
    for (i = 0; i < ncap; i++) {
        float t = i / (float)(ncap - 1) * PI;
        float ax = cosf(t) * w, ay = sinf(t) * w;
        pushV(out, px, py, 0.5f, 1, a);
        pushV(out, px - dlx * ax + dx * ay, py - dly * ax + dy * ay, u0, 1, a);
    }
}

// Convert strip pairs (A0,B0, A1,B1, ...) into a flat triangle list.
// Follows the OpenGL triangle-strip winding: tri k = (2k, 2k+1, 2k+2),
// (2k+1, 2k+3, 2k+2). Degenerate quads produce degenerate triangles, which
// renderers skip harmlessly.
static void StripToTriangles(const std::vector<Vertex> &strip, std::vector<Vertex> &tris)
{
    size_t npairs = strip.size() / 2;
    if (npairs < 2) return;
    tris.reserve(tris.size() + (npairs - 1) * 6);
    for (size_t k = 0; k + 1 < npairs; ++k) {
        const Vertex &a = strip[k * 2];
        const Vertex &b = strip[k * 2 + 1];
        const Vertex &c = strip[k * 2 + 2];
        const Vertex &d = strip[k * 2 + 3];
        tris.push_back(a);
        tris.push_back(b);
        tris.push_back(c);
        tris.push_back(b);
        tris.push_back(d);
        tris.push_back(c);
    }
}

// Fill a Points array from a ring/polyline, computing per-edge direction and
// length. For closed input the last point's edge wraps to the first; for open
// input the last point gets the same phantom wrap edge (matches NanoVG).
static void PreparePoints(const std::vector<std::pair<float, float>> &ring,
                          std::vector<Point> &pts)
{
    size_t n = ring.size();
    pts.resize(n);
    for (size_t i = 0; i < n; ++i) {
        Point &p = pts[i];
        p.x = ring[i].first;
        p.y = ring[i].second;
        size_t j = (i + 1 < n) ? i + 1 : 0;   // phantom wrap on the last point
        p.dx = ring[j].first - ring[i].first;
        p.dy = ring[j].second - ring[i].second;
        p.len = normalize(&p.dx, &p.dy);
        p.dmx = p.dmy = 0;
        p.flags = PT_CORNER;
    }
}

// ---------------------------------------------------------------------------
// Emit one stroke "row run" of the banded strip: a single NanoVG-style strip
// at half-width `d`, where every vertex carries u = uA (lu side) / uB
// (ru side) and alpha `a`. Runs at the outer skirt distance (a = 0) and the
// core distance (a = 1) are emitted with IDENTICAL structure; interleaving
// them yields quads whose interpolated alpha forms the exact AA ramp
// (solid core, linear fade over the fringe skirt) — linear vertex-color
// interpolation reproduces it without a fragment shader.
// ---------------------------------------------------------------------------
static void EmitStrokeStrip(std::vector<Point> &pts, bool closed,
                            float d, float uA, float uB, float a,
                            LineCap cap, LineJoin join, int ncap, float aa,
                            std::vector<Vertex> &strip)
{
    size_t n = pts.size();
    Point *p0, *p1;
    size_t s, e;

    if (closed) {
        p0 = &pts[n - 1];
        p1 = &pts[0];
        s = 0;
        e = n;
    } else {
        p0 = &pts[0];
        p1 = &pts[1];
        s = 1;
        e = n - 1;
    }

    if (!closed) {
        // Add start cap.
        float dx = p1->x - p0->x;
        float dy = p1->y - p0->y;
        normalize(&dx, &dy);
        if (cap == CAP_BUTT)
            buttCapStart(strip, p0, dx, dy, d, -aa * 0.5f, aa, uA, uB, a);
        else if (cap == CAP_BUTT || cap == CAP_SQUARE)
            buttCapStart(strip, p0, dx, dy, d, d - aa, aa, uA, uB, a);
        else if (cap == CAP_ROUND)
            roundCapStart(strip, p0, dx, dy, d, ncap, uA, uB, a);
    }

    for (size_t j = s; j < e; ++j) {
        if ((p1->flags & (PT_BEVEL | PT_INNERBEVEL)) != 0) {
            if (join == JOIN_ROUND) {
                roundJoin(strip, p0, p1, d, d, uA, uB, ncap, a);
            } else {
                bevelJoin(strip, p0, p1, d, d, uA, uB, a);
            }
        } else {
            pushV(strip, p1->x + (p1->dmx * d), p1->y + (p1->dmy * d), uA, 1, a);
            pushV(strip, p1->x - (p1->dmx * d), p1->y - (p1->dmy * d), uB, 1, a);
        }
        p0 = p1++;
    }

    if (closed) {
        // Loop it
        pushV(strip, strip[0].x, strip[0].y, uA, 1, a);
        pushV(strip, strip[1].x, strip[1].y, uB, 1, a);
    } else {
        // Add end cap.
        float dx = p1->x - p0->x;
        float dy = p1->y - p0->y;
        normalize(&dx, &dy);
        if (cap == CAP_BUTT)
            buttCapEnd(strip, p1, dx, dy, d, -aa * 0.5f, aa, uA, uB, a);
        else if (cap == CAP_BUTT || cap == CAP_SQUARE)
            buttCapEnd(strip, p1, dx, dy, d, d - aa, aa, uA, uB, a);
        else if (cap == CAP_ROUND)
            roundCapEnd(strip, p1, dx, dy, d, ncap, uA, uB, a);
    }

    // Cap faces fade along v (NanoVG's min(1, v) factor).
    for (auto &vtx : strip) vtx.a *= vtx.v;
}

} // namespace

// ===========================================================================
// Stroke fringe — banded adaptation of nvg__expandStroke.
//
// NanoVG's strokeMask() pyramid alpha is evaluated per-pixel in its fragment
// shader; it cannot be baked into per-vertex alpha (all strip-edge vertices
// would be 0). Instead the strip is emitted as two aligned runs — the outer
// skirt edge (alpha 0) and the core edge at the true stroke half-width
// (alpha 1) — interleaved into one strip, so linear vertex-color
// interpolation reproduces the exact AA ramp: solid core of `width`, linear
// fade over `fringe` pixels on each side.
// ===========================================================================
void ExpandStroke(const std::vector<std::vector<std::pair<float, float>>> &polylines,
                  bool closed, float width, float fringe,
                  LineCap lineCap, LineJoin lineJoin, float miterLimit, float tessTol,
                  std::vector<Vertex> &out)
{
    float halfW = width * 0.5f;
    float w = halfW + fringe;                    // outer skirt half-width
    int ncap = curveDivs(halfW, PI, tessTol);    // divisions per half circle (pre-aa, like NanoVG)

    for (auto &ring : polylines) {
        std::vector<Point> pts;
        PreparePoints(ring, pts);

        size_t n = pts.size();
        if (closed && n >= 3 && pts[0].x == pts[n - 1].x && pts[0].y == pts[n - 1].y) {
            // Drop the duplicated closing point.
            pts.pop_back();
            n = pts.size();
        }
        if (n < (closed ? 3u : 2u)) continue;

        calculateJoins(pts, w, lineJoin, miterLimit);

        if (fringe <= 0.0f) {
            // AA disabled — single opaque strip.
            std::vector<Vertex> strip;
            EmitStrokeStrip(pts, closed, halfW, 0.5f, 0.5f, 1.0f, lineCap, lineJoin, ncap, 0.0f, strip);
            StripToTriangles(strip, out);
            continue;
        }

        // Core rows sit at the true stroke half-width; u is the normalized
        // position across the full strip: 0 / 1 at the skirt edges.
        float uc = (w - halfW) / (2.0f * w);

        std::vector<Vertex> outerStrip, coreStrip;
        EmitStrokeStrip(pts, closed, w, 0.0f, 1.0f, 0.0f, lineCap, lineJoin, ncap, fringe, outerStrip);
        EmitStrokeStrip(pts, closed, halfW, uc, 1.0f - uc, 1.0f, lineCap, lineJoin, ncap, fringe, coreStrip);

        // Both runs share the same (A, B) pair structure. De-interleave
        // into four aligned row sequences and triangulate the three bands
        // explicitly — a plain strip pairing would zigzag diagonally
        // across the sides instead of following the bands.
        size_t m = outerStrip.size() / 2;
        auto pushQuad = [&out](const Vertex &a, const Vertex &b, const Vertex &c, const Vertex &d) {
            out.push_back(a); out.push_back(b); out.push_back(c);
            out.push_back(a); out.push_back(c); out.push_back(d);
        };
        for (size_t k = 0; k + 1 < m; ++k) {
            const Vertex &aOut0 = outerStrip[k * 2];
            const Vertex &aIn0  = coreStrip[k * 2];
            const Vertex &bIn0  = coreStrip[k * 2 + 1];
            const Vertex &bOut0 = outerStrip[k * 2 + 1];
            const Vertex &aOut1 = outerStrip[(k + 1) * 2];
            const Vertex &aIn1  = coreStrip[(k + 1) * 2];
            const Vertex &bIn1  = coreStrip[(k + 1) * 2 + 1];
            const Vertex &bOut1 = outerStrip[(k + 1) * 2 + 1];
            pushQuad(aOut0, aOut1, aIn1, aIn0);   // fringe band A (alpha 0 → 1)
            pushQuad(aIn0, aIn1, bIn1, bIn0);     // core band (alpha 1)
            pushQuad(bIn0, bIn1, bOut1, bOut0);   // fringe band B (alpha 1 → 0)
        }
    }
}

// ===========================================================================
// Fill fringe — one-sided outward skirt (stencil-free adaptation of
// nvg__expandFill's fringe strip)
// ===========================================================================
void ExpandFill(const std::vector<FillRing> &rings,
                float fringe, LineJoin join, float miterLimit, float tessTol,
                std::vector<Vertex> &out)
{
    if (fringe <= 0.0f) return;

    for (auto &ring : rings) {
        std::vector<Point> pts;
        PreparePoints(ring.points, pts);

        size_t n = pts.size();
        if (n >= 3 && pts[0].x == pts[n - 1].x && pts[0].y == pts[n - 1].y) {
            // Drop the duplicated closing point.
            pts.pop_back();
            n = pts.size();
        }
        if (n < 3) continue;

        // Resolve the skirt side. The fill may lie on either side of a
        // ring — inside it (outer) or outside it (hole) — so the ring's
        // winding alone is not enough; an explicit hole flag wins, and
        // without one the winding convention applies: positive shoelace
        // (CW on screen) = outer, negative = hole.
        float areaSign = polyArea(pts.data(), (int)n) >= 0.0f ? 1.0f : -1.0f;
        int hole = ring.hole;
        if (hole < 0) hole = areaSign < 0.0f ? 1 : 0;

        // The right normals (dy, -dx) point away from a positive-area
        // ring's interior and into a negative-area ring's interior.
        // Skirt toward the non-fill side:
        //   outer (fill inside)  → away from the interior
        //   hole  (fill outside) → toward the interior
        float s = areaSign * (hole ? -1.0f : 1.0f);

        // Convex turns need a join wedge; reflex turns instead partition
        // overlapping edge rectangles at their common angle bisector.
        std::vector<float> nxIn(n), nyIn(n), nxOut(n), nyOut(n), mx(n), my(n);
        std::vector<bool> reflex(n);

        for (size_t i = 0; i < n; ++i) {
            const Point &p0 = pts[(i + n - 1) % n];  // incoming edge
            const Point &p1 = pts[i];                // outgoing edge
            reflex[i] = s * (static_cast<double>(p0.dx) * p1.dy -
                             static_cast<double>(p0.dy) * p1.dx) < 0.0;
            nxIn[i]  = s *  p0.dy;
            nyIn[i]  = s * -p0.dx;
            nxOut[i] = s *  p1.dy;
            nyOut[i] = s * -p1.dx;

            // Miter (scaled like NanoVG's calculateJoins).
            float dmx = (p0.dy + p1.dy) * 0.5f;
            float dmy = (-p0.dx - p1.dx) * 0.5f;
            float dmr2 = dmx * dmx + dmy * dmy;
            if (dmr2 > 0.000001f) {
                float scale = 1.0f / dmr2;
                if (scale > 600.0f) scale = 600.0f;
                dmx *= scale;
                dmy *= scale;
            }
            mx[i] = s * dmx;
            my[i] = s * dmy;
        }

        // Edge quads: (p_i, O_i, O_{i+1}, p_{i+1}) with O_i = p_i offset
        // along edge i's outward normal. Inner ring alpha = 1, outer = 0.
        for (size_t i = 0; i < n; ++i) {
            size_t j = (i + 1) % n;
            if (pts[i].len < 1e-6f) continue;  // degenerate edge

            float oxi = pts[i].x + nxOut[i] * fringe;
            float oyi = pts[i].y + nyOut[i] * fringe;
            float oxj = pts[j].x + nxIn[j] * fringe;
            float oyj = pts[j].y + nyIn[j] * fringe;

            struct BandPoint { double x, y, u; };
            std::array<BandPoint, 8> cell{{
                {pts[i].x, pts[i].y, 0}, {oxi, oyi, 1},
                {oxj, oyj, 1}, {pts[j].x, pts[j].y, 0}}};
            size_t count = 4;
            auto clipJoin = [&](size_t corner, double sign) {
                if (!reflex[corner] || !count) return;
                const Point &before = pts[(corner + n - 1) % n];
                const Point &after = pts[corner];
                const double tx = sign * (static_cast<double>(before.dx) + after.dx);
                const double ty = sign * (static_cast<double>(before.dy) + after.dy);
                auto distance = [&](const BandPoint &p) {
                    return tx * (p.x - after.x) + ty * (p.y - after.y);
                };
                std::array<BandPoint, 8> clipped{};
                size_t used = 0;
                BandPoint previous = cell[count - 1];
                double previousDistance = distance(previous);
                for (size_t k = 0; k < count; ++k) {
                    const BandPoint current = cell[k];
                    const double d = distance(current);
                    if ((d <= 0) != (previousDistance <= 0)) {
                        const double t = previousDistance / (previousDistance - d);
                        clipped[used++] = {previous.x + t * (current.x - previous.x),
                            previous.y + t * (current.y - previous.y),
                            previous.u + t * (current.u - previous.u)};
                    }
                    if (d <= 0) clipped[used++] = current;
                    previous = current; previousDistance = d;
                }
                cell = clipped; count = used;
            };
            clipJoin(i, -1); // outgoing half-plane
            clipJoin(j, 1);  // incoming half-plane
            auto emit = [&](const BandPoint &p) {
                pushV(out, static_cast<float>(p.x), static_cast<float>(p.y),
                      static_cast<float>(p.u), 1, static_cast<float>(1 - p.u));
            };
            for (size_t k = 1; k + 1 < count; ++k) {
                const auto &a = cell[0], &b = cell[k], &c = cell[k + 1];
                if ((b.x-a.x)*(c.y-a.y) == (b.y-a.y)*(c.x-a.x)) continue;
                emit(a); emit(b); emit(c);
            }
        }

        // Corner triangles between adjacent edge quads.
        for (size_t i = 0; i < n; ++i) {
            const Point &p0 = pts[(i + n - 1) % n];
            const Point &p1 = pts[i];
            if (p0.len < 1e-6f || p1.len < 1e-6f) continue;  // degenerate corner
            if (reflex[i]) continue; // clipped bands already cover this corner

            // Collinear corners (both normals parallel) need no fill — the
            // edge quads meet flush.
            float dot = nxIn[i] * nxOut[i] + nyIn[i] * nyOut[i];
            if (dot > 0.9999995f) continue;

            float oxi = p1.x + nxIn[i] * fringe;
            float oyi = p1.y + nyIn[i] * fringe;
            float oxo = p1.x + nxOut[i] * fringe;
            float oyo = p1.y + nyOut[i] * fringe;

            // Miter valid when NanoVG would NOT bevel: dmr2 * miterLimit^2 >= 1.
            float rdmx = (p0.dy + p1.dy) * 0.5f;
            float rdmy = (-p0.dx - p1.dx) * 0.5f;
            float rdmr2 = rdmx * rdmx + rdmy * rdmy;
            bool miterOk = (rdmr2 * miterLimit * miterLimit) >= 1.0f && join == JOIN_MITER;

            if (join == JOIN_ROUND) {
                // Arc through the convex exterior wedge, taking the short way
                // between the outward normals. Reflex turns were clipped above.
                float a0 = atan2f(nyIn[i], nxIn[i]);
                float a1 = atan2f(nyOut[i], nxOut[i]);
                float da = a1 - a0;
                while (da > PI) da -= PI * 2;
                while (da < -PI) da += PI * 2;
                int ncap = curveDivs(fringe, PI, tessTol);
                int segs = maxi(2, (int)ceilf(fabsf(da) / PI * ncap));
                for (int k = 0; k < segs; ++k) {
                    float t0 = k / (float)segs, t1 = (k + 1) / (float)segs;
                    float aA = a0 + da * t0, aB = a0 + da * t1;
                    pushV(out, p1.x, p1.y, 0, 1, 1);
                    pushV(out, p1.x + cosf(aA) * fringe, p1.y + sinf(aA) * fringe, 1, 1, 0);
                    pushV(out, p1.x + cosf(aB) * fringe, p1.y + sinf(aB) * fringe, 1, 1, 0);
                }
            } else if (miterOk) {
                // Complete convex wedge, split at the miter. Both halves
                // interpolate from the true boundary, matching the edge bands.
                float mxi = p1.x + mx[i] * fringe;
                float myi = p1.y + my[i] * fringe;
                pushV(out, p1.x, p1.y, 0, 1, 1);
                pushV(out, oxi, oyi, 1, 1, 0);
                pushV(out, mxi, myi, 1, 1, 0);
                pushV(out, p1.x, p1.y, 0, 1, 1);
                pushV(out, mxi, myi, 1, 1, 0);
                pushV(out, oxo, oyo, 1, 1, 0);
            } else {
                // Bevel triangle through the path corner.
                pushV(out, oxi, oyi, 1, 1, 0);
                pushV(out, p1.x, p1.y, 0, 1, 1);
                pushV(out, oxo, oyo, 1, 1, 0);
            }
        }
    }
}

} // namespace Fringe
