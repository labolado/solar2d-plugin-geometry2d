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

#include "polypartition.h"
#include "mapbox/earcut.hpp"
#include "fringe.h"
#include "utils/LuaEx.h"
#define BR_NAMESPACE_PREFIX geometry2d_br
#include "ByteReader.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers: Read a single polygon from Lua (flat table or bytes)
// ---------------------------------------------------------------------------

// Determine if the table at `arg` is a flat array of numbers (polygon)
// vs. a table of tables (polygon list). Returns true if it looks like a
// flat polygon table (first element is a number, or the table is empty).
static bool IsFlatPolygonTable(lua_State *L, int arg)
{
    if (!lua_istable(L, arg)) return false;

    size_t n = lua_objlen(L, arg);
    if (n == 0) return true;  // empty table → degenerate polygon (will fail validation later)

    lua_rawgeti(L, arg, 1);               // ..., first
    bool isNumber = lua_isnumber(L, -1) != 0;
    lua_pop(L, 1);                        // ...
    return isNumber;
}

// Read a TPPLPoly from a flat Lua table of numbers: {x1, y1, x2, y2, ...}
// Returns true on success.
static bool ReadPolygonFromTable(lua_State *L, int arg, TPPLPoly &poly, bool isHole = false)
{
    arg = CoronaLuaNormalize(L, arg);      // fix to absolute index — loop pushes shift the stack
    size_t n = lua_objlen(L, arg);
    if (n < 6) return false;              // need at least 3 points (6 numbers)
    if ((n % 2) != 0) return false;       // must be pairs

    long numpoints = (long)(n / 2);
    poly.Init(numpoints);
    poly.SetHole(isHole);

    for (int i = 0; i < (int)numpoints; ++i)
    {
        lua_rawgeti(L, arg, i * 2 + 1);          // ..., x
        lua_rawgeti(L, arg, i * 2 + 2);          // ..., x, y

        poly[i].x = (tppl_float)luaL_checknumber(L, -2);
        poly[i].y = (tppl_float)luaL_checknumber(L, -1);
        poly[i].id = i;

        lua_pop(L, 2);                            // ...
    }
    return true;
}

// Read a TPPLPoly from a raw bytes string.
// Tries double (8 bytes/coord) first, then float (4 bytes/coord).
// Returns true on success.
static bool ReadPolygonFromBytes(lua_State *L, int arg, TPPLPoly &poly, bool isHole = false)
{
    ByteReader reader{L, arg};
    if (!reader.mBytes || reader.mCount == 0) return false;

    auto TryRead = [&](size_t elemSize) -> bool {
        size_t numCoords = reader.mCount / elemSize;
        if (numCoords < 6 || (numCoords % 2) != 0) return false;

        long numpoints = (long)(numCoords / 2);
        poly.Init(numpoints);
        poly.SetHole(isHole);

        if (elemSize == sizeof(double))
        {
            const double *data = static_cast<const double *>(reader.mBytes);
            for (int i = 0; i < (int)numpoints; ++i)
            {
                poly[i].x = (tppl_float)data[i * 2];
                poly[i].y = (tppl_float)data[i * 2 + 1];
                poly[i].id = i;
            }
            return true;
        }
        if (elemSize == sizeof(float))
        {
            const float *data = static_cast<const float *>(reader.mBytes);
            for (int i = 0; i < (int)numpoints; ++i)
            {
                poly[i].x = (tppl_float)data[i * 2];
                poly[i].y = (tppl_float)data[i * 2 + 1];
                poly[i].id = i;
            }
            return true;
        }
        if (elemSize == sizeof(int32_t))
        {
            const int32_t *data = static_cast<const int32_t *>(reader.mBytes);
            for (int i = 0; i < (int)numpoints; ++i)
            {
                poly[i].x = (tppl_float)data[i * 2];
                poly[i].y = (tppl_float)data[i * 2 + 1];
                poly[i].id = i;
            }
            return true;
        }
        return false;
    };

    // Try each element size in descending order (prefer double)
    if (TryRead(sizeof(double))) return true;
    if (TryRead(sizeof(float))) return true;
    if (TryRead(sizeof(int32_t))) return true;

    return false;
}

// Read a single polygon from Lua.
// Accepts: flat table {x1,y1,...} or raw bytes string.
// Returns true on success, false if arg doesn't look like a polygon.
static bool ReadPolygon(lua_State *L, int arg, TPPLPoly &poly, bool isHole = false)
{
    arg = CoronaLuaNormalize(L, arg);      // fix to absolute index — getfield pushes shift the stack
    int t = lua_type(L, arg);
    if (t == LUA_TSTRING)
    {
        return ReadPolygonFromBytes(L, arg, poly, isHole);
    }
    if (t == LUA_TTABLE)
    {
        // Check for {points={...}, hole=bool} format
        lua_getfield(L, arg, "points");           // ..., points?
        if (!lua_isnil(L, -1))
        {
            lua_getfield(L, arg, "hole");         // ..., points, hole?
            bool hole = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);                        // ..., points

            bool ok = ReadPolygonFromTable(L, -1, poly, hole);
            lua_pop(L, 1);                        // ...
            return ok;
        }
        lua_pop(L, 1);                            // ...

        return ReadPolygonFromTable(L, arg, poly, isHole);
    }
    return false;
}

// ---------------------------------------------------------------------------
// Helpers: Read a polygon list from Lua
// ---------------------------------------------------------------------------

// Read a TPPLPolyList from a Lua table of polygons.
// Each element may be:
//   - a flat table {x1,y1,...}
//   - a table {points={x1,y1,...}, hole=true/false}
//   - a bytes string
// Returns true on success.
static bool ReadPolygonList(lua_State *L, int arg, TPPLPolyList &list)
{
    if (!lua_istable(L, arg)) return false;

    size_t n = lua_objlen(L, arg);
    for (size_t idx = 1; idx <= n; ++idx)
    {
        lua_rawgeti(L, arg, (int)idx);            // ..., elem

        TPPLPoly poly;
        if (!ReadPolygon(L, -1, poly))
        {
            lua_pop(L, 1);                        // ...
            return false;
        }
        list.push_back(poly);
        lua_pop(L, 1);                            // ...
    }
    return true;
}

// ---------------------------------------------------------------------------
// Helpers: Push results back to Lua
// ---------------------------------------------------------------------------

// Push a TPPLPolyList onto the Lua stack as an array of flat tables.
//   { {x1,y1, x2,y2, x3,y3}, {x1,y1, x2,y2, x3,y3}, ... }
static void PushPolygonList(lua_State *L, TPPLPolyList &list)
{
    int count = 0;
    lua_createtable(L, (int)list.size(), 0);      // ..., result

    for (auto it = list.begin(); it != list.end(); ++it)
    {
        TPPLPoly &poly = *it;
        int np = (int)poly.GetNumPoints();

        lua_createtable(L, np * 2, 0);            // ..., result, poly_table

        for (int i = 0; i < np; ++i)
        {
            lua_pushnumber(L, poly[i].x);
            lua_rawseti(L, -2, i * 2 + 1);
            lua_pushnumber(L, poly[i].y);
            lua_rawseti(L, -2, i * 2 + 2);
        }

        lua_rawseti(L, -2, ++count);              // ..., result
    }
}

// ---------------------------------------------------------------------------
// Algorithm wrappers — each reads input, calls the C++ algorithm, returns result
// ---------------------------------------------------------------------------

#define WRAP_SINGLE_POLY(name)                                              \
static int name##_fn(lua_State *L)                                          \
{                                                                           \
    TPPLPoly poly;                                                          \
    if (!ReadPolygon(L, 1, poly))                                           \
        return luaL_argerror(L, 1, "Expected polygon (flat table or bytes)"); \
    TPPLPartition partition;                                                \
    TPPLPolyList result;                                                    \
    if (!partition.name(&poly, &result))                                    \
        luaL_error(L, #name " failed");                                     \
    PushPolygonList(L, result);                                             \
    return 1;                                                               \
}

#define WRAP_SINGLE_OR_LIST(name)                                           \
static int name##_fn(lua_State *L)                                          \
{                                                                           \
    TPPLPartition partition;                                                \
    TPPLPolyList result;                                                    \
    bool ok;                                                                \
    if (IsFlatPolygonTable(L, 1) || lua_type(L, 1) == LUA_TSTRING)          \
    {                                                                       \
        TPPLPoly poly;                                                      \
        if (!ReadPolygon(L, 1, poly))                                       \
            return luaL_argerror(L, 1, "Expected polygon or polygon list"); \
        ok = partition.name(&poly, &result) != 0;                            \
    }                                                                       \
    else if (lua_istable(L, 1))                                             \
    {                                                                       \
        TPPLPolyList inList;                                                \
        if (!ReadPolygonList(L, 1, inList))                                 \
            return luaL_argerror(L, 1, "Expected polygon or polygon list"); \
        ok = partition.name(&inList, &result) != 0;                          \
    }                                                                       \
    else                                                                    \
    {                                                                       \
        return luaL_argerror(L, 1, "Expected polygon (flat table or bytes) or polygon list"); \
    }                                                                       \
    if (!ok) luaL_error(L, #name " failed");                                \
    PushPolygonList(L, result);                                             \
    return 1;                                                               \
}

#define WRAP_LIST_ONLY(name)                                                \
static int name##_fn(lua_State *L)                                          \
{                                                                           \
    TPPLPolyList inList, result;                                            \
    if (!ReadPolygonList(L, 1, inList))                                     \
        return luaL_argerror(L, 1, "Expected polygon list (table of tables)"); \
    TPPLPartition partition;                                                \
    if (!partition.name(&inList, &result))                                  \
        luaL_error(L, #name " failed");                                     \
    PushPolygonList(L, result);                                             \
    return 1;                                                               \
}

// ---------------------------------------------------------------------------
// Smart split: peel off a chunk of maxVertices from a CONVEX polygon along
// diagonal v0–v_{M-1}. The cut is always inside a convex polygon so both
// pieces are convex. Recurses on the remainder.
// ---------------------------------------------------------------------------
static void SplitConvexPolygon(TPPLPoly &poly, TPPLPolyList &result, int maxVertices)
{
    long n = poly.GetNumPoints();
    if (n <= maxVertices) { result.push_back(poly); return; }

    // Piece A: v0, v1, ..., v_{M-1}  (maxVertices vertices)
    TPPLPoly piece;
    piece.Init(maxVertices);
    for (int i = 0; i < maxVertices; ++i) piece[i] = poly[i];
    piece.SetHole(false);
    result.push_back(piece);

    // Piece B: v0, v_{M-1}, v_M, ..., v_{N-1}  (n - maxVertices + 2 vertices)
    long remaining = n - maxVertices + 2;
    TPPLPoly rest;
    rest.Init(remaining);
    rest[0] = poly[0];                      // v0
    rest[1] = poly[maxVertices - 1];        // v_{M-1}
    for (long i = maxVertices; i < n; ++i)
        rest[i - maxVertices + 2] = poly[i];
    rest.SetHole(false);

    SplitConvexPolygon(rest, result, maxVertices);
}

// ---------------------------------------------------------------------------
// Vertex-limit helper: recursively partition until every part ≤ maxVertices.
// Tries HM first; if the polygon is already convex (HM returns it unchanged),
// uses smart diagonal cuts instead of degenerate triangulation.
// ---------------------------------------------------------------------------
static void PartitionWithLimit(TPPLPoly &poly, TPPLPolyList &result, int maxVertices)
{
    if (poly.GetNumPoints() <= maxVertices)
    {
        result.push_back(poly);
        return;
    }

    TPPLPartition partition;
    TPPLPolyList parts;

    if (!partition.ConvexPartition_HM(&poly, &parts) ||
        (parts.size() == 1 && parts.front().GetNumPoints() == poly.GetNumPoints()))
    {
        // Already convex — smart split keeps chunks large (maxVertices each)
        SplitConvexPolygon(poly, result, maxVertices);
        return;
    }

    for (auto &part : parts) PartitionWithLimit(part, result, maxVertices);
}

// ---------------------------------------------------------------------------
// Helper: parse maxVertices from an optional opts table at stack position `arg`.
// Returns 0 if not present (meaning "no limit").
// ---------------------------------------------------------------------------
static int GetMaxVertices(lua_State *L, int arg)
{
    if (!lua_istable(L, arg)) return 0;
    lua_getfield(L, arg, "maxVertices");      // ..., opts, maxVertices?
    int mv = lua_isnil(L, -1) ? 0 : (int)luaL_checkinteger(L, -1);
    lua_pop(L, 1);                            // ...
    return mv;
}

// ---------------------------------------------------------------------------
// Custom wrappers for convex partition (support optional {maxVertices=N})
// ---------------------------------------------------------------------------
static int ConvexPartition_HM_fn(lua_State *L)
{
    int maxVerts = GetMaxVertices(L, 2);

    TPPLPartition partition;
    TPPLPolyList result;
    bool ok;

    if (IsFlatPolygonTable(L, 1) || lua_type(L, 1) == LUA_TSTRING)
    {
        TPPLPoly poly;
        if (!ReadPolygon(L, 1, poly))
            return luaL_argerror(L, 1, "Expected polygon or polygon list");
        ok = partition.ConvexPartition_HM(&poly, &result) != 0;
    }
    else if (lua_istable(L, 1))
    {
        TPPLPolyList inList;
        if (!ReadPolygonList(L, 1, inList))
            return luaL_argerror(L, 1, "Expected polygon or polygon list");
        ok = partition.ConvexPartition_HM(&inList, &result) != 0;
    }
    else
    {
        return luaL_argerror(L, 1, "Expected polygon or polygon list");
    }
    if (!ok) luaL_error(L, "ConvexPartition_HM failed");

    if (maxVerts > 0)
    {
        // Post-process: recursively split oversized convex parts
        TPPLPolyList limited;
        for (auto &part : result) PartitionWithLimit(part, limited, maxVerts);
        result.swap(limited);
    }

    PushPolygonList(L, result);
    return 1;
}

static int ConvexPartition_OPT_fn(lua_State *L)
{
    TPPLPoly poly;
    if (!ReadPolygon(L, 1, poly))
        return luaL_argerror(L, 1, "Expected polygon (flat table or bytes)");

    int maxVerts = GetMaxVertices(L, 2);

    TPPLPartition partition;
    TPPLPolyList result;
    if (!partition.ConvexPartition_OPT(&poly, &result))
        luaL_error(L, "ConvexPartition_OPT failed");

    if (maxVerts > 0)
    {
        TPPLPolyList limited;
        for (auto &part : result) PartitionWithLimit(part, limited, maxVerts);
        result.swap(limited);
    }

    PushPolygonList(L, result);
    return 1;
}

// Single-polygon-only algorithms
WRAP_SINGLE_POLY(Triangulate_OPT)

// Algorithms with both single-polygon and polygon-list overloads
WRAP_SINGLE_OR_LIST(Triangulate_EC)
WRAP_SINGLE_OR_LIST(Triangulate_MONO)

// Polygon-list-only algorithms
WRAP_LIST_ONLY(RemoveHoles)
WRAP_LIST_ONLY(MonotonePartition)

// ===========================================================================
// earcut module — fast robust triangulation with native hole support
// ===========================================================================

// Read a single ring (flat table) into a vector of {x,y} arrays.
static void ReadEarcutRing(lua_State *L, int arg, std::vector<std::array<double, 2>> &ring)
{
    arg = CoronaLuaNormalize(L, arg);
    size_t n = lua_objlen(L, arg);
    if (n < 6) return;
    ring.reserve(n / 2);
    for (size_t i = 0; i < n / 2; ++i)
    {
        lua_rawgeti(L, arg, (int)(i * 2 + 1));
        lua_rawgeti(L, arg, (int)(i * 2 + 2));
        ring.push_back({luaL_checknumber(L, -2), luaL_checknumber(L, -1)});
        lua_pop(L, 2);
    }
}

// Read polygon rings into the vector-of-rings format that earcut expects.
// Accepts:
//   1) flat table       {x1,y1, x2,y2, ...}          single ring
//   2) table of tables  {{x1,y1,...}, {hx1,hy1,...}}  outer + holes
//   3) named format     {poly={...}, holes={{...},...}}
static void ReadEarcutPolygon(lua_State *L, int arg,
                               std::vector<std::vector<std::array<double, 2>>> &poly)
{
    if (!lua_istable(L, arg)) return;

    // 1) Named format  {poly={...}, holes={{...}, ...}}
    lua_getfield(L, arg, "poly");                    // ..., poly?
    if (!lua_isnil(L, -1))
    {
        poly.resize(1);
        ReadEarcutRing(L, -1, poly[0]);
        lua_pop(L, 1);                               // ...

        lua_getfield(L, arg, "holes");               // ..., holes?
        if (lua_istable(L, -1))
        {
            size_t nh = lua_objlen(L, -1);
            for (size_t i = 1; i <= nh; ++i)
            {
                lua_rawgeti(L, -1, (int)i);
                poly.resize(poly.size() + 1);
                ReadEarcutRing(L, -1, poly.back());
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);                               // ...
        return;
    }
    lua_pop(L, 1);                                   // ...

    // 2) Single flat ring  {x1,y1, x2,y2, ...}
    if (IsFlatPolygonTable(L, arg))
    {
        poly.resize(1);
        ReadEarcutRing(L, arg, poly[0]);
        return;
    }

    // 3) Array of rings  {{x1,y1,...}, {hx1,hy1,...}, ...}
    size_t n = lua_objlen(L, arg);
    poly.resize(n);
    for (size_t i = 1; i <= n; ++i)
    {
        lua_rawgeti(L, arg, (int)i);
        ReadEarcutRing(L, -1, poly[i - 1]);
        lua_pop(L, 1);
    }
}

// Flatten all ring vertices into one contiguous coord array.
static void FlattenEarcutCoords(const std::vector<std::vector<std::array<double, 2>>> &poly,
                                 std::vector<std::array<double, 2>> &coords)
{
    for (auto &ring : poly)
        for (auto &p : ring)
            coords.push_back(p);
}

// Build triangle polygons from earcut indices + coords, push as Lua table
// of flat tables (same format as polypartition output).
static void PushEarcutTriangles(lua_State *L, const std::vector<uint32_t> &indices,
                                 const std::vector<std::array<double, 2>> &coords)
{
    int ntri = (int)indices.size() / 3;
    lua_createtable(L, ntri, 0);                     // ..., result
    for (int t = 0; t < ntri; ++t)
    {
        lua_createtable(L, 6, 0);                    // ..., result, tri
        for (int v = 0; v < 3; ++v)
        {
            uint32_t idx = indices[t * 3 + v];
            lua_pushnumber(L, coords[idx][0]);
            lua_rawseti(L, -2, v * 2 + 1);
            lua_pushnumber(L, coords[idx][1]);
            lua_rawseti(L, -2, v * 2 + 2);
        }
        lua_rawseti(L, -2, t + 1);                   // ..., result
    }
}

// Push {vertices={x1,y1,...}, indices={1,2,3,...}} — ready for display.newMesh.
static void PushEarcutMesh(lua_State *L, const std::vector<uint32_t> &indices,
                            const std::vector<std::array<double, 2>> &coords)
{
    lua_createtable(L, 0, 2);                        // ..., mesh

    // mesh.vertices = {x1, y1, x2, y2, ...}
    int nv = (int)coords.size();
    lua_createtable(L, nv * 2, 0);                   // ..., mesh, vertices
    for (int i = 0; i < nv; ++i)
    {
        lua_pushnumber(L, coords[i][0]);
        lua_rawseti(L, -2, i * 2 + 1);
        lua_pushnumber(L, coords[i][1]);
        lua_rawseti(L, -2, i * 2 + 2);
    }
    lua_setfield(L, -2, "vertices");                 // ..., mesh

    // mesh.indices = {1,2,3, 1,3,4, ...}  (1-based for Corona)
    int ni = (int)indices.size();
    lua_createtable(L, ni, 0);                       // ..., mesh, indices
    for (int i = 0; i < ni; ++i)
    {
        lua_pushinteger(L, indices[i] + 1);           // 0-based → 1-based
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "indices");                  // ..., mesh
}

static int EarcutTriangulate_fn(lua_State *L)
{
    std::vector<std::vector<std::array<double, 2>>> poly;
    ReadEarcutPolygon(L, 1, poly);

    if (poly.empty() || poly[0].size() < 3)
        return luaL_argerror(L, 1, "Expected polygon with at least 3 vertices");

    std::vector<std::array<double, 2>> coords;
    FlattenEarcutCoords(poly, coords);

    auto indices = mapbox::earcut<uint32_t>(poly);

    bool mesh = false;
    if (lua_istable(L, 2))
    {
        lua_getfield(L, 2, "refine");
        if (lua_toboolean(L, -1))
            mapbox::refine<uint32_t>(indices, coords);
        lua_pop(L, 1);

        lua_getfield(L, 2, "mesh");                  // ..., opts, mesh?
        mesh = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
    }

    if (mesh)
    {
        PushEarcutMesh(L, indices, coords);           // ..., mesh_table
        return 1;
    }

    PushEarcutTriangles(L, indices, coords);
    return 1;
}

// ===========================================================================
// fringe module — AA fringe (skirt) generation for display.newMesh
// (adapted from NanoVG's nvg__expandFill / nvg__expandStroke)
// ===========================================================================

struct FringeOpts {
    float fringe = 1.0f;
    Fringe::LineJoin join = Fringe::JOIN_MITER;
    float miterLimit = 2.4f;
    float tessTol = 0.25f;
    Fringe::LineCap cap = Fringe::CAP_BUTT;
    bool closed = false;
};

static FringeOpts GetFringeOpts(lua_State *L, int arg)
{
    FringeOpts o;
    if (!lua_istable(L, arg)) return o;

    lua_getfield(L, arg, "fringe");                  // ..., opts, fringe?
    if (lua_isnumber(L, -1)) o.fringe = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);                                   // ...

    lua_getfield(L, arg, "join");                    // ..., opts, join?
    if (lua_isstring(L, -1))
    {
        const char *j = lua_tostring(L, -1);
        if (strcmp(j, "bevel") == 0) o.join = Fringe::JOIN_BEVEL;
        else if (strcmp(j, "round") == 0) o.join = Fringe::JOIN_ROUND;
    }
    lua_pop(L, 1);                                   // ...

    lua_getfield(L, arg, "cap");                     // ..., opts, cap?
    if (lua_isstring(L, -1))
    {
        const char *c = lua_tostring(L, -1);
        if (strcmp(c, "square") == 0) o.cap = Fringe::CAP_SQUARE;
        else if (strcmp(c, "round") == 0) o.cap = Fringe::CAP_ROUND;
    }
    lua_pop(L, 1);                                   // ...

    lua_getfield(L, arg, "miterLimit");              // ..., opts, miterLimit?
    if (lua_isnumber(L, -1)) o.miterLimit = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);                                   // ...

    lua_getfield(L, arg, "tessTol");                 // ..., opts, tessTol?
    if (lua_isnumber(L, -1)) o.tessTol = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);                                   // ...

    lua_getfield(L, arg, "closed");                  // ..., opts, closed?
    if (lua_isboolean(L, -1)) o.closed = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);                                   // ...

    return o;
}

// Read rings into the vector-of-rings format that the fringe generator
// expects. Accepts a flat table / bytes string (single ring) or a table of
// rings (flat tables, bytes, or {points=..., hole=...} entries).
static bool ReadFringeRings(lua_State *L, int arg,
                            std::vector<std::vector<std::pair<float, float>>> &rings)
{
    if (lua_istable(L, arg))
    {
        if (IsFlatPolygonTable(L, arg))
        {
            TPPLPoly poly;
            if (!ReadPolygon(L, arg, poly)) return false;
            rings.resize(1);
            for (long i = 0; i < poly.GetNumPoints(); ++i)
                rings[0].push_back({(float)poly[i].x, (float)poly[i].y});
            return true;
        }

        TPPLPolyList list;
        if (!ReadPolygonList(L, arg, list)) return false;
        rings.resize(list.size());
        int ri = 0;
        for (auto &poly : list)
        {
            for (long i = 0; i < poly.GetNumPoints(); ++i)
                rings[ri].push_back({(float)poly[i].x, (float)poly[i].y});
            ++ri;
        }
        return true;
    }

    TPPLPoly poly;
    if (!ReadPolygon(L, arg, poly)) return false;
    rings.resize(1);
    for (long i = 0; i < poly.GetNumPoints(); ++i)
        rings[0].push_back({(float)poly[i].x, (float)poly[i].y});
    return true;
}

// Push {vertices={x1,y1,...}, alphas={a1,a2,...}, indices={1,2,3,...},
//       mode="indexed"} — ready for display.newMesh.
// The alphas table holds the per-vertex alpha for mesh:setFillVertexColor
// (precomputed by the fringe generator; linear vertex-color interpolation
// reproduces the AA gradient without a custom shader). No uvs — the fringe
// u/v coordinates are internal (they end up baked into the alpha).
static void PushFringeMesh(lua_State *L, std::vector<Fringe::Vertex> &tris)
{
    int nv = (int)tris.size();
    lua_createtable(L, 0, 4);                        // ..., mesh

    // mesh.vertices = {x1, y1, x2, y2, ...}
    lua_createtable(L, nv * 2, 0);                   // ..., mesh, vertices
    for (int i = 0; i < nv; ++i)
    {
        lua_pushnumber(L, tris[i].x);
        lua_rawseti(L, -2, i * 2 + 1);
        lua_pushnumber(L, tris[i].y);
        lua_rawseti(L, -2, i * 2 + 2);
    }
    lua_setfield(L, -2, "vertices");                 // ..., mesh

    // mesh.indices = sequential 1-based triangle indices
    lua_createtable(L, nv, 0);                       // ..., mesh, indices
    for (int i = 0; i < nv; ++i)
    {
        lua_pushinteger(L, i + 1);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "indices");                  // ..., mesh

    // mesh.alphas = per-vertex AA alpha for mesh:setFillVertexColor
    lua_createtable(L, nv, 0);                       // ..., mesh, alphas
    for (int i = 0; i < nv; ++i)
    {
        lua_pushnumber(L, tris[i].a);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "alphas");                   // ..., mesh

    lua_pushstring(L, "indexed");                    // ..., mesh, "indexed"
    lua_setfield(L, -2, "mode");                     // ..., mesh
}

static int FringeFill_fn(lua_State *L)
{
    std::vector<std::vector<std::pair<float, float>>> rings;
    if (!ReadFringeRings(L, 1, rings))
        return luaL_argerror(L, 1, "Expected polygon or polygon list");

    FringeOpts o = GetFringeOpts(L, 2);

    std::vector<Fringe::Vertex> tris;
    Fringe::ExpandFill(rings, o.fringe, o.join, o.miterLimit, o.tessTol, tris);

    PushFringeMesh(L, tris);
    return 1;
}

static int FringeStroke_fn(lua_State *L)
{
    std::vector<std::vector<std::pair<float, float>>> rings;
    if (!ReadFringeRings(L, 1, rings))
        return luaL_argerror(L, 1, "Expected polyline or list of polylines");

    float width = (float)luaL_checknumber(L, 2);

    FringeOpts o = GetFringeOpts(L, 3);

    std::vector<Fringe::Vertex> tris;
    Fringe::ExpandStroke(rings, o.closed, width, o.fringe, o.cap, o.join, o.miterLimit, o.tessTol, tris);

    PushFringeMesh(L, tris);
    return 1;
}

// ---------------------------------------------------------------------------
// Module entry point
// ---------------------------------------------------------------------------

CORONA_EXPORT int luaopen_plugin_geometry2d(lua_State *L)
{
    // Main geometry2d table
    lua_newtable(L);                                  // geometry2d

    // polypartition sub-namespace
    lua_newtable(L);                                  // geometry2d, polypartition

    luaL_Reg polypartition_funcs[] = {
        {"triangulate_EC",       Triangulate_EC_fn},
        {"triangulate_OPT",      Triangulate_OPT_fn},
        {"triangulate_MONO",     Triangulate_MONO_fn},
        {"convexPartition_HM",   ConvexPartition_HM_fn},
        {"convexPartition_OPT",  ConvexPartition_OPT_fn},
        {"removeHoles",          RemoveHoles_fn},
        {"monotonePartition",    MonotonePartition_fn},
        {nullptr, nullptr}
    };
    luaL_register(L, nullptr, polypartition_funcs);   // geometry2d, polypartition

    lua_setfield(L, -2, "polypartition");             // geometry2d

    // earcut sub-namespace
    lua_newtable(L);                                  // geometry2d, earcut

    luaL_Reg earcut_funcs[] = {
        {"triangulate", EarcutTriangulate_fn},
        {nullptr, nullptr}
    };
    luaL_register(L, nullptr, earcut_funcs);          // geometry2d, earcut

    lua_setfield(L, -2, "earcut");                    // geometry2d

    // fringe sub-namespace — AA fringe skirts for display.newMesh
    lua_newtable(L);                                  // geometry2d, fringe

    luaL_Reg fringe_funcs[] = {
        {"fill",   FringeFill_fn},
        {"stroke", FringeStroke_fn},
        {nullptr, nullptr}
    };
    luaL_register(L, nullptr, fringe_funcs);          // geometry2d, fringe

    lua_setfield(L, -2, "fringe");                    // geometry2d

    return 1;
}
