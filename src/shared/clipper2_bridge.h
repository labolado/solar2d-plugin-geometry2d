#pragma once

#include "bezier_path.h"

#include <string>
#include <vector>

namespace Geometry2D {

// Validates path-fill contours and, when requested, normalizes intersecting
// contours with Clipper2 before they are passed to earcut/fringe generation.
bool PreparePathFillGroups(const std::vector<PathContour> &contours,
                           const MeshOptions &options,
                           std::vector<Polygon> &groups,
                           std::string &error);

} // namespace Geometry2D
