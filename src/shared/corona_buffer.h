#pragma once

#include "CoronaLua.h"

#include <cstddef>

namespace Geometry2D {

void PushReadableBuffer(lua_State *L, const void *bytes, size_t size);
void PushBufferDescriptor(lua_State *L, const void *bytes, size_t size,
                          size_t count, int componentCount = 0);

} // namespace Geometry2D
