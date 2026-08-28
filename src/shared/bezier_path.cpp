// Adaptive cubic subdivision in this file is derived from NanoVG (MIT).
// The rest of the path parsing, contour grouping, and Solar2D bindings are
// specific to plugin.geometry2d.

#include "geometry2d_lua.h"

#include "mesh_builder.h"
#include "mesh_result.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>
#include <string>

namespace Geometry2D {

namespace {

static constexpr uint32_t kPathFlattenOptions = OptionTessTol | OptionMaxCurvePoints;
static constexpr uint32_t kPathFillOptions = OptionFringe | OptionJoin | OptionMiterLimit |
    OptionTessTol | OptionRefine | OptionMode | OptionOutput | OptionMaxCurvePoints |
    OptionLegacyUVs;
static constexpr uint32_t kPathSDFOptions = OptionDistance | OptionDistanceSign |
    OptionJoin | OptionMiterLimit | OptionTessTol | OptionRefine | OptionMode |
    OptionOutput | OptionMaxCurvePoints | OptionLegacyUVs;
static constexpr uint32_t kPathStrokeOptions = OptionFringe | OptionCap | OptionJoin |
    OptionMiterLimit | OptionTessTol | OptionClosed | OptionMode | OptionOutput |
    OptionMaxCurvePoints | OptionLegacyUVs | OptionDashPattern | OptionDashOffset |
    OptionMaxDashSegments;

struct Contour {
    std::vector<std::pair<float, float>> points;
    bool closed = false;
};

static float NumberAt(lua_State *L, int command, int index)
{
    lua_rawgeti(L, command, index);
    if (lua_type(L, -1) != LUA_TNUMBER) luaL_error(L, "Path coordinates must be numbers");
    lua_Number number = lua_tonumber(L, -1);
    if (!std::isfinite(static_cast<double>(number)) ||
        std::fabs(static_cast<double>(number)) > std::numeric_limits<float>::max())
        luaL_error(L, "Path coordinates must be finite floats");
    float result = static_cast<float>(number);
    lua_pop(L, 1);
    return result;
}

static std::string CommandName(lua_State *L, int command)
{
    lua_rawgeti(L, command, 1);
    if (lua_type(L, -1) != LUA_TSTRING) luaL_error(L, "Path command name must be a string");
    const char *value = lua_tostring(L, -1);
    std::string result(value ? value : "");
    lua_pop(L, 1);
    return result;
}

static void CheckCommandLength(lua_State *L, int command, size_t expected,
                               const char *name, size_t commandIndex)
{
    size_t actual = lua_objlen(L, command);
    if (actual != expected)
        luaL_error(L, "Path command #%d '%s' expects %d values, got %d",
                   static_cast<int>(commandIndex), name, static_cast<int>(expected),
                   static_cast<int>(actual));
}

static bool AddPoint(std::vector<std::pair<float, float>> &points, float x, float y,
                     size_t maxPoints)
{
    if (!points.empty())
    {
        float dx = points.back().first - x;
        float dy = points.back().second - y;
        if (dx * dx + dy * dy < 1.0e-12f) return true;
    }
    if (points.size() >= maxPoints) return false;
    points.push_back({x, y});
    return true;
}

// Adaptive cubic subdivision follows NanoVG's nvg__tesselateBezier().
static bool FlattenCubic(lua_State *L, std::vector<std::pair<float, float>> &points,
                         float x1, float y1, float x2, float y2,
                         float x3, float y3, float x4, float y4,
                         float tolerance, int level, size_t maxPoints)
{
    if (level >= 10) return AddPoint(points, x4, y4, maxPoints);
    float x12 = (x1 + x2) * 0.5f, y12 = (y1 + y2) * 0.5f;
    float x23 = (x2 + x3) * 0.5f, y23 = (y2 + y3) * 0.5f;
    float x34 = (x3 + x4) * 0.5f, y34 = (y3 + y4) * 0.5f;
    float x123 = (x12 + x23) * 0.5f, y123 = (y12 + y23) * 0.5f;
    float x234 = (x23 + x34) * 0.5f, y234 = (y23 + y34) * 0.5f;
    float x1234 = (x123 + x234) * 0.5f, y1234 = (y123 + y234) * 0.5f;
    float dx = x4 - x1, dy = y4 - y1;
    float d2 = std::fabs((x2 - x4) * dy - (y2 - y4) * dx);
    float d3 = std::fabs((x3 - x4) * dy - (y3 - y4) * dx);
    if ((d2 + d3) * (d2 + d3) < tolerance * (dx * dx + dy * dy))
    {
        return AddPoint(points, x4, y4, maxPoints);
    }
    if (!FlattenCubic(L, points, x1, y1, x12, y12, x123, y123, x1234, y1234,
                      tolerance, level + 1, maxPoints)) return false;
    return FlattenCubic(L, points, x1234, y1234, x234, y234, x34, y34, x4, y4,
                        tolerance, level + 1, maxPoints);
}

static bool ReadPath(lua_State *L, int arg, const MeshOptions &options,
                     std::vector<Contour> &contours, std::string &geometryError)
{
    if (!lua_istable(L, arg)) return false;
    arg = CoronaLuaNormalize(L, arg);
    lua_getfield(L, arg, "commands");
    int commands = lua_istable(L, -1) ? CoronaLuaNormalize(L, -1) : arg;

    size_t maxPoints = options.maxCurvePoints;
    Contour current;
    bool hasCurrent = false;
    float cx = 0.0f, cy = 0.0f, sx = 0.0f, sy = 0.0f;
    size_t count = lua_objlen(L, commands);
    for (size_t i = 1; i <= count; ++i)
    {
        lua_rawgeti(L, commands, static_cast<int>(i));
        if (!lua_istable(L, -1)) luaL_error(L, "Path command #%d must be a table", static_cast<int>(i));
        int command = CoronaLuaNormalize(L, -1);
        std::string name = CommandName(L, command);
        if (name == "M" || name == "moveTo")
        {
            CheckCommandLength(L, command, 3, name.c_str(), i);
            if (hasCurrent && !current.points.empty()) contours.push_back(std::move(current));
            current = Contour();
            cx = sx = NumberAt(L, command, 2);
            cy = sy = NumberAt(L, command, 3);
            if (!AddPoint(current.points, cx, cy, maxPoints))
            {
                geometryError = "Bezier path exceeds maxCurvePoints";
                return false;
            }
            hasCurrent = true;
        }
        else if (name == "L" || name == "lineTo")
        {
            CheckCommandLength(L, command, 3, name.c_str(), i);
            if (!hasCurrent) luaL_error(L, "lineTo before moveTo at command #%d", static_cast<int>(i));
            cx = NumberAt(L, command, 2); cy = NumberAt(L, command, 3);
            if (!AddPoint(current.points, cx, cy, maxPoints))
            {
                geometryError = "Bezier path exceeds maxCurvePoints";
                return false;
            }
        }
        else if (name == "Q" || name == "quadraticTo")
        {
            CheckCommandLength(L, command, 5, name.c_str(), i);
            if (!hasCurrent) luaL_error(L, "quadraticTo before moveTo at command #%d", static_cast<int>(i));
            float qx = NumberAt(L, command, 2), qy = NumberAt(L, command, 3);
            float ex = NumberAt(L, command, 4), ey = NumberAt(L, command, 5);
            float c1x = cx + (qx - cx) * (2.0f / 3.0f);
            float c1y = cy + (qy - cy) * (2.0f / 3.0f);
            float c2x = ex + (qx - ex) * (2.0f / 3.0f);
            float c2y = ey + (qy - ey) * (2.0f / 3.0f);
            if (!FlattenCubic(L, current.points, cx, cy, c1x, c1y, c2x, c2y, ex, ey,
                              options.tessTol, 0, maxPoints))
            {
                geometryError = "Bezier path exceeds maxCurvePoints";
                return false;
            }
            cx = ex; cy = ey;
        }
        else if (name == "C" || name == "cubicTo")
        {
            CheckCommandLength(L, command, 7, name.c_str(), i);
            if (!hasCurrent) luaL_error(L, "cubicTo before moveTo at command #%d", static_cast<int>(i));
            float c1x = NumberAt(L, command, 2), c1y = NumberAt(L, command, 3);
            float c2x = NumberAt(L, command, 4), c2y = NumberAt(L, command, 5);
            float ex = NumberAt(L, command, 6), ey = NumberAt(L, command, 7);
            if (!FlattenCubic(L, current.points, cx, cy, c1x, c1y, c2x, c2y, ex, ey,
                              options.tessTol, 0, maxPoints))
            {
                geometryError = "Bezier path exceeds maxCurvePoints";
                return false;
            }
            cx = ex; cy = ey;
        }
        else if (name == "Z" || name == "close")
        {
            CheckCommandLength(L, command, 1, name.c_str(), i);
            if (!hasCurrent) luaL_error(L, "close before moveTo at command #%d", static_cast<int>(i));
            current.closed = true; cx = sx; cy = sy;
        }
        else if (name == "m" || name == "l" || name == "q" || name == "c" || name == "z")
            luaL_error(L, "Relative path command '%s' at #%d is not supported; use uppercase absolute commands",
                       name.c_str(), static_cast<int>(i));
        else luaL_error(L, "Unknown path command '%s' at #%d", name.c_str(), static_cast<int>(i));
        lua_pop(L, 1);
    }
    if (hasCurrent && !current.points.empty()) contours.push_back(std::move(current));
    lua_pop(L, 1); // pop commands field / nil
    return !contours.empty();
}

static double SignedArea(const std::vector<std::pair<float, float>> &points)
{
    double area = 0.0;
    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto &a = points[i];
        const auto &b = points[(i + 1) % points.size()];
        area += static_cast<double>(a.first) * b.second - static_cast<double>(a.second) * b.first;
    }
    return area * 0.5;
}

static bool ContainsPoint(const std::vector<std::pair<float, float>> &ring, float x, float y)
{
    bool inside = false;
    for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++)
    {
        float xi = ring[i].first, yi = ring[i].second;
        float xj = ring[j].first, yj = ring[j].second;
        bool crosses = ((yi > y) != (yj > y)) &&
            (x < (xj - xi) * (y - yi) / ((yj - yi) == 0.0f ? 1.0e-20f : (yj - yi)) + xi);
        if (crosses) inside = !inside;
    }
    return inside;
}

static Ring ToRing(const Contour &contour)
{
    Ring result;
    result.reserve(contour.points.size());
    for (const auto &p : contour.points) result.push_back({p.first, p.second});
    return result;
}

static bool GroupFillContours(const std::vector<Contour> &contours,
                              std::vector<Polygon> &groups, std::string &error)
{
    struct Outer { size_t contour; double area; };
    std::vector<Outer> outers;
    std::vector<size_t> holes;
    for (size_t i = 0; i < contours.size(); ++i)
    {
        if (contours[i].points.size() < 3) { error = "fill contour has fewer than 3 points"; return false; }
        double area = SignedArea(contours[i].points);
        if (std::fabs(area) < 1.0e-8) { error = "fill contour has zero area"; return false; }
        if (area > 0.0) outers.push_back({i, area});
        else holes.push_back(i);
    }
    if (outers.empty()) { error = "fill path has no clockwise outer contour"; return false; }
    groups.reserve(outers.size());
    for (const auto &outer : outers) groups.push_back(Polygon(1, ToRing(contours[outer.contour])));

    for (size_t holeIndex : holes)
    {
        const auto &hole = contours[holeIndex];
        size_t best = static_cast<size_t>(-1);
        double bestArea = std::numeric_limits<double>::max();
        for (size_t i = 0; i < outers.size(); ++i)
        {
            const auto &outer = contours[outers[i].contour];
            if (outers[i].area < bestArea && ContainsPoint(outer.points, hole.points[0].first, hole.points[0].second))
            {
                best = i; bestArea = outers[i].area;
            }
        }
        if (best == static_cast<size_t>(-1)) { error = "hole contour is not inside an outer contour"; return false; }
        groups[best].push_back(ToRing(hole));
    }
    return true;
}

static int Flatten(lua_State *L)
{
    MeshOptions options = GetMeshOptions(L, 2, kPathFlattenOptions, "path.flatten");
    std::vector<Contour> contours;
    std::string error;
    if (!ReadPath(L, 1, options, contours, error))
        return error.empty() ? luaL_argerror(L, 1, "Expected a Bezier path command table") :
                               PushGeometryFailure(L, error.c_str());
    lua_createtable(L, static_cast<int>(contours.size()), 0);
    for (size_t i = 0; i < contours.size(); ++i)
    {
        lua_createtable(L, 0, 2);
        lua_createtable(L, static_cast<int>(contours[i].points.size() * 2), 0);
        for (size_t p = 0; p < contours[i].points.size(); ++p)
        {
            lua_pushnumber(L, contours[i].points[p].first); lua_rawseti(L, -2, static_cast<int>(p * 2 + 1));
            lua_pushnumber(L, contours[i].points[p].second); lua_rawseti(L, -2, static_cast<int>(p * 2 + 2));
        }
        lua_setfield(L, -2, "points");
        lua_pushboolean(L, contours[i].closed); lua_setfield(L, -2, "closed");
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    return 1;
}

static int FillMesh(lua_State *L, bool sdf)
{
    MeshOptions options = GetMeshOptions(L, 2, sdf ? kPathSDFOptions : kPathFillOptions,
                                         sdf ? "path.meshSDF" : "path.meshFill");
    std::vector<Contour> contours;
    std::string error;
    if (!ReadPath(L, 1, options, contours, error))
        return error.empty() ? luaL_argerror(L, 1, "Expected a Bezier path command table") :
                               PushGeometryFailure(L, error.c_str());
    std::vector<Polygon> groups;
    if (!GroupFillContours(contours, groups, error))
        return PushGeometryFailure(L, error.c_str());
    MeshResult result;
    if (!BuildFillMesh(groups, options, sdf, result, error))
        return PushGeometryFailure(L, error.c_str());
    return PushMeshResult(L, result, options.output, options.legacyUVs);
}

static int MeshFill(lua_State *L) { return FillMesh(L, false); }
static int MeshSDF(lua_State *L) { return FillMesh(L, true); }

static int MeshStroke(lua_State *L)
{
    MeshOptions options = GetMeshOptions(L, 3, kPathStrokeOptions, "path.meshStroke");
    std::vector<Contour> contours;
    std::string error;
    if (!ReadPath(L, 1, options, contours, error))
        return error.empty() ? luaL_argerror(L, 1, "Expected a Bezier path command table") :
                               PushGeometryFailure(L, error.c_str());
    if (lua_type(L, 2) != LUA_TNUMBER) return luaL_argerror(L, 2, "Stroke width must be a number");
    lua_Number widthNumber = lua_tonumber(L, 2);
    if (!std::isfinite(static_cast<double>(widthNumber)) || widthNumber <= 0.0 ||
        std::fabs(static_cast<double>(widthNumber)) > std::numeric_limits<float>::max())
        return luaL_argerror(L, 2, "Stroke width must be a positive finite float");
    float width = static_cast<float>(widthNumber);
    std::vector<std::pair<std::vector<std::pair<float, float>>, bool>> input;
    input.reserve(contours.size());
    for (auto &contour : contours)
        input.push_back({std::move(contour.points), contour.closed || options.closed});
    MeshResult result;
    if (!BuildStrokeMesh(input, width, options, result, error))
        return PushGeometryFailure(L, error.c_str());
    return PushMeshResult(L, result, options.output, options.legacyUVs);
}

} // namespace

void RegisterPath(lua_State *L)
{
    luaL_Reg functions[] = {
        {"flatten", Flatten}, {"meshFill", MeshFill}, {"meshSDF", MeshSDF},
        {"meshStroke", MeshStroke}, {nullptr, nullptr}
    };
    luaL_register(L, nullptr, functions);
}

} // namespace Geometry2D
