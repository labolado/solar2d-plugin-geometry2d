// Lua bindings for Clipper2 2.x (Boost Software License 1.0).
// Triangulation is intentionally excluded: mesh triangulation remains the
// responsibility of the plugin's established earcut module.

#include "geometry2d_lua.h"

#include "clipper2/clipper.core.h"
#include "clipper2/clipper.engine.h"
#include "clipper2/clipper.h"
#include "clipper2/clipper.minkowski.h"
#include "clipper2/clipper.offset.h"
#include "clipper2/clipper.version.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <string>
#include <vector>

namespace Geometry2D {
namespace {

using namespace Clipper2Lib;

enum ClipperOption : uint32_t {
    CFillRule = 1u << 0,
    CPrecision = 1u << 1,
    CPreserveCollinear = 1u << 2,
    CReverseSolution = 1u << 3,
    COpenSubjects = 1u << 4,
    CPolyTree = 1u << 5,
    CJoinType = 1u << 6,
    CEndType = 1u << 7,
    CMiterLimit = 1u << 8,
    CArcTolerance = 1u << 9,
    CClosed = 1u << 10,
    CSteps = 1u << 11,
};

struct Options {
    FillRule fillRule = FillRule::NonZero;
    int precision = 2;
    bool preserveCollinear = true;
    bool reverseSolution = false;
    bool polyTree = false;
    JoinType joinType = JoinType::Miter;
    EndType endType = EndType::Polygon;
    double miterLimit = 2.0;
    double arcTolerance = 0.0;
    bool closed = true;
    size_t steps = 0;
    PathsD openSubjects;
};

struct OptionDefinition { const char *name; uint32_t flag; };

static const OptionDefinition kOptions[] = {
    {"fillRule", CFillRule}, {"precision", CPrecision},
    {"preserveCollinear", CPreserveCollinear},
    {"reverseSolution", CReverseSolution}, {"openSubjects", COpenSubjects},
    {"polyTree", CPolyTree}, {"joinType", CJoinType}, {"endType", CEndType},
    {"miterLimit", CMiterLimit}, {"arcTolerance", CArcTolerance},
    {"closed", CClosed}, {"steps", CSteps},
};

static void CheckArgCount(lua_State *L, int minimum, int maximum,
                          const char *context)
{
    int count = lua_gettop(L);
    if (count < minimum || count > maximum)
    {
        if (minimum == maximum)
            luaL_error(L, "%s expects exactly %d arguments", context, minimum);
        luaL_error(L, "%s expects %d through %d arguments", context,
                   minimum, maximum);
    }
}

static void CheckDenseArray(lua_State *L, int index, size_t count,
                            const char *context)
{
    index = CoronaLuaNormalize(L, index);
    lua_pushnil(L);
    while (lua_next(L, index) != 0)
    {
        if (lua_type(L, -2) != LUA_TNUMBER)
            luaL_error(L, "%s must contain only array entries", context);
        lua_Number key = lua_tonumber(L, -2);
        if (!std::isfinite(static_cast<double>(key)) || key < 1.0 ||
            std::floor(static_cast<double>(key)) != key ||
            key > static_cast<lua_Number>(count))
            luaL_error(L, "%s must be a dense array", context);
        lua_pop(L, 1);
    }
}

static double CheckFinite(lua_State *L, int index, const char *context)
{
    if (lua_type(L, index) != LUA_TNUMBER)
        luaL_error(L, "%s must be a number", context);
    double value = static_cast<double>(lua_tonumber(L, index));
    if (!std::isfinite(value) || std::fabs(value) > std::numeric_limits<float>::max())
        luaL_error(L, "%s must be a finite float", context);
    return value;
}

static PathD ReadPath(lua_State *L, int index, size_t minPoints,
                      const char *context)
{
    if (!lua_istable(L, index)) luaL_error(L, "%s must be a flat coordinate table", context);
    index = CoronaLuaNormalize(L, index);
    size_t count = lua_objlen(L, index);
    if ((count & 1u) != 0 || count / 2 < minPoints)
        luaL_error(L, "%s must contain at least %d points and an even number of coordinates",
                   context, static_cast<int>(minPoints));
    CheckDenseArray(L, index, count, context);
    PathD path;
    path.reserve(count / 2);
    for (size_t i = 1; i <= count; i += 2)
    {
        lua_rawgeti(L, index, static_cast<int>(i));
        lua_rawgeti(L, index, static_cast<int>(i + 1));
        double x = CheckFinite(L, -2, context);
        double y = CheckFinite(L, -1, context);
        lua_pop(L, 2);
        path.emplace_back(x, y);
    }
    return path;
}

static PathsD ReadPaths(lua_State *L, int index, size_t minPoints,
                        const char *context)
{
    if (!lua_istable(L, index)) luaL_error(L, "%s must be an array of paths", context);
    index = CoronaLuaNormalize(L, index);
    size_t count = lua_objlen(L, index);
    CheckDenseArray(L, index, count, context);
    PathsD paths;
    paths.reserve(count);
    for (size_t i = 1; i <= count; ++i)
    {
        lua_rawgeti(L, index, static_cast<int>(i));
        paths.push_back(ReadPath(L, -1, minPoints, context));
        lua_pop(L, 1);
    }
    return paths;
}

static std::vector<std::vector<double>> ReadOffsetDeltas(lua_State *L, int index,
                                                          const PathsD &paths)
{
    if (!lua_istable(L, index))
        luaL_error(L, "delta must be a finite number or an array of per-path delta arrays");
    index = CoronaLuaNormalize(L, index);
    size_t pathCount = lua_objlen(L, index);
    if (pathCount != paths.size())
        luaL_error(L, "variable delta array count must match the path count");
    CheckDenseArray(L, index, pathCount, "variable delta paths");
    std::vector<std::vector<double>> result(pathCount);
    for (size_t pathIndex = 0; pathIndex < pathCount; ++pathIndex)
    {
        lua_rawgeti(L, index, static_cast<int>(pathIndex + 1));
        if (!lua_istable(L, -1))
            luaL_error(L, "variable delta path #%d must be an array",
                       static_cast<int>(pathIndex + 1));
        int deltas = CoronaLuaNormalize(L, -1);
        size_t count = lua_objlen(L, deltas);
        if (count != paths[pathIndex].size())
            luaL_error(L, "variable delta path #%d must contain one value per vertex",
                       static_cast<int>(pathIndex + 1));
        CheckDenseArray(L, deltas, count, "variable delta path");
        result[pathIndex].reserve(count);
        for (size_t i = 1; i <= count; ++i)
        {
            lua_rawgeti(L, deltas, static_cast<int>(i));
            result[pathIndex].push_back(CheckFinite(L, -1, "variable delta"));
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    return result;
}

static PointD ReadPoint(lua_State *L, int index, const char *context)
{
    PathD point = ReadPath(L, index, 1, context);
    if (point.size() != 1) luaL_error(L, "%s must contain exactly {x, y}", context);
    return point.front();
}

static void PushPath(lua_State *L, const PathD &path)
{
    lua_createtable(L, static_cast<int>(path.size() * 2), 0);
    int output = 1;
    for (const PointD &point : path)
    {
        lua_pushnumber(L, point.x); lua_rawseti(L, -2, output++);
        lua_pushnumber(L, point.y); lua_rawseti(L, -2, output++);
    }
}

static void PushPaths(lua_State *L, const PathsD &paths)
{
    lua_createtable(L, static_cast<int>(paths.size()), 0);
    for (size_t i = 0; i < paths.size(); ++i)
    {
        PushPath(L, paths[i]);
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
}

static PathD ScalePathToD(const Path64 &path, double inverseScale)
{
    int errorCode = 0;
    return ScalePath<double, int64_t>(path, inverseScale, errorCode);
}

static void PushPolyPath(lua_State *L, const PolyPathD &node)
{
    lua_createtable(L, 0, 3);
    PushPath(L, node.Polygon()); lua_setfield(L, -2, "polygon");
    lua_pushboolean(L, node.IsHole()); lua_setfield(L, -2, "isHole");
    lua_createtable(L, static_cast<int>(node.Count()), 0);
    for (size_t i = 0; i < node.Count(); ++i)
    {
        PushPolyPath(L, *node.Child(i));
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    lua_setfield(L, -2, "children");
}

static void PushPolyTree(lua_State *L, const PolyTreeD &tree)
{
    lua_createtable(L, static_cast<int>(tree.Count()), 0);
    for (size_t i = 0; i < tree.Count(); ++i)
    {
        PushPolyPath(L, *tree.Child(i));
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
}

static void PushPolyPath64(lua_State *L, const PolyPath64 &node, double inverseScale)
{
    lua_createtable(L, 0, 3);
    PushPath(L, ScalePathToD(node.Polygon(), inverseScale));
    lua_setfield(L, -2, "polygon");
    lua_pushboolean(L, node.IsHole()); lua_setfield(L, -2, "isHole");
    lua_createtable(L, static_cast<int>(node.Count()), 0);
    for (size_t i = 0; i < node.Count(); ++i)
    {
        PushPolyPath64(L, *node.Child(i), inverseScale);
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    lua_setfield(L, -2, "children");
}

static void PushPolyTree64(lua_State *L, const PolyTree64 &tree, double inverseScale)
{
    lua_createtable(L, static_cast<int>(tree.Count()), 0);
    for (size_t i = 0; i < tree.Count(); ++i)
    {
        PushPolyPath64(L, *tree.Child(i), inverseScale);
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
}

static uint32_t OptionFlag(const char *name)
{
    for (const OptionDefinition &option : kOptions)
        if (std::strcmp(option.name, name) == 0) return option.flag;
    return 0;
}

static const char *StringField(lua_State *L, int options, const char *name)
{
    lua_getfield(L, options, name);
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return nullptr; }
    if (lua_type(L, -1) != LUA_TSTRING) luaL_error(L, "Option '%s' must be a string", name);
    const char *value = lua_tostring(L, -1);
    lua_pop(L, 1);
    return value;
}

static bool BooleanField(lua_State *L, int options, const char *name, bool fallback)
{
    lua_getfield(L, options, name);
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return fallback; }
    if (!lua_isboolean(L, -1)) luaL_error(L, "Option '%s' must be a boolean", name);
    bool value = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return value;
}

static double NumberField(lua_State *L, int options, const char *name,
                          double fallback, bool positive, bool allowZero)
{
    lua_getfield(L, options, name);
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return fallback; }
    double value = CheckFinite(L, -1, name);
    lua_pop(L, 1);
    if (positive && (allowZero ? value < 0.0 : value <= 0.0))
        luaL_error(L, "Option '%s' must be %s", name,
                   allowZero ? "non-negative" : "positive");
    return value;
}

static Options ReadOptions(lua_State *L, int index, uint32_t allowed,
                           const char *context)
{
    Options result;
    if (lua_isnoneornil(L, index)) return result;
    if (!lua_istable(L, index)) luaL_argerror(L, index, "expected an options table");
    index = CoronaLuaNormalize(L, index);
    lua_pushnil(L);
    while (lua_next(L, index) != 0)
    {
        if (lua_type(L, -2) != LUA_TSTRING)
            luaL_error(L, "%s option keys must be strings", context);
        const char *name = lua_tostring(L, -2);
        uint32_t flag = OptionFlag(name);
        if (flag == 0 || (allowed & flag) == 0)
            luaL_error(L, "Unknown %s option '%s'", context, name);
        lua_pop(L, 1);
    }
    if (allowed & CFillRule)
    {
        const char *value = StringField(L, index, "fillRule");
        if (value && std::strcmp(value, "evenOdd") == 0) result.fillRule = FillRule::EvenOdd;
        else if (value && std::strcmp(value, "nonZero") == 0) result.fillRule = FillRule::NonZero;
        else if (value && std::strcmp(value, "positive") == 0) result.fillRule = FillRule::Positive;
        else if (value && std::strcmp(value, "negative") == 0) result.fillRule = FillRule::Negative;
        else if (value) luaL_error(L, "Invalid fillRule '%s'", value);
    }
    if (allowed & CPrecision)
    {
        lua_getfield(L, index, "precision");
        if (!lua_isnil(L, -1))
        {
            double value = CheckFinite(L, -1, "precision");
            if (std::floor(value) != value || value < -8.0 || value > 8.0)
                luaL_error(L, "Option 'precision' must be an integer from -8 through 8");
            result.precision = static_cast<int>(value);
        }
        lua_pop(L, 1);
    }
    if (allowed & CPreserveCollinear)
        result.preserveCollinear = BooleanField(L, index, "preserveCollinear", true);
    if (allowed & CReverseSolution)
        result.reverseSolution = BooleanField(L, index, "reverseSolution", false);
    if (allowed & CPolyTree)
        result.polyTree = BooleanField(L, index, "polyTree", false);
    if (allowed & COpenSubjects)
    {
        lua_getfield(L, index, "openSubjects");
        if (!lua_isnil(L, -1)) result.openSubjects = ReadPaths(L, -1, 2, "openSubjects");
        lua_pop(L, 1);
    }
    if (allowed & CJoinType)
    {
        const char *value = StringField(L, index, "joinType");
        if (value && std::strcmp(value, "square") == 0) result.joinType = JoinType::Square;
        else if (value && std::strcmp(value, "bevel") == 0) result.joinType = JoinType::Bevel;
        else if (value && std::strcmp(value, "round") == 0) result.joinType = JoinType::Round;
        else if (value && std::strcmp(value, "miter") == 0) result.joinType = JoinType::Miter;
        else if (value) luaL_error(L, "Invalid joinType '%s'", value);
    }
    if (allowed & CEndType)
    {
        const char *value = StringField(L, index, "endType");
        if (value && std::strcmp(value, "polygon") == 0) result.endType = EndType::Polygon;
        else if (value && std::strcmp(value, "joined") == 0) result.endType = EndType::Joined;
        else if (value && std::strcmp(value, "butt") == 0) result.endType = EndType::Butt;
        else if (value && std::strcmp(value, "square") == 0) result.endType = EndType::Square;
        else if (value && std::strcmp(value, "round") == 0) result.endType = EndType::Round;
        else if (value) luaL_error(L, "Invalid endType '%s'", value);
    }
    if (allowed & CMiterLimit)
        result.miterLimit = NumberField(L, index, "miterLimit", 2.0, true, false);
    if (allowed & CArcTolerance)
        result.arcTolerance = NumberField(L, index, "arcTolerance", 0.0, true, true);
    if (allowed & CClosed)
        result.closed = BooleanField(L, index, "closed", true);
    if (allowed & CSteps)
    {
        lua_getfield(L, index, "steps");
        if (!lua_isnil(L, -1))
        {
            double value = CheckFinite(L, -1, "steps");
            if (std::floor(value) != value || value < 0.0 ||
                value > static_cast<double>(std::numeric_limits<int>::max()))
                luaL_error(L, "Option 'steps' must be a non-negative integer");
            result.steps = static_cast<size_t>(value);
        }
        lua_pop(L, 1);
    }
    return result;
}

static ClipType ReadClipType(lua_State *L, int index)
{
    const char *value = luaL_checkstring(L, index);
    if (std::strcmp(value, "intersection") == 0) return ClipType::Intersection;
    if (std::strcmp(value, "union") == 0) return ClipType::Union;
    if (std::strcmp(value, "difference") == 0) return ClipType::Difference;
    if (std::strcmp(value, "xor") == 0) return ClipType::Xor;
    luaL_argerror(L, index, "expected 'intersection', 'union', 'difference', or 'xor'");
    return ClipType::NoClip;
}

static int PushException(lua_State *L, const char *context,
                         const std::exception &exception)
{
    std::string message = std::string(context) + ": " + exception.what();
    return PushGeometryFailure(L, message.c_str());
}

static bool ExecuteBoolean(const PathsD &subjects, const PathsD &clips,
                           ClipType clipType, const Options &options,
                           PathsD &closed, PathsD &open, PolyTreeD *tree,
                           std::string &error)
{
    ClipperD clipper(options.precision);
    clipper.PreserveCollinear(options.preserveCollinear);
    clipper.ReverseSolution(options.reverseSolution);
    clipper.AddSubject(subjects);
    clipper.AddOpenSubject(options.openSubjects);
    clipper.AddClip(clips);
    bool ok = tree ? clipper.Execute(clipType, options.fillRule, *tree, open) :
                     clipper.Execute(clipType, options.fillRule, closed, open);
    if (!ok || clipper.ErrorCode() != 0)
    {
        error = "Clipper2 boolean operation failed";
        return false;
    }
    return true;
}

static int BooleanOp(lua_State *L)
{
    CheckArgCount(L, 2, 4, "clipper2.booleanOp");
    ClipType clipType = ReadClipType(L, 1);
    PathsD subjects = ReadPaths(L, 2, 3, "subjects");
    PathsD clips = lua_isnoneornil(L, 3) ? PathsD() : ReadPaths(L, 3, 3, "clips");
    Options options = ReadOptions(L, 4, CFillRule | CPrecision | CPreserveCollinear |
        CReverseSolution | COpenSubjects | CPolyTree, "clipper2.booleanOp");
    try
    {
        PathsD closed, open;
        PolyTreeD tree;
        std::string error;
        if (!ExecuteBoolean(subjects, clips, clipType, options, closed, open,
                            options.polyTree ? &tree : nullptr, error))
            return PushGeometryFailure(L, error.c_str());
        lua_createtable(L, 0, 2);
        if (options.polyTree) { PushPolyTree(L, tree); lua_setfield(L, -2, "tree"); }
        else { PushPaths(L, closed); lua_setfield(L, -2, "closed"); }
        PushPaths(L, open); lua_setfield(L, -2, "open");
        return 1;
    }
    catch (const std::exception &exception)
    { return PushException(L, "Clipper2 boolean operation failed", exception); }
}

static int SimpleBoolean(lua_State *L, ClipType clipType, bool needsClips,
                         const char *context)
{
    CheckArgCount(L, needsClips ? 2 : 1, needsClips ? 3 : 2, context);
    PathsD subjects = ReadPaths(L, 1, 3, "subjects");
    PathsD clips;
    int optionIndex = 2;
    if (needsClips)
    {
        clips = ReadPaths(L, 2, 3, "clips");
        optionIndex = 3;
    }
    Options options = ReadOptions(L, optionIndex, CFillRule | CPrecision |
        CPreserveCollinear | CReverseSolution, context);
    try
    {
        PathsD closed, open;
        std::string error;
        if (!ExecuteBoolean(subjects, clips, clipType, options, closed, open, nullptr, error))
            return PushGeometryFailure(L, error.c_str());
        PushPaths(L, closed);
        return 1;
    }
    catch (const std::exception &exception) { return PushException(L, context, exception); }
}

static int Intersection(lua_State *L)
{ return SimpleBoolean(L, ClipType::Intersection, true, "clipper2.intersection"); }
static int Union(lua_State *L)
{ return SimpleBoolean(L, ClipType::Union, false, "clipper2.union"); }
static int Difference(lua_State *L)
{ return SimpleBoolean(L, ClipType::Difference, true, "clipper2.difference"); }
static int Xor(lua_State *L)
{ return SimpleBoolean(L, ClipType::Xor, true, "clipper2.xor"); }

static int Inflate(lua_State *L)
{
    CheckArgCount(L, 2, 3, "clipper2.inflate");
    PathsD paths = ReadPaths(L, 1, 2, "paths");
    bool variable = lua_istable(L, 2);
    double delta = variable ? 0.0 : CheckFinite(L, 2, "delta");
    std::vector<std::vector<double>> deltas = variable ?
        ReadOffsetDeltas(L, 2, paths) : std::vector<std::vector<double>>();
    Options options = ReadOptions(L, 3, CPrecision | CJoinType | CEndType |
        CMiterLimit | CArcTolerance | CPreserveCollinear | CReverseSolution |
        CPolyTree, "clipper2.inflate");
    try
    {
        const double scale = std::pow(10.0, options.precision);
        int errorCode = 0;
        Paths64 scaled = ScalePaths<int64_t, double>(paths, scale, errorCode);
        if (errorCode != 0) return PushGeometryFailure(L, "Clipper2 inflate input exceeds its precision range");
        ClipperOffset offset(options.miterLimit, options.arcTolerance * scale,
                             options.preserveCollinear, options.reverseSolution);
        offset.AddPaths(scaled, options.joinType, options.endType);
        if (variable && options.polyTree)
            return luaL_error(L, "clipper2.inflate variable deltas do not support polyTree=true");
        if (options.polyTree)
        {
            PolyTree64 tree;
            offset.Execute(delta * scale, tree);
            if (offset.ErrorCode() != 0) return PushGeometryFailure(L, "Clipper2 inflate failed");
            PushPolyTree64(L, tree, 1.0 / scale);
        }
        else
        {
            Paths64 result;
            if (variable)
            {
                const Path64 *currentPath = nullptr;
                size_t currentIndex = static_cast<size_t>(-1);
                bool callbackError = false;
                offset.Execute([&](const Path64 &path, const PathD &, size_t vertex,
                                   size_t) -> double {
                    if (&path != currentPath)
                    {
                        currentPath = &path;
                        ++currentIndex;
                    }
                    if (currentIndex >= deltas.size() ||
                        vertex >= deltas[currentIndex].size())
                    {
                        callbackError = true;
                        return 0.0;
                    }
                    return deltas[currentIndex][vertex] * scale;
                }, result);
                if (callbackError)
                    return PushGeometryFailure(L, "Clipper2 variable offset callback order was invalid");
            }
            else offset.Execute(delta * scale, result);
            if (offset.ErrorCode() != 0) return PushGeometryFailure(L, "Clipper2 inflate failed");
            PathsD output = ScalePaths<double, int64_t>(result, 1.0 / scale, errorCode);
            PushPaths(L, output);
        }
        return 1;
    }
    catch (const std::exception &exception)
    { return PushException(L, "Clipper2 inflate failed", exception); }
}

static RectD ReadRect(lua_State *L, int index)
{
    PathD values = ReadPath(L, index, 2, "rect");
    if (values.size() != 2) luaL_error(L, "rect must be {left, top, right, bottom}");
    RectD rect(values[0].x, values[0].y, values[1].x, values[1].y);
    if (rect.right < rect.left || rect.bottom < rect.top)
        luaL_error(L, "rect must satisfy left <= right and top <= bottom");
    return rect;
}

static int DoRectClip(lua_State *L, bool lines)
{
    CheckArgCount(L, 2, 3, lines ? "clipper2.rectClipLines" : "clipper2.rectClip");
    RectD rect = ReadRect(L, 1);
    PathsD paths = ReadPaths(L, 2, lines ? 2 : 3, lines ? "lines" : "paths");
    Options options = ReadOptions(L, 3, CPrecision,
        lines ? "clipper2.rectClipLines" : "clipper2.rectClip");
    try
    {
        PathsD result = lines ? RectClipLines(rect, paths, options.precision) :
                                RectClip(rect, paths, options.precision);
        PushPaths(L, result);
        return 1;
    }
    catch (const std::exception &exception)
    { return PushException(L, "Clipper2 rectangle clipping failed", exception); }
}

static int RectClipBinding(lua_State *L) { return DoRectClip(L, false); }
static int RectClipLinesBinding(lua_State *L) { return DoRectClip(L, true); }

static int DoMinkowski(lua_State *L, bool sum)
{
    CheckArgCount(L, 2, 3, sum ? "clipper2.minkowskiSum" : "clipper2.minkowskiDiff");
    PathD pattern = ReadPath(L, 1, 1, "pattern");
    PathD path = ReadPath(L, 2, 1, "path");
    Options options = ReadOptions(L, 3, CPrecision | CClosed,
        sum ? "clipper2.minkowskiSum" : "clipper2.minkowskiDiff");
    try
    {
        PathsD result = sum ? MinkowskiSum(pattern, path, options.closed, options.precision) :
                              MinkowskiDiff(pattern, path, options.closed, options.precision);
        PushPaths(L, result);
        return 1;
    }
    catch (const std::exception &exception)
    { return PushException(L, "Clipper2 Minkowski operation failed", exception); }
}

static int MinkowskiSumBinding(lua_State *L) { return DoMinkowski(L, true); }
static int MinkowskiDiffBinding(lua_State *L) { return DoMinkowski(L, false); }

static int Simplify(lua_State *L)
{
    CheckArgCount(L, 2, 3, "clipper2.simplify");
    PathsD paths = ReadPaths(L, 1, 1, "paths");
    double epsilon = CheckFinite(L, 2, "epsilon");
    if (epsilon < 0.0) return luaL_argerror(L, 2, "epsilon must be non-negative");
    Options options = ReadOptions(L, 3, CClosed, "clipper2.simplify");
    PushPaths(L, SimplifyPaths(paths, epsilon, options.closed));
    return 1;
}

static int RamerDouglasPeuckerBinding(lua_State *L)
{
    CheckArgCount(L, 2, 2, "clipper2.ramerDouglasPeucker");
    PathsD paths = ReadPaths(L, 1, 1, "paths");
    double epsilon = CheckFinite(L, 2, "epsilon");
    if (epsilon < 0.0) return luaL_argerror(L, 2, "epsilon must be non-negative");
    PushPaths(L, RamerDouglasPeucker(paths, epsilon));
    return 1;
}

static int TrimCollinearBinding(lua_State *L)
{
    CheckArgCount(L, 1, 2, "clipper2.trimCollinear");
    PathD path = ReadPath(L, 1, 1, "path");
    Options options = ReadOptions(L, 2, CPrecision | CClosed, "clipper2.trimCollinear");
    try
    {
        PushPath(L, TrimCollinear(path, options.precision, !options.closed));
        return 1;
    }
    catch (const std::exception &exception)
    { return PushException(L, "Clipper2 trimCollinear failed", exception); }
}

static int StripDuplicatesBinding(lua_State *L)
{
    CheckArgCount(L, 1, 2, "clipper2.stripDuplicates");
    PathD path = ReadPath(L, 1, 0, "path");
    Options options = ReadOptions(L, 2, CClosed, "clipper2.stripDuplicates");
    StripDuplicates(path, options.closed);
    PushPath(L, path);
    return 1;
}

static int StripNearEqualBinding(lua_State *L)
{
    CheckArgCount(L, 2, 3, "clipper2.stripNearEqual");
    PathD path = ReadPath(L, 1, 0, "path");
    double maxDistance = CheckFinite(L, 2, "maxDistance");
    if (maxDistance < 0.0) return luaL_argerror(L, 2, "maxDistance must be non-negative");
    Options options = ReadOptions(L, 3, CClosed, "clipper2.stripNearEqual");
    PushPath(L, StripNearEqual(path, maxDistance * maxDistance, options.closed));
    return 1;
}

static int Translate(lua_State *L)
{
    CheckArgCount(L, 3, 3, "clipper2.translate");
    PathsD paths = ReadPaths(L, 1, 0, "paths");
    double dx = CheckFinite(L, 2, "dx"), dy = CheckFinite(L, 3, "dy");
    PushPaths(L, TranslatePaths(paths, dx, dy));
    return 1;
}

static int Reverse(lua_State *L)
{
    CheckArgCount(L, 1, 1, "clipper2.reverse");
    PathsD paths = ReadPaths(L, 1, 0, "paths");
    for (PathD &path : paths) std::reverse(path.begin(), path.end());
    PushPaths(L, paths);
    return 1;
}

static int AreaBinding(lua_State *L)
{
    CheckArgCount(L, 1, 1, "clipper2.area");
    PathsD paths = ReadPaths(L, 1, 0, "paths");
    lua_pushnumber(L, Area(paths));
    return 1;
}

static int IsPositiveBinding(lua_State *L)
{
    CheckArgCount(L, 1, 1, "clipper2.isPositive");
    PathD path = ReadPath(L, 1, 3, "path");
    lua_pushboolean(L, IsPositive(path));
    return 1;
}

static int LengthBinding(lua_State *L)
{
    CheckArgCount(L, 1, 2, "clipper2.length");
    PathD path = ReadPath(L, 1, 0, "path");
    bool closed = false;
    if (!lua_isnoneornil(L, 2))
    {
        if (!lua_isboolean(L, 2)) return luaL_argerror(L, 2, "closed must be a boolean");
        closed = lua_toboolean(L, 2) != 0;
    }
    lua_pushnumber(L, Length(path, closed));
    return 1;
}

static int PointInPolygonBinding(lua_State *L)
{
    CheckArgCount(L, 2, 2, "clipper2.pointInPolygon");
    PointD point = ReadPoint(L, 1, "point");
    PathD path = ReadPath(L, 2, 3, "polygon");
    PointInPolygonResult result = PointInPolygon(point, path);
    lua_pushstring(L, result == PointInPolygonResult::IsInside ? "inside" :
                      result == PointInPolygonResult::IsOutside ? "outside" : "on");
    return 1;
}

static int GetBoundsBinding(lua_State *L)
{
    CheckArgCount(L, 1, 1, "clipper2.getBounds");
    PathsD paths = ReadPaths(L, 1, 0, "paths");
    RectD bounds = GetBounds(paths);
    bool hasPoint = false;
    for (const PathD &path : paths) if (!path.empty()) { hasPoint = true; break; }
    if (!hasPoint) { lua_pushnil(L); return 1; }
    lua_createtable(L, 4, 4);
    const double values[] = {bounds.left, bounds.top, bounds.right, bounds.bottom};
    const char *names[] = {"left", "top", "right", "bottom"};
    for (int i = 0; i < 4; ++i)
    {
        lua_pushnumber(L, values[i]); lua_rawseti(L, -2, i + 1);
        lua_pushnumber(L, values[i]); lua_setfield(L, -2, names[i]);
    }
    return 1;
}

static int PathContainsBinding(lua_State *L)
{
    CheckArgCount(L, 2, 2, "clipper2.pathContains");
    PathD inner = ReadPath(L, 1, 3, "inner");
    PathD outer = ReadPath(L, 2, 3, "outer");
    lua_pushboolean(L, Path2ContainsPath1(inner, outer));
    return 1;
}

static int ClosestPointBinding(lua_State *L)
{
    CheckArgCount(L, 3, 3, "clipper2.closestPoint");
    PointD point = ReadPoint(L, 1, "point");
    PointD a = ReadPoint(L, 2, "segment start");
    PointD b = ReadPoint(L, 3, "segment end");
    PointD result = GetClosestPointOnSegment(point, a, b);
    lua_createtable(L, 2, 0);
    lua_pushnumber(L, result.x); lua_rawseti(L, -2, 1);
    lua_pushnumber(L, result.y); lua_rawseti(L, -2, 2);
    return 1;
}

static int EllipseBinding(lua_State *L)
{
    CheckArgCount(L, 3, 5, "clipper2.ellipse");
    double cx = CheckFinite(L, 1, "centerX");
    double cy = CheckFinite(L, 2, "centerY");
    double rx = CheckFinite(L, 3, "radiusX");
    bool optionsInFourth = lua_istable(L, 4);
    double ry = lua_isnoneornil(L, 4) || optionsInFourth ?
        rx : CheckFinite(L, 4, "radiusY");
    if (rx <= 0.0 || ry <= 0.0) return luaL_error(L, "ellipse radii must be positive");
    if (optionsInFourth && !lua_isnoneornil(L, 5))
        return luaL_error(L, "clipper2.ellipse accepts only one options table");
    Options options = ReadOptions(L, optionsInFourth ? 4 : 5,
                                  CSteps, "clipper2.ellipse");
    PushPath(L, Ellipse(PointD(cx, cy), rx, ry, options.steps));
    return 1;
}

} // namespace

void RegisterClipper2(lua_State *L)
{
    luaL_Reg functions[] = {
        {"booleanOp", BooleanOp}, {"intersection", Intersection},
        {"union", Union}, {"difference", Difference}, {"xor", Xor},
        {"inflate", Inflate}, {"rectClip", RectClipBinding},
        {"rectClipLines", RectClipLinesBinding},
        {"minkowskiSum", MinkowskiSumBinding},
        {"minkowskiDiff", MinkowskiDiffBinding}, {"simplify", Simplify},
        {"ramerDouglasPeucker", RamerDouglasPeuckerBinding},
        {"trimCollinear", TrimCollinearBinding},
        {"stripDuplicates", StripDuplicatesBinding},
        {"stripNearEqual", StripNearEqualBinding}, {"translate", Translate},
        {"reverse", Reverse}, {"area", AreaBinding},
        {"isPositive", IsPositiveBinding}, {"length", LengthBinding},
        {"pointInPolygon", PointInPolygonBinding}, {"getBounds", GetBoundsBinding},
        {"pathContains", PathContainsBinding}, {"closestPoint", ClosestPointBinding},
        {"ellipse", EllipseBinding}, {nullptr, nullptr}
    };
    luaL_register(L, nullptr, functions);
    lua_pushstring(L, CLIPPER2_VERSION);
    lua_setfield(L, -2, "version");
}

} // namespace Geometry2D
