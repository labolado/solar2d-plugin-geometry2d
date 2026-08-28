#pragma once

#include "geometry2d_lua.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Geometry2D {

struct MeshResult {
    std::vector<float> vertices;
    std::vector<uint16_t> indices;
    std::vector<float> values;
    const char *valueName = nullptr;
    bool triangles = false;

    size_t VertexCount() const { return vertices.size() / 2; }
};

// Returns the number of Lua values pushed: one for table/buffers, two for mesh
// (`display mesh`, then packed auxiliary attributes).
int PushMeshResult(lua_State *L, const MeshResult &mesh, OutputMode output,
                   bool legacyUVs = false);

} // namespace Geometry2D
