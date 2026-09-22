#include "mesh_result.h"

#include "corona_buffer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace Geometry2D {

static void PushFloatTable(lua_State *L, const std::vector<float> &values)
{
    lua_createtable(L, static_cast<int>(values.size()), 0);
    for (size_t i = 0; i < values.size(); ++i)
    {
        lua_pushnumber(L, values[i]);
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
}

static void PushIndexTable(lua_State *L, const std::vector<uint16_t> &values)
{
    lua_createtable(L, static_cast<int>(values.size()), 0);
    for (size_t i = 0; i < values.size(); ++i)
    {
        lua_pushinteger(L, static_cast<lua_Integer>(values[i]) + 1);
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
}

static bool HasAlphaValues(const MeshResult &mesh)
{
    return mesh.valueName && std::strcmp(mesh.valueName, "alphas") == 0;
}

static void PushFillVertexColors(lua_State *L, const MeshResult &mesh)
{
    // Solar2D's packed ColorUnion layout is RGBA bytes on supported targets.
    // White RGB lets mesh:setFillColor() tint the mesh while preserving the
    // plugin's per-vertex AA alpha.
    std::vector<uint8_t> colors(mesh.values.size() * 4);
    for (size_t i = 0; i < mesh.values.size(); ++i)
    {
        float alpha = std::isfinite(mesh.values[i]) ? mesh.values[i] : 0.0f;
        alpha = std::max(0.0f, std::min(1.0f, alpha));
        colors[i * 4] = 255;
        colors[i * 4 + 1] = 255;
        colors[i * 4 + 2] = 255;
        colors[i * 4 + 3] = static_cast<uint8_t>(alpha * 255.0f + 0.5f);
    }
    PushBufferDescriptor(L, colors.data(), colors.size(), mesh.VertexCount());
}

static void PushGeometryBufferTable(lua_State *L, const MeshResult &mesh, bool legacyUVs,
                                    int attributesIndex = 0)
{
    if (attributesIndex) attributesIndex = CoronaLuaNormalize(L, attributesIndex);
    lua_createtable(L, 0, mesh.triangles ? 4 : 6);
    PushBufferDescriptor(L, mesh.vertices.data(), mesh.vertices.size() * sizeof(float),
                         mesh.VertexCount());
    lua_setfield(L, -2, "vertices");

    if (!mesh.uvs.empty())
    {
        PushBufferDescriptor(L, mesh.uvs.data(), mesh.uvs.size() * sizeof(float), mesh.VertexCount());
        lua_setfield(L, -2, "uvs");
    }
    else if (legacyUVs)
    {
        std::vector<float> uvs(mesh.vertices.size(), 0.0f);
        float minX = mesh.vertices[0], maxX = mesh.vertices[0];
        float minY = mesh.vertices[1], maxY = mesh.vertices[1];
        for (size_t i = 2; i < mesh.vertices.size(); i += 2)
        {
            minX = std::min(minX, mesh.vertices[i]);
            maxX = std::max(maxX, mesh.vertices[i]);
            minY = std::min(minY, mesh.vertices[i + 1]);
            maxY = std::max(maxY, mesh.vertices[i + 1]);
        }
        float invWidth = maxX == minX ? 0.0f : 1.0f / (maxX - minX);
        float invHeight = maxY == minY ? 0.0f : 1.0f / (maxY - minY);
        for (size_t i = 0; i < mesh.vertices.size(); i += 2)
        {
            uvs[i] = (mesh.vertices[i] - minX) * invWidth;
            uvs[i + 1] = (mesh.vertices[i + 1] - minY) * invHeight;
        }
        PushBufferDescriptor(L, uvs.data(), uvs.size() * sizeof(float), mesh.VertexCount());
        lua_setfield(L, -2, "uvs");
    }

    if (!mesh.triangles)
    {
        PushBufferDescriptor(L, mesh.indices.data(), mesh.indices.size() * sizeof(uint16_t),
                             mesh.indices.size());
        lua_setfield(L, -2, "indices");
        lua_pushboolean(L, 1);
        lua_setfield(L, -2, "zeroBasedIndices");
    }
    lua_pushstring(L, mesh.triangles ? "triangles" : "indexed");
    lua_setfield(L, -2, "mode");

    if (HasAlphaValues(mesh))
    {
        if (attributesIndex) lua_getfield(L, attributesIndex, "fillVertexColors");
        else PushFillVertexColors(L, mesh);
        lua_setfield(L, -2, "fillVertexColors");
    }
}

static void PushSDFMetadata(lua_State *L, const MeshResult &mesh)
{
    if (!mesh.sdfResult && !mesh.fillResult) return;
    auto number = [&](const char *name, double value) {
        lua_pushnumber(L, value); lua_setfield(L, -2, name);
    };
    lua_pushstring(L,mesh.sdfResult?"distance":"fill");lua_setfield(L,-2,"kind");
    if(mesh.sdfResult) {
        lua_pushstring(L,mesh.earcutBackend?"local":"partition");lua_setfield(L,-2,"method");
        lua_pushboolean(L,mesh.earcutBackend);lua_setfield(L,-2,"approximate");
        number("innerRange", mesh.sdfOptions.geometry==SDFGeometry::InnerStroke ? mesh.sdfOptions.innerRange : 0);
        number("outerRange", mesh.sdfOptions.outerRange);
    } else {
        lua_pushstring(L,mesh.fringeWidth>0?"vertex":"none");lua_setfield(L,-2,"aa");
        if(mesh.fringeWidth>0)number("aaWidth",mesh.fringeWidth);
        lua_pushstring(L,mesh.normalizeFill?"normalize":"direct");lua_setfield(L,-2,"topology");
    }
    lua_createtable(L, 4, 0);
    for (int i=0;i<4;++i) { lua_pushnumber(L,mesh.uvBounds[i]); lua_rawseti(L,-2,i+1); }
    lua_setfield(L, -2, "uvBounds");
    lua_createtable(L, 0, 11);
    if (mesh.sdfResult && !mesh.earcutBackend) {
        number("prepareMs", mesh.sdfStats.prepareMs);
        number("partitionMs", mesh.sdfStats.partitionMs);
        number("triangulateMs", mesh.sdfStats.triangulateMs);
        number("inputEdges", mesh.sdfStats.inputEdges);
        number("cells", mesh.sdfStats.cells);
        number("work", mesh.sdfStats.work);
        number("uniqueVertices", mesh.sdfStats.uniqueVertices);
    }
    if (mesh.sdfResult && mesh.earcutBackend) {
        number("inputEdges", mesh.sdfStats.inputEdges);
        number("work", mesh.sdfStats.work);
        number("uniqueVertices", mesh.sdfStats.uniqueVertices);
    }
    number("totalMs", mesh.sdfStats.totalMs);
    number("outputVertices", mesh.VertexCount());
    number("indices", mesh.indices.size());
    number("triangles", mesh.sdfStats.triangles);
    number("outputBytes", mesh.sdfStats.outputBytes);
    lua_setfield(L, -2, "stats");
}

static void PushAuxiliaryBuffers(lua_State *L, const MeshResult &mesh)
{
    lua_createtable(L, 0, mesh.valueName ? (HasAlphaValues(mesh) ? 3 : 2) : 1);
    lua_pushinteger(L, static_cast<lua_Integer>(mesh.VertexCount()));
    lua_setfield(L, -2, "vertexCount");
    if (mesh.valueName)
    {
        PushBufferDescriptor(L, mesh.values.data(), mesh.values.size() * sizeof(float),
                             mesh.values.size(), 1);
        lua_setfield(L, -2, mesh.valueName);
    }
    if (HasAlphaValues(mesh))
    {
        PushFillVertexColors(L, mesh);
        lua_setfield(L, -2, "fillVertexColors");
    }
    PushSDFMetadata(L, mesh);
}

static int PushTableResult(lua_State *L, const MeshResult &mesh)
{
    lua_createtable(L, 0, mesh.triangles ? 3 : 4);
    PushFloatTable(L, mesh.vertices);
    lua_setfield(L, -2, "vertices");
    if (!mesh.uvs.empty())
    {
        PushFloatTable(L, mesh.uvs);
        lua_setfield(L, -2, "uvs");
    }
    if (mesh.valueName)
    {
        PushFloatTable(L, mesh.values);
        lua_setfield(L, -2, mesh.valueName);
    }
    if (!mesh.triangles)
    {
        PushIndexTable(L, mesh.indices);
        lua_setfield(L, -2, "indices");
    }
    lua_pushstring(L, mesh.triangles ? "triangles" : "indexed");
    lua_setfield(L, -2, "mode");
    PushSDFMetadata(L, mesh);
    return 1;
}

static int PushBufferResult(lua_State *L, const MeshResult &mesh, bool legacyUVs)
{
    PushGeometryBufferTable(L, mesh, legacyUVs);
    if (mesh.valueName)
    {
        PushBufferDescriptor(L, mesh.values.data(), mesh.values.size() * sizeof(float),
                             mesh.values.size(), 1);
        lua_setfield(L, -2, mesh.valueName);
    }
    PushSDFMetadata(L, mesh);
    return 1;
}

static int PushDisplayMesh(lua_State *L, const MeshResult &mesh, bool legacyUVs)
{
    PushAuxiliaryBuffers(L, mesh);
    int attributesIndex = lua_gettop(L);

    lua_getglobal(L, "display");
    if (!lua_istable(L, -1))
    {
        lua_settop(L, attributesIndex - 1);
        return PushGeometryFailure(L, "display API is unavailable");
    }
    lua_getfield(L, -1, "newMesh");
    if (!lua_isfunction(L, -1))
    {
        lua_settop(L, attributesIndex - 1);
        return PushGeometryFailure(L, "display.newMesh is unavailable");
    }
    PushGeometryBufferTable(L, mesh, legacyUVs, attributesIndex);
    if (lua_pcall(L, 1, 1, 0) != 0)
    {
        lua_remove(L, -2); // remove display table, leave error object
        lua_remove(L, attributesIndex);
        if (lua_type(L, -1) != LUA_TSTRING)
        {
            lua_pop(L, 1);
            lua_pushliteral(L, "display.newMesh failed");
        }
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    if (lua_isnil(L, -1))
    {
        lua_settop(L, attributesIndex - 1);
        return PushGeometryFailure(L, "display.newMesh returned nil");
    }
    lua_remove(L, -2); // remove display table, leave mesh
    lua_insert(L, attributesIndex); // return mesh before attributes
    return 2;
}

int PushMeshResult(lua_State *L, const MeshResult &mesh, OutputMode output,
                   bool legacyUVs)
{
    if (mesh.vertices.size() < 6 || mesh.vertices.size() % 2 != 0)
        return PushGeometryFailure(L, "Mesh result has fewer than 3 vertices");
    if (mesh.valueName && mesh.values.size() != mesh.VertexCount())
        return luaL_error(L, "Mesh auxiliary value count does not match vertex count");
    if (!mesh.triangles && mesh.VertexCount() > 65535)
        return PushGeometryFailure(L, "Indexed mesh exceeds the 65535 vertex limit; use mode='triangles'");
    if (mesh.triangles && mesh.VertexCount() % 3 != 0)
        return luaL_error(L, "Triangle-list mesh vertex count is not divisible by 3");

    switch (output)
    {
        case OutputMode::Buffers: return PushBufferResult(L, mesh, legacyUVs);
        case OutputMode::Mesh: return PushDisplayMesh(L, mesh, legacyUVs);
        default: return PushTableResult(L, mesh);
    }
}

void PushMeshUpdateDescriptor(lua_State *L, const MeshResult &mesh,
                              bool legacyUVs)
{
    PushGeometryBufferTable(L, mesh, legacyUVs);
}

} // namespace Geometry2D
