#include "mesh_result.h"

#include "corona_buffer.h"

#include <algorithm>

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

static void PushGeometryBufferTable(lua_State *L, const MeshResult &mesh, bool legacyUVs)
{
    lua_createtable(L, 0, mesh.triangles ? 3 : 5);
    PushBufferDescriptor(L, mesh.vertices.data(), mesh.vertices.size() * sizeof(float),
                         mesh.VertexCount());
    lua_setfield(L, -2, "vertices");

    if (legacyUVs)
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
}

static void PushAuxiliaryBuffers(lua_State *L, const MeshResult &mesh)
{
    lua_createtable(L, 0, mesh.valueName ? 2 : 1);
    lua_pushinteger(L, static_cast<lua_Integer>(mesh.VertexCount()));
    lua_setfield(L, -2, "vertexCount");
    if (mesh.valueName)
    {
        PushBufferDescriptor(L, mesh.values.data(), mesh.values.size() * sizeof(float),
                             mesh.values.size(), 1);
        lua_setfield(L, -2, mesh.valueName);
    }
}

static int PushTableResult(lua_State *L, const MeshResult &mesh)
{
    lua_createtable(L, 0, mesh.triangles ? 3 : 4);
    PushFloatTable(L, mesh.vertices);
    lua_setfield(L, -2, "vertices");
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
    return 1;
}

static int PushDisplayMesh(lua_State *L, const MeshResult &mesh, bool legacyUVs)
{
    lua_getglobal(L, "display");
    if (!lua_istable(L, -1)) return luaL_error(L, "display API is unavailable");
    lua_getfield(L, -1, "newMesh");
    if (!lua_isfunction(L, -1)) return luaL_error(L, "display.newMesh is unavailable");
    PushGeometryBufferTable(L, mesh, legacyUVs);
    if (lua_pcall(L, 1, 1, 0) != 0)
    {
        lua_remove(L, -2); // remove display table, leave error object
        if (lua_type(L, -1) != LUA_TSTRING)
        {
            lua_pop(L, 1);
            lua_pushliteral(L, "display.newMesh failed");
        }
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    lua_remove(L, -2); // remove display table, leave mesh
    PushAuxiliaryBuffers(L, mesh);
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

} // namespace Geometry2D
