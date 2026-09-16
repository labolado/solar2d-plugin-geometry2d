/*
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * [ MIT license: http://www.opensource.org/licenses/mit-license.php ]
 */

/*
 * plugin.geometry2d — Solar2D native geometry helpers.
 *
 * The entry point intentionally contains registration only. Algorithm,
 * path-flattening, buffer, and mesh-output code lives in focused translation
 * units under src/shared/.
 */

#include "geometry2d_lua.h"

using namespace Geometry2D;

CORONA_EXPORT int luaopen_plugin_geometry2d(lua_State *L)
{
    lua_newtable(L);

    lua_newtable(L);
    RegisterPolypartition(L);
    lua_setfield(L, -2, "polypartition");

    lua_newtable(L);
    RegisterEarcut(L);
    lua_setfield(L, -2, "earcut");

    lua_newtable(L);
    RegisterFringe(L);
    lua_setfield(L, -2, "fringe");

    lua_newtable(L);
    RegisterUtil(L);
    lua_setfield(L, -2, "util");

    lua_newtable(L);
    RegisterPath(L);
    lua_setfield(L, -2, "path");

    lua_newtable(L);
    RegisterClipper2(L);
    lua_setfield(L, -2, "clipper2");

    lua_newtable(L);
    RegisterRibbon(L);
    lua_setfield(L, -2, "ribbon");

    return 1;
}
