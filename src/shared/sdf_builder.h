#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Geometry2D {

using SDFPoint = std::array<double, 2>;
using SDFRing = std::vector<SDFPoint>;
using SDFPolygon = std::vector<SDFRing>;

enum class SDFGeometry { Fill, FillAA, InnerStroke };

inline const char* SDFGeometryName(SDFGeometry geometry) {
    return geometry == SDFGeometry::Fill ? "fill" :
        geometry == SDFGeometry::FillAA ? "fillAA" : "innerStroke";
}

struct SDFOptions {
    SDFGeometry geometry = SDFGeometry::InnerStroke;
    double innerRange = 8;
    double outerRange = 2;
    double distanceTolerance = 0.1;
    // x' = a*x+c*y+tx, y' = b*x+d*y+ty. Distances use this space;
    // output positions are inverse-mapped to the original local space.
    std::array<double, 6> transform{{1, 0, 0, 1, 0, 0}};
    size_t maxWork = 2000000;
    size_t maxVertices = 1000000;
};

struct SDFStats {
    double prepareMs = 0, partitionMs = 0, triangulateMs = 0, totalMs = 0;
    size_t inputEdges = 0, cells = 0, work = 0, uniqueVertices = 0;
    size_t triangles = 0, outputBytes = 0;
};

struct SDFMesh {
    std::vector<float> vertices, distances, uvs;
    std::vector<uint32_t> indices;
    std::array<double, 4> bounds{}; // original local contour bounds, not AA bounds
    SDFStats stats;
};

// A continuous piecewise-affine approximation to signed Euclidean distance.
// Negative inside, zero on the input contour, positive outside. Deep fill is
// saturated at -innerRange. FillAA has zero interior / positive exterior
// distances; Fill returns no distances. No borrowed memory or input mutation.
bool BuildDistanceMesh(const std::vector<SDFPolygon>& groups, const SDFOptions& options,
                       SDFMesh& mesh, std::string& error);

} // namespace Geometry2D
