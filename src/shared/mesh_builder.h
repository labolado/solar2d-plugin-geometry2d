#pragma once

#include "geometry2d_lua.h"
#include "mesh_result.h"

#include <utility>
#include <string>
#include <vector>

namespace Geometry2D {

bool BuildFillMesh(const std::vector<Polygon> &groups, const MeshOptions &options,
                   bool sdf, MeshResult &result, std::string &error);

bool BuildStrokeMesh(
    const std::vector<std::pair<std::vector<std::pair<float, float>>, bool>> &contours,
    float width, const MeshOptions &options, MeshResult &result, std::string &error);

} // namespace Geometry2D
