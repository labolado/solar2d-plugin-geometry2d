#include "tiles.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <chrono>
namespace Prototype {
namespace {
using namespace Clipper2Lib;
struct Edge {PointD a,b;};
double Distance(PointD p,Edge e) {
    double x=e.b.x-e.a.x,y=e.b.y-e.a.y,den=x*x+y*y;
    double t=den>0?std::clamp(((p.x-e.a.x)*x+(p.y-e.a.y)*y)/den,0.,1.):0;
    return std::hypot(p.x-e.a.x-t*x,p.y-e.a.y-t*y);
}
int Winding(PointD p,const std::vector<Edge>& edges) {
    int n=0;
    for(auto e:edges) {
        double cross=(e.b.x-e.a.x)*(p.y-e.a.y)-(e.b.y-e.a.y)*(p.x-e.a.x);
        if(e.a.y<=p.y&&e.b.y>p.y&&cross>0)++n;
        if(e.a.y>p.y&&e.b.y<=p.y&&cross<0)--n;
    }return n;
}
std::vector<Edge> Edges(const PathsD& paths,bool floating=false) {
    std::vector<Edge> out;
    for(const auto& path:paths)for(size_t i=0;i<path.size();++i) {
        auto a=path[i],b=path[(i+1)%path.size()];
        if(floating){a={double(float(a.x)),double(float(a.y))};b={double(float(b.x)),double(float(b.y))};}
        if(a!=b)out.push_back({a,b});
    }return out;
}
struct Builder {
    PathsD shape;std::vector<Edge> edges;TileResult& out;size_t distanceSlots,signSlots;
    void Work(size_t n) {
        if(n>20000000-out.work)throw std::runtime_error("tile work budget exceeded");
        out.work+=n;
    }
    void Visit(double x0,double y0,double x1,double y1,const std::vector<size_t>& parent,size_t depth) {
        if(++out.cells>65536)throw std::runtime_error("tile cell budget exceeded");
        out.maxDepth=std::max(out.maxDepth,depth);
        PointD center((x0+x1)*.5,(y0+y1)*.5);double radius=std::hypot(x1-x0,y1-y0)*.5;
        double nearest=std::numeric_limits<double>::infinity();
        Work(parent.size());for(auto i:parent)nearest=std::min(nearest,Distance(center,edges[i]));
        // Width <= 4 plus AA half-footprint <= 4 local units. For affine
        // transforms with minimum singular value >= .2, a one-pixel L1
        // footprint has half-width <= sqrt(2)/(.2*2) < 4.
        if(nearest>8+radius) {
            Work(edges.size());
            if(Winding(center,edges)!=0) {
                ++out.deepInside;
                out.output.push_back({{float(x0),float(y0),float(x1),float(y1)},{},{},1});
            }else ++out.emptyOutside;
            ++out.leaves;return;
        } // beyond reserved inner/outer support; sign is constant in the box
        std::vector<size_t> candidates;
        // Lipschitz bound: an edge farther than nearest(center)+2*radius
        // cannot win anywhere in this box. This is conservative, not top-K.
        Work(parent.size());for(auto i:parent)if(Distance(center,edges[i])<=nearest+2*radius+1e-9)candidates.push_back(i);
        PathsD clipped;
        int constant=0;
        if(candidates.size()<=distanceSlots) {
            // Sign-only rectangle edges must lie outside the rendered box.
            // Otherwise top-left raster ownership and float interpolation can
            // classify a covered pixel on a tile seam as outside. This guard
            // changes neither the shape nor the rendered tile/distance edges.
            const double magnitude=std::max({1.,std::abs(x0),std::abs(y0),std::abs(x1),std::abs(y1)});
            // Also reserve a raster subpixel guard: covered fragments can be
            // just outside the mathematical box after raster edge snapping.
            // .0625 local units is tested at scale >= .2 on the macOS target;
            // it is not a portable guarantee of arbitrary raster precision.
            const double guard=std::max(.0625,16*std::numeric_limits<float>::epsilon()*magnitude);
            Work(edges.size());
            if(nearest>radius+guard) {
                // No boundary reaches this box. Retain analytic distance, but
                // a single winding classification replaces the clipped polygon.
                constant=Winding(center,edges)!=0?2:-1;
            } else {
                ClipperD clip(6);clip.AddSubject(shape);
                clip.AddClip({{{x0-guard,y0-guard},{x1+guard,y0-guard},{x1+guard,y1+guard},{x0-guard,y1+guard}}});
                if(!clip.Execute(ClipType::Intersection,FillRule::NonZero,clipped)||clip.ErrorCode())throw std::runtime_error("tile sign clipping failed");
            }
        }
        auto signEdges=Edges(clipped,true);
        const bool sharedOverflow=distanceSlots==11 && candidates.size()+signEdges.size()>11;
        if(candidates.size()>distanceSlots||signEdges.size()>signSlots||sharedOverflow) {
            if(depth>=18 || !(float(x1)>float(x0)) || !(float(y1)>float(y0)))
                throw std::runtime_error("tile capacity unresolved: distance="+std::to_string(candidates.size())+", sign="+std::to_string(signEdges.size()));
            Visit(x0,y0,center.x,center.y,candidates,depth+1);
            Visit(center.x,y0,x1,center.y,candidates,depth+1);
            Visit(x0,center.y,center.x,y1,candidates,depth+1);
            Visit(center.x,center.y,x1,y1,candidates,depth+1);
            return;
        }
        ++out.leaves;out.maxDistanceEdges=std::max(out.maxDistanceEdges,candidates.size());
        out.maxSignEdges=std::max(out.maxSignEdges,signEdges.size());
        std::vector<Edge> floatCandidates;
        for(auto i:candidates) {auto e=edges[i];floatCandidates.push_back({{double(float(e.a.x)),double(float(e.a.y))},{double(float(e.b.x)),double(float(e.b.y))}});}
        Tile tile{{float(x0),float(y0),float(x1),float(y1)},{},{},constant};
        for(auto e:floatCandidates)tile.distanceEdges.push_back({float(e.a.x),float(e.a.y),float(e.b.x),float(e.b.y)});
        for(auto e:signEdges)tile.signEdges.push_back({float(e.a.x),float(e.a.y),float(e.b.x),float(e.b.y)});
        out.output.push_back(std::move(tile));
        auto auditStart=std::chrono::steady_clock::now();
        for(double u:{.113,.487,.891})for(double v:{.173,.531,.827}) {
            PointD p(x0+(x1-x0)*u,y0+(y1-y0)*v);
            double exact=1e100,candidate=1e100;
            out.auditWork+=edges.size()+floatCandidates.size();
            for(auto e:edges)exact=std::min(exact,Distance(p,e));
            for(auto e:floatCandidates)candidate=std::min(candidate,Distance(p,e));
            double delta=std::abs(exact-candidate);out.maxDistanceError=std::max(out.maxDistanceError,delta);++out.probes;
            if(delta>.001)throw std::runtime_error("tile float candidate distance error exceeds .001");
            const bool inside=constant==2 || (constant==0 && Winding(p,signEdges)!=0);
            if(exact>.001 && ((Winding(p,edges)!=0)!=inside))
                throw std::runtime_error("tile sign oracle mismatch away from boundary");
        }
        out.auditMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-auditStart).count();
    }
};
}
bool Tiles(const std::vector<Geometry2D::SDFPolygon>& input,TileResult& out,std::string& error,size_t distanceSlots,size_t signSlots) {
    out={};
    try {
        auto shape=NormalizedPaths(input);auto edges=Edges(shape);
        if(edges.empty())throw std::runtime_error("empty normalized input");
        auto bounds=GetBounds(shape);
        std::vector<size_t> ids;for(size_t i=0;i<edges.size();++i)ids.push_back(i);
        Builder b{shape,edges,out,distanceSlots,signSlots};b.Visit(bounds.left-4,bounds.top-4,bounds.right+4,bounds.bottom+4,ids,0);
        return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
}
