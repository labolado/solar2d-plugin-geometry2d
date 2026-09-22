#include "geometry2d_lua.h"

#include "CoronaMemory.h"

#include <cstdint>
#include <cmath>
#include <cstring>
#include <limits>

namespace Geometry2D {

size_t ValidateDenseArray(lua_State *L, int arg, const char *context)
{
    if (!lua_istable(L, arg)) luaL_error(L, "%s must be an array", context);
    arg = CoronaLuaNormalize(L, arg);
    size_t count = lua_objlen(L, arg), seen = 0;
    if (count > static_cast<size_t>(std::numeric_limits<int>::max()))
        luaL_error(L, "%s array is too large", context);
    lua_pushnil(L);
    while (lua_next(L, arg))
    {
        double key = lua_tonumber(L, -2);
        if (lua_type(L, -2) != LUA_TNUMBER || key < 1 || key > count || key != std::floor(key))
            luaL_error(L, "%s must be a dense array with only array entries", context);
        ++seen; lua_pop(L, 1);
    }
    if (seen != count) luaL_error(L, "%s must be a dense array", context);
    return count;
}

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
    size_t n = ValidateDenseArray(L, arg, "polygon");
    if ((n % 2) != 0) luaL_error(L, "Polygon coordinates must be x/y pairs");
    int count = static_cast<int>(n / 2);
    if (count >= 3) { poly.Init(count); poly.SetHole(isHole); }
    for (int i = 0; i < count; ++i)
    {
        lua_rawgeti(L, arg, static_cast<int>(i * 2 + 1));
        lua_rawgeti(L, arg, static_cast<int>(i * 2 + 2));
        if (lua_type(L, -2) != LUA_TNUMBER || lua_type(L, -1) != LUA_TNUMBER)
            luaL_error(L, "Polygon coordinates must be numbers");
        lua_Number x = lua_tonumber(L, -2), y = lua_tonumber(L, -1);
        if (!std::isfinite(static_cast<double>(x)) || !std::isfinite(static_cast<double>(y)) ||
            std::abs(x) > std::numeric_limits<float>::max() || std::abs(y) > std::numeric_limits<float>::max())
            luaL_error(L, "Polygon coordinates must be finite floats");
        if (count >= 3) {
            poly[i].x = static_cast<tppl_float>(x);
            poly[i].y = static_cast<tppl_float>(y);
            poly[i].id = static_cast<int>(i);
        }
        lua_pop(L, 2);
    }
    return count >= 3;
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
        if (!std::isfinite(x) || !std::isfinite(y) ||
            std::abs(x) > std::numeric_limits<float>::max() || std::abs(y) > std::numeric_limits<float>::max())
            luaL_error(L, "Packed polygon coordinates must be finite floats");
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
            if (!lua_istable(L, -1)) luaL_error(L, "poly must be a ring table or typed buffer descriptor");
            Ring outer;
            bool ok = ReadRing(L, -1, outer);
            lua_pop(L, 1);
            if (!ok) return false;
            poly.push_back(std::move(outer));

            lua_getfield(L, arg, "holes");
            if (!lua_isnil(L, -1) && !lua_istable(L, -1))
                luaL_error(L, "holes must be an array");
            if (lua_istable(L, -1))
            {
                size_t count = ValidateDenseArray(L, -1, "holes");
                for (size_t i = 1; i <= count; ++i)
                {
                    lua_rawgeti(L, -1, static_cast<int>(i));
                    if (!lua_istable(L, -1)) luaL_error(L, "Each hole must be a ring table or typed buffer descriptor");
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

    size_t count = ValidateDenseArray(L, arg, "polygon rings");
    for (size_t i = 1; i <= count; ++i)
    {
        lua_rawgeti(L, arg, static_cast<int>(i));
        if (!lua_istable(L, -1)) luaL_error(L, "Each polygon ring must be a table or typed buffer descriptor");
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
    {"mode", OptionMode}, {"output", OptionOutput}, {"maxCurvePoints", OptionMaxCurvePoints},
    {"method", OptionSDF}, {"innerRange", OptionSDF}, {"outerRange", OptionSDF},
    {"distanceTolerance", OptionSDF}, {"distanceTransform", OptionSDF},
    {"maxWork", OptionSDF}, {"maxVertices", OptionSDF | OptionLimits},
    {"aa", OptionAA}, {"aaWidth", OptionAA}, {"topology", OptionTopology},
    {"legacyUVs", OptionLegacyUVs}, {"dashPattern", OptionDashPattern},
    {"dashOffset", OptionDashOffset}, {"maxDashSegments", OptionMaxDashSegments},
    {"fillRule", OptionFillRule}, {"intersections", OptionIntersections},
    {"clipperPrecision", OptionClipperPrecision},
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
        number >= static_cast<lua_Number>(std::numeric_limits<size_t>::max()))
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
                           const char *context, const MeshOptions *defaults)
{
    MeshOptions result = defaults ? *defaults : MeshOptions();
    // Public distance defaults are independent of native algorithm fixtures.
    if (!defaults && (allowed & OptionSDF)) result.earcutBackend = true;
    if (lua_isnoneornil(L, arg)) return result;
    if (!lua_istable(L, arg)) luaL_argerror(L, arg, "Expected an options table");
    arg = CoronaLuaNormalize(L, arg);
    if (allowed & OptionSDF) {
        const char *backend = ReadStringField(L, arg, "method");
        if (backend) {
            if (std::strcmp(backend, "local") == 0) result.earcutBackend = true;
            else if (std::strcmp(backend, "partition") == 0) result.earcutBackend = false;
            else luaL_error(L, "method must be 'partition' or 'local'");
        }
        if (result.earcutBackend)
            allowed |= OptionMiterLimit;
    }
    ValidateMeshOptionFields(L, arg, allowed, context);

    if (allowed & OptionAA) {
        const char *aa = ReadStringField(L, arg, "aa");
        if (aa && std::strcmp(aa,"none")==0) result.vertexAA=false;
        else if (aa && std::strcmp(aa,"vertex")==0) result.vertexAA=true;
        else if (aa) luaL_error(L,"aa must be 'none' or 'vertex'");
        result.fringe=ReadFiniteNumberField(L,arg,"aaWidth",result.fringe,false);
        if (!result.vertexAA) {
            lua_getfield(L,arg,"aaWidth");
            if (!lua_isnil(L,-1)) luaL_error(L,"aaWidth is not supported when aa='none'");
            lua_pop(L,1);
            if (allowed & OptionTopology) {
                for (const char* field : {"join","miterLimit"}) {
                    lua_getfield(L,arg,field);
                    if (!lua_isnil(L,-1)) luaL_error(L,"%s is not supported by fill aa='none'",field);
                    lua_pop(L,1);
                }
                if (!(allowed & OptionMaxCurvePoints)) {
                    lua_getfield(L,arg,"tessTol");
                    if(!lua_isnil(L,-1)) luaL_error(L,"tessTol is not supported by util fill aa='none'");
                    lua_pop(L,1);
                }
            }
        }
    }
    if (allowed & OptionTopology) {
        const char* topology=ReadStringField(L,arg,"topology");
        if (topology && std::strcmp(topology,"direct")==0) result.normalizeFill=false;
        else if (topology && std::strcmp(topology,"normalize")==0) result.normalizeFill=true;
        else if (topology) luaL_error(L,"topology must be 'direct' or 'normalize'");
    }
    if (allowed & OptionLimits) {
        result.sdf.maxVertices=ReadPositiveIntegerField(L,arg,"maxVertices",result.sdf.maxVertices);
        if(result.sdf.maxVertices>1000000) luaL_error(L,"maxVertices must be <= 1000000");
    }

    if (allowed & OptionFringe)
        result.fringe = ReadFiniteNumberField(L, arg, "fringe", result.fringe, true);
    if (allowed & OptionMiterLimit)
        result.miterLimit = ReadFiniteNumberField(L, arg, "miterLimit", result.miterLimit, false);
    if (allowed & OptionTessTol)
        result.tessTol = ReadFiniteNumberField(L, arg, "tessTol", result.tessTol, false);
    if (allowed & OptionSDF)
    {
        auto reject = [&](const char *name) {
            lua_getfield(L, arg, name);
            if (!lua_isnil(L, -1)) luaL_error(L, "%s is not supported by method='local'", name);
            lua_pop(L, 1);
        };
        if (result.earcutBackend) {
            reject("distanceTolerance"); reject("distanceTransform");
        }
        auto range = [&](const char *name, double fallback) {
            lua_getfield(L, arg, name);
            if (lua_isnil(L, -1)) { lua_pop(L, 1); return fallback; }
            double value = lua_tonumber(L, -1);
            if (lua_type(L, -1) != LUA_TNUMBER || !std::isfinite(value) ||
                value < 0 || (value == 0 && std::strcmp(name,"innerRange")!=0) || value > std::numeric_limits<float>::max())
                luaL_error(L, "%s must be finite and positive (innerRange may be zero)", name);
            lua_pop(L, 1);
            return value;
        };
        result.sdf.innerRange = range("innerRange", result.sdf.innerRange);
        if(result.earcutBackend && result.sdf.innerRange==0)
            luaL_error(L,"method='local' requires positive innerRange; use method='partition' for an exterior-only distance band");
        result.sdf.geometry=result.sdf.innerRange==0 ? SDFGeometry::FillAA : SDFGeometry::InnerStroke;
        result.sdf.outerRange = range("outerRange", result.sdf.outerRange);
        result.sdf.distanceTolerance = range("distanceTolerance", result.sdf.distanceTolerance);
        if (result.sdf.distanceTolerance < 0.0001)
            luaL_error(L, "distanceTolerance must be at least 0.0001");
        result.sdf.maxWork = ReadPositiveIntegerField(L, arg, "maxWork", result.sdf.maxWork);
        result.sdf.maxVertices = ReadPositiveIntegerField(L, arg, "maxVertices", result.sdf.maxVertices);
        if (result.sdf.maxVertices > 1000000 || result.sdf.maxWork > 20000000)
            luaL_error(L, "SDF limits: maxVertices <= 1000000, maxWork <= 20000000");
        lua_getfield(L, arg, "distanceTransform");
        if (!lua_isnil(L, -1))
        {
            if (!lua_istable(L, -1) || lua_objlen(L, -1) != 6)
                luaL_error(L, "distanceTransform must be a dense array {a,b,c,d,tx,ty}");
            int transform = CoronaLuaNormalize(L, -1);
            lua_pushnil(L);
            while (lua_next(L, transform))
            {
                double key = lua_tonumber(L, -2);
                if (lua_type(L, -2) != LUA_TNUMBER || key < 1 || key > 6 || key != std::floor(key))
                    luaL_error(L, "distanceTransform must contain exactly six array entries");
                lua_pop(L, 1);
            }
            for (int i = 0; i < 6; ++i)
            {
                lua_rawgeti(L, transform, i + 1);
                double v = lua_tonumber(L, -1);
                if (lua_type(L, -1) != LUA_TNUMBER || !std::isfinite(v) || std::abs(v) > std::numeric_limits<float>::max())
                    luaL_error(L, "distanceTransform entries must be finite floats");
                result.sdf.transform[i] = v;
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }
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

    if (allowed & OptionFillRule)
    {
        const char *s = ReadStringField(L, arg, "fillRule");
        if (s && std::strcmp(s, "nonZero") == 0) result.fillRule = PathFillRule::NonZero;
        else if (s && std::strcmp(s, "evenOdd") == 0) result.fillRule = PathFillRule::EvenOdd;
        else if (s) luaL_error(L, "Invalid fillRule '%s'; expected 'nonZero' or 'evenOdd'", s);
    }

    if (allowed & OptionIntersections)
    {
        const char *s = ReadStringField(L, arg, "intersections");
        if (s && std::strcmp(s, "error") == 0) result.intersections = PathIntersectionMode::Error;
        else if (s && std::strcmp(s, "resolve") == 0) result.intersections = PathIntersectionMode::Resolve;
        else if (s) luaL_error(L, "Invalid intersections '%s'; expected 'error' or 'resolve'", s);
    }

    if (allowed & OptionClipperPrecision)
    {
        lua_getfield(L, arg, "clipperPrecision");
        if (!lua_isnil(L, -1))
        {
            if (lua_type(L, -1) != LUA_TNUMBER)
                luaL_error(L, "Option 'clipperPrecision' must be an integer from -8 through 8");
            lua_Number number = lua_tonumber(L, -1);
            if (!std::isfinite(static_cast<double>(number)) ||
                std::floor(static_cast<double>(number)) != number ||
                number < -8.0 || number > 8.0)
                luaL_error(L, "Option 'clipperPrecision' must be an integer from -8 through 8");
            result.clipperPrecision = static_cast<int>(number);
        }
        lua_pop(L, 1);
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
