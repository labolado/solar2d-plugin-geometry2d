#include "mesh_builder.h"

#include "mapbox/earcut.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Geometry2D {

static bool AppendFillGroup(const Polygon &poly, const MeshOptions &options, float bandWidth,
                            std::vector<Point> &coords, std::vector<uint32_t> &indices,
                            std::vector<Fringe::Vertex> &skirt)
{
    if (poly.empty() || poly[0].size() < 3) return false;

    std::vector<Point> groupCoords;
    FlattenPolygon(poly, groupCoords);
    auto groupIndices = mapbox::earcut<uint32_t>(poly);
    if (options.refine) mapbox::refine<uint32_t>(groupIndices, groupCoords);
    if (groupIndices.empty()) return false;

    uint32_t base = static_cast<uint32_t>(coords.size());
    coords.insert(coords.end(), groupCoords.begin(), groupCoords.end());
    for (uint32_t index : groupIndices) indices.push_back(index + base);

    std::vector<Fringe::FillRing> rings;
    rings.reserve(poly.size());
    for (const auto &source : poly)
    {
        Fringe::FillRing ring;
        // Polygon groups already identify outer/hole roles. Do not infer
        // them again from winding: earcut accepts either orientation.
        ring.hole = rings.empty() ? 0 : 1;
        ring.points.reserve(source.size());
        for (const auto &point : source)
            ring.points.push_back({static_cast<float>(point[0]), static_cast<float>(point[1])});
        rings.push_back(std::move(ring));
    }
    Fringe::ExpandFill(rings, bandWidth, options.join, options.miterLimit,
                       options.tessTol, skirt);
    return true;
}

static void AppendVertex(MeshResult &result, float x, float y, float value)
{
    result.vertices.push_back(x);
    result.vertices.push_back(y);
    result.values.push_back(value);
}

using StrokePoint = std::pair<float, float>;
using StrokePolyline = std::vector<StrokePoint>;
using StrokeContour = std::pair<StrokePolyline, bool>;

static bool SameStrokePoint(const StrokePoint &a, const StrokePoint &b)
{
    double dx = static_cast<double>(a.first) - b.first;
    double dy = static_cast<double>(a.second) - b.second;
    return dx * dx + dy * dy <= 1.0e-12;
}

static void AppendStrokePoint(StrokePolyline &points, const StrokePoint &point)
{
    if (points.empty() || !SameStrokePoint(points.back(), point)) points.push_back(point);
}

struct DashCursor {
    size_t index = 0;
    double remaining = 0.0;
    bool draw = true;
};

// The dash phase and closed-seam behavior follow ThorVG's MIT-licensed
// StrokeDashPath implementation. This version operates on the flattened
// polylines already produced by plugin.geometry2d instead of ThorVG RenderPath.
static bool PrepareDashPattern(const MeshOptions &options,
                               std::vector<double> &pattern,
                               DashCursor &initial, std::string &error)
{
    pattern.reserve(options.dashPattern.size() * (options.dashPattern.size() % 2 ? 2 : 1));
    for (float value : options.dashPattern) pattern.push_back(value);
    if (pattern.size() % 2 != 0)
        for (float value : options.dashPattern) pattern.push_back(value);

    double period = 0.0;
    for (double value : pattern) period += value;
    if (!std::isfinite(period) || period <= 0.0)
    {
        error = "dashPattern has an invalid total length";
        return false;
    }

    double offset = std::fmod(static_cast<double>(options.dashOffset), period);
    if (offset < 0.0) offset += period;
    initial.index = 0;
    initial.remaining = pattern[0];
    initial.draw = true;
    while (offset >= initial.remaining)
    {
        offset -= initial.remaining;
        initial.index = (initial.index + 1) % pattern.size();
        initial.remaining = pattern[initial.index];
        initial.draw = !initial.draw;
    }
    initial.remaining -= offset;
    return true;
}

static void AdvanceDash(const std::vector<double> &pattern, DashCursor &cursor)
{
    cursor.index = (cursor.index + 1) % pattern.size();
    cursor.remaining = pattern[cursor.index];
    cursor.draw = !cursor.draw;
}

static void FinishDashRun(StrokePolyline &current, std::vector<StrokePolyline> &runs)
{
    if (current.size() >= 2) runs.push_back(std::move(current));
    current.clear();
}

static bool SplitDashedContour(const StrokeContour &source,
                               const std::vector<double> &pattern,
                               const DashCursor &initial,
                               size_t maxDashSegments, size_t &dashSegments,
                               std::vector<StrokeContour> &output,
                               std::string &error)
{
    const StrokePolyline &points = source.first;
    bool closed = source.second;
    if (points.size() < (closed ? 3u : 2u)) return true;

    DashCursor cursor = initial;
    std::vector<StrokePolyline> runs;
    StrokePolyline current;
    size_t edgeCount = points.size() - 1 + (closed ? 1 : 0);

    for (size_t edge = 0; edge < edgeCount; ++edge)
    {
        const StrokePoint &from = points[edge % points.size()];
        const StrokePoint &to = points[(edge + 1) % points.size()];
        double dx = static_cast<double>(to.first) - from.first;
        double dy = static_cast<double>(to.second) - from.second;
        double length = std::hypot(dx, dy);
        if (length <= 1.0e-12) continue;

        double consumed = 0.0;
        while (consumed < length)
        {
            double available = length - consumed;
            bool patternBoundary = cursor.remaining <= available;
            double take = patternBoundary ? cursor.remaining : available;
            if (!(take > 0.0) || !std::isfinite(take))
            {
                error = "dashPattern could not advance along the path";
                return false;
            }

            double startT = consumed / length;
            double endT = std::min(1.0, (consumed + take) / length);
            StrokePoint start(
                static_cast<float>(from.first + dx * startT),
                static_cast<float>(from.second + dy * startT));
            StrokePoint end(
                static_cast<float>(from.first + dx * endT),
                static_cast<float>(from.second + dy * endT));

            if (cursor.draw)
            {
                if (current.empty())
                {
                    if (dashSegments >= maxDashSegments)
                    {
                        error = "dashed stroke exceeds maxDashSegments";
                        return false;
                    }
                    ++dashSegments;
                    AppendStrokePoint(current, start);
                }
                AppendStrokePoint(current, end);
            }

            consumed += take;
            if (length - consumed <= std::numeric_limits<double>::epsilon() *
                                               std::max(1.0, length))
                consumed = length;

            if (patternBoundary)
            {
                if (cursor.draw) FinishDashRun(current, runs);
                AdvanceDash(pattern, cursor);
            }
            else cursor.remaining -= take;
        }
    }
    FinishDashRun(current, runs);
    if (runs.empty()) return true;

    const StrokePoint &seam = points.front();
    if (closed && runs.size() == 1 &&
        SameStrokePoint(runs[0].front(), seam) && SameStrokePoint(runs[0].back(), seam))
    {
        output.push_back({std::move(runs[0]), true});
        return true;
    }

    if (closed && runs.size() >= 2 &&
        SameStrokePoint(runs.front().front(), seam) &&
        SameStrokePoint(runs.back().back(), seam))
    {
        StrokePolyline merged = std::move(runs.back());
        for (size_t i = 1; i < runs.front().size(); ++i)
            AppendStrokePoint(merged, runs.front()[i]);
        output.push_back({std::move(merged), false});
        for (size_t i = 1; i + 1 < runs.size(); ++i)
            output.push_back({std::move(runs[i]), false});
        return true;
    }

    for (auto &run : runs) output.push_back({std::move(run), false});
    return true;
}

bool BuildFillMesh(const std::vector<Polygon> &groups, const MeshOptions &options,
                   bool sdf, MeshResult &result, std::string &error)
{
    std::vector<Point> coords;
    std::vector<uint32_t> bodyIndices;
    std::vector<Fringe::Vertex> skirt;
    float bandWidth = sdf ? options.distance : options.fringe;
    for (size_t i = 0; i < groups.size(); ++i)
    {
        if (!AppendFillGroup(groups[i], options, bandWidth, coords, bodyIndices, skirt))
        {
            error = "invalid or non-triangulable polygon group #" + std::to_string(i + 1);
            return false;
        }
    }
    if (groups.empty()) { error = "no polygon groups"; return false; }

    result.triangles = options.triangles;
    result.valueName = sdf ? "distances" : "alphas";
    float sign = options.outsidePositive ? 1.0f : -1.0f;

    if (options.triangles)
    {
        result.vertices.reserve((bodyIndices.size() + skirt.size()) * 2);
        result.values.reserve(bodyIndices.size() + skirt.size());
        for (uint32_t index : bodyIndices)
        {
            const Point &p = coords[index];
            AppendVertex(result, static_cast<float>(p[0]), static_cast<float>(p[1]), sdf ? 0.0f : 1.0f);
        }
        for (const auto &v : skirt)
            AppendVertex(result, v.x, v.y, sdf ? sign * options.distance * v.u : v.a);
    }
    else
    {
        size_t total = coords.size() + skirt.size();
        if (total > 65535)
        {
            error = "indexed mesh exceeds 65535 vertices; use mode='triangles'";
            return false;
        }
        result.vertices.reserve(total * 2);
        result.values.reserve(total);
        for (const auto &p : coords)
            AppendVertex(result, static_cast<float>(p[0]), static_cast<float>(p[1]), sdf ? 0.0f : 1.0f);
        for (const auto &v : skirt)
            AppendVertex(result, v.x, v.y, sdf ? sign * options.distance * v.u : v.a);

        result.indices.reserve(bodyIndices.size() + skirt.size());
        for (uint32_t index : bodyIndices)
        {
            if (index > std::numeric_limits<uint16_t>::max())
            {
                error = "indexed mesh contains an out-of-range index";
                return false;
            }
            result.indices.push_back(static_cast<uint16_t>(index));
        }
        for (size_t i = 0; i < skirt.size(); ++i)
            result.indices.push_back(static_cast<uint16_t>(coords.size() + i));
    }
    return true;
}

bool BuildStrokeMesh(
    const std::vector<std::pair<std::vector<std::pair<float, float>>, bool>> &contours,
    float width, const MeshOptions &options, MeshResult &result, std::string &error)
{
    if (width <= 0.0f) { error = "stroke width must be positive"; return false; }
    std::vector<StrokeContour> dashedContours;
    const std::vector<StrokeContour> *strokeContours = &contours;
    if (!options.dashPattern.empty())
    {
        std::vector<double> pattern;
        DashCursor initial;
        if (!PrepareDashPattern(options, pattern, initial, error)) return false;
        size_t dashSegments = 0;
        for (const auto &contour : contours)
            if (!SplitDashedContour(contour, pattern, initial, options.maxDashSegments,
                                    dashSegments, dashedContours, error))
                return false;
        strokeContours = &dashedContours;
    }

    std::vector<Fringe::Vertex> triangles;
    for (const auto &contour : *strokeContours)
    {
        if (contour.first.size() < 2) continue;
        std::vector<std::vector<std::pair<float, float>>> one(1, contour.first);
        Fringe::ExpandStroke(one, contour.second, width, options.fringe, options.cap,
                             options.join, options.miterLimit, options.tessTol, triangles);
    }
    if (triangles.empty()) { error = "path contains no drawable stroke contour"; return false; }
    if (!options.triangles && triangles.size() > 65535)
    {
        error = "indexed mesh exceeds 65535 vertices; use mode='triangles'";
        return false;
    }

    result.triangles = options.triangles;
    result.valueName = "alphas";
    result.vertices.reserve(triangles.size() * 2);
    result.values.reserve(triangles.size());
    result.indices.reserve(options.triangles ? 0 : triangles.size());
    for (size_t i = 0; i < triangles.size(); ++i)
    {
        AppendVertex(result, triangles[i].x, triangles[i].y, triangles[i].a);
        if (!options.triangles) result.indices.push_back(static_cast<uint16_t>(i));
    }
    return true;
}

} // namespace Geometry2D
