#pragma once
#include "sdf_builder.h"
#include "clipper2/clipper.h"
#include <string>
#include <vector>
namespace Prototype {
Clipper2Lib::PathsD NormalizedPaths(const std::vector<Geometry2D::SDFPolygon>& input);
struct RegionResult {
    std::vector<float> vertices;
    std::vector<unsigned> indices;
    std::vector<unsigned> labels; // per triangle: 0 core, 1 stroke; NOT distances
    double normalizeMs=0, offsetMs=0, triangulateMs=0, validateMs=0;
    double coverageDifference=0, overlapArea=0;
    size_t coreRegions=0;
    int failedMaterial=-1;
    double failedOriginalCross=0, failedFloatCross=0;
};
bool Region(const std::vector<Geometry2D::SDFPolygon>& input, double width,
            RegionResult& out, std::string& error);
}
