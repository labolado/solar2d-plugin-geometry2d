#include "corona_buffer.h"

#include "CoronaMemory.h"

#include <cstring>

namespace Geometry2D {

void PushReadableBuffer(lua_State *L, const void *bytes, size_t size)
{
    void *userdata = lua_newuserdata(L, size);
    if (size > 0) std::memcpy(userdata, bytes, size);

    static const char *kMetatable = "geometry2d.readable_buffer";
    if (luaL_newmetatable(L, kMetatable))
    {
        CoronaMemoryInterfaceInfo info = {};
        info.callbacks.getReadableBytes = [](CoronaMemoryWorkspace *workspace) {
            return workspace->vars[0].cp;
        };
        info.callbacks.getByteCount = [](CoronaMemoryWorkspace *workspace) {
            return workspace->vars[1].size;
        };
        info.getObject = [](lua_State *state, int arg, CoronaMemoryWorkspace *workspace) {
            workspace->vars[0].cp = static_cast<const char *>(lua_touserdata(state, arg));
            workspace->vars[1].size = lua_objlen(state, arg);
            return 1;
        };
        CoronaMemoryCreateInterface(L, &info);
        lua_setfield(L, -2, "__memory");
    }
    lua_setmetatable(L, -2);
}

void PushBufferDescriptor(lua_State *L, const void *bytes, size_t size,
                          size_t count, int componentCount)
{
    lua_createtable(L, 0, componentCount > 0 ? 3 : 2);
    PushReadableBuffer(L, bytes, size);
    lua_setfield(L, -2, "buffer");
    lua_pushinteger(L, static_cast<lua_Integer>(count));
    lua_setfield(L, -2, "count");
    if (componentCount > 0)
    {
        lua_pushinteger(L, componentCount);
        lua_setfield(L, -2, "componentCount");
    }
}

} // namespace Geometry2D
