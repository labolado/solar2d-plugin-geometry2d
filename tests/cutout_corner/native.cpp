#include "fringe.h"
#include <cassert>
#include <cmath>
#include <cstdio>

int main()
{
    for (auto join : {Fringe::JOIN_BEVEL, Fringe::JOIN_ROUND, Fringe::JOIN_MITER})
        for (float band : {10.f, 40.f}) {
            Fringe::FillRing ring;
            ring.hole = 0;
            ring.points = {{0,0},{200,0},{200,80},{80,80},{80,200},{0,200}};
            std::vector<Fringe::Vertex> mesh;
            Fringe::ExpandFill({ring}, band, join, 4, .25f, mesh);
            int hits = 0;
            for (size_t i = 0; i < mesh.size(); i += 3) {
                const auto &a=mesh[i], &b=mesh[i+1], &c=mesh[i+2];
                for (size_t j=i; j<i+3; ++j) {
                    assert(std::isfinite(mesh[j].x) && std::isfinite(mesh[j].y));
                    assert(mesh[j].a >= 0 && mesh[j].a <= 1);
                }
                double det=(b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);
                if (std::abs(det)<1e-7) continue;
                double u=((b.y-c.y)*(83-c.x)+(c.x-b.x)*(82-c.y))/det;
                double v=((c.y-a.y)*(83-c.x)+(a.x-c.x)*(82-c.y))/det;
                double w=1-u-v;
                if (u>1e-6 && v>1e-6 && w>1e-6) {
                    ++hits;
                    assert(std::abs((u*a.u+v*b.u+w*c.u)*band-2)<.001);
                }
            }
            assert(hits==1);
        }
    std::puts("CUTOUT_NATIVE PASS fill coverage and interpolated distance");
}
