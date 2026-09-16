#include "ribbon_builder.h"

// Business trail_renderer.lua supplies the sampling/reversal semantics. Joins
// now partition neighboring segments at a shared bisector, clip the inner side,
// and bevel excessive outer miters. Core and exterior AA use disjoint convex
// cells; non-adjacent crossings and reversal-separated runs are not unioned.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>

namespace Geometry2D {
namespace {

static constexpr float kEpsilon = 1.0e-8f;
static constexpr uint16_t kInvalidVertex = (std::numeric_limits<uint16_t>::max)();

static size_t NextPowerOfTwo(size_t value, size_t minimum)
{
    size_t result = minimum;
    while (result < value && result <= (std::numeric_limits<size_t>::max)() / 2)
        result *= 2;
    return result < value ? value : result;
}

// A segment cell is convex. Fixed scratch storage covers four rectangle edges,
// two join partitions and up to six silhouette planes without heap allocation.
struct CellPoint { double x, y; };
struct Plane {
    double x, y, offset;
    double Distance(const CellPoint &p) const { return x * p.x + y * p.y - offset; }
};
struct Cell {
    std::array<CellPoint, 24> points;
    size_t count = 0;
};

static bool ClipCell(const Cell &input, const Plane &plane, Cell &output)
{
    output.count = 0;
    if (!input.count) return true;
    auto append = [&](const CellPoint &p) {
        if (output.count && output.points[output.count - 1].x == p.x &&
            output.points[output.count - 1].y == p.y) return true;
        if (output.count == output.points.size()) return false;
        output.points[output.count++] = p;
        return true;
    };
    CellPoint previous = input.points[input.count - 1];
    double previousDistance = plane.Distance(previous);
    for (size_t i = 0; i < input.count; ++i)
    {
        const CellPoint current = input.points[i];
        double distance = plane.Distance(current);
        if ((previousDistance <= 0.0) != (distance <= 0.0))
        {
            double t = previousDistance / (previousDistance - distance);
            if (!append({previous.x + t * (current.x - previous.x),
                         previous.y + t * (current.y - previous.y)})) return false;
        }
        if (distance <= 0.0 && !append(current)) return false;
        previous = current;
        previousDistance = distance;
    }
    if (output.count > 1 && output.points[0].x == output.points[output.count - 1].x &&
        output.points[0].y == output.points[output.count - 1].y) --output.count;
    return true;
}

} // namespace

RibbonBuilder::RibbonBuilder(const RibbonBuildOptions &options)
: options_(options)
{
    points_.resize(options_.initialPointCapacity);
}

const RibbonPoint &RibbonBuilder::PointAt(size_t index) const
{
    return points_[(pointHead_ + index) % points_.size()];
}

RibbonPoint &RibbonBuilder::PointAt(size_t index)
{
    return points_[(pointHead_ + index) % points_.size()];
}

void RibbonBuilder::MarkGeometry()
{
    ++geometryRevision_;
    if (geometryRevision_ == 0) ++geometryRevision_;
}

bool RibbonBuilder::EnsurePointCapacity(size_t count, std::string &error)
{
    if (count <= points_.size()) return true;
    if (count > options_.maxPoints)
    {
        error = "ribbon point count exceeds maxPoints";
        return false;
    }
    size_t capacity = NextPowerOfTwo(count, options_.initialPointCapacity);
    capacity = (std::min)(capacity, options_.maxPoints);
    std::vector<RibbonPoint> replacement(capacity);
    for (size_t i = 0; i < pointCount_; ++i) replacement[i] = PointAt(i);
    points_.swap(replacement);
    pointHead_ = 0;
    return true;
}

bool RibbonBuilder::ReservePoints(size_t count, std::string &error)
{
    if (count < pointCount_) count = pointCount_;
    return EnsurePointCapacity(count, error);
}

bool RibbonBuilder::AddPoint(float x, float y, double time, bool &added,
                             std::string &error)
{
    added = false;
    if (pointCount_ > 0)
    {
        const RibbonPoint &last = PointAt(pointCount_ - 1);
        if (options_.timestampMode == RibbonTimestampMode::Monotonic && time < last.time)
        {
            error = "ribbon point timestamps must be non-decreasing";
            return false;
        }
        const double dx = static_cast<double>(x) - last.x;
        const double dy = static_cast<double>(y) - last.y;
        const double distance = std::hypot(dx, dy);
        if (distance <= options_.minDistance)
            return true;
        const double nextPathDistance =
            static_cast<double>(last.pathDistance) + distance;
        if (!std::isfinite(nextPathDistance) ||
            nextPathDistance > std::numeric_limits<float>::max())
        {
            error = "ribbon cumulative path length exceeds the finite-float range";
            return false;
        }
        if (!EnsurePointCapacity(pointCount_ + 1, error)) return false;
        RibbonPoint &point = PointAt(pointCount_);
        point.x = x;
        point.y = y;
        point.time = time;
        point.pathDistance = static_cast<float>(nextPathDistance);
    }
    else
    {
        if (!EnsurePointCapacity(1, error)) return false;
        RibbonPoint &point = PointAt(0);
        point.x = x;
        point.y = y;
        point.time = time;
        point.pathDistance = 0.0f;
    }
    ++pointCount_;
    added = true;
    MarkGeometry();
    return true;
}

size_t RibbonBuilder::Expire(double now, double maxAge)
{
    if (options_.timestampMode == RibbonTimestampMode::LegacyCount)
    {
        // Match TrailRenderer:trailDisappearing(): count expired points across
        // the entire queue, then pop that many points from the front. Do not
        // remove individual expired points or re-test while popping: a future
        // point may be popped and an expired point may survive this call.
        size_t removed = 0;
        for (size_t i = 0; i < pointCount_; ++i)
            if (now - PointAt(i).time >= maxAge) ++removed;
        if (removed > 0)
        {
            pointHead_ = (pointHead_ + removed) % points_.size();
            pointCount_ -= removed;
            if (pointCount_ == 0) pointHead_ = 0;
            MarkGeometry();
        }
        return removed;
    }
    const double cutoff = now - maxAge;
    size_t removed = 0;
    while (pointCount_ > 0 && PointAt(0).time <= cutoff)
    {
        pointHead_ = (pointHead_ + 1) % points_.size();
        --pointCount_;
        ++removed;
    }
    if (removed > 0)
    {
        if (pointCount_ == 0) pointHead_ = 0;
        MarkGeometry();
    }
    return removed;
}

void RibbonBuilder::Clear()
{
    if (pointCount_ == 0) return;
    pointHead_ = 0;
    pointCount_ = 0;
    MarkGeometry();
}

bool RibbonBuilder::SetWidth(float width)
{
    if (options_.width == width) return false;
    options_.width = width;
    MarkGeometry();
    return true;
}

bool RibbonBuilder::SetAAWidth(float width)
{
    if (options_.aaWidth == width) return false;
    options_.aaWidth = width;
    MarkGeometry();
    return true;
}

float RibbonBuilder::TailLength() const
{
    return pointCount_ == 0 ? 0.0f : PointAt(0).pathDistance;
}

float RibbonBuilder::HeadLength() const
{
    return pointCount_ == 0 ? 0.0f : PointAt(pointCount_ - 1).pathDistance;
}

float RibbonBuilder::ActiveLength() const
{
    return HeadLength() - TailLength();
}

void RibbonBuilder::ResetOutput()
{
    vertices_.clear();
    uvs_.clear();
    indices_.clear();
    pathDistances_.clear();
    contourDistances_.clear();
    directions_.clear();
    logicalVertexCount_ = 0;
    logicalIndexCount_ = 0;
    vertexCapacity_ = 0;
    indexCapacity_ = 0;
}

bool RibbonBuilder::AddVertex(float x, float y, float pathDistance,
                               float contourDistance, std::string &error)
{
    if (!std::isfinite(x) || !std::isfinite(y) ||
        !std::isfinite(pathDistance) || !std::isfinite(contourDistance))
    {
        error = "ribbon geometry exceeds the finite-float range";
        return false;
    }
    const size_t maximum = options_.triangles ? 3 * 1048576 : kInvalidVertex;
    if (vertices_.size() / 2 >= maximum)
    {
        error = options_.triangles ? "ribbon exceeds the triangle-list vertex limit (3145728)" :
            "ribbon exceeds the 65535 indexed-vertex limit; use mode='triangles'";
        return false;
    }
    vertices_.push_back(x);
    vertices_.push_back(y);
    uvs_.push_back(pathDistance);
    uvs_.push_back(contourDistance);
    pathDistances_.push_back(pathDistance);
    contourDistances_.push_back(contourDistance);
    return true;
}

bool RibbonBuilder::BuildGeometry(std::string &error)
{
    if (pointCount_ < 2) return true;
    directions_.resize((pointCount_ - 1) * 2);
    for (size_t i = 0; i + 1 < pointCount_; ++i)
    {
        const RibbonPoint &a = PointAt(i), &b = PointAt(i + 1);
        const double dx = static_cast<double>(b.x) - a.x;
        const double dy = static_cast<double>(b.y) - a.y;
        const double length = std::hypot(dx, dy);
        if (!(length > kEpsilon))
        {
            error = "ribbon contains coincident adjacent points";
            return false;
        }
        directions_[i * 2] = static_cast<float>(dx / length);
        directions_[i * 2 + 1] = static_cast<float>(dy / length);
    }

    const double half = options_.width * 0.5;
    const double aa = options_.aaWidth;
    const double radius = half + aa;
    for (size_t segment = 0; segment + 1 < pointCount_; ++segment)
    {
        const RibbonPoint &a = PointAt(segment), &b = PointAt(segment + 1);
        const double dx = directions_[segment * 2], dy = directions_[segment * 2 + 1];
        const double nx = -dy, ny = dx;
        const double length = std::hypot(static_cast<double>(b.x) - a.x,
                                        static_cast<double>(b.y) - a.y);
        const CellPoint end{static_cast<double>(b.x) - a.x, static_cast<double>(b.y) - a.y};
        std::array<Plane, 6> silhouette;
        size_t planeCount = 0;
        silhouette[planeCount++] = {nx, ny, half};
        silhouette[planeCount++] = {-nx, -ny, half};
        // Start/end travel planes also define a continuous path coordinate.
        Plane startPlane{-dx, -dy, 0.0}, endPlane{dx, dy, dx * end.x + dy * end.y};
        bool splitStart = false, splitEnd = false;
        double startCos = 1.0, endCos = 1.0;
        double startExtent = radius, endExtent = radius;

        auto join = [&](size_t previous, size_t next, const CellPoint &center, bool atStart) {
            double px = directions_[previous * 2], py = directions_[previous * 2 + 1];
            double qx = directions_[next * 2], qy = directions_[next * 2 + 1];
            double dot = (std::max)(-1.0, (std::min)(1.0, px * qx + py * qy));
            double tx = px + qx, ty = py + qy;
            double sumLength = std::hypot(tx, ty);
            if (dot < options_.reverseDot || sumLength <= kEpsilon) return;
            tx /= sumLength; ty /= sumLength;
            double cosHalf = std::sqrt((1.0 + dot) * 0.5);
            if (cosHalf <= kEpsilon) return;
            double offset = tx * center.x + ty * center.y;
            if (atStart)
            {
                splitStart = true;
                startPlane = {-tx, -ty, -offset};
                startCos = tx * dx + ty * dy;
                startExtent = radius * (1.0 + 1.0 / cosHalf);
            }
            else
            {
                splitEnd = true;
                endPlane = {tx, ty, offset};
                endCos = tx * dx + ty * dy;
                endExtent = radius * (1.0 + 1.0 / cosHalf);
            }
            if (1.0 / cosHalf > options_.miterLimit)
            {
                // Bevel through the two outer offset endpoints. Both neighbors
                // use this same silhouette plane and the same join partition.
                double sign = px * qy - py * qx > 0.0 ? -1.0 : 1.0;
                double ox = -ty * sign, oy = tx * sign;
                silhouette[planeCount++] = {ox, oy, ox * center.x + oy * center.y + half * cosHalf};
            }
        };
        if (segment > 0) join(segment - 1, segment, {0.0, 0.0}, true);
        if (segment + 2 < pointCount_) join(segment, segment + 1, end, false);
        if (!splitStart) silhouette[planeCount++] = startPlane;
        if (!splitEnd) silhouette[planeCount++] = endPlane;

        Cell remaining, scratch;
        remaining.count = 4;
        remaining.points[0] = {-dx * startExtent + nx * radius, -dy * startExtent + ny * radius};
        remaining.points[1] = {dx * (length + endExtent) + nx * radius, dy * (length + endExtent) + ny * radius};
        remaining.points[2] = {dx * (length + endExtent) - nx * radius, dy * (length + endExtent) - ny * radius};
        remaining.points[3] = {-dx * startExtent - nx * radius, -dy * startExtent - ny * radius};
        auto clip = [&](const Plane &plane) {
            if (!ClipCell(remaining, plane, scratch))
            { error = "ribbon clipping workspace limit exceeded"; return false; }
            remaining = scratch;
            return true;
        };
        if (splitStart && !clip(startPlane)) return false;
        if (splitEnd && !clip(endPlane)) return false;
        for (size_t i = 0; i < planeCount; ++i)
        {
            Plane outer = silhouette[i];
            outer.offset += aa;
            if (!clip(outer)) return false;
        }

        auto emit = [&](const Cell &cell, bool core) {
            if (cell.count < 3) return true;
            double area = 0.0;
            for (size_t i = 1; i + 1 < cell.count; ++i)
                area += (cell.points[i].x - cell.points[0].x) * (cell.points[i + 1].y - cell.points[0].y) -
                        (cell.points[i].y - cell.points[0].y) * (cell.points[i + 1].x - cell.points[0].x);
            if (area == 0.0) return true;
            auto vertex = [&](size_t i) {
                const CellPoint &p = cell.points[i];
                double fromStart = (std::max)(0.0, -startPlane.Distance(p) / startCos);
                double toEnd = (std::max)(0.0, -endPlane.Distance(p) / endCos);
                double fraction = fromStart + toEnd > 0.0 ? fromStart / (fromStart + toEnd) : 0.0;
                double distance = 0.0;
                if (!core)
                    for (size_t j = 0; j < planeCount; ++j)
                        distance = (std::max)(distance, silhouette[j].Distance(p));
                return AddVertex(static_cast<float>(a.x + p.x), static_cast<float>(a.y + p.y),
                    static_cast<float>(a.pathDistance + fraction * (b.pathDistance - a.pathDistance)),
                    static_cast<float>(distance), error);
            };
            if (options_.triangles)
            {
                for (size_t i = 1; i + 1 < cell.count; ++i)
                    if (!vertex(0) || !vertex(i) || !vertex(i + 1)) return false;
            }
            else
            {
                size_t base = vertices_.size() / 2;
                for (size_t i = 0; i < cell.count; ++i)
                    if (!vertex(i)) return false;
                for (size_t i = 1; i + 1 < cell.count; ++i)
                {
                    indices_.push_back(static_cast<uint16_t>(base));
                    indices_.push_back(static_cast<uint16_t>(base + i));
                    indices_.push_back(static_cast<uint16_t>(base + i + 1));
                }
            }
            return true;
        };

        // max(0, D0, ..., Dn) is piecewise affine, not affine over an
        // arbitrary exterior slice. Partition by the dominant distance plane
        // before triangulating: otherwise cap distances at slice corners can
        // incorrectly flatten an entire side's AA ramp to the outer distance.
        // These cells have disjoint interiors and retain the shared join cuts.
        const Cell outerCell = remaining;
        for (size_t i = 0; i < planeCount; ++i)
        {
            if (aa > 0.0)
            {
                const Plane &p = silhouette[i];
                Cell fringe;
                if (!ClipCell(outerCell, {-p.x, -p.y, -p.offset}, fringe))
                { error = "ribbon clipping workspace limit exceeded"; return false; }
                for (size_t j = 0; j < planeCount && fringe.count; ++j)
                {
                    if (j == i) continue;
                    const Plane &q = silhouette[j];
                    if (p.x == q.x && p.y == q.y && p.offset == q.offset)
                    {
                        // Identical planes own the same area; assign it once.
                        if (j < i) fringe.count = 0;
                        continue;
                    }
                    // Keep Di >= Dj.
                    if (!ClipCell(fringe, {q.x - p.x, q.y - p.y, q.offset - p.offset}, scratch))
                    { error = "ribbon clipping workspace limit exceeded"; return false; }
                    fringe = scratch;
                }
                if (!emit(fringe, false)) return false;
            }
            if (!clip(silhouette[i])) return false;
        }
        if (!emit(remaining, true)) return false;
    }
    return true;
}

bool RibbonBuilder::PadToCapacity(std::string &error)
{
    logicalVertexCount_ = vertices_.size() / 2;
    logicalIndexCount_ = indices_.size();
    if (logicalVertexCount_ == 0)
    {
        vertexCapacity_ = 0;
        indexCapacity_ = 0;
        return true;
    }

    if (options_.capacityTiers)
    {
        if (options_.triangles)
        {
            retainedTriangleCapacity_ = (std::max)(retainedTriangleCapacity_,
                NextPowerOfTwo(logicalVertexCount_ / 3, 8));
            vertexCapacity_ = retainedTriangleCapacity_ * 3;
            indexCapacity_ = 0;
        }
        else
        {
            size_t requiredVertices = NextPowerOfTwo(logicalVertexCount_, 16);
            if (requiredVertices > kInvalidVertex) requiredVertices = kInvalidVertex;
            if (requiredVertices < logicalVertexCount_)
            {
                error = "ribbon vertex capacity exceeds the indexed-mesh limit; use mode='triangles'";
                return false;
            }
            retainedVertexCapacity_ = (std::max)(retainedVertexCapacity_, requiredVertices);
            size_t logicalTriangles = (logicalIndexCount_ + 2) / 3;
            retainedTriangleCapacity_ = (std::max)(retainedTriangleCapacity_,
                NextPowerOfTwo(logicalTriangles, 8));
            vertexCapacity_ = retainedVertexCapacity_;
            indexCapacity_ = retainedTriangleCapacity_ * 3;
        }
    }
    else
    {
        vertexCapacity_ = logicalVertexCount_;
        indexCapacity_ = logicalIndexCount_;
    }

    float padX = vertices_[0];
    float padY = vertices_[1];
    float padPath = pathDistances_[0];
    vertices_.resize(vertexCapacity_ * 2);
    uvs_.resize(vertexCapacity_ * 2);
    pathDistances_.resize(vertexCapacity_);
    contourDistances_.resize(vertexCapacity_);
    for (size_t i = logicalVertexCount_; i < vertexCapacity_; ++i)
    {
        vertices_[i * 2] = padX;
        vertices_[i * 2 + 1] = padY;
        uvs_[i * 2] = padPath;
        uvs_[i * 2 + 1] = options_.aaWidth;
        pathDistances_[i] = padPath;
        contourDistances_[i] = options_.aaWidth;
    }
    indices_.resize(indexCapacity_, 0);
    return true;
}

bool RibbonBuilder::Build(std::string &error, bool &rebuilt)
{
    if (buildRevision_ == geometryRevision_)
    {
        rebuilt = false;
        ++noOpBuildCount_;
        return true;
    }
    const auto start = std::chrono::steady_clock::now();
    ResetOutput();
    bool ok = BuildGeometry(error) && PadToCapacity(error);
    const auto end = std::chrono::steady_clock::now();
    lastBuildMilliseconds_ =
        std::chrono::duration<double, std::milli>(end - start).count();
    totalBuildMilliseconds_ += lastBuildMilliseconds_;
    ++buildCount_;
    rebuilt = true;
    if (ok) buildRevision_ = geometryRevision_;
    return ok;
}

size_t RibbonBuilder::NativeCapacityBytes() const
{
    return points_.capacity() * sizeof(RibbonPoint) +
        directions_.capacity() * sizeof(float) +
        vertices_.capacity() * sizeof(float) +
        uvs_.capacity() * sizeof(float) +
        indices_.capacity() * sizeof(uint16_t) +
        pathDistances_.capacity() * sizeof(float) +
        contourDistances_.capacity() * sizeof(float);
}

} // namespace Geometry2D
