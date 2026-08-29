#include "bezier_path.h"
#include "clipper2_bridge.h"
#include "mesh_builder.h"
#include "mesh_result.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace Geometry2D {
namespace {

static const char *kShapeMetatable = "plugin.geometry2d.retainedShape";

static constexpr uint32_t kConfigureOptions = OptionFringe | OptionMiterLimit |
    OptionTessTol | OptionRefine | OptionMode | OptionMaxCurvePoints |
    OptionMaxDashSegments | OptionFillRule | OptionIntersections |
    OptionClipperPrecision;

struct ShapeColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
    bool enabled = true;
};

struct RetainedShape {
    std::vector<PathCommand> commands;
    MeshOptions fillOptions;
    MeshOptions strokeOptions;
    ShapeColor fillColor;
    ShapeColor strokeColor;
    float strokeWidth = 0.0f;

    uint64_t geometryRevision = 1;
    uint64_t styleRevision = 1;
    uint64_t cacheRevision = 0;
    MeshResult fillMesh;
    MeshResult strokeMesh;
    bool hasFillMesh = false;
    bool hasStrokeMesh = false;
};

static RetainedShape *CheckShape(lua_State *L, int index)
{
    return static_cast<RetainedShape *>(luaL_checkudata(L, index, kShapeMetatable));
}

static int PushFailure(lua_State *L, const std::string &error)
{
    lua_pushnil(L);
    lua_pushlstring(L, error.c_str(), error.size());
    return 2;
}

static std::string LuaErrorString(lua_State *L, const char *fallback)
{
    const char *value = lua_tostring(L, -1);
    std::string result = value ? value : fallback;
    lua_pop(L, 1);
    return result;
}

static float CheckFiniteFloat(lua_State *L, int index, const char *name)
{
    if (lua_type(L, index) != LUA_TNUMBER)
        luaL_error(L, "%s must be a number", name);
    lua_Number value = lua_tonumber(L, index);
    if (!std::isfinite(static_cast<double>(value)) ||
        std::fabs(static_cast<double>(value)) > std::numeric_limits<float>::max())
        luaL_error(L, "%s must be a finite float", name);
    return static_cast<float>(value);
}

static float CheckUnitColor(lua_State *L, int index, const char *name)
{
    float value = CheckFiniteFloat(L, index, name);
    if (value < 0.0f || value > 1.0f)
        luaL_error(L, "%s must be between 0 and 1", name);
    return value;
}

static void MarkGeometry(RetainedShape &shape)
{
    ++shape.geometryRevision;
    if (shape.geometryRevision == 0) ++shape.geometryRevision;
}

static void MarkStyle(RetainedShape &shape)
{
    ++shape.styleRevision;
    if (shape.styleRevision == 0) ++shape.styleRevision;
}

static PathCommand MakeCommand(lua_State *L, PathVerb verb, int firstValue,
                               size_t valueCount, const char *name)
{
    if (lua_gettop(L) != firstValue + static_cast<int>(valueCount) - 1)
        luaL_error(L, "%s expects %d coordinates", name, static_cast<int>(valueCount));
    PathCommand command;
    command.verb = verb;
    for (size_t i = 0; i < valueCount; ++i)
        command.values[i] = CheckFiniteFloat(L, firstValue + static_cast<int>(i), "Path coordinate");
    return command;
}

static bool ParseVerb(const char *name, PathVerb &verb, size_t &valueCount)
{
    if (!name) return false;
    if (std::strcmp(name, "M") == 0 || std::strcmp(name, "moveTo") == 0)
    { verb = PathVerb::MoveTo; valueCount = 2; return true; }
    if (std::strcmp(name, "L") == 0 || std::strcmp(name, "lineTo") == 0)
    { verb = PathVerb::LineTo; valueCount = 2; return true; }
    if (std::strcmp(name, "Q") == 0 || std::strcmp(name, "quadraticTo") == 0)
    { verb = PathVerb::QuadraticTo; valueCount = 4; return true; }
    if (std::strcmp(name, "C") == 0 || std::strcmp(name, "cubicTo") == 0)
    { verb = PathVerb::CubicTo; valueCount = 6; return true; }
    if (std::strcmp(name, "Z") == 0 || std::strcmp(name, "close") == 0)
    { verb = PathVerb::Close; valueCount = 0; return true; }
    return false;
}

static int ReturnSelf(lua_State *L)
{
    lua_settop(L, 1);
    return 1;
}

static int AppendCommand(lua_State *L, PathVerb verb, size_t valueCount,
                         const char *name)
{
    RetainedShape *shape = CheckShape(L, 1);
    PathCommand command = MakeCommand(L, verb, 2, valueCount, name);
    shape->commands.push_back(command);
    std::string error;
    if (!ValidatePathCommands(shape->commands, error))
    {
        shape->commands.pop_back();
        return luaL_error(L, "%s", error.c_str());
    }
    MarkGeometry(*shape);
    return ReturnSelf(L);
}

static int MoveTo(lua_State *L) { return AppendCommand(L, PathVerb::MoveTo, 2, "moveTo"); }
static int LineTo(lua_State *L) { return AppendCommand(L, PathVerb::LineTo, 2, "lineTo"); }
static int QuadraticTo(lua_State *L) { return AppendCommand(L, PathVerb::QuadraticTo, 4, "quadraticTo"); }
static int CubicTo(lua_State *L) { return AppendCommand(L, PathVerb::CubicTo, 6, "cubicTo"); }

static int Close(lua_State *L)
{
    if (lua_gettop(L) != 1) return luaL_error(L, "close expects no arguments");
    return AppendCommand(L, PathVerb::Close, 0, "close");
}

static int Clear(lua_State *L)
{
    RetainedShape *shape = CheckShape(L, 1);
    if (lua_gettop(L) != 1) return luaL_error(L, "clear expects no arguments");
    if (!shape->commands.empty())
    {
        shape->commands.clear();
        MarkGeometry(*shape);
    }
    return ReturnSelf(L);
}

static int CommandCount(lua_State *L)
{
    RetainedShape *shape = CheckShape(L, 1);
    if (lua_gettop(L) != 1) return luaL_error(L, "commandCount expects no arguments");
    lua_pushinteger(L, static_cast<lua_Integer>(shape->commands.size()));
    return 1;
}

static int SetCommand(lua_State *L)
{
    RetainedShape *shape = CheckShape(L, 1);
    int index = luaL_checkint(L, 2);
    if (index < 1 || static_cast<size_t>(index) > shape->commands.size())
        return luaL_argerror(L, 2, "command index is out of bounds");
    const char *name = luaL_checkstring(L, 3);
    PathVerb verb;
    size_t valueCount = 0;
    if (!ParseVerb(name, verb, valueCount))
        return luaL_argerror(L, 3, "unknown absolute path command");
    PathCommand replacement = MakeCommand(L, verb, 4, valueCount, "setCommand");
    PathCommand old = shape->commands[static_cast<size_t>(index - 1)];
    shape->commands[static_cast<size_t>(index - 1)] = replacement;
    std::string error;
    if (!ValidatePathCommands(shape->commands, error))
    {
        shape->commands[static_cast<size_t>(index - 1)] = old;
        return luaL_error(L, "%s", error.c_str());
    }
    MarkGeometry(*shape);
    return ReturnSelf(L);
}

static int RemoveCommand(lua_State *L)
{
    RetainedShape *shape = CheckShape(L, 1);
    if (lua_gettop(L) != 2) return luaL_error(L, "removeCommand expects one index");
    int index = luaL_checkint(L, 2);
    if (index < 1 || static_cast<size_t>(index) > shape->commands.size())
        return luaL_argerror(L, 2, "command index is out of bounds");
    size_t position = static_cast<size_t>(index - 1);
    PathCommand old = shape->commands[position];
    shape->commands.erase(shape->commands.begin() + position);
    std::string error;
    if (!ValidatePathCommands(shape->commands, error))
    {
        shape->commands.insert(shape->commands.begin() + position, old);
        return luaL_error(L, "%s", error.c_str());
    }
    MarkGeometry(*shape);
    return ReturnSelf(L);
}

static bool ReadColor(lua_State *L, int first, ShapeColor &color, const char *context)
{
    if (lua_type(L, first) == LUA_TBOOLEAN && !lua_toboolean(L, first))
    {
        if (lua_gettop(L) != first) luaL_error(L, "%s(false) accepts no additional values", context);
        bool changed = color.enabled;
        color.enabled = false;
        return changed;
    }
    int count = lua_gettop(L) - first + 1;
    if (count != 3 && count != 4) luaL_error(L, "%s expects r, g, b[, a], or false", context);
    ShapeColor replacement;
    replacement.r = CheckUnitColor(L, first, "Red");
    replacement.g = CheckUnitColor(L, first + 1, "Green");
    replacement.b = CheckUnitColor(L, first + 2, "Blue");
    replacement.a = count == 4 ? CheckUnitColor(L, first + 3, "Alpha") : 1.0f;
    replacement.enabled = true;
    bool changed = color.r != replacement.r || color.g != replacement.g ||
                   color.b != replacement.b || color.a != replacement.a ||
                   color.enabled != replacement.enabled;
    color = replacement;
    return changed;
}

static int Fill(lua_State *L)
{
    RetainedShape *shape = CheckShape(L, 1);
    bool wasEnabled = shape->fillColor.enabled;
    if (ReadColor(L, 2, shape->fillColor, "fill"))
    {
        if (wasEnabled != shape->fillColor.enabled) MarkGeometry(*shape);
        MarkStyle(*shape);
    }
    return ReturnSelf(L);
}

static int StrokeFill(lua_State *L)
{
    RetainedShape *shape = CheckShape(L, 1);
    bool wasEnabled = shape->strokeColor.enabled;
    if (ReadColor(L, 2, shape->strokeColor, "strokeFill"))
    {
        if (wasEnabled != shape->strokeColor.enabled) MarkGeometry(*shape);
        MarkStyle(*shape);
    }
    return ReturnSelf(L);
}

static int StrokeWidth(lua_State *L)
{
    RetainedShape *shape = CheckShape(L, 1);
    if (lua_gettop(L) != 2) return luaL_error(L, "strokeWidth expects one number");
    float width = CheckFiniteFloat(L, 2, "Stroke width");
    if (width < 0.0f) return luaL_argerror(L, 2, "stroke width must be non-negative");
    if (shape->strokeWidth != width)
    {
        shape->strokeWidth = width;
        MarkGeometry(*shape);
    }
    return ReturnSelf(L);
}

static Fringe::LineJoin CheckJoin(lua_State *L, int index)
{
    const char *value = luaL_checkstring(L, index);
    if (std::strcmp(value, "miter") == 0) return Fringe::JOIN_MITER;
    if (std::strcmp(value, "bevel") == 0) return Fringe::JOIN_BEVEL;
    if (std::strcmp(value, "round") == 0) return Fringe::JOIN_ROUND;
    luaL_argerror(L, index, "expected 'miter', 'bevel', or 'round'");
    return Fringe::JOIN_MITER;
}

static int StrokeJoin(lua_State *L)
{
    RetainedShape *shape = CheckShape(L, 1);
    if (lua_gettop(L) != 2) return luaL_error(L, "strokeJoin expects one string");
    Fringe::LineJoin join = CheckJoin(L, 2);
    if (shape->strokeOptions.join != join)
    {
        shape->strokeOptions.join = join;
        MarkGeometry(*shape);
    }
    return ReturnSelf(L);
}

static int FillJoin(lua_State *L)
{
    RetainedShape *shape = CheckShape(L, 1);
    if (lua_gettop(L) != 2) return luaL_error(L, "fillJoin expects one string");
    Fringe::LineJoin join = CheckJoin(L, 2);
    if (shape->fillOptions.join != join)
    {
        shape->fillOptions.join = join;
        MarkGeometry(*shape);
    }
    return ReturnSelf(L);
}

static int StrokeCap(lua_State *L)
{
    RetainedShape *shape = CheckShape(L, 1);
    if (lua_gettop(L) != 2) return luaL_error(L, "strokeCap expects one string");
    const char *value = luaL_checkstring(L, 2);
    Fringe::LineCap cap;
    if (std::strcmp(value, "butt") == 0) cap = Fringe::CAP_BUTT;
    else if (std::strcmp(value, "square") == 0) cap = Fringe::CAP_SQUARE;
    else if (std::strcmp(value, "round") == 0) cap = Fringe::CAP_ROUND;
    else return luaL_argerror(L, 2, "expected 'butt', 'square', or 'round'");
    if (shape->strokeOptions.cap != cap)
    {
        shape->strokeOptions.cap = cap;
        MarkGeometry(*shape);
    }
    return ReturnSelf(L);
}

static int StrokeDash(lua_State *L)
{
    RetainedShape *shape = CheckShape(L, 1);
    if (lua_gettop(L) < 2 || lua_gettop(L) > 3)
        return luaL_error(L, "strokeDash expects pattern[, offset]");
    std::vector<float> pattern;
    if (!lua_isnoneornil(L, 2))
    {
        if (!lua_istable(L, 2)) return luaL_argerror(L, 2, "dash pattern must be a table or nil");
        size_t count = lua_objlen(L, 2);
        if (count == 0) return luaL_argerror(L, 2, "dash pattern must not be empty; use nil to clear it");
        if (count > static_cast<size_t>(std::numeric_limits<int>::max()))
            return luaL_argerror(L, 2, "dash pattern is too large");
        pattern.reserve(count);
        for (size_t i = 1; i <= count; ++i)
        {
            lua_rawgeti(L, 2, static_cast<int>(i));
            float value = CheckFiniteFloat(L, -1, "Dash length");
            lua_pop(L, 1);
            if (value <= 0.0f) return luaL_argerror(L, 2, "dash lengths must be positive");
            pattern.push_back(value);
        }
        lua_pushnil(L);
        while (lua_next(L, 2) != 0)
        {
            if (lua_type(L, -2) != LUA_TNUMBER)
                return luaL_argerror(L, 2, "dash pattern must contain only array entries");
            lua_Number key = lua_tonumber(L, -2);
            if (key < 1.0 || std::floor(static_cast<double>(key)) != key ||
                key > static_cast<lua_Number>(count))
                return luaL_argerror(L, 2, "dash pattern must be a dense array");
            lua_pop(L, 1);
        }
    }
    float offset = lua_gettop(L) == 3 ? CheckFiniteFloat(L, 3, "Dash offset") : 0.0f;
    if (pattern.empty() && offset != 0.0f)
        return luaL_argerror(L, 3, "dash offset requires a non-empty pattern");
    if (shape->strokeOptions.dashPattern != pattern || shape->strokeOptions.dashOffset != offset)
    {
        shape->strokeOptions.dashPattern = std::move(pattern);
        shape->strokeOptions.dashOffset = offset;
        MarkGeometry(*shape);
    }
    return ReturnSelf(L);
}

static int Configure(lua_State *L)
{
    RetainedShape *shape = CheckShape(L, 1);
    if (lua_gettop(L) != 2) return luaL_error(L, "configure expects one options table");
    MeshOptions defaults = shape->fillOptions;
    defaults.maxDashSegments = shape->strokeOptions.maxDashSegments;
    MeshOptions options = GetMeshOptions(L, 2, kConfigureOptions,
                                         "retainedShape.configure", &defaults);
    bool changed = shape->fillOptions.fringe != options.fringe ||
        shape->fillOptions.miterLimit != options.miterLimit ||
        shape->fillOptions.tessTol != options.tessTol ||
        shape->fillOptions.maxCurvePoints != options.maxCurvePoints ||
        shape->fillOptions.triangles != options.triangles ||
        shape->fillOptions.refine != options.refine ||
        shape->strokeOptions.maxDashSegments != options.maxDashSegments ||
        shape->fillOptions.fillRule != options.fillRule ||
        shape->fillOptions.intersections != options.intersections ||
        shape->fillOptions.clipperPrecision != options.clipperPrecision;
    shape->fillOptions.fringe = shape->strokeOptions.fringe = options.fringe;
    shape->fillOptions.miterLimit = shape->strokeOptions.miterLimit = options.miterLimit;
    shape->fillOptions.tessTol = shape->strokeOptions.tessTol = options.tessTol;
    shape->fillOptions.maxCurvePoints = shape->strokeOptions.maxCurvePoints = options.maxCurvePoints;
    shape->fillOptions.triangles = shape->strokeOptions.triangles = options.triangles;
    shape->fillOptions.refine = options.refine;
    shape->fillOptions.fillRule = options.fillRule;
    shape->fillOptions.intersections = options.intersections;
    shape->fillOptions.clipperPrecision = options.clipperPrecision;
    shape->strokeOptions.maxDashSegments = options.maxDashSegments;
    if (changed) MarkGeometry(*shape);
    return ReturnSelf(L);
}

static bool EnsureGeometry(RetainedShape &shape, std::string &error)
{
    if (shape.cacheRevision == shape.geometryRevision) return true;
    if (shape.commands.empty()) { error = "retained shape contains no path commands"; return false; }
    if (!ValidatePathCommands(shape.commands, error)) return false;

    std::vector<PathContour> contours;
    if (!FlattenPathCommands(shape.commands, shape.fillOptions, contours, error))
    {
        if (error.empty()) error = "retained shape contains no drawable contour";
        return false;
    }

    MeshResult fillMesh;
    MeshResult strokeMesh;
    bool hasFill = shape.fillColor.enabled;
    bool hasStroke = shape.strokeColor.enabled && shape.strokeWidth > 0.0f;
    if (hasFill)
    {
        std::vector<Polygon> groups;
        if (!PreparePathFillGroups(contours, shape.fillOptions, groups, error) ||
            !BuildFillMesh(groups, shape.fillOptions, false, fillMesh, error))
            return false;
    }
    if (hasStroke)
    {
        std::vector<std::pair<std::vector<std::pair<float, float>>, bool>> strokeContours;
        strokeContours.reserve(contours.size());
        for (const PathContour &contour : contours)
            strokeContours.push_back({contour.points, contour.closed});
        if (!BuildStrokeMesh(strokeContours, shape.strokeWidth, shape.strokeOptions,
                             strokeMesh, error))
            return false;
    }
    shape.fillMesh = std::move(fillMesh);
    shape.strokeMesh = std::move(strokeMesh);
    shape.hasFillMesh = hasFill;
    shape.hasStrokeMesh = hasStroke;
    shape.cacheRevision = shape.geometryRevision;
    return true;
}

static bool NewDisplayGroup(lua_State *L, std::string &error)
{
    int base = lua_gettop(L);
    lua_getglobal(L, "display");
    if (!lua_istable(L, -1))
    {
        lua_settop(L, base); error = "display API is unavailable"; return false;
    }
    lua_getfield(L, -1, "newGroup");
    if (!lua_isfunction(L, -1))
    {
        lua_settop(L, base); error = "display.newGroup is unavailable"; return false;
    }
    if (lua_pcall(L, 0, 1, 0) != 0)
    {
        error = LuaErrorString(L, "display.newGroup failed");
        lua_settop(L, base); return false;
    }
    lua_remove(L, base + 1); // display table
    return true;
}

static bool CallSetFillColor(lua_State *L, int object, const ShapeColor &color,
                             std::string &error)
{
    int base = lua_gettop(L);
    object = CoronaLuaNormalize(L, object);
    lua_getfield(L, object, "setFillColor");
    if (!lua_isfunction(L, -1))
    { lua_settop(L, base); error = "mesh:setFillColor is unavailable"; return false; }
    lua_pushvalue(L, object);
    lua_pushnumber(L, color.r); lua_pushnumber(L, color.g);
    lua_pushnumber(L, color.b); lua_pushnumber(L, color.a);
    if (lua_pcall(L, 5, 0, 0) != 0)
    {
        error = LuaErrorString(L, "mesh:setFillColor failed");
        lua_settop(L, base); return false;
    }
    return true;
}

static bool GetMeshOffset(lua_State *L, int mesh, float &x, float &y,
                          std::string &error)
{
    int base = lua_gettop(L);
    mesh = CoronaLuaNormalize(L, mesh);
    lua_getfield(L, mesh, "path");
    if (lua_isnil(L, -1))
    { lua_settop(L, base); error = "mesh.path is unavailable"; return false; }
    int path = CoronaLuaNormalize(L, -1);
    lua_getfield(L, path, "getVertexOffset");
    if (!lua_isfunction(L, -1))
    { lua_settop(L, base); error = "mesh.path:getVertexOffset is unavailable"; return false; }
    lua_pushvalue(L, path);
    if (lua_pcall(L, 1, 2, 0) != 0)
    {
        error = LuaErrorString(L, "mesh.path:getVertexOffset failed");
        lua_settop(L, base); return false;
    }
    if (!lua_isnumber(L, -2) || !lua_isnumber(L, -1))
    { lua_settop(L, base); error = "mesh.path:getVertexOffset returned invalid values"; return false; }
    x = static_cast<float>(lua_tonumber(L, -2));
    y = static_cast<float>(lua_tonumber(L, -1));
    lua_settop(L, base);
    return true;
}

static bool TranslateObject(lua_State *L, int object, float x, float y,
                            std::string &error)
{
    if (x == 0.0f && y == 0.0f) return true;
    int base = lua_gettop(L);
    object = CoronaLuaNormalize(L, object);
    lua_getfield(L, object, "translate");
    if (!lua_isfunction(L, -1))
    { lua_settop(L, base); error = "mesh:translate is unavailable"; return false; }
    lua_pushvalue(L, object); lua_pushnumber(L, x); lua_pushnumber(L, y);
    if (lua_pcall(L, 3, 0, 0) != 0)
    {
        error = LuaErrorString(L, "mesh:translate failed");
        lua_settop(L, base); return false;
    }
    return true;
}

static bool RemoveDisplayObject(lua_State *L, int object, std::string &error)
{
    int base = lua_gettop(L);
    object = CoronaLuaNormalize(L, object);
    lua_getfield(L, object, "removeSelf");
    if (!lua_isfunction(L, -1))
    { lua_settop(L, base); error = "display object was already removed"; return false; }
    lua_pushvalue(L, object);
    if (lua_pcall(L, 1, 0, 0) != 0)
    {
        error = LuaErrorString(L, "display object removeSelf failed");
        lua_settop(L, base); return false;
    }
    return true;
}

static bool InsertChild(lua_State *L, int group, int child, bool first,
                        std::string &error)
{
    int base = lua_gettop(L);
    group = CoronaLuaNormalize(L, group);
    child = CoronaLuaNormalize(L, child);
    lua_getfield(L, group, "insert");
    if (!lua_isfunction(L, -1))
    { lua_settop(L, base); error = "view.group:insert is unavailable"; return false; }
    lua_pushvalue(L, group);
    if (first) lua_pushinteger(L, 1);
    lua_pushvalue(L, child);
    if (lua_pcall(L, first ? 3 : 2, 0, 0) != 0)
    {
        error = LuaErrorString(L, "view.group:insert failed");
        lua_settop(L, base); return false;
    }
    return true;
}

static bool CreateDisplayMesh(lua_State *L, const MeshResult &mesh,
                              const ShapeColor &color, float &offsetX,
                              float &offsetY, std::string &error)
{
    int base = lua_gettop(L);
    int count = PushMeshResult(L, mesh, OutputMode::Mesh, false);
    if (count != 2 || lua_isnil(L, base + 1))
    {
        if (count == 2 && lua_type(L, base + 2) == LUA_TSTRING)
            error.assign(lua_tostring(L, base + 2));
        else error = "display.newMesh failed";
        lua_settop(L, base);
        return false;
    }
    lua_remove(L, base + 2); // attributes; the mesh owns its color buffer copy
    int meshIndex = base + 1;
    if (!GetMeshOffset(L, meshIndex, offsetX, offsetY, error) ||
        !TranslateObject(L, meshIndex, offsetX, offsetY, error) ||
        !CallSetFillColor(L, meshIndex, color, error))
    {
        std::string ignored;
        RemoveDisplayObject(L, meshIndex, ignored);
        lua_settop(L, base);
        return false;
    }
    return true;
}

static bool UpdateDisplayMesh(lua_State *L, int mesh, const MeshResult &result,
                              const ShapeColor &color, float oldOffsetX,
                              float oldOffsetY, float &newOffsetX,
                              float &newOffsetY, std::string &error)
{
    int base = lua_gettop(L);
    mesh = CoronaLuaNormalize(L, mesh);
    lua_getfield(L, mesh, "path");
    if (lua_isnil(L, -1))
    { lua_settop(L, base); error = "mesh.path is unavailable"; return false; }
    int path = CoronaLuaNormalize(L, -1);
    lua_getfield(L, path, "update");
    if (!lua_isfunction(L, -1))
    { lua_settop(L, base); error = "mesh.path:update is unavailable"; return false; }
    lua_pushvalue(L, path);
    PushMeshUpdateDescriptor(L, result, false);
    if (lua_pcall(L, 2, 0, 0) != 0)
    {
        error = LuaErrorString(L, "mesh.path:update failed");
        lua_settop(L, base); return false;
    }
    lua_settop(L, base);
    if (!GetMeshOffset(L, mesh, newOffsetX, newOffsetY, error) ||
        !TranslateObject(L, mesh, newOffsetX - oldOffsetX, newOffsetY - oldOffsetY, error) ||
        !CallSetFillColor(L, mesh, color, error))
        return false;
    return true;
}

static size_t GetSizeField(lua_State *L, int table, const char *field)
{
    lua_getfield(L, table, field);
    size_t value = lua_isnumber(L, -1) ? static_cast<size_t>(lua_tonumber(L, -1)) : 0;
    lua_pop(L, 1);
    return value;
}

static double GetNumberField(lua_State *L, int table, const char *field)
{
    lua_getfield(L, table, field);
    double value = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 0.0;
    lua_pop(L, 1);
    return value;
}

static bool GetBooleanField(lua_State *L, int table, const char *field)
{
    lua_getfield(L, table, field);
    bool value = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return value;
}

static void SetPartMetadata(lua_State *L, int view, const char *prefix,
                            const MeshResult &mesh, float offsetX, float offsetY)
{
    std::string field = std::string("_") + prefix;
    lua_pushnumber(L, static_cast<lua_Number>(mesh.VertexCount()));
    lua_setfield(L, view, (field + "VertexCount").c_str());
    lua_pushnumber(L, static_cast<lua_Number>(mesh.indices.size()));
    lua_setfield(L, view, (field + "IndexCount").c_str());
    lua_pushboolean(L, mesh.triangles);
    lua_setfield(L, view, (field + "Triangles").c_str());
    lua_pushnumber(L, offsetX); lua_setfield(L, view, (field + "OffsetX").c_str());
    lua_pushnumber(L, offsetY); lua_setfield(L, view, (field + "OffsetY").c_str());
}

static bool TopologyMatches(lua_State *L, int view, const char *prefix,
                            const MeshResult &mesh)
{
    std::string field = std::string("_") + prefix;
    return GetSizeField(L, view, (field + "VertexCount").c_str()) == mesh.VertexCount() &&
           GetSizeField(L, view, (field + "IndexCount").c_str()) == mesh.indices.size() &&
           GetBooleanField(L, view, (field + "Triangles").c_str()) == mesh.triangles;
}

static bool SyncPart(lua_State *L, int view, int group, const char *prefix,
                     const char *meshField, bool enabled, const MeshResult &result,
                     const ShapeColor &color, bool geometryChanged,
                     bool styleChanged, bool insertFirst, bool &replaced,
                     std::string &error)
{
    int base = lua_gettop(L);
    view = CoronaLuaNormalize(L, view);
    group = CoronaLuaNormalize(L, group);
    lua_getfield(L, view, meshField);
    int oldMesh = CoronaLuaNormalize(L, -1);
    bool hasOld = !lua_isnil(L, oldMesh);

    if (!enabled)
    {
        if (hasOld)
        {
            if (!RemoveDisplayObject(L, oldMesh, error)) { lua_settop(L, base); return false; }
            lua_pushnil(L); lua_setfield(L, view, meshField);
            replaced = true;
        }
        lua_settop(L, base);
        return true;
    }

    if (hasOld && geometryChanged && TopologyMatches(L, view, prefix, result))
    {
        std::string field = std::string("_") + prefix;
        float oldX = static_cast<float>(GetNumberField(L, view, (field + "OffsetX").c_str()));
        float oldY = static_cast<float>(GetNumberField(L, view, (field + "OffsetY").c_str()));
        float newX = 0.0f, newY = 0.0f;
        if (!UpdateDisplayMesh(L, oldMesh, result, color, oldX, oldY, newX, newY, error))
        { lua_settop(L, base); return false; }
        SetPartMetadata(L, view, prefix, result, newX, newY);
        lua_settop(L, base);
        return true;
    }

    if (hasOld && !geometryChanged)
    {
        if (styleChanged && !CallSetFillColor(L, oldMesh, color, error))
        { lua_settop(L, base); return false; }
        lua_settop(L, base);
        return true;
    }

    float offsetX = 0.0f, offsetY = 0.0f;
    if (!CreateDisplayMesh(L, result, color, offsetX, offsetY, error))
    { lua_settop(L, base); return false; }
    int newMesh = CoronaLuaNormalize(L, -1);
    if (!InsertChild(L, group, newMesh, insertFirst, error))
    {
        std::string ignored;
        RemoveDisplayObject(L, newMesh, ignored);
        lua_settop(L, base);
        return false;
    }
    if (hasOld && !RemoveDisplayObject(L, oldMesh, error))
    { lua_settop(L, base); return false; }
    lua_pushvalue(L, newMesh); lua_setfield(L, view, meshField);
    SetPartMetadata(L, view, prefix, result, offsetX, offsetY);
    replaced = true;
    lua_settop(L, base);
    return true;
}

static bool ViewBelongsToShape(lua_State *L, int shapeIndex, int view)
{
    shapeIndex = CoronaLuaNormalize(L, shapeIndex);
    view = CoronaLuaNormalize(L, view);
    lua_getfield(L, view, "_shape");
    bool matches = lua_rawequal(L, shapeIndex, -1) != 0;
    lua_pop(L, 1);
    return matches;
}

static bool SyncView(lua_State *L, RetainedShape &shape, int view, bool force,
                     bool &replaced, std::string &error)
{
    view = CoronaLuaNormalize(L, view);
    uint64_t viewGeometry = static_cast<uint64_t>(GetNumberField(L, view, "_geometryRevision"));
    uint64_t viewStyle = static_cast<uint64_t>(GetNumberField(L, view, "_styleRevision"));
    bool geometryChanged = force || viewGeometry != shape.geometryRevision;
    bool styleChanged = force || viewStyle != shape.styleRevision;
    if (!geometryChanged && !styleChanged) return true;
    if (geometryChanged && !EnsureGeometry(shape, error)) return false;

    lua_getfield(L, view, "group");
    if (lua_isnil(L, -1))
    { lua_pop(L, 1); error = "retained shape view.group was removed"; return false; }
    int group = CoronaLuaNormalize(L, -1);
    bool ok = SyncPart(L, view, group, "fill", "fillMesh", shape.hasFillMesh,
                       shape.fillMesh, shape.fillColor, geometryChanged, styleChanged,
                       true, replaced, error) &&
              SyncPart(L, view, group, "stroke", "strokeMesh", shape.hasStrokeMesh,
                       shape.strokeMesh, shape.strokeColor, geometryChanged, styleChanged,
                       false, replaced, error);
    lua_pop(L, 1); // group
    if (!ok) return false;
    lua_pushnumber(L, static_cast<lua_Number>(shape.geometryRevision));
    lua_setfield(L, view, "_geometryRevision");
    lua_pushnumber(L, static_cast<lua_Number>(shape.styleRevision));
    lua_setfield(L, view, "_styleRevision");
    return true;
}

static int NewView(lua_State *L)
{
    RetainedShape *shape = CheckShape(L, 1);
    if (lua_gettop(L) != 1) return luaL_error(L, "newView expects no arguments; call configure() first");
    std::string error;
    if (!EnsureGeometry(*shape, error)) return PushFailure(L, error);
    int base = lua_gettop(L);
    if (!NewDisplayGroup(L, error)) return PushFailure(L, error);
    int group = CoronaLuaNormalize(L, -1);
    lua_createtable(L, 0, 10);
    int view = CoronaLuaNormalize(L, -1);
    lua_pushvalue(L, group); lua_setfield(L, view, "group");
    lua_pushvalue(L, 1); lua_setfield(L, view, "_shape");
    lua_pushnumber(L, 0); lua_setfield(L, view, "_geometryRevision");
    lua_pushnumber(L, 0); lua_setfield(L, view, "_styleRevision");
    bool replaced = false;
    if (!SyncView(L, *shape, view, true, replaced, error))
    {
        std::string ignored;
        RemoveDisplayObject(L, group, ignored);
        lua_settop(L, base);
        return PushFailure(L, error);
    }
    lua_remove(L, group); // leave view
    return 1;
}

static int UpdateView(lua_State *L)
{
    RetainedShape *shape = CheckShape(L, 1);
    if (lua_gettop(L) != 2 || !lua_istable(L, 2))
        return luaL_argerror(L, 2, "expected a retained shape view table");
    if (!ViewBelongsToShape(L, 1, 2))
        return luaL_argerror(L, 2, "view belongs to another retained shape");
    bool replaced = false;
    std::string error;
    if (!SyncView(L, *shape, 2, false, replaced, error)) return PushFailure(L, error);
    lua_pushvalue(L, 2);
    lua_pushboolean(L, replaced);
    return 2;
}

static int GarbageCollect(lua_State *L)
{
    RetainedShape *shape = static_cast<RetainedShape *>(luaL_checkudata(L, 1, kShapeMetatable));
    shape->~RetainedShape();
    return 0;
}

static int ToString(lua_State *L)
{
    RetainedShape *shape = CheckShape(L, 1);
    lua_pushfstring(L, "plugin.geometry2d retained shape (%d commands)",
                    static_cast<int>(shape->commands.size()));
    return 1;
}

static int NewShape(lua_State *L)
{
    if (lua_gettop(L) > 1) return luaL_error(L, "path.newShape accepts at most one path table");
    void *memory = lua_newuserdata(L, sizeof(RetainedShape));
    RetainedShape *shape = new (memory) RetainedShape();
    luaL_getmetatable(L, kShapeMetatable);
    lua_setmetatable(L, -2);
    if (lua_gettop(L) >= 2 && !lua_isnoneornil(L, 1))
    {
        if (!ReadPathCommands(L, 1, shape->commands))
            return luaL_argerror(L, 1, "expected a Bezier path command table");
    }
    return 1;
}

} // namespace

void RegisterRetainedShape(lua_State *L)
{
    if (luaL_newmetatable(L, kShapeMetatable))
    {
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, GarbageCollect); lua_setfield(L, -2, "__gc");
        lua_pushcfunction(L, ToString); lua_setfield(L, -2, "__tostring");
        luaL_Reg methods[] = {
            {"moveTo", MoveTo}, {"lineTo", LineTo},
            {"quadraticTo", QuadraticTo}, {"cubicTo", CubicTo},
            {"close", Close}, {"clear", Clear},
            {"setCommand", SetCommand}, {"removeCommand", RemoveCommand},
            {"commandCount", CommandCount}, {"fill", Fill},
            {"fillJoin", FillJoin}, {"strokeWidth", StrokeWidth},
            {"strokeFill", StrokeFill}, {"strokeJoin", StrokeJoin},
            {"strokeCap", StrokeCap}, {"strokeDash", StrokeDash},
            {"configure", Configure}, {"newView", NewView},
            {"updateView", UpdateView}, {nullptr, nullptr}
        };
        luaL_register(L, nullptr, methods);
    }
    lua_pop(L, 1);
    lua_pushcfunction(L, NewShape);
    lua_setfield(L, -2, "newShape");
}

} // namespace Geometry2D
