#pragma once
#include "sdf_builder.h"

namespace Geometry2D {
// Local, bounded-miter distance bands. No global distance envelope/boolean
// normalization. Unsupported offset topology returns failure, never fallback.
bool BuildLocalStrokeMesh(const std::vector<SDFPolygon>& groups,
                          const SDFOptions& options, double miterLimit,
                          SDFMesh& mesh, std::string& error);
}
