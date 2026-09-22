// Adaptive cubic subdivision in this file is derived from NanoVG (MIT).
// The rest of the path parsing, contour grouping, and Solar2D bindings are
// specific to plugin.geometry2d.

#include "bezier_path.h"

#include "clipper2_bridge.h"
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
static constexpr uint32_t kPathFillOptions = OptionAA | OptionTopology | OptionLimits | OptionJoin | OptionMiterLimit |
    OptionTessTol | OptionRefine | OptionMode | OptionOutput | OptionMaxCurvePoints |
    OptionLegacyUVs | OptionFillRule | OptionIntersections | OptionClipperPrecision;
static constexpr uint32_t kPathSDFOptions = OptionSDF |
    OptionTessTol | OptionMode |
    OptionOutput | OptionMaxCurvePoints | OptionFillRule |
    OptionIntersections | OptionClipperPrecision;
static constexpr uint32_t kPathStrokeOptions = OptionAA | OptionLimits | OptionCap | OptionJoin |
    OptionMiterLimit | OptionTessTol | OptionClosed | OptionMode | OptionOutput |
    OptionMaxCurvePoints | OptionLegacyUVs | OptionDashPattern | OptionDashOffset |
    OptionMaxDashSegments;

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

static void CheckDenseArray(lua_State *L, int table, size_t count,
                            const char *context)
{
    table = CoronaLuaNormalize(L, table);
    lua_pushnil(L);
    while (lua_next(L, table) != 0)
    {
        if (lua_type(L, -2) != LUA_TNUMBER)
            luaL_error(L, "%s must contain only array entries", context);
        lua_Number key = lua_tonumber(L, -2);
        if (key < 1.0 || std::floor(static_cast<double>(key)) != key ||
            key > static_cast<lua_Number>(count))
            luaL_error(L, "%s must be a dense array", context);
        lua_pop(L, 1);
    }
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
static bool FlattenCubic(std::vector<std::pair<float, float>> &points,
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
    if (!FlattenCubic(points, x1, y1, x12, y12, x123, y123, x1234, y1234,
                      tolerance, level + 1, maxPoints)) return false;
    return FlattenCubic(points, x1234, y1234, x234, y234, x34, y34, x4, y4,
                        tolerance, level + 1, maxPoints);
}

} // namespace

bool ValidatePathCommands(const std::vector<PathCommand> &commands, std::string &error)
{
    bool hasCurrent = false;
    for (size_t i = 0; i < commands.size(); ++i)
    {
        switch (commands[i].verb)
        {
            case PathVerb::MoveTo: hasCurrent = true; break;
            case PathVerb::LineTo:
                if (!hasCurrent) { error = "lineTo before moveTo at command #" + std::to_string(i + 1); return false; }
                break;
            case PathVerb::QuadraticTo:
                if (!hasCurrent) { error = "quadraticTo before moveTo at command #" + std::to_string(i + 1); return false; }
                break;
            case PathVerb::CubicTo:
                if (!hasCurrent) { error = "cubicTo before moveTo at command #" + std::to_string(i + 1); return false; }
                break;
            case PathVerb::Close:
                if (!hasCurrent) { error = "close before moveTo at command #" + std::to_string(i + 1); return false; }
                break;
        }
    }
    return true;
}

bool ReadPathCommands(lua_State *L, int arg, std::vector<PathCommand> &commands)
{
    if (!lua_istable(L, arg)) return false;
    arg = CoronaLuaNormalize(L, arg);
    lua_getfield(L, arg, "commands");
    bool wrapped = lua_istable(L, -1);
    int commandArray = wrapped ? CoronaLuaNormalize(L, -1) : arg;
    if (wrapped)
    {
        lua_pushnil(L);
        while (lua_next(L, arg) != 0)
        {
            if (lua_type(L, -2) != LUA_TSTRING ||
                std::strcmp(lua_tostring(L, -2), "commands") != 0)
                luaL_error(L, "Bezier path descriptor accepts only the 'commands' field");
            lua_pop(L, 1);
        }
    }
    size_t count = lua_objlen(L, commandArray);
    if (count > static_cast<size_t>(std::numeric_limits<int>::max()))
        luaL_error(L, "Bezier path command list is too large");
    CheckDenseArray(L, commandArray, count, "Bezier path command list");
    commands.clear();
    commands.reserve(count);
    for (size_t i = 1; i <= count; ++i)
    {
        lua_rawgeti(L, commandArray, static_cast<int>(i));
        if (!lua_istable(L, -1))
            luaL_error(L, "Path command #%d must be a table", static_cast<int>(i));
        int commandIndex = CoronaLuaNormalize(L, -1);
        std::string name = CommandName(L, commandIndex);
        PathCommand command;
        size_t valueCount = 0;
        if (name == "M" || name == "moveTo")
        {
            command.verb = PathVerb::MoveTo; valueCount = 2;
        }
        else if (name == "L" || name == "lineTo")
        {
            command.verb = PathVerb::LineTo; valueCount = 2;
        }
        else if (name == "Q" || name == "quadraticTo")
        {
            command.verb = PathVerb::QuadraticTo; valueCount = 4;
        }
        else if (name == "C" || name == "cubicTo")
        {
            command.verb = PathVerb::CubicTo; valueCount = 6;
        }
        else if (name == "Z" || name == "close")
        {
            command.verb = PathVerb::Close; valueCount = 0;
        }
        else if (name == "m" || name == "l" || name == "q" ||
                 name == "c" || name == "z")
        {
            luaL_error(L, "Relative path command '%s' at #%d is not supported; use uppercase absolute commands",
                       name.c_str(), static_cast<int>(i));
        }
        else
        {
            luaL_error(L, "Unknown path command '%s' at #%d", name.c_str(), static_cast<int>(i));
        }
        CheckCommandLength(L, commandIndex, valueCount + 1, name.c_str(), i);
        CheckDenseArray(L, commandIndex, valueCount + 1, "Path command");
        for (size_t value = 0; value < valueCount; ++value)
            command.values[value] = NumberAt(L, commandIndex, static_cast<int>(value + 2));
        commands.push_back(command);
        lua_pop(L, 1);
    }
    lua_pop(L, 1); // commands field / nil
    std::string error;
    if (!ValidatePathCommands(commands, error)) luaL_error(L, "%s", error.c_str());
    return true;
}

bool FlattenPathCommands(const std::vector<PathCommand> &commands,
                         const MeshOptions &options,
                         std::vector<PathContour> &contours,
                         std::string &error)
{
    contours.clear();
    PathContour current;
    bool hasCurrent = false;
    float cx = 0.0f, cy = 0.0f, sx = 0.0f, sy = 0.0f;
    for (const PathCommand &command : commands)
    {
        switch (command.verb)
        {
            case PathVerb::MoveTo:
                if (hasCurrent && !current.points.empty()) contours.push_back(std::move(current));
                current = PathContour();
                cx = sx = command.values[0]; cy = sy = command.values[1];
                if (!AddPoint(current.points, cx, cy, options.maxCurvePoints))
                { error = "Bezier path exceeds maxCurvePoints"; return false; }
                hasCurrent = true;
                break;
            case PathVerb::LineTo:
                cx = command.values[0]; cy = command.values[1];
                if (!AddPoint(current.points, cx, cy, options.maxCurvePoints))
                { error = "Bezier path exceeds maxCurvePoints"; return false; }
                break;
            case PathVerb::QuadraticTo:
            {
                float qx = command.values[0], qy = command.values[1];
                float ex = command.values[2], ey = command.values[3];
                float c1x = cx + (qx - cx) * (2.0f / 3.0f);
                float c1y = cy + (qy - cy) * (2.0f / 3.0f);
                float c2x = ex + (qx - ex) * (2.0f / 3.0f);
                float c2y = ey + (qy - ey) * (2.0f / 3.0f);
                if (!FlattenCubic(current.points, cx, cy, c1x, c1y, c2x, c2y, ex, ey,
                                  options.tessTol, 0, options.maxCurvePoints))
                { error = "Bezier path exceeds maxCurvePoints"; return false; }
                cx = ex; cy = ey;
                break;
            }
            case PathVerb::CubicTo:
            {
                float ex = command.values[4], ey = command.values[5];
                if (!FlattenCubic(current.points, cx, cy,
                                  command.values[0], command.values[1],
                                  command.values[2], command.values[3], ex, ey,
                                  options.tessTol, 0, options.maxCurvePoints))
                { error = "Bezier path exceeds maxCurvePoints"; return false; }
                cx = ex; cy = ey;
                break;
            }
            case PathVerb::Close:
                current.closed = true; cx = sx; cy = sy;
                break;
        }
    }
    if (hasCurrent && !current.points.empty()) contours.push_back(std::move(current));
    return !contours.empty();
}

namespace {

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

static Ring ToRing(const PathContour &contour)
{
    Ring result;
    result.reserve(contour.points.size());
    for (const auto &p : contour.points) result.push_back({p.first, p.second});
    return result;
}

} // namespace

bool GroupPathFillContours(const std::vector<PathContour> &contours,
                           std::vector<Polygon> &groups, std::string &error)
{
    groups.clear();

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

namespace {

static bool ReadPath(lua_State *L, int arg, const MeshOptions &options,
                     std::vector<PathContour> &contours, std::string &error)
{
    std::vector<PathCommand> commands;
    if (!ReadPathCommands(L, arg, commands)) return false;
    return FlattenPathCommands(commands, options, contours, error);
}

static int Flatten(lua_State *L)
{
    MeshOptions options = GetMeshOptions(L, 2, kPathFlattenOptions, "path.flatten");
    std::vector<PathContour> contours;
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
                                         sdf ? "path.meshDistance" : "path.meshFill");
    std::vector<PathContour> contours;
    std::string error;
    if (!ReadPath(L, 1, options, contours, error))
        return error.empty() ? luaL_argerror(L, 1, "Expected a Bezier path command table") :
                               PushGeometryFailure(L, error.c_str());
    std::vector<Polygon> groups;
    if (!PreparePathFillGroups(contours, options, groups, error))
        return PushGeometryFailure(L, error.c_str());
    MeshResult result;
    if (!BuildFillMesh(groups, options, sdf, result, error) ||
        !ApplyPathUVBounds(contours, result, error))
        return PushGeometryFailure(L, error.c_str());
    return PushMeshResult(L, result, options.output, options.legacyUVs);
}

static int MeshFill(lua_State *L) { return FillMesh(L, false); }
static int MeshDistance(lua_State *L) { return FillMesh(L, true); }

static int MeshStroke(lua_State *L)
{
    MeshOptions options = GetMeshOptions(L, 3, kPathStrokeOptions, "path.meshStroke");
    std::vector<PathContour> contours;
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

bool ApplyPathUVBounds(const std::vector<PathContour>& contours,
                       MeshResult& mesh, std::string& error)
{
    std::array<double,4> bounds{};
    bool first=true;
    for (const auto& contour:contours) for (auto p:contour.points) {
        if (first) { bounds={p.first,p.second,p.first,p.second};first=false; }
        bounds[0]=std::min(bounds[0],double(p.first));
        bounds[1]=std::min(bounds[1],double(p.second));
        bounds[2]=std::max(bounds[2],double(p.first));
        bounds[3]=std::max(bounds[3],double(p.second));
    }
    if (first || !(bounds[2]>bounds[0]) || !(bounds[3]>bounds[1])) {
        error="path requires non-degenerate UV bounds";return false;
    }
    if (bounds==mesh.uvBounds) return true;
    mesh.uvs.resize(mesh.vertices.size());
    for (size_t i=0;i<mesh.vertices.size();++i) {
        size_t axis=i%2;
        double value=(double(mesh.vertices[i])-bounds[axis])/(bounds[axis+2]-bounds[axis]);
        if (!std::isfinite(value) || std::abs(value)>std::numeric_limits<float>::max()) {
            error="path float32 UV precision insufficient";return false;
        }
        mesh.uvs[i]=static_cast<float>(value);
    }
    mesh.uvBounds=bounds;
    return true;
}

void RegisterPath(lua_State *L)
{
    luaL_Reg functions[] = {
        {"flatten", Flatten}, {"meshFill", MeshFill}, {"meshDistance", MeshDistance},
        {"meshStroke", MeshStroke}, {nullptr, nullptr}
    };
    luaL_register(L, nullptr, functions);
    RegisterRetainedShape(L);
}

} // namespace Geometry2D
