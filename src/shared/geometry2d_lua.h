#pragma once

#include "fringe.h"
#include "polypartition.h"

#include "CoronaLua.h"

#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>

namespace Geometry2D {

using Point = std::array<double, 2>;
using Ring = std::vector<Point>;
using Polygon = std::vector<Ring>;

enum class OutputMode {
    Table,
    Buffers,
    Mesh,
};

enum class PathFillRule {
    NonZero,
    EvenOdd,
};

enum class PathIntersectionMode {
    Error,
    Resolve,
};

enum MeshOption : uint32_t {
    OptionFringe        = 1u << 0,
    OptionJoin          = 1u << 1,
    OptionMiterLimit    = 1u << 2,
    OptionTessTol       = 1u << 3,
    OptionCap           = 1u << 4,
    OptionClosed        = 1u << 5,
    OptionRefine        = 1u << 6,
    OptionMode          = 1u << 7,
    OptionOutput        = 1u << 8,
    OptionDistance      = 1u << 9,
    OptionDistanceSign  = 1u << 10,
    OptionMaxCurvePoints = 1u << 11,
    OptionLegacyUVs     = 1u << 12,
    OptionDashPattern   = 1u << 13,
    OptionDashOffset    = 1u << 14,
    OptionMaxDashSegments = 1u << 15,
    OptionFillRule       = 1u << 16,
    OptionIntersections  = 1u << 17,
    OptionClipperPrecision = 1u << 18,
};

struct MeshOptions {
    float fringe = 1.0f;
    Fringe::LineJoin join = Fringe::JOIN_MITER;
    Fringe::LineCap cap = Fringe::CAP_BUTT;
    float miterLimit = 2.4f;
    float tessTol = 0.25f;
    bool closed = false;
    bool refine = false;
    bool triangles = false;
    bool outsidePositive = false;
    bool legacyUVs = false;
    float distance = 5.0f;
    size_t maxCurvePoints = 262144;
    std::vector<float> dashPattern;
    float dashOffset = 0.0f;
    size_t maxDashSegments = 4096;
    PathFillRule fillRule = PathFillRule::NonZero;
    PathIntersectionMode intersections = PathIntersectionMode::Error;
    int clipperPrecision = 4;
    OutputMode output = OutputMode::Table;
};

bool IsFlatPolygonTable(lua_State *L, int arg);
bool ReadPolygon(lua_State *L, int arg, TPPLPoly &poly, bool isHole = false);
bool ReadPolygonList(lua_State *L, int arg, TPPLPolyList &list);
void PushPolygonList(lua_State *L, TPPLPolyList &list);

bool ReadRing(lua_State *L, int arg, Ring &ring);
bool ReadEarcutPolygon(lua_State *L, int arg, Polygon &poly);
void FlattenPolygon(const Polygon &poly, std::vector<Point> &coords);

// Expected geometry/algorithm failure: returns `nil, message` to Lua.
int PushGeometryFailure(lua_State *L, const char *message);

MeshOptions GetMeshOptions(lua_State *L, int arg, uint32_t allowed,
                           const char *context,
                           const MeshOptions *defaults = nullptr);
int GetMaxVertices(lua_State *L, int arg);

void RegisterPolypartition(lua_State *L);
void RegisterEarcut(lua_State *L);
void RegisterFringe(lua_State *L);
void RegisterUtil(lua_State *L);
void RegisterPath(lua_State *L);
void RegisterRetainedShape(lua_State *L);
void RegisterClipper2(lua_State *L);
void RegisterRibbon(lua_State *L);

} // namespace Geometry2D
