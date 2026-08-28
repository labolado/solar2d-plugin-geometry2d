#include "geometry2d_lua.h"

#include "mapbox/earcut.hpp"

#include <cstring>

namespace Geometry2D {

static void PushTriangles(lua_State *L, const std::vector<uint32_t> &indices,
                          const std::vector<Point> &coords)
{
    int count = static_cast<int>(indices.size() / 3);
    lua_createtable(L, count, 0);
    for (int t = 0; t < count; ++t)
    {
        lua_createtable(L, 6, 0);
        for (int v = 0; v < 3; ++v)
        {
            const Point &point = coords[indices[t * 3 + v]];
            lua_pushnumber(L, point[0]); lua_rawseti(L, -2, v * 2 + 1);
            lua_pushnumber(L, point[1]); lua_rawseti(L, -2, v * 2 + 2);
        }
        lua_rawseti(L, -2, t + 1);
    }
}

static void PushMesh(lua_State *L, const std::vector<uint32_t> &indices,
                     const std::vector<Point> &coords)
{
    lua_createtable(L, 0, 3);
    lua_createtable(L, static_cast<int>(coords.size() * 2), 0);
    for (size_t i = 0; i < coords.size(); ++i)
    {
        lua_pushnumber(L, coords[i][0]); lua_rawseti(L, -2, static_cast<int>(i * 2 + 1));
        lua_pushnumber(L, coords[i][1]); lua_rawseti(L, -2, static_cast<int>(i * 2 + 2));
    }
    lua_setfield(L, -2, "vertices");
    lua_createtable(L, static_cast<int>(indices.size()), 0);
    for (size_t i = 0; i < indices.size(); ++i)
    {
        lua_pushinteger(L, static_cast<lua_Integer>(indices[i]) + 1);
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    lua_setfield(L, -2, "indices");
    lua_pushliteral(L, "indexed");
    lua_setfield(L, -2, "mode");
}

static int Triangulate(lua_State *L)
{
    Polygon polygon;
    if (!ReadEarcutPolygon(L, 1, polygon) || polygon.empty() || polygon[0].size() < 3)
        return luaL_argerror(L, 1, "Expected polygon with at least 3 vertices");
    std::vector<Point> coords;
    FlattenPolygon(polygon, coords);
    auto indices = mapbox::earcut<uint32_t>(polygon);

    bool refine = false;
    bool indexed = false;
    if (!lua_isnoneornil(L, 2))
    {
        if (!lua_istable(L, 2)) return luaL_argerror(L, 2, "Expected an options table");
        int options = 2;
        lua_pushnil(L);
        while (lua_next(L, options) != 0)
        {
            if (lua_type(L, -2) != LUA_TSTRING) luaL_error(L, "earcut option keys must be strings");
            const char *name = lua_tostring(L, -2);
            if (std::strcmp(name, "refine") != 0 && std::strcmp(name, "result") != 0)
            {
                if (std::strcmp(name, "mesh") == 0)
                    luaL_error(L, "earcut option 'mesh' was replaced by result='indexed'");
                luaL_error(L, "Unknown earcut option '%s'", name);
            }
            lua_pop(L, 1);
        }

        lua_getfield(L, 2, "refine");
        if (!lua_isnil(L, -1) && !lua_isboolean(L, -1))
            luaL_error(L, "earcut option 'refine' must be a boolean");
        refine = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);

        lua_getfield(L, 2, "result");
        if (!lua_isnil(L, -1))
        {
            if (lua_type(L, -1) != LUA_TSTRING) luaL_error(L, "earcut option 'result' must be a string");
            const char *result = lua_tostring(L, -1);
            if (std::strcmp(result, "triangles") == 0) indexed = false;
            else if (std::strcmp(result, "indexed") == 0) indexed = true;
            else luaL_error(L, "Invalid earcut result '%s'; expected 'triangles' or 'indexed'", result);
        }
        lua_pop(L, 1);
    }
    if (refine) mapbox::refine<uint32_t>(indices, coords);
    if (indices.empty())
        return PushGeometryFailure(L, "earcut failed to triangulate polygon");
    if (indexed && coords.size() > 65535)
        return PushGeometryFailure(L, "Indexed earcut result exceeds the 65535 vertex limit");
    if (indexed) PushMesh(L, indices, coords);
    else PushTriangles(L, indices, coords);
    return 1;
}

void RegisterEarcut(lua_State *L)
{
    luaL_Reg functions[] = {{"triangulate", Triangulate}, {nullptr, nullptr}};
    luaL_register(L, nullptr, functions);
}

} // namespace Geometry2D
