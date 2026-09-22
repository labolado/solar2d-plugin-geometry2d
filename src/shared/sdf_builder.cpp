// Plugin-specific distance-envelope tessellator. Clipper2 (Boost license)
// performs planar booleans; mapbox earcut (ISC) triangulates the resulting cells.
// No stencil/coverage renderer is assumed. Third-party sources are unmodified.
#include "sdf_builder.h"
#include "clipper2/clipper.h"
#include "mapbox/earcut.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

namespace Geometry2D {
namespace {
using namespace Clipper2Lib;
using Clock = std::chrono::steady_clock;
constexpr double kScale = 1000000.0;
constexpr double kGrid = 1.0 / kScale;
constexpr double kPi = 3.14159265358979323846;
struct Plane {
    double x, y, c;
    double operator()(const SDFPoint& p) const { return x*p[0]+y*p[1]+c; }
};
struct Site {
    std::vector<Plane> planes;
    SDFRing capsule;
    double minX, minY, maxX, maxY;
};
double PointSegmentDistance(SDFPoint p,SDFPoint a,SDFPoint b) {
    double dx=b[0]-a[0],dy=b[1]-a[1];
    double t=std::clamp(((p[0]-a[0])*dx+(p[1]-a[1])*dy)/(dx*dx+dy*dy),0.0,1.0);
    return std::hypot(p[0]-a[0]-t*dx,p[1]-a[1]-t*dy);
}
double Elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now()-start).count();
}
void Clip(SDFRing& ring, Plane p, SDFRing& out) {
    out.clear();
    if (ring.empty()) return;
    out.reserve(ring.size()+1);
    auto evaluate=[&](SDFPoint v) {
        double d=p(v);
        double roundoff=8*std::numeric_limits<double>::epsilon()*(std::abs(p.x*v[0])+std::abs(p.y*v[1])+std::abs(p.c));
        return std::abs(d)<=roundoff?0.0:d;
    };
    auto a=ring.back(); double da=evaluate(a);
    for (auto b: ring) {
        double db=evaluate(b);
        if ((da>0)!=(db>0)) {
            double t=da/(da-db);
            out.push_back({a[0]+t*(b[0]-a[0]),a[1]+t*(b[1]-a[1])});
        }
        if (db<=0) out.push_back(b);
        a=b; da=db;
    }
    ring.swap(out);
}
Path64 Quantize(const SDFRing& ring) {
    Path64 out; out.reserve(ring.size());
    for (auto p: ring) {
        Point64 q(std::llround(p[0]*kScale),std::llround(p[1]*kScale));
        if (out.empty() || out.back()!=q) out.push_back(q);
    }
    if (out.size()>1 && out.front()==out.back()) out.pop_back();
    if (out.size()<3 || Area(out)==0) return {};
    if (!IsPositive(out)) std::reverse(out.begin(),out.end());
    return out;
}
long double ExactArea(const Path64& path) {
    if(path.size()<3)return 0;
    long double sum=0;auto a=path.front();
    for(size_t i=1;i+1<path.size();++i) {
        auto b=path[i],c=path[i+1];
        sum+=static_cast<long double>(b.x-a.x)*(c.y-a.y)-static_cast<long double>(b.y-a.y)*(c.x-a.x);
    }
    return std::abs(sum)*.5;
}
Path64 CleanCellRing(const Path64& path) {
    auto clean=TrimCollinear(path);
    double perimeter=0;
    if(clean.size()<3)return {};
    for(size_t i=0;i<clean.size();++i) {
        auto a=clean[i],b=clean[(i+1)%clean.size()];
        perimeter+=std::hypot(double(b.x-a.x),double(b.y-a.y));
    }
    // Only generated boolean-cell artifacts, never user contours. Below this
    // width the ring lies within the clipping grid's uncertainty envelope.
    if(ExactArea(clean)<=2*perimeter)return {};
    return clean;
}
Paths64 Boolean(ClipType type,const Paths64& a,const Paths64& b) {
    Clipper64 clip; clip.PreserveCollinear(false);
    clip.AddSubject(a); clip.AddClip(b);
    Paths64 out;
    if (!clip.Execute(type,FillRule::NonZero,out)) throw std::runtime_error("SDF boolean failed");
    return out;
}
bool Overlap(const Site& a,const Site& b) {
    return a.minX<=b.maxX && b.minX<=a.maxX && a.minY<=b.maxY && b.minY<=a.maxY;
}

struct Builder {
    const SDFOptions& opt;
    SDFMesh& out;
    std::vector<Site> sites;
    Paths64 shape;
    std::map<std::pair<int64_t,int64_t>,uint32_t> vertices;
    double det;
    SDFRing clipScratch;

    void Work(size_t n=1) {
        if (n>opt.maxWork-out.stats.work) throw std::runtime_error("SDF exceeds maxWork; simplify the path or increase distanceTolerance");
        out.stats.work+=n;
    }

    void Prepare(const std::vector<SDFPolygon>& groups) {
        if (groups.empty()) throw std::runtime_error("SDF requires polygon groups");
        det=opt.transform[0]*opt.transform[3]-opt.transform[1]*opt.transform[2];
        if (!std::isfinite(det) || std::abs(det)<1e-12) throw std::runtime_error("SDF transform is singular or ill-conditioned");
        double radius=opt.geometry==SDFGeometry::InnerStroke ? std::max(opt.innerRange,opt.outerRange) :
            opt.geometry==SDFGeometry::FillAA ? opt.outerRange : 0;
        // Uniform circle directions plus each segment's two exact normals.
        // For every point within radius, 0 <= exact - approximation <= tolerance.
        double angle=std::acos(radius/(radius+opt.distanceTolerance));
        if(!(angle>0) || kPi/angle>256)
            throw std::runtime_error("SDF distanceTolerance requires more than 256 angular facets");
        size_t facets=static_cast<size_t>(std::ceil(kPi/angle));
        facets=std::max(size_t(8),facets);
        if (facets>256) throw std::runtime_error("SDF distanceTolerance requires more than 256 angular facets");
        double reach=radius/std::cos(kPi/facets)+4*kGrid;
        bool first=true;
        std::vector<std::pair<SDFPoint,SDFPoint>> inputEdges;
        for (const auto& group:groups) {
            if (group.empty()) throw std::runtime_error("SDF polygon group is empty");
            Paths64 outer, holes;
            for (size_t r=0;r<group.size();++r) {
                SDFRing ring;
                for (auto p:group[r]) {
                    if (!std::isfinite(p[0]) || !std::isfinite(p[1])) throw std::runtime_error("SDF non-finite coordinate");
                    if (first) { out.bounds={p[0],p[1],p[0],p[1]}; first=false; }
                    out.bounds[0]=std::min(out.bounds[0],p[0]); out.bounds[1]=std::min(out.bounds[1],p[1]);
                    out.bounds[2]=std::max(out.bounds[2],p[0]); out.bounds[3]=std::max(out.bounds[3],p[1]);
                    const auto& t=opt.transform;
                    SDFPoint q{t[0]*p[0]+t[2]*p[1]+t[4],t[1]*p[0]+t[3]*p[1]+t[5]};
                    if (!std::isfinite(q[0]) || !std::isfinite(q[1]) ||
                        std::max(std::abs(q[0]),std::abs(q[1]))+reach>1e7)
                        throw std::runtime_error("SDF transformed coordinates/ranges exceed supported magnitude 1e7");
                    if (ring.empty() || q!=ring.back()) ring.push_back(q);
                }
                if (ring.size()>1 && ring.front()==ring.back()) ring.pop_back();
                for(size_t i=0;i<ring.size();++i) {
                    auto a=ring[i],b=ring[(i+1)%ring.size()];
                    if(std::hypot(a[0]-b[0],a[1]-b[1])<8*kGrid)
                        throw std::runtime_error("SDF input detail below safe 1e-6 grid resolution");
                    inputEdges.push_back({a,b});
                }
                auto path=Quantize(ring);
                if (path.empty() || path.size()!=ring.size()) throw std::runtime_error("SDF degenerate ring or detail below 1e-6 coordinate resolution");
                // Group role, not caller winding, defines the filled region.
                (r ? holes : outer).push_back(std::move(path));
            }
            if (!holes.empty() && !Boolean(ClipType::Difference,holes,outer).empty())
                throw std::runtime_error("SDF hole is not contained in its group's outer contour");
            auto region=Boolean(ClipType::Difference,outer,Boolean(ClipType::Union,holes,{}));
            shape.insert(shape.end(),region.begin(),region.end());
        }
        // Reject nearly touching, but distinct, input boundaries before grid
        // normalization can close a legitimate tiny gap. Exact contacts and
        // proper crossings are handled by the boolean union policy above.
        for(size_t i=0;i<inputEdges.size();++i) for(size_t j=i+1;j<inputEdges.size();++j) {
            Work();
            auto a=inputEdges[i].first,b=inputEdges[i].second,c=inputEdges[j].first,d=inputEdges[j].second;
            constexpr double guard=8*kGrid;
            if(std::max(a[0],b[0])+guard<std::min(c[0],d[0]) || std::max(c[0],d[0])+guard<std::min(a[0],b[0]) ||
               std::max(a[1],b[1])+guard<std::min(c[1],d[1]) || std::max(c[1],d[1])+guard<std::min(a[1],b[1])) continue;
            double gap=std::min({PointSegmentDistance(a,c,d),PointSegmentDistance(b,c,d),PointSegmentDistance(c,a,b),PointSegmentDistance(d,a,b)});
            if(gap>1e-12 && gap<guard) throw std::runtime_error("SDF nearly touching boundaries below grid resolution; rescale input");
        }
        // Canonical union handles islands and overlapping groups once. Extract
        // only actual union boundaries: buried input edges must not affect SDF.
        shape=Boolean(ClipType::Union,shape,{});
        if (shape.empty()) throw std::runtime_error("SDF input has no filled area");
        for(auto& path:shape) path=TrimCollinear(path);
        for(const auto& path:shape) out.stats.inputEdges+=path.size();
        // Fill-only never constructs segment sites, capsules or distance cells.
        if(opt.geometry==SDFGeometry::Fill) return;
        sites.reserve(inputEdges.size());
        for (const auto& path:shape) {
            for (size_t i=0;i<path.size();++i) {
                SDFPoint a{path[i].x*kGrid,path[i].y*kGrid};
                auto q=path[(i+1)%path.size()]; SDFPoint b{q.x*kGrid,q.y*kGrid};
                double dx=b[0]-a[0],dy=b[1]-a[1],len=std::hypot(dx,dy);
                if (len<kGrid) throw std::runtime_error("SDF boundary below coordinate resolution");
                Site site;
                site.planes.reserve(facets+2);
                auto add=[&](double x,double y) {
                    Plane p{x,y,-std::max(x*a[0]+y*a[1],x*b[0]+y*b[1])};
                    for (auto v:site.planes) if (std::abs(v.x-x)<1e-12 && std::abs(v.y-y)<1e-12) return;
                    site.planes.push_back(p);
                };
                // Put edge planes first to keep the large rectangular cells simple.
                add(-dy/len,dx/len); add(dy/len,-dx/len);
                for (size_t k=0;k<facets;++k) add(std::cos(2*kPi*k/facets),std::sin(2*kPi*k/facets));
                site.minX=std::min(a[0],b[0])-reach; site.maxX=std::max(a[0],b[0])+reach;
                site.minY=std::min(a[1],b[1])-reach; site.maxY=std::max(a[1],b[1])+reach;
                site.capsule={{site.minX,site.minY},{site.maxX,site.minY},{site.maxX,site.maxY},{site.minX,site.maxY}};
                for (auto p:site.planes) {p.c-=radius; Clip(site.capsule,p,clipScratch);}
                sites.push_back(std::move(site));
                Work(facets);
            }
        }
        out.stats.inputEdges=sites.size();
    }

    uint32_t Vertex(Point64 q, double distance) {
        auto key=std::make_pair(q.x,q.y);
        auto found=vertices.find(key);
        if (found!=vertices.end()) {
            if (std::abs(out.distances[found->second]-distance)>2e-4+opt.distanceTolerance*1e-3)
                throw std::runtime_error("SDF inconsistent distance at shared vertex; reduce coordinate magnitude");
            return found->second;
        }
        if (vertices.size()>=opt.maxVertices) throw std::runtime_error("SDF exceeds maxVertices; simplify input or increase distanceTolerance");
        const auto& t=opt.transform;
        double x=q.x*kGrid-t[4],y=q.y*kGrid-t[5];
        double lx=(t[3]*x-t[2]*y)/det,ly=(-t[1]*x+t[0]*y)/det;
        float fx=static_cast<float>(lx),fy=static_cast<float>(ly);
        // Fail rather than silently lose fine details after float32 upload.
        double ex=t[0]*(fx-lx)+t[2]*(fy-ly),ey=t[1]*(fx-lx)+t[3]*(fy-ly);
        if (!std::isfinite(fx) || !std::isfinite(fy) || std::hypot(ex,ey)>opt.distanceTolerance*.05)
            throw std::runtime_error("SDF float32 position precision insufficient; recenter/rescale coordinates");
        auto index=static_cast<uint32_t>(out.distances.size()); vertices.emplace(key,index);
        out.vertices.push_back(fx);out.vertices.push_back(fy);
        out.distances.push_back(static_cast<float>(distance));
        out.uvs.push_back(static_cast<float>((fx-out.bounds[0])/(out.bounds[2]-out.bounds[0])));
        out.uvs.push_back(static_cast<float>((fy-out.bounds[1])/(out.bounds[3]-out.bounds[1])));
        return index;
    }

    void EmitSlabs(const Paths64& rings,Plane plane,double sign,bool deep) {
        // Rare fallback for a weakly-simple cell (a hole touching its outer
        // boundary). Cut at existing vertex Y events, never at regular area/
        // bandwidth intervals. Each resulting span is a convex trapezoid.
        struct Edge {Point64 a,b;double x;int delta;};
        std::vector<int64_t> levels;
        for(const auto& ring:rings)for(auto p:ring)levels.push_back(p.y);
        std::sort(levels.begin(),levels.end());levels.erase(std::unique(levels.begin(),levels.end()),levels.end());
        for(size_t l=1;l<levels.size();++l) {
            int64_t low=levels[l-1],high=levels[l];
            double middle=(double(low)+double(high))*.5;
            std::vector<Edge> edges;
            for(const auto& ring:rings)for(size_t i=0;i<ring.size();++i) {
                Work();
                auto a=ring[i],b=ring[(i+1)%ring.size()];
                if(middle<=std::min(a.y,b.y) || middle>=std::max(a.y,b.y))continue;
                edges.push_back({a,b,a.x+(middle-a.y)*double(b.x-a.x)/double(b.y-a.y),b.y<a.y?1:-1});
            }
            std::sort(edges.begin(),edges.end(),[](const Edge& a,const Edge& b){return a.x<b.x;});
            auto at=[](const Edge& e,int64_t y) {
                // Point's constructor requires matching argument types; int64_t
                // is long on Android LP64, while llround returns long long.
                const int64_t x=static_cast<int64_t>(std::llround(e.a.x+double(y-e.a.y)*double(e.b.x-e.a.x)/double(e.b.y-e.a.y)));
                return Point64(x,y);
            };
            int winding=0;Edge left{Point64(),Point64(),0,0};
            for(const auto& edge:edges) {
                int before=winding;winding+=edge.delta;
                if(before<=0 && winding>0)left=edge;
                if(before>0 && winding<=0) {
                    Path64 trapezoid{at(left,low),at(edge,low),at(edge,high),at(left,high)};
                    trapezoid=CleanCellRing(trapezoid);
                    if(!trapezoid.empty())Emit({trapezoid},plane,sign,deep,false);
                }
            }
            if(winding!=0)throw std::runtime_error("SDF slab winding mismatch");
        }
    }

    void Emit(const Paths64& paths, Plane plane, double sign, bool deep=false,bool allowSlabs=true) {
        if (paths.empty()) return;
        auto start=Clock::now();
        double previousMs=out.stats.triangulateMs;
        Clipper64 clip;clip.PreserveCollinear(false);clip.AddSubject(paths);
        PolyTree64 tree;
        if (!clip.Execute(ClipType::Union,FillRule::NonZero,tree)) throw std::runtime_error("SDF cell union failed");
        auto visit=[&](auto&& self,const PolyPath64& node)->void {
            if (!node.Polygon().empty() && !node.IsHole()) {
                // Keep integer-grid coordinates for earcut. Converting to
                // decimal units first can destroy exact collinearity and make
                // near-touching cells appear weakly self-intersecting.
                auto integerRing=[](const Path64& p) {
                    SDFRing r; r.reserve(p.size());
                    for(auto q:p)r.push_back({double(q.x),double(q.y)});
                    return r;
                };
                auto outer=CleanCellRing(node.Polygon());
                if(outer.size()<3) return;
                std::vector<SDFRing> polygon{integerRing(outer)};
                std::vector<Point64> points=outer;
                Paths64 cleanRings{outer};
                long double expected=ExactArea(outer);
                for (const auto& child:node) {
                    auto hole=CleanCellRing(child->Polygon());
                    if(hole.size()<3)continue;
                    expected-=ExactArea(hole);
                    polygon.push_back(integerRing(hole));
                    points.insert(points.end(),hole.begin(),hole.end());
                    cleanRings.push_back(std::move(hole));
                }
                auto indices=mapbox::earcut<uint32_t>(polygon);
                if (indices.empty()) throw std::runtime_error("SDF cell triangulation failed");
                long double triangulated=0;
                for(size_t k=0;k<indices.size();k+=3) {
                    auto a=points[indices[k]],b=points[indices[k+1]],c=points[indices[k+2]];
                    triangulated+=(static_cast<long double>(b.x-a.x)*(c.y-a.y)-static_cast<long double>(b.y-a.y)*(c.x-a.x))*.5;
                }
                if(std::abs(triangulated-expected)>std::max(1.0L,expected*1e-9L)) {
                    if(!allowSlabs)throw std::runtime_error("SDF slab triangulation area mismatch");
                    EmitSlabs(cleanRings,plane,sign,deep);
                    for(const auto& child:node)self(self,*child);
                    return;
                }
                long double area=0;
                for(size_t k=0;k<indices.size();k+=3) {
                    auto a=points[indices[k]],b=points[indices[k+1]],c=points[indices[k+2]];
                    long double cross=static_cast<long double>(b.x-a.x)*(c.y-a.y)-static_cast<long double>(b.y-a.y)*(c.x-a.x);
                    if (cross==0) continue;
                    // Earcut may bridge quantized, nearly touching vertices
                    // with a sub-grid sliver. Do not turn a reversed sliver
                    // into an overlapping positive triangle by swapping it.
                    area+=cross*.5;
                    double longest=std::max({std::hypot(double(b.x-a.x),double(b.y-a.y)),std::hypot(double(c.x-a.x),double(c.y-a.y)),std::hypot(double(c.x-b.x),double(c.y-b.y))});
                    if(static_cast<double>(std::abs(cross))/longest<=4) continue;
                    if(cross<0) throw std::runtime_error("SDF triangulation reversed a non-degenerate cell");
                    uint32_t tri[3];int j=0;
                    for (auto p:{a,b,c}) {
                        double value=deep ? -opt.innerRange : sign*std::clamp(plane({p.x*kGrid,p.y*kGrid}),0.0,sign<0?opt.innerRange:opt.outerRange);
                        tri[j++]=Vertex(p,value);
                    }
                    auto ax=out.vertices[2*tri[0]],ay=out.vertices[2*tri[0]+1];
                    auto bx=out.vertices[2*tri[1]],by=out.vertices[2*tri[1]+1];
                    auto cx=out.vertices[2*tri[2]],cy=out.vertices[2*tri[2]+1];
                    double farea=(double(bx)-ax)*(double(cy)-ay)-(double(by)-ay)*(double(cx)-ax);
                    // Boolean intersections can leave sub-grid slivers. A
                    // triangle that becomes exactly collinear after float32
                    // upload has no raster footprint and must not be emitted.
                    if (farea==0) continue;
                    if (farea*det<0) throw std::runtime_error("SDF cell flips in float32; simplify or recenter input");
                    out.indices.insert(out.indices.end(),tri,tri+3);
                    Work();
                }
                if (std::abs(area-expected)>std::max(1.0L,expected*1e-9L)) {
                    throw std::runtime_error("SDF triangulation area mismatch");
                }
                ++out.stats.cells;
            }
            for (const auto& child:node) self(self,*child);
        };
        visit(visit,tree);
        out.stats.triangulateMs=previousMs+Elapsed(start);
    }

    void Partition() {
        Paths64 innerCapsules;
        const bool innerStroke=opt.geometry==SDFGeometry::InnerStroke;
        if(opt.geometry==SDFGeometry::Fill) {
            Emit(shape,{0,0,0},1);
            return;
        }
        if(innerStroke) innerCapsules.reserve(sites.size());
        std::vector<std::array<double,4>> shapeBounds;
        for(const auto& ring:shape) {
            std::array<double,4> box{1e30,1e30,-1e30,-1e30};
            for(auto p:ring) {box[0]=std::min(box[0],p.x*kGrid);box[1]=std::min(box[1],p.y*kGrid);box[2]=std::max(box[2],p.x*kGrid);box[3]=std::max(box[3],p.y*kGrid);}
            shapeBounds.push_back(box);
        }
        for (size_t i=0;i<sites.size();++i) {
            const auto& site=sites[i];
            Paths64 localShape;
            Work(shape.size());
            for(size_t r=0;r<shape.size();++r) {
                auto b=shapeBounds[r];
                if(b[0]<=site.maxX && b[2]>=site.minX && b[1]<=site.maxY && b[3]>=site.minY)
                    localShape.push_back(shape[r]);
            }
            if(innerStroke) {
                auto inner=site.capsule;
                for(auto p:site.planes) {p.c-=opt.innerRange;Clip(inner,p,clipScratch);}
                auto ip=Quantize(inner); if(!ip.empty()) innerCapsules.push_back(std::move(ip));
            }
            std::vector<size_t> neighbors;
            Work(sites.size());
            for(size_t j=0;j<sites.size();++j) if(i!=j && Overlap(site,sites[j])) neighbors.push_back(j);
            for (auto face:site.planes) {
                Work();
                auto cell=site.capsule;
                for(auto p:site.planes) Clip(cell,{p.x-face.x,p.y-face.y,p.c-face.c},clipScratch);
                auto initial=Quantize(cell);if(initial.empty()) continue;
                Paths64 visible{initial};
                // On this face Di is affine. Dj < Di is the intersection of
                // all (plane_j - Di < 0) halfplanes: a convex dominance region.
                // Subtract it. Equal planes use stable site order, including
                // shared endpoint cones, so tied areas are emitted only once.
                for(size_t j:neighbors) {
                    Work();
                    auto dominated=cell;
                    bool canWin=true;
                    for(auto p:sites[j].planes) {
                        Plane delta{p.x-face.x,p.y-face.y,p.c-face.c};
                        if(std::abs(delta.x)<1e-12 && std::abs(delta.y)<1e-12 && std::abs(delta.c)<1e-9) {
                            if(j>i) {canWin=false;break;}
                            continue;
                        }
                        Clip(dominated,delta,clipScratch);
                        if(dominated.size()<3) break;
                    }
                    if(!canWin) continue;
                    auto cut=Quantize(dominated);
                    if(!cut.empty()) visible=Boolean(ClipType::Difference,visible,{cut});
                    if(visible.empty()) break;
                }
                if(visible.empty()) continue;
                for (int side:{-1,1}) {
                    if(side<0 && !innerStroke) continue;
                    double range=side<0?opt.innerRange:opt.outerRange;
                    Plane cap=face;cap.c-=range;
                    auto limitedRing=cell;Clip(limitedRing,cap,clipScratch);
                    auto limited=Quantize(limitedRing);if(limited.empty()) continue;
                    auto region=Boolean(ClipType::Intersection,visible,{limited});
                    region=Boolean(side<0?ClipType::Intersection:ClipType::Difference,region,localShape);
                    Emit(region,face,side);
                }
            }
        }
        if(innerStroke) {
            auto deep=Boolean(ClipType::Difference,shape,Boolean(ClipType::Union,innerCapsules,{}));
            Emit(deep,{0,0,0},-1,true);
        } else Emit(shape,{0,0,0},1);
    }
};
} // namespace

bool BuildDistanceMesh(const std::vector<SDFPolygon>& groups,const SDFOptions& options,
                       SDFMesh& mesh,std::string& error) {
    mesh=SDFMesh();
    auto start=Clock::now();
    try {
        if ((options.geometry==SDFGeometry::InnerStroke && !(options.innerRange>0)) ||
            (options.geometry!=SDFGeometry::Fill && !(options.outerRange>0)) || !(options.distanceTolerance>=1e-4) ||
            !std::isfinite(options.innerRange) || !std::isfinite(options.outerRange) || !std::isfinite(options.distanceTolerance))
            throw std::runtime_error("SDF requires positive finite ranges and distanceTolerance >= 0.0001");
        {
        Builder builder{options,mesh,{}, {}, {},0,{}};
        builder.Prepare(groups);mesh.stats.prepareMs=Elapsed(start);
        auto partitionStart=Clock::now();builder.Partition();
        mesh.stats.partitionMs=Elapsed(partitionStart)-mesh.stats.triangulateMs;
        // Remove vertices referenced only by numerical zero-area slivers.
        std::vector<uint32_t> remap(mesh.distances.size(),UINT32_MAX);
        std::map<std::array<float,3>,uint32_t> shared;
        uint32_t count=0;
        for(auto& i:mesh.indices) {
            if(remap[i]==UINT32_MAX) {
                std::array<float,3> key{mesh.vertices[2*i],mesh.vertices[2*i+1],mesh.distances[i]};
                auto entry=shared.emplace(key,count);
                if(entry.second)++count;
                remap[i]=entry.first->second;
            }
        }
        std::vector<float> positions(count*2),uvs(count*2),values(count);
        for(size_t i=0;i<remap.size();++i) if(remap[i]!=UINT32_MAX) {
            auto j=remap[i];positions[2*j]=mesh.vertices[2*i];positions[2*j+1]=mesh.vertices[2*i+1];
            uvs[2*j]=mesh.uvs[2*i];uvs[2*j+1]=mesh.uvs[2*i+1];values[j]=mesh.distances[i];
        }
        for(auto& i:mesh.indices) i=remap[i];
        mesh.vertices=std::move(positions);mesh.uvs=std::move(uvs);mesh.distances=std::move(values);
        } // Include temporary-array/map destruction in total native time.
        mesh.stats.totalMs=Elapsed(start);
        mesh.stats.uniqueVertices=mesh.distances.size();mesh.stats.triangles=mesh.indices.size()/3;
        if(options.geometry==SDFGeometry::Fill) std::vector<float>().swap(mesh.distances);
        mesh.stats.outputBytes=(mesh.vertices.size()+mesh.distances.size()+mesh.uvs.size())*sizeof(float)+mesh.indices.size()*sizeof(uint32_t);
        if(mesh.indices.empty()) throw std::runtime_error("SDF produced no triangles");
        return true;
    } catch(const std::exception& e) {
        error=e.what();mesh=SDFMesh();return false;
    }
}
} // namespace Geometry2D
