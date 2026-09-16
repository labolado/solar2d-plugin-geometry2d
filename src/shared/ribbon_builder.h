#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Geometry2D {

enum class RibbonTimestampMode {
    Monotonic,
    LegacyCount,
};

struct RibbonPoint {
    float x = 0.0f;
    float y = 0.0f;
    double time = 0.0;
    float pathDistance = 0.0f;
};

struct RibbonBuildOptions {
    float width = 28.0f;
    float aaWidth = 2.0f;
    float minDistance = 5.0f;
    float reverseDot = -0.9f;
    float miterLimit = 2.4f;
    size_t initialPointCapacity = 16;
    size_t maxPoints = 4096;
    bool capacityTiers = true;
    bool triangles = false;
    RibbonTimestampMode timestampMode = RibbonTimestampMode::Monotonic;
};

class RibbonBuilder {
public:
    explicit RibbonBuilder(const RibbonBuildOptions &options);

    bool AddPoint(float x, float y, double time, bool &added,
                  std::string &error);
    size_t Expire(double now, double maxAge);
    void Clear();
    bool ReservePoints(size_t count, std::string &error);

    bool SetWidth(float width);
    bool SetAAWidth(float width);

    bool Build(std::string &error, bool &rebuilt);

    size_t PointCount() const { return pointCount_; }
    size_t PointCapacity() const { return points_.size(); }
    size_t MaxPoints() const { return options_.maxPoints; }
    float Width() const { return options_.width; }
    float AAWidth() const { return options_.aaWidth; }
    float MinDistance() const { return options_.minDistance; }
    float ReverseDot() const { return options_.reverseDot; }
    float MiterLimit() const { return options_.miterLimit; }
    bool CapacityTiers() const { return options_.capacityTiers; }
    bool Triangles() const { return options_.triangles; }
    RibbonTimestampMode TimestampMode() const { return options_.timestampMode; }

    size_t LogicalVertexCount() const { return logicalVertexCount_; }
    size_t LogicalIndexCount() const { return logicalIndexCount_; }
    size_t VertexCapacity() const { return vertexCapacity_; }
    size_t IndexCapacity() const { return indexCapacity_; }
    size_t LogicalTriangleCount() const { return (options_.triangles ? logicalVertexCount_ : logicalIndexCount_) / 3; }
    size_t TriangleCapacity() const { return (options_.triangles ? vertexCapacity_ : indexCapacity_) / 3; }
    float TailLength() const;
    float HeadLength() const;
    float ActiveLength() const;
    double LastTime() const { return pointCount_ == 0 ? 0.0 : PointAt(pointCount_ - 1).time; }

    uint64_t GeometryRevision() const { return geometryRevision_; }
    uint64_t BuildRevision() const { return buildRevision_; }
    uint64_t BuildCount() const { return buildCount_; }
    uint64_t NoOpBuildCount() const { return noOpBuildCount_; }
    double LastBuildMilliseconds() const { return lastBuildMilliseconds_; }
    double TotalBuildMilliseconds() const { return totalBuildMilliseconds_; }
    size_t NativeCapacityBytes() const;

    const std::vector<float> &Vertices() const { return vertices_; }
    const std::vector<float> &UVs() const { return uvs_; }
    const std::vector<uint16_t> &Indices() const { return indices_; }
    const std::vector<float> &PathDistances() const { return pathDistances_; }
    const std::vector<float> &ContourDistances() const { return contourDistances_; }

private:
    const RibbonPoint &PointAt(size_t index) const;
    RibbonPoint &PointAt(size_t index);
    bool EnsurePointCapacity(size_t count, std::string &error);
    void MarkGeometry();
    void ResetOutput();
    bool AddVertex(float x, float y, float pathDistance,
                   float contourDistance, std::string &error);
    bool BuildGeometry(std::string &error);
    bool PadToCapacity(std::string &error);

    RibbonBuildOptions options_;
    std::vector<RibbonPoint> points_;
    size_t pointHead_ = 0;
    size_t pointCount_ = 0;

    std::vector<float> directions_;

    std::vector<float> vertices_;
    std::vector<float> uvs_;
    std::vector<uint16_t> indices_;
    std::vector<float> pathDistances_;
    std::vector<float> contourDistances_;

    size_t logicalVertexCount_ = 0;
    size_t logicalIndexCount_ = 0;
    size_t vertexCapacity_ = 0;
    size_t indexCapacity_ = 0;
    size_t retainedVertexCapacity_ = 0;
    size_t retainedTriangleCapacity_ = 0;

    uint64_t geometryRevision_ = 1;
    uint64_t buildRevision_ = 0;
    uint64_t buildCount_ = 0;
    uint64_t noOpBuildCount_ = 0;
    double lastBuildMilliseconds_ = 0.0;
    double totalBuildMilliseconds_ = 0.0;
};

} // namespace Geometry2D
