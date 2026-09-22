#include "geometry2d_lua.h"

#include "mesh_builder.h"
#include "mesh_result.h"

#include <cmath>
#include <limits>
#include <string>

namespace Geometry2D {

static constexpr uint32_t kFringeFillOptions = OptionFringe | OptionJoin |
    OptionMiterLimit | OptionTessTol;
static constexpr uint32_t kFringeStrokeOptions = kFringeFillOptions | OptionCap | OptionClosed;
static constexpr uint32_t kFillMeshOptions = OptionAA | OptionTopology | OptionLimits | OptionJoin | OptionMiterLimit | OptionTessTol | OptionRefine |
    OptionMode | OptionOutput | OptionLegacyUVs;
static constexpr uint32_t kSDFMeshOptions = OptionSDF | OptionMode | OptionOutput;

static bool IsTaggedRing(lua_State *L, int arg)
{
    if (!lua_istable(L, arg)) return false;
    arg = CoronaLuaNormalize(L, arg);
    lua_getfield(L, arg, "points");
    bool tagged = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (!tagged)
    {
        lua_getfield(L, arg, "bytes");
        tagged = !lua_isnil(L, -1);
        lua_pop(L, 1);
    }
    return tagged;
}

static bool ReadFringeRings(lua_State *L, int arg, std::vector<Fringe::FillRing> &rings)
{
    if (!lua_istable(L, arg) || IsFlatPolygonTable(L, arg))
    {
        TPPLPoly poly;
        if (!ReadPolygon(L, arg, poly)) return false;
        Fringe::FillRing ring;
        if (lua_istable(L, arg))
            ring.hole = IsTaggedRing(L, arg) ? (poly.IsHole() ? 1 : 0) : -1;
        int pointCount = static_cast<int>(poly.GetNumPoints());
        for (int i = 0; i < pointCount; ++i)
            ring.points.push_back({static_cast<float>(poly[i].x), static_cast<float>(poly[i].y)});
        rings.push_back(std::move(ring));
        return true;
    }

    arg = CoronaLuaNormalize(L, arg);
    size_t count = lua_objlen(L, arg);
    for (size_t i = 1; i <= count; ++i)
    {
        lua_rawgeti(L, arg, static_cast<int>(i));
        TPPLPoly poly;
        bool ok = ReadPolygon(L, -1, poly);
        if (!ok) { lua_pop(L, 1); return false; }
        Fringe::FillRing ring;
        ring.hole = IsTaggedRing(L, -1) ? (poly.IsHole() ? 1 : 0) : -1;
        int pointCount = static_cast<int>(poly.GetNumPoints());
        for (int p = 0; p < pointCount; ++p)
            ring.points.push_back({static_cast<float>(poly[p].x), static_cast<float>(poly[p].y)});
        rings.push_back(std::move(ring));
        lua_pop(L, 1);
    }
    return !rings.empty();
}

static MeshResult FringeResult(const std::vector<Fringe::Vertex> &vertices)
{
    MeshResult result;
    result.valueName = "alphas";
    result.vertices.reserve(vertices.size() * 2);
    result.values.reserve(vertices.size());
    result.indices.reserve(vertices.size());
    for (size_t i = 0; i < vertices.size(); ++i)
    {
        result.vertices.push_back(vertices[i].x);
        result.vertices.push_back(vertices[i].y);
        result.values.push_back(vertices[i].a);
        result.indices.push_back(static_cast<uint16_t>(i));
    }
    return result;
}

static int FringeFill(lua_State *L)
{
    std::vector<Fringe::FillRing> rings;
    if (!ReadFringeRings(L, 1, rings)) return luaL_argerror(L, 1, "Expected polygon or polygon list");
    MeshOptions options = GetMeshOptions(L, 2, kFringeFillOptions, "fringe.fill");
    std::vector<Fringe::Vertex> triangles;
    if (!Fringe::ExpandFill(rings, options.fringe, options.join, options.miterLimit,
                       options.tessTol, triangles))
        return PushGeometryFailure(L, "fringe.fill subdivision precision exceeded; increase tessTol");
    if (triangles.empty())
        return PushGeometryFailure(L, "fringe.fill produced no geometry");
    if (triangles.size() > 65535)
        return PushGeometryFailure(L, "fringe.fill exceeds the 65535 vertex limit");
    return PushMeshResult(L, FringeResult(triangles), OutputMode::Table);
}

static int FringeStroke(lua_State *L)
{
    std::vector<Fringe::FillRing> fillRings;
    if (!ReadFringeRings(L, 1, fillRings)) return luaL_argerror(L, 1, "Expected polyline or list of polylines");
    if (lua_type(L, 2) != LUA_TNUMBER) return luaL_argerror(L, 2, "Stroke width must be a number");
    lua_Number widthNumber = lua_tonumber(L, 2);
    if (!std::isfinite(static_cast<double>(widthNumber)) || widthNumber <= 0.0 ||
        std::fabs(static_cast<double>(widthNumber)) > std::numeric_limits<float>::max())
        return luaL_argerror(L, 2, "Stroke width must be a positive finite float");
    float width = static_cast<float>(widthNumber);
    MeshOptions options = GetMeshOptions(L, 3, kFringeStrokeOptions, "fringe.stroke");
    std::vector<std::vector<std::pair<float, float>>> rings(fillRings.size());
    for (size_t i = 0; i < fillRings.size(); ++i) rings[i] = std::move(fillRings[i].points);
    std::vector<Fringe::Vertex> triangles;
    if (!Fringe::ExpandStroke(rings, options.closed, width, options.fringe, options.cap,
                         options.join, options.miterLimit, options.tessTol, triangles, 65535))
        return PushGeometryFailure(L, "fringe.stroke subdivision precision or vertex limit exceeded");
    if (triangles.empty())
        return PushGeometryFailure(L, "fringe.stroke produced no geometry");
    if (triangles.size() > 65535)
        return PushGeometryFailure(L, "fringe.stroke exceeds the 65535 vertex limit");
    return PushMeshResult(L, FringeResult(triangles), OutputMode::Table);
}

static bool ReadGroups(lua_State *L, int arg, bool groupsInput,
                       std::vector<Polygon> &groups, std::string &error)
{
    if (!lua_istable(L, arg)) luaL_argerror(L, arg, "Expected polygon or polygon groups table");
    if (!groupsInput)
    {
        Polygon polygon;
        if (!ReadEarcutPolygon(L, arg, polygon)) { error = "Expected polygon with at least 3 vertices"; return false; }
        groups.push_back(std::move(polygon));
        return true;
    }
    if (!lua_istable(L, arg)) { error = "Expected array of polygon groups"; return false; }
    arg = CoronaLuaNormalize(L, arg);
    size_t count = ValidateDenseArray(L, arg, "polygon groups");
    for (size_t i = 1; i <= count; ++i)
    {
        lua_rawgeti(L, arg, static_cast<int>(i));
        if (!lua_istable(L, -1)) luaL_error(L, "Each polygon group must be a table");
        Polygon polygon;
        bool ok = ReadEarcutPolygon(L, -1, polygon);
        lua_pop(L, 1);
        if (!ok) { error = "invalid polygon group #" + std::to_string(i); return false; }
        groups.push_back(std::move(polygon));
    }
    return !groups.empty();
}

static int UtilMesh(lua_State *L, bool groupsInput, bool sdf)
{
    const char *context = sdf ? (groupsInput ? "meshDistanceGroups" : "meshDistance") :
                                (groupsInput ? "meshFillGroups" : "meshFill");
    MeshOptions options = GetMeshOptions(L, 2, sdf ? kSDFMeshOptions : kFillMeshOptions,
                                         context);
    std::vector<Polygon> groups;
    std::string error;
    if (!ReadGroups(L, 1, groupsInput, groups, error))
    {
        return PushGeometryFailure(L, error.empty() ? "No polygon groups" : error.c_str());
    }
    MeshResult result;
    if (!BuildFillMesh(groups, options, sdf, result, error))
        return PushGeometryFailure(L, error.c_str());
    return PushMeshResult(L, result, options.output, options.legacyUVs);
}

static int UtilMeshFill(lua_State *L) { return UtilMesh(L, false, false); }
static int UtilMeshFillGroups(lua_State *L) { return UtilMesh(L, true, false); }
static int UtilMeshDistance(lua_State *L) { return UtilMesh(L, false, true); }
static int UtilMeshDistanceGroups(lua_State *L) { return UtilMesh(L, true, true); }

void RegisterFringe(lua_State *L)
{
    luaL_Reg functions[] = {
        {"fill", FringeFill}, {"stroke", FringeStroke}, {nullptr, nullptr}
    };
    luaL_register(L, nullptr, functions);
}

void RegisterUtil(lua_State *L)
{
    luaL_Reg functions[] = {
        {"meshFill", UtilMeshFill},
        {"meshFillGroups", UtilMeshFillGroups},
        {"meshDistance", UtilMeshDistance},
        {"meshDistanceGroups", UtilMeshDistanceGroups},
        {nullptr, nullptr}
    };
    luaL_register(L, nullptr, functions);
}

} // namespace Geometry2D
