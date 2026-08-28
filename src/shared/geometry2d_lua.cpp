#include "geometry2d_lua.h"

#include "CoronaMemory.h"

#include <cstdint>
#include <cmath>
#include <cstring>
#include <limits>

namespace Geometry2D {

bool IsFlatPolygonTable(lua_State *L, int arg)
{
    if (!lua_istable(L, arg)) return false;
    size_t n = lua_objlen(L, arg);
    if (n == 0) return true;
    lua_rawgeti(L, arg, 1);
    bool result = lua_type(L, -1) == LUA_TNUMBER;
    lua_pop(L, 1);
    return result;
}

static bool ReadPolygonFromTable(lua_State *L, int arg, TPPLPoly &poly, bool isHole)
{
    arg = CoronaLuaNormalize(L, arg);
    size_t n = lua_objlen(L, arg);
    if (n < 6 || (n % 2) != 0) return false;

    int count = static_cast<int>(n / 2);
    poly.Init(count);
    poly.SetHole(isHole);
    for (int i = 0; i < count; ++i)
    {
        lua_rawgeti(L, arg, static_cast<int>(i * 2 + 1));
        lua_rawgeti(L, arg, static_cast<int>(i * 2 + 2));
        if (lua_type(L, -2) != LUA_TNUMBER || lua_type(L, -1) != LUA_TNUMBER)
            luaL_error(L, "Polygon coordinates must be numbers");
        lua_Number x = lua_tonumber(L, -2), y = lua_tonumber(L, -1);
        if (!std::isfinite(static_cast<double>(x)) || !std::isfinite(static_cast<double>(y)))
            luaL_error(L, "Polygon coordinates must be finite");
        poly[i].x = static_cast<tppl_float>(x);
        poly[i].y = static_cast<tppl_float>(y);
        poly[i].id = static_cast<int>(i);
        lua_pop(L, 2);
    }
    return true;
}

enum class ScalarType { Float32, Float64, Int32 };

static bool ReadPolygonFromBytes(lua_State *L, int arg, TPPLPoly &poly, bool isHole,
                                 ScalarType type)
{
    CoronaMemoryAcquireState memory = {};
    if (!CoronaMemoryAcquireInterface(L, arg, &memory) ||
        !CORONA_MEMORY_HAS(memory, getReadableBytes))
        return false;

    size_t byteCount = CORONA_MEMORY_GET(memory, ByteCount);
    const void *readableBytes = CORONA_MEMORY_GET(memory, ReadableBytes);
    if (!readableBytes || byteCount == 0) return false;

    size_t scalarSize = type == ScalarType::Float64 ? sizeof(double) :
        (type == ScalarType::Float32 ? sizeof(float) : sizeof(int32_t));
    size_t coordCount = byteCount / scalarSize;
    if (byteCount % scalarSize != 0 || coordCount < 6 || coordCount % 2 != 0)
        return false;
    int count = static_cast<int>(coordCount / 2);
    poly.Init(count);
    poly.SetHole(isHole);
    for (int i = 0; i < count; ++i)
    {
        double x = 0.0, y = 0.0;
        const char *bytes = static_cast<const char *>(readableBytes) +
            static_cast<size_t>(i * 2) * scalarSize;
        if (type == ScalarType::Float64)
        {
            double values[2];
            std::memcpy(values, bytes, sizeof(values));
            x = values[0]; y = values[1];
        }
        else if (type == ScalarType::Float32)
        {
            float values[2];
            std::memcpy(values, bytes, sizeof(values));
            x = values[0]; y = values[1];
        }
        else
        {
            int32_t values[2];
            std::memcpy(values, bytes, sizeof(values));
            x = values[0]; y = values[1];
        }
        if (!std::isfinite(x) || !std::isfinite(y))
            luaL_error(L, "Packed polygon coordinates must be finite");
        poly[i].x = static_cast<tppl_float>(x);
        poly[i].y = static_cast<tppl_float>(y);
        poly[i].id = static_cast<int>(i);
    }
    return true;
}

static void ValidateDescriptorFields(lua_State *L, int arg, bool bytes)
{
    arg = CoronaLuaNormalize(L, arg);
    lua_pushnil(L);
    while (lua_next(L, arg) != 0)
    {
        if (lua_type(L, -2) != LUA_TSTRING)
            luaL_error(L, "Polygon descriptor keys must be strings");
        const char *key = lua_tostring(L, -2);
        bool allowed = std::strcmp(key, "hole") == 0 ||
            (bytes && (std::strcmp(key, "bytes") == 0 || std::strcmp(key, "type") == 0)) ||
            (!bytes && std::strcmp(key, "points") == 0);
        if (!allowed) luaL_error(L, "Unknown polygon descriptor field '%s'", key);
        lua_pop(L, 1);
    }
}

static bool ReadPolygonFromBytesDescriptor(lua_State *L, int arg, TPPLPoly &poly)
{
    arg = CoronaLuaNormalize(L, arg);
    ValidateDescriptorFields(L, arg, true);

    lua_getfield(L, arg, "hole");
    if (!lua_isnil(L, -1) && !lua_isboolean(L, -1))
        luaL_error(L, "Polygon descriptor 'hole' must be a boolean");
    bool hole = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);

    lua_getfield(L, arg, "type");
    if (lua_type(L, -1) != LUA_TSTRING)
        luaL_error(L, "Polygon bytes descriptor requires type='float32', 'float64', or 'int32'");
    const char *name = lua_tostring(L, -1);
    ScalarType type;
    if (std::strcmp(name, "float32") == 0) type = ScalarType::Float32;
    else if (std::strcmp(name, "float64") == 0) type = ScalarType::Float64;
    else if (std::strcmp(name, "int32") == 0) type = ScalarType::Int32;
    else luaL_error(L, "Invalid polygon scalar type '%s'; expected 'float32', 'float64', or 'int32'", name);
    lua_pop(L, 1);

    int top = lua_gettop(L);
    lua_getfield(L, arg, "bytes");
    bool ok = ReadPolygonFromBytes(L, -1, poly, hole, type);
    lua_settop(L, top);
    return ok;
}

bool ReadPolygon(lua_State *L, int arg, TPPLPoly &poly, bool isHole)
{
    arg = CoronaLuaNormalize(L, arg);
    if (!lua_istable(L, arg)) return false;

    lua_getfield(L, arg, "bytes");
    bool hasBytes = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (hasBytes) return ReadPolygonFromBytesDescriptor(L, arg, poly);

    lua_getfield(L, arg, "points");
    if (!lua_isnil(L, -1))
    {
        ValidateDescriptorFields(L, arg, false);
        lua_getfield(L, arg, "hole");
        if (!lua_isnil(L, -1) && !lua_isboolean(L, -1))
            luaL_error(L, "Polygon descriptor 'hole' must be a boolean");
        bool hole = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        bool ok = ReadPolygonFromTable(L, -1, poly, hole);
        lua_pop(L, 1);
        return ok;
    }
    lua_pop(L, 1);
    return ReadPolygonFromTable(L, arg, poly, isHole);
}

bool ReadPolygonList(lua_State *L, int arg, TPPLPolyList &list)
{
    if (!lua_istable(L, arg)) return false;
    arg = CoronaLuaNormalize(L, arg);
    size_t n = lua_objlen(L, arg);
    for (size_t i = 1; i <= n; ++i)
    {
        lua_rawgeti(L, arg, static_cast<int>(i));
        TPPLPoly poly;
        bool ok = ReadPolygon(L, -1, poly);
        lua_pop(L, 1);
        if (!ok) return false;
        list.push_back(poly);
    }
    return true;
}

void PushPolygonList(lua_State *L, TPPLPolyList &list)
{
    lua_createtable(L, static_cast<int>(list.size()), 0);
    int out = 0;
    for (auto &poly : list)
    {
        int count = static_cast<int>(poly.GetNumPoints());
        lua_createtable(L, count * 2, 0);
        for (int i = 0; i < count; ++i)
        {
            lua_pushnumber(L, poly[i].x); lua_rawseti(L, -2, i * 2 + 1);
            lua_pushnumber(L, poly[i].y); lua_rawseti(L, -2, i * 2 + 2);
        }
        lua_rawseti(L, -2, ++out);
    }
}

bool ReadRing(lua_State *L, int arg, Ring &ring)
{
    TPPLPoly poly;
    if (!ReadPolygon(L, arg, poly)) return false;
    int count = static_cast<int>(poly.GetNumPoints());
    ring.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
        ring.push_back({static_cast<double>(poly[i].x), static_cast<double>(poly[i].y)});
    return ring.size() >= 3;
}

bool ReadEarcutPolygon(lua_State *L, int arg, Polygon &poly)
{
    if (!lua_istable(L, arg)) return false;
    arg = CoronaLuaNormalize(L, arg);

    if (lua_istable(L, arg))
    {
        lua_getfield(L, arg, "poly");
        if (!lua_isnil(L, -1))
        {
            Ring outer;
            bool ok = ReadRing(L, -1, outer);
            lua_pop(L, 1);
            if (!ok) return false;
            poly.push_back(std::move(outer));

            lua_getfield(L, arg, "holes");
            if (lua_istable(L, -1))
            {
                size_t count = lua_objlen(L, -1);
                for (size_t i = 1; i <= count; ++i)
                {
                    lua_rawgeti(L, -1, static_cast<int>(i));
                    Ring hole;
                    ok = ReadRing(L, -1, hole);
                    lua_pop(L, 1);
                    if (!ok) { lua_pop(L, 1); return false; }
                    poly.push_back(std::move(hole));
                }
            }
            lua_pop(L, 1);
            return true;
        }
        lua_pop(L, 1);
    }

    if (IsFlatPolygonTable(L, arg))
    {
        Ring ring;
        if (!ReadRing(L, arg, ring)) return false;
        poly.push_back(std::move(ring));
        return true;
    }

    size_t count = lua_objlen(L, arg);
    for (size_t i = 1; i <= count; ++i)
    {
        lua_rawgeti(L, arg, static_cast<int>(i));
        Ring ring;
        bool ok = ReadRing(L, -1, ring);
        lua_pop(L, 1);
        if (!ok) return false;
        poly.push_back(std::move(ring));
    }
    return !poly.empty();
}

void FlattenPolygon(const Polygon &poly, std::vector<Point> &coords)
{
    size_t count = 0;
    for (const auto &ring : poly) count += ring.size();
    coords.reserve(coords.size() + count);
    for (const auto &ring : poly)
        coords.insert(coords.end(), ring.begin(), ring.end());
}

int PushGeometryFailure(lua_State *L, const char *message)
{
    lua_pushnil(L);
    lua_pushstring(L, message ? message : "Geometry operation failed");
    return 2;
}

struct MeshOptionDefinition {
    const char *name;
    uint32_t flag;
};

static const MeshOptionDefinition kMeshOptionDefinitions[] = {
    {"fringe", OptionFringe}, {"join", OptionJoin},
    {"miterLimit", OptionMiterLimit}, {"tessTol", OptionTessTol},
    {"cap", OptionCap}, {"closed", OptionClosed}, {"refine", OptionRefine},
    {"mode", OptionMode}, {"output", OptionOutput}, {"distance", OptionDistance},
    {"distanceSign", OptionDistanceSign}, {"maxCurvePoints", OptionMaxCurvePoints},
    {"legacyUVs", OptionLegacyUVs}, {"dashPattern", OptionDashPattern},
    {"dashOffset", OptionDashOffset}, {"maxDashSegments", OptionMaxDashSegments},
};

static uint32_t FindMeshOption(const char *name)
{
    for (const auto &definition : kMeshOptionDefinitions)
        if (std::strcmp(name, definition.name) == 0) return definition.flag;
    return 0;
}

static void ValidateMeshOptionFields(lua_State *L, int arg, uint32_t allowed,
                                     const char *context)
{
    lua_pushnil(L);
    while (lua_next(L, arg) != 0)
    {
        if (lua_type(L, -2) != LUA_TSTRING)
            luaL_error(L, "%s option keys must be strings", context);
        const char *name = lua_tostring(L, -2);
        uint32_t flag = FindMeshOption(name);
        if (flag == 0)
        {
            if (std::strcmp(name, "joint") == 0)
                luaL_error(L, "Unknown %s option 'joint'; use 'join'", context);
            luaL_error(L, "Unknown %s option '%s'", context, name);
        }
        if ((allowed & flag) == 0)
            luaL_error(L, "%s does not accept option '%s'", context, name);
        lua_pop(L, 1);
    }
}

static bool ReadBooleanField(lua_State *L, int arg, const char *name, bool fallback)
{
    lua_getfield(L, arg, name);
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return fallback; }
    if (!lua_isboolean(L, -1)) luaL_error(L, "Option '%s' must be a boolean", name);
    bool result = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return result;
}

static float ReadFiniteNumberField(lua_State *L, int arg, const char *name,
                                   float fallback, bool allowZero)
{
    lua_getfield(L, arg, name);
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return fallback; }
    if (lua_type(L, -1) != LUA_TNUMBER) luaL_error(L, "Option '%s' must be a number", name);
    lua_Number number = lua_tonumber(L, -1);
    lua_pop(L, 1);
    if (!std::isfinite(static_cast<double>(number)) ||
        std::fabs(static_cast<double>(number)) > std::numeric_limits<float>::max())
        luaL_error(L, "Option '%s' must be a finite float", name);
    if (allowZero ? number < 0.0 : number <= 0.0)
        luaL_error(L, "Option '%s' must be %s", name, allowZero ? "non-negative" : "positive");
    return static_cast<float>(number);
}

static float ReadAnyFiniteNumberField(lua_State *L, int arg, const char *name,
                                      float fallback)
{
    lua_getfield(L, arg, name);
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return fallback; }
    if (lua_type(L, -1) != LUA_TNUMBER) luaL_error(L, "Option '%s' must be a number", name);
    lua_Number number = lua_tonumber(L, -1);
    lua_pop(L, 1);
    if (!std::isfinite(static_cast<double>(number)) ||
        std::fabs(static_cast<double>(number)) > std::numeric_limits<float>::max())
        luaL_error(L, "Option '%s' must be a finite float", name);
    return static_cast<float>(number);
}

static size_t ReadPositiveIntegerField(lua_State *L, int arg, const char *name,
                                       size_t fallback)
{
    lua_getfield(L, arg, name);
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return fallback; }
    if (lua_type(L, -1) != LUA_TNUMBER)
        luaL_error(L, "Option '%s' must be a positive integer", name);
    lua_Number number = lua_tonumber(L, -1);
    lua_pop(L, 1);
    if (!std::isfinite(static_cast<double>(number)) || number < 1.0 ||
        std::floor(static_cast<double>(number)) != number ||
        number > static_cast<lua_Number>(std::numeric_limits<size_t>::max()))
        luaL_error(L, "Option '%s' must be a positive integer", name);
    return static_cast<size_t>(number);
}

static std::vector<float> ReadDashPatternField(lua_State *L, int arg)
{
    std::vector<float> pattern;
    lua_getfield(L, arg, "dashPattern");
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return pattern; }
    if (!lua_istable(L, -1)) luaL_error(L, "Option 'dashPattern' must be an array");
    int table = CoronaLuaNormalize(L, -1);
    size_t count = lua_objlen(L, table);
    if (count == 0) luaL_error(L, "Option 'dashPattern' must not be empty");
    if (count > static_cast<size_t>(std::numeric_limits<int>::max()))
        luaL_error(L, "Option 'dashPattern' is too large");

    pattern.reserve(count);
    for (size_t i = 1; i <= count; ++i)
    {
        lua_rawgeti(L, table, static_cast<int>(i));
        if (lua_type(L, -1) != LUA_TNUMBER)
            luaL_error(L, "dashPattern[%d] must be a positive finite number", static_cast<int>(i));
        lua_Number number = lua_tonumber(L, -1);
        lua_pop(L, 1);
        if (!std::isfinite(static_cast<double>(number)) || number <= 0.0 ||
            std::fabs(static_cast<double>(number)) > std::numeric_limits<float>::max())
            luaL_error(L, "dashPattern[%d] must be a positive finite number", static_cast<int>(i));
        pattern.push_back(static_cast<float>(number));
    }

    lua_pushnil(L);
    while (lua_next(L, table) != 0)
    {
        if (lua_type(L, -2) != LUA_TNUMBER)
            luaL_error(L, "Option 'dashPattern' must contain only array entries");
        lua_Number key = lua_tonumber(L, -2);
        if (key < 1.0 || std::floor(static_cast<double>(key)) != key ||
            key > static_cast<lua_Number>(count))
            luaL_error(L, "Option 'dashPattern' must be a dense array");
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return pattern;
}

static const char *ReadStringField(lua_State *L, int arg, const char *name)
{
    lua_getfield(L, arg, name);
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return nullptr; }
    if (lua_type(L, -1) != LUA_TSTRING) luaL_error(L, "Option '%s' must be a string", name);
    const char *result = lua_tostring(L, -1);
    lua_pop(L, 1);
    return result;
}

MeshOptions GetMeshOptions(lua_State *L, int arg, uint32_t allowed,
                           const char *context)
{
    MeshOptions result;
    if (lua_isnoneornil(L, arg)) return result;
    if (!lua_istable(L, arg)) luaL_argerror(L, arg, "Expected an options table");
    arg = CoronaLuaNormalize(L, arg);
    ValidateMeshOptionFields(L, arg, allowed, context);

    if (allowed & OptionFringe)
        result.fringe = ReadFiniteNumberField(L, arg, "fringe", result.fringe, true);
    if (allowed & OptionMiterLimit)
        result.miterLimit = ReadFiniteNumberField(L, arg, "miterLimit", result.miterLimit, false);
    if (allowed & OptionTessTol)
        result.tessTol = ReadFiniteNumberField(L, arg, "tessTol", result.tessTol, false);
    if (allowed & OptionDistance)
        result.distance = ReadFiniteNumberField(L, arg, "distance", result.distance, false);
    if (allowed & OptionDashPattern)
        result.dashPattern = ReadDashPatternField(L, arg);
    if (allowed & OptionDashOffset)
        result.dashOffset = ReadAnyFiniteNumberField(L, arg, "dashOffset", result.dashOffset);
    if (allowed & OptionMaxDashSegments)
        result.maxDashSegments = ReadPositiveIntegerField(
            L, arg, "maxDashSegments", result.maxDashSegments);

    if (allowed & OptionJoin)
    {
        const char *s = ReadStringField(L, arg, "join");
        if (s && std::strcmp(s, "miter") == 0) result.join = Fringe::JOIN_MITER;
        else if (s && std::strcmp(s, "bevel") == 0) result.join = Fringe::JOIN_BEVEL;
        else if (s && std::strcmp(s, "round") == 0) result.join = Fringe::JOIN_ROUND;
        else if (s) luaL_error(L, "Invalid join '%s'; expected 'miter', 'bevel', or 'round'", s);
    }

    if (allowed & OptionCap)
    {
        const char *s = ReadStringField(L, arg, "cap");
        if (s && std::strcmp(s, "butt") == 0) result.cap = Fringe::CAP_BUTT;
        else if (s && std::strcmp(s, "square") == 0) result.cap = Fringe::CAP_SQUARE;
        else if (s && std::strcmp(s, "round") == 0) result.cap = Fringe::CAP_ROUND;
        else if (s) luaL_error(L, "Invalid cap '%s'; expected 'butt', 'square', or 'round'", s);
    }

    if (allowed & OptionMode)
    {
        const char *s = ReadStringField(L, arg, "mode");
        if (s && std::strcmp(s, "indexed") == 0) result.triangles = false;
        else if (s && std::strcmp(s, "triangles") == 0) result.triangles = true;
        else if (s) luaL_error(L, "Invalid mode '%s'; expected 'indexed' or 'triangles'", s);
    }

    if (allowed & OptionOutput)
    {
        const char *s = ReadStringField(L, arg, "output");
        if (s && std::strcmp(s, "table") == 0) result.output = OutputMode::Table;
        else if (s && std::strcmp(s, "buffers") == 0) result.output = OutputMode::Buffers;
        else if (s && std::strcmp(s, "mesh") == 0) result.output = OutputMode::Mesh;
        else if (s) luaL_error(L, "Invalid output '%s'; expected 'table', 'buffers', or 'mesh'", s);
    }

    if (allowed & OptionDistanceSign)
    {
        const char *s = ReadStringField(L, arg, "distanceSign");
        if (s && std::strcmp(s, "outsideNegative") == 0) result.outsidePositive = false;
        else if (s && std::strcmp(s, "outsidePositive") == 0) result.outsidePositive = true;
        else if (s) luaL_error(L, "Invalid distanceSign '%s'; expected 'outsideNegative' or 'outsidePositive'", s);
    }

    if (allowed & OptionClosed) result.closed = ReadBooleanField(L, arg, "closed", result.closed);
    if (allowed & OptionRefine) result.refine = ReadBooleanField(L, arg, "refine", result.refine);
    if (allowed & OptionLegacyUVs) result.legacyUVs = ReadBooleanField(L, arg, "legacyUVs", false);
    if (allowed & OptionMaxCurvePoints)
    {
        lua_getfield(L, arg, "maxCurvePoints");
        if (!lua_isnil(L, -1))
        {
            if (lua_type(L, -1) != LUA_TNUMBER) luaL_error(L, "Option 'maxCurvePoints' must be an integer");
            lua_Number number = lua_tonumber(L, -1);
            if (!std::isfinite(static_cast<double>(number)) || number < 3.0 ||
                std::floor(static_cast<double>(number)) != number)
                luaL_error(L, "Option 'maxCurvePoints' must be an integer of at least 3");
            if (number > static_cast<lua_Number>(std::numeric_limits<size_t>::max()))
                luaL_error(L, "Option 'maxCurvePoints' is too large");
            result.maxCurvePoints = static_cast<size_t>(number);
        }
        lua_pop(L, 1);
    }
    if (result.legacyUVs && result.output == OutputMode::Table)
        luaL_error(L, "legacyUVs=true requires output='buffers' or output='mesh'");
    if (result.dashPattern.empty() && result.dashOffset != 0.0f)
        luaL_error(L, "dashOffset requires dashPattern");
    return result;
}

int GetMaxVertices(lua_State *L, int arg)
{
    if (lua_isnoneornil(L, arg)) return 0;
    if (!lua_istable(L, arg)) luaL_argerror(L, arg, "Expected an options table");
    arg = CoronaLuaNormalize(L, arg);
    lua_pushnil(L);
    while (lua_next(L, arg) != 0)
    {
        if (lua_type(L, -2) != LUA_TSTRING || std::strcmp(lua_tostring(L, -2), "maxVertices") != 0)
            luaL_error(L, "Unknown convex partition option '%s'",
                       lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : "<non-string>");
        lua_pop(L, 1);
    }
    lua_getfield(L, arg, "maxVertices");
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return 0; }
    if (lua_type(L, -1) != LUA_TNUMBER) luaL_error(L, "maxVertices must be an integer");
    lua_Number number = lua_tonumber(L, -1);
    if (!std::isfinite(static_cast<double>(number)) || std::floor(static_cast<double>(number)) != number ||
        number > std::numeric_limits<int>::max())
        luaL_error(L, "maxVertices must be a finite integer");
    int result = static_cast<int>(number);
    lua_pop(L, 1);
    if (result != 0 && result < 3)
        luaL_error(L, "maxVertices must be at least 3");
    return result;
}

} // namespace Geometry2D
