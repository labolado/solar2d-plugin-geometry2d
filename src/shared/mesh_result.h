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
    std::vector<float> uvs;
    bool sdfResult = false; // Distance output only.
    bool fillResult = false;
    bool normalizeFill = false;
    bool earcutBackend = false;
    float fringeWidth = 0;
    SDFOptions sdfOptions;
    SDFStats sdfStats;
    std::array<double, 4> uvBounds{};
    const char *valueName = nullptr;
    bool triangles = false;

    size_t VertexCount() const { return vertices.size() / 2; }
};

// Returns the number of Lua values pushed: one for table/buffers, two for mesh
// (`display mesh`, then packed auxiliary attributes).
int PushMeshResult(lua_State *L, const MeshResult &mesh, OutputMode output,
                   bool legacyUVs = false);

// Pushes the packed descriptor consumed by mesh.path:update(). The descriptor
// owns its buffers, so the engine may synchronously copy them without relying
// on MeshResult storage after this call returns.
void PushMeshUpdateDescriptor(lua_State *L, const MeshResult &mesh,
                              bool legacyUVs = false);

} // namespace Geometry2D
