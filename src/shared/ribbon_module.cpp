#include "geometry2d_lua.h"
#include "ribbon_builder.h"

#include "CoronaMemory.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace Geometry2D {
namespace {

static const char *kRibbonMetatable = "plugin.geometry2d.ribbon";
static const char *kRibbonBufferMetatable = "plugin.geometry2d.ribbonBuffer";

struct RibbonColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct RibbonState {
    explicit RibbonState(const RibbonBuildOptions &options)
    : builder(options) {}

    RibbonBuilder builder;
    RibbonColor color;
    float alpha = 1.0f;
    uint64_t styleRevision = 1;
    uint64_t bufferRevision = 1;
    uint64_t meshCreateCount = 0;
    uint64_t meshUpdateCount = 0;
    bool alive = true;
    bool updatingView = false;
};

using RibbonHandle = std::shared_ptr<RibbonState>;

struct RibbonUserdata {
    RibbonHandle state;
};

enum class RibbonBufferKind {
    Vertices,
    UVs,
    Indices,
    PathDistances,
    ContourDistances,
};

struct RibbonBufferUserdata {
    // Borrowed views must not keep a destroyed generator's native arrays alive.
    std::weak_ptr<RibbonState> state;
    RibbonBufferKind kind = RibbonBufferKind::Vertices;
    uint64_t revision = 0;
};

static int PushFailure(lua_State *L, const std::string &error)
{
    lua_pushnil(L);
    lua_pushlstring(L, error.c_str(), error.size());
    return 2;
}

static std::string PopError(lua_State *L, const char *fallback)
{
    const char *message = lua_tostring(L, -1);
    std::string result = message ? message : fallback;
    lua_pop(L, 1);
    return result;
}

static void CheckArgCount(lua_State *L, int expected, const char *context)
{
    if (lua_gettop(L) != expected)
        luaL_error(L, "%s expects exactly %d argument%s", context,
                   expected - 1, expected == 2 ? "" : "s");
}

static float CheckFiniteFloat(lua_State *L, int index, const char *name)
{
    if (lua_type(L, index) != LUA_TNUMBER)
        luaL_error(L, "%s must be a number", name);
    double value = static_cast<double>(lua_tonumber(L, index));
    if (!std::isfinite(value) ||
        std::fabs(value) > std::numeric_limits<float>::max())
        luaL_error(L, "%s must be a finite float", name);
    return static_cast<float>(value);
}

static double CheckFiniteDouble(lua_State *L, int index, const char *name)
{
    if (lua_type(L, index) != LUA_TNUMBER)
        luaL_error(L, "%s must be a number", name);
    double value = static_cast<double>(lua_tonumber(L, index));
    if (!std::isfinite(value)) luaL_error(L, "%s must be finite", name);
    return value;
}

static float CheckUnit(lua_State *L, int index, const char *name)
{
    float value = CheckFiniteFloat(L, index, name);
    if (value < 0.0f || value > 1.0f)
        luaL_error(L, "%s must be between 0 and 1", name);
    return value;
}

static size_t CheckSize(lua_State *L, int index, size_t minimum,
                        size_t maximum, const char *name)
{
    double value = CheckFiniteDouble(L, index, name);
    if (std::floor(value) != value || value < static_cast<double>(minimum) ||
        value > static_cast<double>(maximum))
        luaL_error(L, "%s must be an integer from %d through %d", name,
                   static_cast<int>(minimum), static_cast<int>(maximum));
    return static_cast<size_t>(value);
}

static RibbonState *CheckRibbon(lua_State *L, int index)
{
    RibbonUserdata *userdata = static_cast<RibbonUserdata *>(
        luaL_checkudata(L, index, kRibbonMetatable));
    if (!userdata->state || !userdata->state->alive)
        luaL_error(L, "ribbon generator has been destroyed");
    if (userdata->state->updatingView)
        luaL_error(L, "ribbon cannot be accessed during a view update");
    return userdata->state.get();
}

static RibbonHandle CheckRibbonHandle(lua_State *L, int index)
{
    CheckRibbon(L, index);
    RibbonUserdata *userdata = static_cast<RibbonUserdata *>(
        luaL_checkudata(L, index, kRibbonMetatable));
    if (!userdata->state || !userdata->state->alive)
        luaL_error(L, "ribbon generator has been destroyed");
    return userdata->state;
}

static void InvalidateBuffers(RibbonState &state)
{
    ++state.bufferRevision;
    if (state.bufferRevision == 0) ++state.bufferRevision;
}

static void MarkStyle(RibbonState &state)
{
    ++state.styleRevision;
    if (state.styleRevision == 0) ++state.styleRevision;
}

static bool BufferView(const RibbonBufferUserdata &buffer,
                       const void *&bytes, size_t &byteCount)
{
    RibbonHandle state = buffer.state.lock();
    if (!state || !state->alive ||
        buffer.revision != state->bufferRevision)
        return false;
    const RibbonBuilder &builder = state->builder;
    switch (buffer.kind)
    {
        case RibbonBufferKind::Vertices:
            bytes = builder.Vertices().data();
            byteCount = builder.Vertices().size() * sizeof(float);
            break;
        case RibbonBufferKind::UVs:
            bytes = builder.UVs().data();
            byteCount = builder.UVs().size() * sizeof(float);
            break;
        case RibbonBufferKind::Indices:
            bytes = builder.Indices().data();
            byteCount = builder.Indices().size() * sizeof(uint16_t);
            break;
        case RibbonBufferKind::PathDistances:
            bytes = builder.PathDistances().data();
            byteCount = builder.PathDistances().size() * sizeof(float);
            break;
        case RibbonBufferKind::ContourDistances:
            bytes = builder.ContourDistances().data();
            byteCount = builder.ContourDistances().size() * sizeof(float);
            break;
    }
    return byteCount == 0 || bytes != nullptr;
}

static int RibbonBufferGC(lua_State *L)
{
    RibbonBufferUserdata *buffer = static_cast<RibbonBufferUserdata *>(
        luaL_checkudata(L, 1, kRibbonBufferMetatable));
    buffer->~RibbonBufferUserdata();
    return 0;
}

static void PushBorrowedBuffer(lua_State *L, const RibbonHandle &state,
                               RibbonBufferKind kind)
{
    void *memory = lua_newuserdata(L, sizeof(RibbonBufferUserdata));
    RibbonBufferUserdata *buffer = new (memory) RibbonBufferUserdata();
    buffer->state = state;
    buffer->kind = kind;
    buffer->revision = state->bufferRevision;

    if (luaL_newmetatable(L, kRibbonBufferMetatable))
    {
        lua_pushcfunction(L, RibbonBufferGC);
        lua_setfield(L, -2, "__gc");
        lua_pushliteral(L, "geometry2d ribbon buffer");
        lua_setfield(L, -2, "__metatable");
        CoronaMemoryInterfaceInfo info = {};
        info.callbacks.getReadableBytes = [](CoronaMemoryWorkspace *workspace) {
            return workspace->vars[0].cp;
        };
        info.callbacks.getByteCount = [](CoronaMemoryWorkspace *workspace) {
            return workspace->vars[1].size;
        };
        info.getObject = [](lua_State *stateL, int arg,
                            CoronaMemoryWorkspace *workspace) {
            RibbonBufferUserdata *value = static_cast<RibbonBufferUserdata *>(
                luaL_checkudata(stateL, arg, kRibbonBufferMetatable));
            const void *bytes = nullptr;
            size_t byteCount = 0;
            if (!BufferView(*value, bytes, byteCount))
                return luaL_error(stateL, "ribbon buffer has expired; request a new snapshot");
            workspace->vars[0].cp = static_cast<const char *>(bytes);
            workspace->vars[1].size = byteCount;
            return 1;
        };
        CoronaMemoryCreateInterface(L, &info);
        lua_setfield(L, -2, "__memory");
    }
    lua_setmetatable(L, -2);
}

static void PushBorrowedDescriptor(lua_State *L, const RibbonHandle &state,
                                   RibbonBufferKind kind, size_t count,
                                   int componentCount = 0)
{
    lua_createtable(L, 0, componentCount > 0 ? 3 : 2);
    PushBorrowedBuffer(L, state, kind);
    lua_setfield(L, -2, "buffer");
    lua_pushnumber(L, static_cast<lua_Number>(count));
    lua_setfield(L, -2, "count");
    if (componentCount > 0)
    {
        lua_pushinteger(L, componentCount);
        lua_setfield(L, -2, "componentCount");
    }
}

static bool EnsureBuilt(RibbonState &state, std::string &error, bool &rebuilt)
{
    return state.builder.Build(error, rebuilt);
}

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

static void PushOutputMetadata(lua_State *L, const RibbonState &state, int table)
{
    table = CoronaLuaNormalize(L, table);
    const RibbonBuilder &builder = state.builder;
    lua_pushnumber(L, static_cast<lua_Number>(builder.LogicalVertexCount()));
    lua_setfield(L, table, "logicalVertexCount");
    lua_pushnumber(L, static_cast<lua_Number>(builder.LogicalIndexCount()));
    lua_setfield(L, table, "logicalIndexCount");
    lua_pushnumber(L, static_cast<lua_Number>(builder.VertexCapacity()));
    lua_setfield(L, table, "vertexCapacity");
    lua_pushnumber(L, static_cast<lua_Number>(builder.IndexCapacity()));
    lua_setfield(L, table, "indexCapacity");
    lua_pushnumber(L, static_cast<lua_Number>(builder.LogicalTriangleCount()));
    lua_setfield(L, table, "logicalTriangleCount");
    lua_pushnumber(L, static_cast<lua_Number>(builder.TriangleCapacity()));
    lua_setfield(L, table, "triangleCapacity");
    lua_pushnumber(L, builder.TailLength()); lua_setfield(L, table, "tailLength");
    lua_pushnumber(L, builder.HeadLength()); lua_setfield(L, table, "headLength");
    lua_pushnumber(L, builder.ActiveLength()); lua_setfield(L, table, "activeLength");
    lua_pushnumber(L, static_cast<lua_Number>(state.bufferRevision));
    lua_setfield(L, table, "bufferRevision");
    lua_pushliteral(L, "valid until the generator's next geometry mutation or destruction");
    lua_setfield(L, table, "bufferValidity");
}

static void PushBufferOutput(lua_State *L, const RibbonHandle &state)
{
    const RibbonBuilder &builder = state->builder;
    lua_createtable(L, 0, 14);
    int result = CoronaLuaNormalize(L, -1);
    PushBorrowedDescriptor(L, state, RibbonBufferKind::Vertices,
                           builder.VertexCapacity());
    lua_setfield(L, result, "vertices");
    PushBorrowedDescriptor(L, state, RibbonBufferKind::UVs,
                           builder.VertexCapacity());
    lua_setfield(L, result, "uvs");
    if (!builder.Triangles())
    {
        PushBorrowedDescriptor(L, state, RibbonBufferKind::Indices, builder.IndexCapacity());
        lua_setfield(L, result, "indices");
        lua_pushboolean(L, 1); lua_setfield(L, result, "zeroBasedIndices");
    }
    PushBorrowedDescriptor(L, state, RibbonBufferKind::PathDistances,
                           builder.VertexCapacity(), 1);
    lua_setfield(L, result, "pathDistances");
    PushBorrowedDescriptor(L, state, RibbonBufferKind::ContourDistances,
                           builder.VertexCapacity(), 1);
    lua_setfield(L, result, "contourDistances");
    lua_pushstring(L, builder.Triangles() ? "triangles" : "indexed"); lua_setfield(L, result, "mode");
    PushOutputMetadata(L, *state, result);
}

static void PushTableOutput(lua_State *L, const RibbonState &state)
{
    const RibbonBuilder &builder = state.builder;
    lua_createtable(L, 0, 14);
    int result = CoronaLuaNormalize(L, -1);
    PushFloatTable(L, builder.Vertices()); lua_setfield(L, result, "vertices");
    PushFloatTable(L, builder.UVs()); lua_setfield(L, result, "uvs");
    if (!builder.Triangles())
    {
        PushIndexTable(L, builder.Indices()); lua_setfield(L, result, "indices");
    }
    PushFloatTable(L, builder.PathDistances());
    lua_setfield(L, result, "pathDistances");
    PushFloatTable(L, builder.ContourDistances());
    lua_setfield(L, result, "contourDistances");
    lua_pushstring(L, builder.Triangles() ? "triangles" : "indexed"); lua_setfield(L, result, "mode");
    PushOutputMetadata(L, state, result);
}

static void PushMeshDescriptor(lua_State *L, const RibbonHandle &state)
{
    const RibbonBuilder &builder = state->builder;
    lua_createtable(L, 0, 6);
    int descriptor = CoronaLuaNormalize(L, -1);
    PushBorrowedDescriptor(L, state, RibbonBufferKind::Vertices,
                           builder.VertexCapacity());
    lua_setfield(L, descriptor, "vertices");
    PushBorrowedDescriptor(L, state, RibbonBufferKind::UVs,
                           builder.VertexCapacity());
    lua_setfield(L, descriptor, "uvs");
    if (!builder.Triangles())
    {
        PushBorrowedDescriptor(L, state, RibbonBufferKind::Indices, builder.IndexCapacity());
        lua_setfield(L, descriptor, "indices");
        lua_pushboolean(L, 1); lua_setfield(L, descriptor, "zeroBasedIndices");
    }
    lua_pushstring(L, builder.Triangles() ? "triangles" : "indexed"); lua_setfield(L, descriptor, "mode");
}

static int ReturnSelf(lua_State *L)
{
    lua_settop(L, 1);
    return 1;
}

static int AddPoint(lua_State *L)
{
    CheckArgCount(L, 4, "ribbon:addPoint");
    RibbonState *state = CheckRibbon(L, 1);
    float x = CheckFiniteFloat(L, 2, "x");
    float y = CheckFiniteFloat(L, 3, "y");
    double time = CheckFiniteDouble(L, 4, "time");
    if (state->builder.TimestampMode() == RibbonTimestampMode::Monotonic &&
        state->builder.PointCount() && time < state->builder.LastTime())
        return luaL_argerror(L, 4, "point timestamps must be non-decreasing");
    bool added = false;
    std::string error;
    if (!state->builder.AddPoint(x, y, time, added, error))
        return PushFailure(L, error);
    if (added) InvalidateBuffers(*state);
    lua_pushboolean(L, added);
    return 1;
}

static int Expire(lua_State *L)
{
    CheckArgCount(L, 3, "ribbon:expire");
    RibbonState *state = CheckRibbon(L, 1);
    double now = CheckFiniteDouble(L, 2, "now");
    double maxAge = CheckFiniteDouble(L, 3, "maxAge");
    if (maxAge < 0.0) return luaL_argerror(L, 3, "maxAge must be non-negative");
    size_t removed = state->builder.Expire(now, maxAge);
    if (removed > 0) InvalidateBuffers(*state);
    lua_pushnumber(L, static_cast<lua_Number>(removed));
    return 1;
}

static int Clear(lua_State *L)
{
    CheckArgCount(L, 1, "ribbon:clear");
    RibbonState *state = CheckRibbon(L, 1);
    bool changed = state->builder.PointCount() > 0;
    state->builder.Clear();
    if (changed) InvalidateBuffers(*state);
    return ReturnSelf(L);
}

static int ReservePoints(lua_State *L)
{
    CheckArgCount(L, 2, "ribbon:reservePoints");
    RibbonState *state = CheckRibbon(L, 1);
    size_t count = CheckSize(L, 2, 2, state->builder.MaxPoints(), "point capacity");
    std::string error;
    if (!state->builder.ReservePoints(count, error)) return PushFailure(L, error);
    return ReturnSelf(L);
}

static int SetWidth(lua_State *L)
{
    CheckArgCount(L, 2, "ribbon:setWidth");
    RibbonState *state = CheckRibbon(L, 1);
    float width = CheckFiniteFloat(L, 2, "width");
    if (width <= 0.0f) return luaL_argerror(L, 2, "width must be positive");
    if (state->builder.SetWidth(width)) InvalidateBuffers(*state);
    return ReturnSelf(L);
}

static int SetAAWidth(lua_State *L)
{
    CheckArgCount(L, 2, "ribbon:setAAWidth");
    RibbonState *state = CheckRibbon(L, 1);
    float width = CheckFiniteFloat(L, 2, "AA width");
    if (width < 0.0f) return luaL_argerror(L, 2, "AA width must be non-negative");
    if (state->builder.SetAAWidth(width)) InvalidateBuffers(*state);
    return ReturnSelf(L);
}

static int SetColor(lua_State *L)
{
    int count = lua_gettop(L);
    if (count != 4 && count != 5)
        return luaL_error(L, "ribbon:setColor expects r, g, b[, a]");
    RibbonState *state = CheckRibbon(L, 1);
    RibbonColor color;
    color.r = CheckUnit(L, 2, "red");
    color.g = CheckUnit(L, 3, "green");
    color.b = CheckUnit(L, 4, "blue");
    color.a = count == 5 ? CheckUnit(L, 5, "color alpha") : 1.0f;
    if (color.r != state->color.r || color.g != state->color.g ||
        color.b != state->color.b || color.a != state->color.a)
    {
        state->color = color;
        MarkStyle(*state);
    }
    return ReturnSelf(L);
}

static int SetAlpha(lua_State *L)
{
    CheckArgCount(L, 2, "ribbon:setAlpha");
    RibbonState *state = CheckRibbon(L, 1);
    float alpha = CheckUnit(L, 2, "alpha");
    if (state->alpha != alpha)
    {
        state->alpha = alpha;
        MarkStyle(*state);
    }
    return ReturnSelf(L);
}

static int PointCount(lua_State *L)
{
    CheckArgCount(L, 1, "ribbon:pointCount");
    RibbonState *state = CheckRibbon(L, 1);
    lua_pushnumber(L, static_cast<lua_Number>(state->builder.PointCount()));
    return 1;
}

static void PushStats(lua_State *L, const RibbonState &state)
{
    const RibbonBuilder &builder = state.builder;
    lua_createtable(L, 0, 24);
    int result = CoronaLuaNormalize(L, -1);
#define RIBBON_NUMBER_FIELD(name, value) \
    do { lua_pushnumber(L, static_cast<lua_Number>(value)); lua_setfield(L, result, name); } while (0)
    RIBBON_NUMBER_FIELD("pointCount", builder.PointCount());
    RIBBON_NUMBER_FIELD("pointCapacity", builder.PointCapacity());
    RIBBON_NUMBER_FIELD("maxPoints", builder.MaxPoints());
    RIBBON_NUMBER_FIELD("logicalVertexCount", builder.LogicalVertexCount());
    RIBBON_NUMBER_FIELD("logicalIndexCount", builder.LogicalIndexCount());
    RIBBON_NUMBER_FIELD("logicalTriangleCount", builder.LogicalTriangleCount());
    RIBBON_NUMBER_FIELD("vertexCapacity", builder.VertexCapacity());
    RIBBON_NUMBER_FIELD("indexCapacity", builder.IndexCapacity());
    RIBBON_NUMBER_FIELD("triangleCapacity", builder.TriangleCapacity());
    RIBBON_NUMBER_FIELD("tailLength", builder.TailLength());
    RIBBON_NUMBER_FIELD("headLength", builder.HeadLength());
    RIBBON_NUMBER_FIELD("activeLength", builder.ActiveLength());
    RIBBON_NUMBER_FIELD("width", builder.Width());
    RIBBON_NUMBER_FIELD("aaWidth", builder.AAWidth());
    RIBBON_NUMBER_FIELD("miterLimit", builder.MiterLimit());
    RIBBON_NUMBER_FIELD("geometryRevision", builder.GeometryRevision());
    RIBBON_NUMBER_FIELD("buildRevision", builder.BuildRevision());
    RIBBON_NUMBER_FIELD("bufferRevision", state.bufferRevision);
    RIBBON_NUMBER_FIELD("styleRevision", state.styleRevision);
    RIBBON_NUMBER_FIELD("buildCount", builder.BuildCount());
    RIBBON_NUMBER_FIELD("noOpBuildCount", builder.NoOpBuildCount());
    RIBBON_NUMBER_FIELD("lastBuildMilliseconds", builder.LastBuildMilliseconds());
    RIBBON_NUMBER_FIELD("totalBuildMilliseconds", builder.TotalBuildMilliseconds());
    RIBBON_NUMBER_FIELD("meshCreateCount", state.meshCreateCount);
    RIBBON_NUMBER_FIELD("meshUpdateCount", state.meshUpdateCount);
    RIBBON_NUMBER_FIELD("nativeCapacityBytes", builder.NativeCapacityBytes());
#undef RIBBON_NUMBER_FIELD
    lua_pushboolean(L, builder.LogicalVertexCount() > 0);
    lua_setfield(L, result, "drawable");
    lua_pushboolean(L, builder.CapacityTiers());
    lua_setfield(L, result, "capacityTiers");
    lua_pushstring(L, builder.TimestampMode() == RibbonTimestampMode::Monotonic
        ? "monotonic" : "legacyCount");
    lua_setfield(L, result, "timestampMode");
    lua_pushstring(L, builder.Triangles() ? "triangles" : "indexed");
    lua_setfield(L, result, "mode");
}

static int GetStats(lua_State *L)
{
    CheckArgCount(L, 1, "ribbon:getStats");
    RibbonState *state = CheckRibbon(L, 1);
    std::string error;
    bool rebuilt = false;
    if (!EnsureBuilt(*state, error, rebuilt)) return PushFailure(L, error);
    PushStats(L, *state);
    return 1;
}

static int Snapshot(lua_State *L)
{
    int count = lua_gettop(L);
    if (count < 1 || count > 2)
        return luaL_error(L, "ribbon:snapshot expects at most one output mode");
    CheckRibbon(L, 1);
    if (count == 2 && lua_type(L, 2) != LUA_TSTRING)
        return luaL_argerror(L, 2, "expected 'buffers' or 'table'");
    const char *output = count == 2 ? luaL_checkstring(L, 2) : "buffers";
    if (std::strcmp(output, "buffers") != 0 && std::strcmp(output, "table") != 0)
        return luaL_argerror(L, 2, "expected 'buffers' or 'table'");
    RibbonHandle state = CheckRibbonHandle(L, 1);
    std::string error;
    bool rebuilt = false;
    if (!EnsureBuilt(*state, error, rebuilt)) return PushFailure(L, error);
    if (state->builder.LogicalVertexCount() == 0)
        return PushFailure(L, "ribbon requires at least two accepted points");
    if (std::strcmp(output, "table") == 0) PushTableOutput(L, *state);
    else PushBufferOutput(L, state);
    return 1;
}

static bool NewDisplayGroup(lua_State *L, std::string &error)
{
    int base = lua_gettop(L);
    lua_getglobal(L, "display");
    if (!lua_istable(L, -1))
    { lua_settop(L, base); error = "display API is unavailable"; return false; }
    lua_getfield(L, -1, "newGroup");
    if (!lua_isfunction(L, -1))
    { lua_settop(L, base); error = "display.newGroup is unavailable"; return false; }
    if (lua_pcall(L, 0, 1, 0) != 0)
    { error = PopError(L, "display.newGroup failed"); lua_settop(L, base); return false; }
    lua_remove(L, base + 1);
    return true;
}

static bool RemoveObject(lua_State *L, int object, std::string &error)
{
    int base = lua_gettop(L);
    object = CoronaLuaNormalize(L, object);
    lua_getfield(L, object, "removeSelf");
    if (!lua_isfunction(L, -1))
    { lua_settop(L, base); error = "display object was already removed"; return false; }
    lua_pushvalue(L, object);
    if (lua_pcall(L, 1, 0, 0) != 0)
    { error = PopError(L, "display object removeSelf failed"); lua_settop(L, base); return false; }
    return true;
}

static bool InsertChild(lua_State *L, int group, int child, std::string &error)
{
    int base = lua_gettop(L);
    group = CoronaLuaNormalize(L, group);
    child = CoronaLuaNormalize(L, child);
    lua_getfield(L, group, "insert");
    if (!lua_isfunction(L, -1))
    { lua_settop(L, base); error = "view.group:insert is unavailable"; return false; }
    lua_pushvalue(L, group);
    lua_pushvalue(L, child);
    if (lua_pcall(L, 2, 0, 0) != 0)
    { error = PopError(L, "view.group:insert failed"); lua_settop(L, base); return false; }
    return true;
}

static bool CallSetFillColor(lua_State *L, int mesh, const RibbonState &state,
                             std::string &error)
{
    int base = lua_gettop(L);
    mesh = CoronaLuaNormalize(L, mesh);
    lua_getfield(L, mesh, "setFillColor");
    if (!lua_isfunction(L, -1))
    { lua_settop(L, base); error = "mesh:setFillColor is unavailable"; return false; }
    lua_pushvalue(L, mesh);
    lua_pushnumber(L, state.color.r);
    lua_pushnumber(L, state.color.g);
    lua_pushnumber(L, state.color.b);
    lua_pushnumber(L, state.color.a * state.alpha);
    if (lua_pcall(L, 5, 0, 0) != 0)
    { error = PopError(L, "mesh:setFillColor failed"); lua_settop(L, base); return false; }
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
    { error = PopError(L, "mesh.path:getVertexOffset failed"); lua_settop(L, base); return false; }
    if (!lua_isnumber(L, -2) || !lua_isnumber(L, -1))
    { lua_settop(L, base); error = "mesh.path:getVertexOffset returned invalid values"; return false; }
    x = static_cast<float>(lua_tonumber(L, -2));
    y = static_cast<float>(lua_tonumber(L, -1));
    if (!std::isfinite(x) || !std::isfinite(y))
    { lua_settop(L, base); error = "mesh.path:getVertexOffset returned non-finite values"; return false; }
    lua_settop(L, base);
    return true;
}

static bool TranslateMesh(lua_State *L, int mesh, float x, float y,
                          std::string &error)
{
    if (x == 0.0f && y == 0.0f) return true;
    int base = lua_gettop(L);
    mesh = CoronaLuaNormalize(L, mesh);
    lua_getfield(L, mesh, "translate");
    if (!lua_isfunction(L, -1))
    { lua_settop(L, base); error = "mesh:translate is unavailable"; return false; }
    lua_pushvalue(L, mesh); lua_pushnumber(L, x); lua_pushnumber(L, y);
    if (lua_pcall(L, 3, 0, 0) != 0)
    { error = PopError(L, "mesh:translate failed"); lua_settop(L, base); return false; }
    return true;
}

static int ApplyEffectThunk(lua_State *L)
{
    lua_getfield(L, 1, "fill");
    int fill = CoronaLuaNormalize(L, -1);
    if (!lua_isnil(L, 2))
    {
        lua_pushvalue(L, 2);
        lua_setfield(L, fill, "effect");
    }
    lua_getfield(L, fill, "effect");
    if (!lua_istable(L, -1) && !lua_isuserdata(L, -1))
        return luaL_error(L, "ribbon shader effect is unavailable");
    lua_pushvalue(L, 3); lua_setfield(L, -2, "tailLength");
    lua_pushvalue(L, 4); lua_setfield(L, -2, "headLength");
    return 0;
}

static bool ApplyEffect(lua_State *L, int mesh, const char *effect,
                        bool assign, const RibbonBuilder &builder,
                        std::string &error)
{
    if (!effect || effect[0] == '\0') return true;
    int base = lua_gettop(L);
    mesh = CoronaLuaNormalize(L, mesh);
    lua_pushcfunction(L, ApplyEffectThunk);
    lua_pushvalue(L, mesh);
    if (assign) lua_pushstring(L, effect); else lua_pushnil(L);
    lua_pushnumber(L, builder.TailLength());
    lua_pushnumber(L, builder.HeadLength());
    if (lua_pcall(L, 4, 0, 0) != 0)
    { error = PopError(L, "could not configure ribbon shader"); lua_settop(L, base); return false; }
    return true;
}

static bool CreateMesh(lua_State *L, const RibbonHandle &state,
                       int group, const char *effect,
                       float &offsetX, float &offsetY, std::string &error)
{
    int base = lua_gettop(L);
    group = CoronaLuaNormalize(L, group);
    lua_getglobal(L, "display");
    if (!lua_istable(L, -1))
    { lua_settop(L, base); error = "display API is unavailable"; return false; }
    lua_getfield(L, -1, "newMesh");
    if (!lua_isfunction(L, -1))
    { lua_settop(L, base); error = "display.newMesh is unavailable"; return false; }
    PushMeshDescriptor(L, state);
    if (lua_pcall(L, 1, 1, 0) != 0)
    { error = PopError(L, "display.newMesh failed"); lua_settop(L, base); return false; }
    lua_remove(L, base + 1);
    int mesh = CoronaLuaNormalize(L, -1);
    if (!GetMeshOffset(L, mesh, offsetX, offsetY, error) ||
        !TranslateMesh(L, mesh, offsetX, offsetY, error) ||
        !InsertChild(L, group, mesh, error) ||
        !CallSetFillColor(L, mesh, *state, error) ||
        !ApplyEffect(L, mesh, effect, true, state->builder, error))
    {
        std::string ignored;
        RemoveObject(L, mesh, ignored);
        lua_settop(L, base);
        return false;
    }
    ++state->meshCreateCount;
    return true;
}

static bool UpdateMesh(lua_State *L, int mesh, const RibbonHandle &state,
                       const char *effect, float oldX, float oldY,
                       float &newX, float &newY, std::string &error)
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
    PushMeshDescriptor(L, state);
    if (lua_pcall(L, 2, 0, 0) != 0)
    { error = PopError(L, "mesh.path:update failed"); lua_settop(L, base); return false; }
    lua_settop(L, base);
    if (!GetMeshOffset(L, mesh, newX, newY, error) ||
        !TranslateMesh(L, mesh, newX - oldX, newY - oldY, error) ||
        !CallSetFillColor(L, mesh, *state, error) ||
        !ApplyEffect(L, mesh, effect, false, state->builder, error))
        return false;
    ++state->meshUpdateCount;
    return true;
}

static double NumberField(lua_State *L, int table, const char *field)
{
    table = CoronaLuaNormalize(L, table);
    lua_getfield(L, table, field);
    double result = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 0.0;
    lua_pop(L, 1);
    // View fields are exposed Lua values: never cast NaN or infinity to a count.
    if (!std::isfinite(result))
        luaL_error(L, "invalid ribbon view field '%s'", field);
    return result;
}

static uint64_t CountField(lua_State *L, int table, const char *field)
{
    double value = NumberField(L, table, field);
    if (value < 0.0 || value > 9007199254740991.0 || std::floor(value) != value)
        luaL_error(L, "invalid ribbon view field '%s'", field);
    return static_cast<uint64_t>(value);
}

static const char *StringField(lua_State *L, int table, const char *field)
{
    table = CoronaLuaNormalize(L, table);
    lua_getfield(L, table, field);
    const char *result = lua_isstring(L, -1) ? lua_tostring(L, -1) : nullptr;
    lua_pop(L, 1);
    return result;
}

static void SetViewMetadata(lua_State *L, int view, const RibbonState &state,
                            float offsetX, float offsetY)
{
    view = CoronaLuaNormalize(L, view);
    const RibbonBuilder &builder = state.builder;
#define VIEW_NUMBER_FIELD(name, value) \
    do { lua_pushnumber(L, static_cast<lua_Number>(value)); lua_setfield(L, view, name); } while (0)
    VIEW_NUMBER_FIELD("_geometryRevision", builder.GeometryRevision());
    VIEW_NUMBER_FIELD("_styleRevision", state.styleRevision);
    VIEW_NUMBER_FIELD("_vertexCapacity", builder.VertexCapacity());
    VIEW_NUMBER_FIELD("_indexCapacity", builder.IndexCapacity());
    VIEW_NUMBER_FIELD("_offsetX", offsetX);
    VIEW_NUMBER_FIELD("_offsetY", offsetY);
    VIEW_NUMBER_FIELD("logicalVertexCount", builder.LogicalVertexCount());
    VIEW_NUMBER_FIELD("logicalIndexCount", builder.LogicalIndexCount());
    VIEW_NUMBER_FIELD("vertexCapacity", builder.VertexCapacity());
    VIEW_NUMBER_FIELD("indexCapacity", builder.IndexCapacity());
    VIEW_NUMBER_FIELD("logicalTriangleCount", builder.LogicalTriangleCount());
    VIEW_NUMBER_FIELD("triangleCapacity", builder.TriangleCapacity());
    VIEW_NUMBER_FIELD("tailLength", builder.TailLength());
    VIEW_NUMBER_FIELD("headLength", builder.HeadLength());
    VIEW_NUMBER_FIELD("activeLength", builder.ActiveLength());
#undef VIEW_NUMBER_FIELD
    lua_pushstring(L, builder.Triangles() ? "triangles" : "indexed");
    lua_setfield(L, view, "mode");
}

static bool ViewBelongs(lua_State *L, int ribbon, int view)
{
    ribbon = CoronaLuaNormalize(L, ribbon);
    view = CoronaLuaNormalize(L, view);
    lua_getfield(L, view, "_ribbon");
    bool result = lua_rawequal(L, ribbon, -1) != 0;
    lua_pop(L, 1);
    return result;
}

static bool SyncView(lua_State *L, const RibbonHandle &state, int view,
                     bool force, bool &replaced, bool &updated,
                     std::string &error)
{
    view = CoronaLuaNormalize(L, view);
    uint64_t viewGeometry = CountField(L, view, "_geometryRevision");
    uint64_t viewStyle = CountField(L, view, "_styleRevision");
    bool geometryChanged = force || viewGeometry != state->builder.GeometryRevision();
    bool styleChanged = force || viewStyle != state->styleRevision;
    replaced = false;
    updated = geometryChanged || styleChanged;
    if (!updated) return true;

    bool rebuilt = false;
    if (geometryChanged && !EnsureBuilt(*state, error, rebuilt)) return false;
    lua_getfield(L, view, "group");
    if (lua_isnil(L, -1))
    { lua_pop(L, 1); error = "ribbon view.group was removed"; return false; }
    int group = CoronaLuaNormalize(L, -1);
    lua_getfield(L, view, "mesh");
    int oldMesh = CoronaLuaNormalize(L, -1);
    bool hasMesh = !lua_isnil(L, oldMesh);
    const char *effect = StringField(L, view, "_effect");
    float offsetX = static_cast<float>(NumberField(L, view, "_offsetX"));
    float offsetY = static_cast<float>(NumberField(L, view, "_offsetY"));

    if (state->builder.LogicalVertexCount() == 0)
    {
        if (hasMesh)
        {
            if (!RemoveObject(L, oldMesh, error))
            { lua_pop(L, 2); return false; }
            lua_pushnil(L); lua_setfield(L, view, "mesh");
            replaced = true;
        }
        offsetX = offsetY = 0.0f;
    }
    else if (geometryChanged && hasMesh &&
             CountField(L, view, "_vertexCapacity") ==
                 state->builder.VertexCapacity() &&
             CountField(L, view, "_indexCapacity") ==
                 state->builder.IndexCapacity())
    {
        float newX = 0.0f, newY = 0.0f;
        if (!UpdateMesh(L, oldMesh, state, effect, offsetX, offsetY,
                        newX, newY, error))
        { lua_pop(L, 2); return false; }
        offsetX = newX;
        offsetY = newY;
    }
    else if (geometryChanged)
    {
        int base = lua_gettop(L);
        float newX = 0.0f, newY = 0.0f;
        if (!CreateMesh(L, state, group, effect, newX, newY, error))
        { lua_pop(L, 2); return false; }
        int newMesh = CoronaLuaNormalize(L, -1);
        if (hasMesh && !RemoveObject(L, oldMesh, error))
        {
            std::string ignored;
            RemoveObject(L, newMesh, ignored);
            lua_settop(L, base);
            lua_pop(L, 2);
            return false;
        }
        lua_pushvalue(L, newMesh); lua_setfield(L, view, "mesh");
        lua_pop(L, 1);
        offsetX = newX;
        offsetY = newY;
        replaced = true;
    }
    else if (hasMesh && styleChanged)
    {
        if (!CallSetFillColor(L, oldMesh, *state, error))
        { lua_pop(L, 2); return false; }
    }

    lua_pop(L, 2); // old mesh and group
    SetViewMetadata(L, view, *state, offsetX, offsetY);
    return true;
}

struct ViewUpdateContext {
    const RibbonHandle &state;
    bool force;
    bool replaced = false;
    bool updated = false;
    bool succeeded = false;
    std::string &error;
};

static int SyncViewThunk(lua_State *L)
{
    auto *context = static_cast<ViewUpdateContext *>(lua_touserdata(L, 1));
    try
    {
        context->succeeded = SyncView(L, context->state, 2, context->force,
            context->replaced, context->updated, context->error);
    }
    catch (const std::bad_alloc &)
    {
        context->error = "ribbon view update ran out of native memory";
    }
    catch (const std::exception &exception)
    {
        context->error = exception.what();
    }
    return 0;
}

static bool ProtectedSyncView(lua_State *L, const RibbonHandle &state, int view,
                              bool force, bool &replaced, bool &updated,
                              std::string &error)
{
    view = CoronaLuaNormalize(L, view);
    ViewUpdateContext context{state, force, false, false, false, error};
    lua_pushcfunction(L, SyncViewThunk);
    lua_pushlightuserdata(L, &context);
    lua_pushvalue(L, view);
    // display.* can be wrapped in Lua. Prevent reentrant mutation while the
    // engine consumes borrowed arrays, and restore the guard after Lua errors.
    state->updatingView = true;
    int status = lua_pcall(L, 2, 0, 0);
    state->updatingView = false;
    if (status != 0)
    {
        error = PopError(L, "ribbon view update failed");
        return false;
    }
    replaced = context.replaced;
    updated = context.updated;
    return context.succeeded;
}

static void ValidateViewOptions(lua_State *L, int options, std::string &effect)
{
    if (lua_isnoneornil(L, options)) return;
    if (!lua_istable(L, options)) luaL_argerror(L, options, "expected an options table");
    options = CoronaLuaNormalize(L, options);
    lua_pushnil(L);
    while (lua_next(L, options) != 0)
    {
        if (lua_type(L, -2) != LUA_TSTRING)
            luaL_error(L, "ribbon.newView option keys must be strings");
        const char *name = lua_tostring(L, -2);
        if (std::strcmp(name, "effect") != 0)
            luaL_error(L, "Unknown ribbon.newView option '%s'", name);
        lua_pop(L, 1);
    }
    lua_getfield(L, options, "effect");
    if (!lua_isnil(L, -1))
    {
        if (lua_type(L, -1) != LUA_TSTRING)
            luaL_error(L, "ribbon.newView option 'effect' must be a string");
        effect.assign(lua_tostring(L, -1));
        if (effect.empty()) luaL_error(L, "ribbon.newView effect must not be empty");
    }
    lua_pop(L, 1);
}

static int NewView(lua_State *L)
{
    int count = lua_gettop(L);
    if (count < 1 || count > 2)
        return luaL_error(L, "ribbon:newView expects at most one options table");
    CheckRibbon(L, 1);
    std::string effect;
    ValidateViewOptions(L, 2, effect);
    RibbonHandle state = CheckRibbonHandle(L, 1);
    std::string error;
    bool rebuilt = false;
    if (!EnsureBuilt(*state, error, rebuilt)) return PushFailure(L, error);
    int base = lua_gettop(L);
    if (!NewDisplayGroup(L, error)) return PushFailure(L, error);
    int group = CoronaLuaNormalize(L, -1);
    lua_createtable(L, 0, 18);
    int view = CoronaLuaNormalize(L, -1);
    lua_pushvalue(L, group); lua_setfield(L, view, "group");
    lua_pushvalue(L, 1); lua_setfield(L, view, "_ribbon");
    lua_pushlstring(L, effect.c_str(), effect.size()); lua_setfield(L, view, "_effect");
    lua_pushnumber(L, 0); lua_setfield(L, view, "_geometryRevision");
    lua_pushnumber(L, 0); lua_setfield(L, view, "_styleRevision");
    lua_pushnil(L); lua_setfield(L, view, "mesh");
    bool replaced = false, updated = false;
    if (!ProtectedSyncView(L, state, view, true, replaced, updated, error))
    {
        std::string ignored;
        RemoveObject(L, group, ignored);
        lua_settop(L, base);
        return PushFailure(L, error);
    }
    lua_remove(L, group);
    return 1;
}

static int UpdateView(lua_State *L)
{
    CheckArgCount(L, 2, "ribbon:updateView");
    CheckRibbon(L, 1);
    if (!lua_istable(L, 2)) return luaL_argerror(L, 2, "expected a ribbon view table");
    if (!ViewBelongs(L, 1, 2)) return luaL_argerror(L, 2, "view belongs to another ribbon");
    RibbonHandle state = CheckRibbonHandle(L, 1);
    bool replaced = false, updated = false;
    std::string error;
    if (!ProtectedSyncView(L, state, 2, false, replaced, updated, error))
        return PushFailure(L, error);
    lua_pushvalue(L, 2);
    lua_pushboolean(L, replaced);
    lua_pushboolean(L, updated);
    return 3;
}

static int Destroy(lua_State *L)
{
    CheckArgCount(L, 1, "ribbon:destroy");
    auto *userdata = static_cast<RibbonUserdata *>(
        luaL_checkudata(L, 1, kRibbonMetatable));
    bool destroyed = static_cast<bool>(userdata->state);
    if (destroyed)
    {
        CheckRibbon(L, 1); // rejects destruction inside a display callback
        userdata->state->alive = false;
        InvalidateBuffers(*userdata->state);
        userdata->state.reset();
    }
    lua_pushboolean(L, destroyed);
    return 1;
}

static int RibbonGC(lua_State *L)
{
    RibbonUserdata *userdata = static_cast<RibbonUserdata *>(
        luaL_checkudata(L, 1, kRibbonMetatable));
    if (userdata->state)
    {
        userdata->state->alive = false;
        InvalidateBuffers(*userdata->state);
    }
    userdata->~RibbonUserdata();
    return 0;
}

static int RibbonToString(lua_State *L)
{
    auto *userdata = static_cast<RibbonUserdata *>(
        luaL_checkudata(L, 1, kRibbonMetatable));
    if (!userdata->state)
    {
        lua_pushliteral(L, "plugin.geometry2d ribbon (destroyed)");
        return 1;
    }
    RibbonState *state = userdata->state.get();
    lua_pushfstring(L, "plugin.geometry2d ribbon (%d points)",
                    static_cast<int>(state->builder.PointCount()));
    return 1;
}

static void CheckDenseColor(lua_State *L, int table, RibbonColor &color)
{
    if (!lua_istable(L, table)) luaL_error(L, "ribbon color must be a dense array");
    table = CoronaLuaNormalize(L, table);
    size_t count = lua_objlen(L, table);
    if (count != 3 && count != 4) luaL_error(L, "ribbon color must contain r, g, b[, a]");
    lua_pushnil(L);
    while (lua_next(L, table) != 0)
    {
        if (lua_type(L, -2) != LUA_TNUMBER)
            luaL_error(L, "ribbon color must contain only array entries");
        double key = lua_tonumber(L, -2);
        if (std::floor(key) != key || key < 1.0 || key > count)
            luaL_error(L, "ribbon color must be a dense array");
        lua_pop(L, 1);
    }
    float values[4] = {1, 1, 1, 1};
    for (size_t i = 0; i < count; ++i)
    {
        lua_rawgeti(L, table, static_cast<int>(i + 1));
        values[i] = CheckUnit(L, -1, "color component");
        lua_pop(L, 1);
    }
    color = {values[0], values[1], values[2], values[3]};
}

static void ReadNewOptions(lua_State *L, int options, RibbonBuildOptions &build,
                           RibbonColor &color, float &alpha)
{
    if (lua_isnoneornil(L, options)) return;
    if (!lua_istable(L, options)) luaL_argerror(L, options, "expected an options table");
    options = CoronaLuaNormalize(L, options);
    const char *allowed[] = {"width", "aaWidth", "minDistance", "reverseDot",
        "initialPointCapacity", "maxPoints", "capacityTiers", "color", "alpha",
        "timestampMode", "mode", "miterLimit"};
    lua_pushnil(L);
    while (lua_next(L, options) != 0)
    {
        if (lua_type(L, -2) != LUA_TSTRING)
            luaL_error(L, "ribbon.new option keys must be strings");
        const char *name = lua_tostring(L, -2);
        bool known = false;
        for (const char *candidate : allowed)
            if (std::strcmp(name, candidate) == 0) { known = true; break; }
        if (!known) luaL_error(L, "Unknown ribbon.new option '%s'", name);
        lua_pop(L, 1);
    }

    lua_getfield(L, options, "mode");
    if (!lua_isnil(L, -1))
    {
        if (lua_type(L, -1) != LUA_TSTRING)
            luaL_error(L, "ribbon mode must be 'indexed' or 'triangles'");
        size_t length = 0;
        const char *mode = lua_tolstring(L, -1, &length);
        if (length == 7 && std::memcmp(mode, "indexed", 7) == 0) build.triangles = false;
        else if (length == 9 && std::memcmp(mode, "triangles", 9) == 0) build.triangles = true;
        else luaL_error(L, "ribbon mode must be 'indexed' or 'triangles'");
    }
    lua_pop(L, 1);
    lua_getfield(L, options, "miterLimit");
    if (!lua_isnil(L, -1))
    {
        build.miterLimit = CheckFiniteFloat(L, -1, "miterLimit");
        if (build.miterLimit < 1.0f) luaL_error(L, "miterLimit must be at least 1");
    }
    lua_pop(L, 1);

    lua_getfield(L, options, "timestampMode");
    if (!lua_isnil(L, -1))
    {
        if (lua_type(L, -1) != LUA_TSTRING)
            luaL_error(L, "timestampMode must be 'monotonic' or 'legacyCount'");
        size_t length = 0;
        const char *mode = lua_tolstring(L, -1, &length);
        if (length == 9 && std::memcmp(mode, "monotonic", 9) == 0)
            build.timestampMode = RibbonTimestampMode::Monotonic;
        else if (length == 11 && std::memcmp(mode, "legacyCount", 11) == 0)
            build.timestampMode = RibbonTimestampMode::LegacyCount;
        else
            luaL_error(L, "timestampMode must be 'monotonic' or 'legacyCount'");
    }
    lua_pop(L, 1);

    lua_getfield(L, options, "width");
    if (!lua_isnil(L, -1))
    { build.width = CheckFiniteFloat(L, -1, "width"); if (build.width <= 0) luaL_error(L, "width must be positive"); }
    lua_pop(L, 1);
    lua_getfield(L, options, "aaWidth");
    if (!lua_isnil(L, -1))
    { build.aaWidth = CheckFiniteFloat(L, -1, "aaWidth"); if (build.aaWidth < 0) luaL_error(L, "aaWidth must be non-negative"); }
    lua_pop(L, 1);
    lua_getfield(L, options, "minDistance");
    if (!lua_isnil(L, -1))
    { build.minDistance = CheckFiniteFloat(L, -1, "minDistance"); if (build.minDistance < 0) luaL_error(L, "minDistance must be non-negative"); }
    lua_pop(L, 1);
    lua_getfield(L, options, "reverseDot");
    if (!lua_isnil(L, -1))
    { build.reverseDot = CheckFiniteFloat(L, -1, "reverseDot"); if (build.reverseDot < -1 || build.reverseDot > 1) luaL_error(L, "reverseDot must be between -1 and 1"); }
    lua_pop(L, 1);

    lua_getfield(L, options, "maxPoints");
    bool hasMaxPoints = !lua_isnil(L, -1);
    if (hasMaxPoints) build.maxPoints = CheckSize(L, -1, 2, 8191, "maxPoints");
    lua_pop(L, 1);
    lua_getfield(L, options, "initialPointCapacity");
    bool hasInitialCapacity = !lua_isnil(L, -1);
    if (hasInitialCapacity)
        build.initialPointCapacity = CheckSize(L, -1, 2, build.maxPoints,
                                               "initialPointCapacity");
    lua_pop(L, 1);
    if (hasMaxPoints && !hasInitialCapacity)
        build.initialPointCapacity = (std::min)(build.initialPointCapacity,
                                                build.maxPoints);
    if (build.initialPointCapacity > build.maxPoints)
        luaL_error(L, "initialPointCapacity must not exceed maxPoints");

    lua_getfield(L, options, "capacityTiers");
    if (!lua_isnil(L, -1))
    {
        if (!lua_isboolean(L, -1)) luaL_error(L, "capacityTiers must be a boolean");
        build.capacityTiers = lua_toboolean(L, -1) != 0;
    }
    lua_pop(L, 1);
    lua_getfield(L, options, "color");
    if (!lua_isnil(L, -1)) CheckDenseColor(L, -1, color);
    lua_pop(L, 1);
    lua_getfield(L, options, "alpha");
    if (!lua_isnil(L, -1)) alpha = CheckUnit(L, -1, "alpha");
    lua_pop(L, 1);
}

static int NewRibbon(lua_State *L)
{
    if (lua_gettop(L) > 1) return luaL_error(L, "ribbon.new accepts at most one options table");
    RibbonBuildOptions build;
    RibbonColor color;
    float alpha = 1.0f;
    ReadNewOptions(L, 1, build, color, alpha);
    void *memory = lua_newuserdata(L, sizeof(RibbonUserdata));
    RibbonUserdata *userdata = new (memory) RibbonUserdata();
    // Install __gc before allocating the state so failed allocation is safe.
    luaL_getmetatable(L, kRibbonMetatable);
    lua_setmetatable(L, -2);
    userdata->state = std::make_shared<RibbonState>(build);
    userdata->state->color = color;
    userdata->state->alpha = alpha;
    return 1;
}

template<int (*Function)(lua_State *)>
static int CheckedCall(lua_State *L)
{
    try { return Function(L); }
    catch (const std::bad_alloc &)
    {
        lua_pushnil(L);
        lua_pushliteral(L, "ribbon ran out of native memory");
    }
    catch (const std::exception &exception)
    {
        lua_pushnil(L);
        lua_pushstring(L, exception.what());
    }
    return 2;
}

} // namespace

void RegisterRibbon(lua_State *L)
{
    if (luaL_newmetatable(L, kRibbonMetatable))
    {
        lua_pushvalue(L, -1); lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, RibbonGC); lua_setfield(L, -2, "__gc");
        lua_pushcfunction(L, RibbonToString); lua_setfield(L, -2, "__tostring");
        lua_pushliteral(L, "geometry2d ribbon"); lua_setfield(L, -2, "__metatable");
        luaL_Reg methods[] = {
            {"addPoint", CheckedCall<AddPoint>}, {"expire", CheckedCall<Expire>},
            {"clear", CheckedCall<Clear>}, {"destroy", CheckedCall<Destroy>},
            {"reservePoints", CheckedCall<ReservePoints>}, {"setWidth", CheckedCall<SetWidth>},
            {"setAAWidth", CheckedCall<SetAAWidth>}, {"setColor", CheckedCall<SetColor>},
            {"setAlpha", CheckedCall<SetAlpha>}, {"pointCount", CheckedCall<PointCount>},
            {"getStats", CheckedCall<GetStats>}, {"snapshot", CheckedCall<Snapshot>},
            {"newView", CheckedCall<NewView>}, {"updateView", CheckedCall<UpdateView>},
            {nullptr, nullptr}
        };
        luaL_register(L, nullptr, methods);
    }
    lua_pop(L, 1);
    lua_pushcfunction(L, CheckedCall<NewRibbon>);
    lua_setfield(L, -2, "new");
}

} // namespace Geometry2D
