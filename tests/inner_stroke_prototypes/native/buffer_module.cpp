// Test-only owning buffer loader. Same entry symbol, explicitly loaded by path;
// never installed or linked into the released plugin.
#include "corona_buffer.h"
static int Bytes(lua_State* L) {
    size_t size=0;const char* bytes=luaL_checklstring(L,1,&size);
    Geometry2D::PushReadableBuffer(L,bytes,size);return 1;
}
CORONA_EXPORT int luaopen_plugin_geometry2d(lua_State* L) {
    lua_newtable(L);lua_pushcfunction(L,Bytes);lua_setfield(L,-2,"bytes");return 1;
}
