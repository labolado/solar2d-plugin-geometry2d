#include "geometry2d_lua.h"

namespace Geometry2D {

#define WRAP_SINGLE_POLY(name) \
static int name##_fn(lua_State *L) { \
    TPPLPoly poly; \
    if (!ReadPolygon(L, 1, poly)) return luaL_argerror(L, 1, "Expected polygon (flat table or typed bytes descriptor)"); \
    TPPLPartition partition; TPPLPolyList result; \
    if (!partition.name(&poly, &result)) return PushGeometryFailure(L, #name " failed"); \
    PushPolygonList(L, result); return 1; \
}

#define WRAP_SINGLE_OR_LIST(name) \
static int name##_fn(lua_State *L) { \
    TPPLPartition partition; TPPLPolyList result; bool ok = false; \
    if (IsFlatPolygonTable(L, 1)) { \
        TPPLPoly poly; if (!ReadPolygon(L, 1, poly)) return luaL_argerror(L, 1, "Expected polygon or polygon list"); \
        ok = partition.name(&poly, &result) != 0; \
    } else if (lua_istable(L, 1)) { \
        TPPLPolyList list; if (!ReadPolygonList(L, 1, list)) return luaL_argerror(L, 1, "Expected polygon or polygon list"); \
        ok = partition.name(&list, &result) != 0; \
    } else return luaL_argerror(L, 1, "Expected polygon or polygon list"); \
    if (!ok) return PushGeometryFailure(L, #name " failed"); PushPolygonList(L, result); return 1; \
}

#define WRAP_LIST_ONLY(name) \
static int name##_fn(lua_State *L) { \
    TPPLPolyList input, result; \
    if (!ReadPolygonList(L, 1, input)) return luaL_argerror(L, 1, "Expected polygon list"); \
    TPPLPartition partition; if (!partition.name(&input, &result)) return PushGeometryFailure(L, #name " failed"); \
    PushPolygonList(L, result); return 1; \
}

static void SplitConvexPolygon(TPPLPoly &poly, TPPLPolyList &result, int maxVertices)
{
    long n = poly.GetNumPoints();
    if (n <= maxVertices) { result.push_back(poly); return; }
    TPPLPoly piece; piece.Init(maxVertices); piece.SetHole(false);
    for (int i = 0; i < maxVertices; ++i) piece[i] = poly[i];
    result.push_back(piece);

    long remaining = n - maxVertices + 2;
    TPPLPoly rest; rest.Init(remaining); rest.SetHole(false);
    rest[0] = poly[0]; rest[1] = poly[maxVertices - 1];
    for (int i = maxVertices; i < static_cast<int>(n); ++i)
        rest[i - maxVertices + 2] = poly[i];
    SplitConvexPolygon(rest, result, maxVertices);
}

static void PartitionWithLimit(TPPLPoly &poly, TPPLPolyList &result, int maxVertices)
{
    if (poly.GetNumPoints() <= maxVertices) { result.push_back(poly); return; }
    TPPLPartition partition; TPPLPolyList parts;
    if (!partition.ConvexPartition_HM(&poly, &parts) ||
        (parts.size() == 1 && parts.front().GetNumPoints() == poly.GetNumPoints()))
    {
        SplitConvexPolygon(poly, result, maxVertices);
        return;
    }
    for (auto &part : parts) PartitionWithLimit(part, result, maxVertices);
}

static int ConvexPartition_HM_fn(lua_State *L)
{
    int maxVertices = GetMaxVertices(L, 2);
    TPPLPartition partition; TPPLPolyList result; bool ok = false;
    if (IsFlatPolygonTable(L, 1))
    {
        TPPLPoly poly;
        if (!ReadPolygon(L, 1, poly)) return luaL_argerror(L, 1, "Expected polygon or polygon list");
        ok = partition.ConvexPartition_HM(&poly, &result) != 0;
    }
    else if (lua_istable(L, 1))
    {
        TPPLPolyList input;
        if (!ReadPolygonList(L, 1, input)) return luaL_argerror(L, 1, "Expected polygon or polygon list");
        ok = partition.ConvexPartition_HM(&input, &result) != 0;
    }
    else return luaL_argerror(L, 1, "Expected polygon or polygon list");
    if (!ok) return PushGeometryFailure(L, "ConvexPartition_HM failed");

    if (maxVertices > 0)
    {
        TPPLPolyList limited;
        for (auto &part : result) PartitionWithLimit(part, limited, maxVertices);
        result.swap(limited);
    }
    PushPolygonList(L, result);
    return 1;
}

static int ConvexPartition_OPT_fn(lua_State *L)
{
    TPPLPoly poly;
    if (!ReadPolygon(L, 1, poly)) return luaL_argerror(L, 1, "Expected polygon");
    int maxVertices = GetMaxVertices(L, 2);
    TPPLPartition partition; TPPLPolyList result;
    if (!partition.ConvexPartition_OPT(&poly, &result))
        return PushGeometryFailure(L, "ConvexPartition_OPT failed");
    if (maxVertices > 0)
    {
        TPPLPolyList limited;
        for (auto &part : result) PartitionWithLimit(part, limited, maxVertices);
        result.swap(limited);
    }
    PushPolygonList(L, result);
    return 1;
}

WRAP_SINGLE_POLY(Triangulate_OPT)
WRAP_SINGLE_OR_LIST(Triangulate_EC)
WRAP_SINGLE_OR_LIST(Triangulate_MONO)
WRAP_LIST_ONLY(RemoveHoles)
WRAP_LIST_ONLY(MonotonePartition)

void RegisterPolypartition(lua_State *L)
{
    luaL_Reg functions[] = {
        {"triangulate_EC", Triangulate_EC_fn},
        {"triangulate_OPT", Triangulate_OPT_fn},
        {"triangulate_MONO", Triangulate_MONO_fn},
        {"convexPartition_HM", ConvexPartition_HM_fn},
        {"convexPartition_OPT", ConvexPartition_OPT_fn},
        {"removeHoles", RemoveHoles_fn},
        {"monotonePartition", MonotonePartition_fn},
        {nullptr, nullptr}
    };
    luaL_register(L, nullptr, functions);
}

} // namespace Geometry2D
