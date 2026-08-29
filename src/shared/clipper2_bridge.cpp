// Clipper2 is Copyright Angus Johnson and distributed under the Boost
// Software License 1.0. This file contains plugin-specific conversion and
// path-fill policy; third_party/clipper2 remains unmodified.

#include "clipper2_bridge.h"

#include "clipper2/clipper.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>

namespace Geometry2D {
namespace {

using namespace Clipper2Lib;

struct Segment {
    Point64 a;
    Point64 b;
    int64_t minX;
    int64_t maxX;
    int64_t minY;
    int64_t maxY;
    size_t contour;
    size_t edge;
    size_t edgeCount;
};

static PathsD ToClipperPaths(const std::vector<PathContour> &contours)
{
    PathsD paths;
    paths.reserve(contours.size());
    for (const PathContour &contour : contours)
    {
        PathD path;
        path.reserve(contour.points.size());
        for (const auto &point : contour.points)
            path.emplace_back(static_cast<double>(point.first),
                              static_cast<double>(point.second));
        StripDuplicates(path, true);
        paths.push_back(std::move(path));
    }
    return paths;
}

static bool OnSegment(const Point64 &a, const Point64 &b, const Point64 &point)
{
    return CrossProductSign(a, b, point) == 0 &&
        point.x >= (std::min)(a.x, b.x) && point.x <= (std::max)(a.x, b.x) &&
        point.y >= (std::min)(a.y, b.y) && point.y <= (std::max)(a.y, b.y);
}

static bool SegmentsTouchOrCross(const Segment &lhs, const Segment &rhs)
{
    int a = CrossProductSign(lhs.a, lhs.b, rhs.a);
    int b = CrossProductSign(lhs.a, lhs.b, rhs.b);
    int c = CrossProductSign(rhs.a, rhs.b, lhs.a);
    int d = CrossProductSign(rhs.a, rhs.b, lhs.b);
    if (a == 0 && OnSegment(lhs.a, lhs.b, rhs.a)) return true;
    if (b == 0 && OnSegment(lhs.a, lhs.b, rhs.b)) return true;
    if (c == 0 && OnSegment(rhs.a, rhs.b, lhs.a)) return true;
    if (d == 0 && OnSegment(rhs.a, rhs.b, lhs.b)) return true;
    return a != 0 && b != 0 && c != 0 && d != 0 && a != b && c != d;
}

static bool AreAdjacent(const Segment &lhs, const Segment &rhs)
{
    if (lhs.contour != rhs.contour) return false;
    size_t delta = lhs.edge > rhs.edge ? lhs.edge - rhs.edge : rhs.edge - lhs.edge;
    return delta == 1 || delta + 1 == lhs.edgeCount;
}

static bool HasIntersections(const Paths64 &paths)
{
    std::vector<Segment> segments;
    size_t segmentCount = 0;
    for (const Path64 &path : paths) segmentCount += path.size();
    segments.reserve(segmentCount);
    for (size_t contour = 0; contour < paths.size(); ++contour)
    {
        const Path64 &path = paths[contour];
        for (size_t edge = 0; edge < path.size(); ++edge)
        {
            const Point64 &a = path[edge];
            const Point64 &b = path[(edge + 1) % path.size()];
            if (a == b) continue;
            segments.push_back({a, b, (std::min)(a.x, b.x), (std::max)(a.x, b.x),
                (std::min)(a.y, b.y), (std::max)(a.y, b.y),
                contour, edge, path.size()});
        }
    }
    std::sort(segments.begin(), segments.end(), [](const Segment &a, const Segment &b) {
        if (a.minX != b.minX) return a.minX < b.minX;
        if (a.minY != b.minY) return a.minY < b.minY;
        return a.maxX < b.maxX;
    });
    for (size_t i = 0; i < segments.size(); ++i)
    {
        const Segment &lhs = segments[i];
        for (size_t j = i + 1; j < segments.size(); ++j)
        {
            const Segment &rhs = segments[j];
            if (rhs.minX > lhs.maxX) break;
            if (rhs.minY > lhs.maxY || rhs.maxY < lhs.minY || AreAdjacent(lhs, rhs))
                continue;
            if (SegmentsTouchOrCross(lhs, rhs)) return true;
        }
    }
    return false;
}

static bool NormalizeWithClipper(const PathsD &input, const MeshOptions &options,
                                 std::vector<PathContour> &contours,
                                 std::string &error)
{
    try
    {
        ClipperD clipper(options.clipperPrecision);
        clipper.PreserveCollinear(true);
        clipper.AddSubject(input);
        PathsD solution;
        FillRule rule = options.fillRule == PathFillRule::EvenOdd ?
            FillRule::EvenOdd : FillRule::NonZero;
        if (!clipper.Execute(ClipType::Union, rule, solution) || clipper.ErrorCode() != 0)
        {
            error = "Clipper2 could not resolve the path fill";
            return false;
        }
        if (solution.empty())
        {
            error = "resolved path fill is empty";
            return false;
        }
        contours.clear();
        contours.reserve(solution.size());
        for (PathD &path : solution)
        {
            StripDuplicates(path, true);
            if (path.size() < 3) continue;
            PathContour contour;
            contour.closed = true;
            contour.points.reserve(path.size());
            for (const PointD &point : path)
            {
                if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
                    std::fabs(point.x) > std::numeric_limits<float>::max() ||
                    std::fabs(point.y) > std::numeric_limits<float>::max())
                {
                    error = "Clipper2 produced a coordinate outside the finite-float range";
                    return false;
                }
                contour.points.emplace_back(static_cast<float>(point.x),
                                            static_cast<float>(point.y));
            }
            contours.push_back(std::move(contour));
        }
        if (contours.empty())
        {
            error = "resolved path fill contains no usable contour";
            return false;
        }
        return true;
    }
    catch (const std::exception &exception)
    {
        error = std::string("Clipper2 path resolution failed: ") + exception.what();
        return false;
    }
}

} // namespace

bool PreparePathFillGroups(const std::vector<PathContour> &contours,
                           const MeshOptions &options,
                           std::vector<Polygon> &groups,
                           std::string &error)
{
    PathsD input = ToClipperPaths(contours);
    for (const PathD &path : input)
    {
        if (path.size() < 3)
        {
            error = "fill contour has fewer than 3 distinct points";
            return false;
        }
    }

    bool mustNormalize = options.intersections == PathIntersectionMode::Resolve ||
        options.fillRule == PathFillRule::EvenOdd;
    if (options.intersections == PathIntersectionMode::Error)
    {
        try
        {
            int scaleError = 0;
            const double scale = std::pow(10.0, options.clipperPrecision);
            Paths64 scaled = ScalePaths<int64_t, double>(input, scale, scaleError);
            if (scaleError != 0)
            {
                error = "path coordinates exceed the selected Clipper2 precision range";
                return false;
            }
            if (HasIntersections(scaled))
            {
                error = "path fill contours intersect; use intersections='resolve' to normalize them";
                return false;
            }
        }
        catch (const std::exception &exception)
        {
            error = std::string("Clipper2 intersection validation failed: ") + exception.what();
            return false;
        }
        if (!mustNormalize) return GroupPathFillContours(contours, groups, error);
    }

    std::vector<PathContour> normalized;
    if (!NormalizeWithClipper(input, options, normalized, error)) return false;
    return GroupPathFillContours(normalized, groups, error);
}

} // namespace Geometry2D
