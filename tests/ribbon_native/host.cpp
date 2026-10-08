// Lua 5.1 binding test host. Display is mocked; this does not test Solar2D rendering.
#include "geometry2d_lua.h"
#include "CoronaMemory.h"
#include <cstdio>
#include <cstring>

extern "C" int CoronaLuaNormalize(lua_State *L, int index)
{
    return index < 0 && index > LUA_REGISTRYINDEX ? lua_gettop(L) + index + 1 : index;
}

extern "C" int CoronaMemoryCreateInterface(lua_State *L, const CoronaMemoryInterfaceInfo *info)
{
    auto *copy = static_cast<CoronaMemoryInterfaceInfo *>(lua_newuserdata(L, sizeof(*info)));
    *copy = *info;
    return 1;
}

static int ReadBuffer(lua_State *L)
{
    bool indices = lua_toboolean(L, 2) != 0;
    lua_getfield(L, 1, "buffer");
    int buffer = lua_gettop(L);
    lua_getmetatable(L, buffer);
    lua_getfield(L, -1, "__memory");
    auto *info = static_cast<CoronaMemoryInterfaceInfo *>(lua_touserdata(L, -1));
    CoronaMemoryWorkspace workspace{};
    if (!info || !info->getObject(L, buffer, &workspace)) return luaL_error(L, "invalid buffer");
    const void *bytes = info->callbacks.getReadableBytes(&workspace);
    size_t count = info->callbacks.getByteCount(&workspace) / (indices ? sizeof(uint16_t) : sizeof(float));
    lua_createtable(L, static_cast<int>(count), 0);
    for (size_t i=0; i<count; ++i)
    {
        lua_pushnumber(L, indices ? static_cast<const uint16_t *>(bytes)[i] + 1 :
                                   static_cast<const float *>(bytes)[i]);
        lua_rawseti(L, -2, static_cast<int>(i+1));
    }
    return 1;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    lua_newtable(L);
    Geometry2D::RegisterRibbon(L);
    lua_setglobal(L, "Ribbon");
    lua_pushcfunction(L, ReadBuffer); lua_setglobal(L, "readBuffer");
    int status = luaL_dofile(L, argv[1]);
    if (status) std::fprintf(stderr, "%s\n", lua_tostring(L, -1));
    lua_close(L);
    return status ? 1 : 0;
}
