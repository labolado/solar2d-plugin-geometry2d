#pragma once
#include "region.h"
namespace Prototype {
struct Tile {
    std::array<float,4> bounds;
    std::vector<std::array<float,4>> distanceEdges, signEdges;
    int constant=0; // 0 clipped winding, 1 deep inside, 2 analytic inside, -1 outside
};
struct TileResult {
    size_t cells=0, leaves=0, work=0, maxDepth=0, probes=0, maxDistanceEdges=0, maxSignEdges=0;
    size_t auditWork=0;
    size_t deepInside=0, emptyOutside=0;
    double maxDistanceError=0, auditMs=0;
    std::vector<Tile> output;
};
// CPU feasibility gate: four distance edges + eight clipped-sign edges per tile.
// It does not yet export a renderable tiled mesh or claim shader parity.
bool Tiles(const std::vector<Geometry2D::SDFPolygon>& input,TileResult& out,std::string& error,
           size_t distanceSlots=4,size_t signSlots=8);
}
