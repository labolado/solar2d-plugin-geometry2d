// Plugin-owned local offset-band construction; mapbox earcut (ISC) is used
// only for the remaining core. This is NOT an exact Euclidean distance field.
#include "earcut_stroke_builder.h"
#include "mapbox/earcut.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Geometry2D {
namespace {
using P = SDFPoint;
double Cross(P a,P b,P c) {return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);}
double Area(const SDFRing& r) {
    double a=0;for(size_t i=1;i+1<r.size();++i)a+=Cross(r[0],r[i],r[i+1]);return a*.5;
}
struct Builder {
    const SDFOptions& opt;double limit;SDFMesh& out;
    void Fail(const char* why) {throw std::runtime_error(std::string("earcut innerStroke: ")+why+"; adjust ranges/limits, simplify input, or use backend='robust'");}
    void Work(size_t n=1) {
        if(n>opt.maxWork-out.stats.work)Fail("maxWork exceeded");out.stats.work+=n;
    }
    P Position(P p) {
        P q{static_cast<float>(p[0]),static_cast<float>(p[1])};
        if(!std::isfinite(q[0]) || !std::isfinite(q[1]) ||
            std::hypot(q[0]-p[0],q[1]-p[1])>std::min(opt.innerRange,opt.outerRange)*.001)
            Fail("float32 precision insufficient (recenter/rescale coordinates)");
        return q;
    }
    SDFRing Normalize(const SDFRing& input,bool hole) {
        SDFRing r;P previous{};
        for(auto p:input) {
            Work();if(!std::isfinite(p[0]) || !std::isfinite(p[1]))Fail("non-finite input");
            auto q=Position(p);
            if(!r.empty() && q==r.back() && p!=previous)Fail("float32 merged distinct input points");
            if(r.empty() || q!=r.back())r.push_back(q);
            previous=p;
        }
        if(r.size()>1 && r.front()==r.back()) {
            if(input.front()!=input.back())Fail("float32 merged distinct closing points");
            r.pop_back();
        }
        if(r.size()<3 || std::abs(Area(r))<1e-10)Fail("degenerate ring");
        if((Area(r)>0)==hole)std::reverse(r.begin(),r.end());
        return r;
    }
    bool Inside(P p,const SDFRing& ring) {
        bool in=false;
        for(size_t i=0,j=ring.size()-1;i<ring.size();j=i++) {
            Work();auto a=ring[j],b=ring[i];
            if((a[1]>p[1])!=(b[1]>p[1]) && p[0]<(b[0]-a[0])*(p[1]-a[1])/(b[1]-a[1])+a[0])in=!in;
        }
        return in;
    }
    bool Filled(P p,const SDFPolygon& poly) {
        if(!Inside(p,poly[0]))return false;
        for(size_t i=1;i<poly.size();++i)if(Inside(p,poly[i]))return false;
        return true;
    }
    void Crossings(const SDFPolygon& rings) {
        struct Edge {P a,b;size_t ring,index,count;};std::vector<Edge> edges;
        for(size_t r=0;r<rings.size();++r)for(size_t i=0;i<rings[r].size();++i)
            edges.push_back({rings[r][i],rings[r][(i+1)%rings[r].size()],r,i,rings[r].size()});
        std::sort(edges.begin(),edges.end(),[](const Edge& a,const Edge& b){return std::min(a.a[0],a.b[0])<std::min(b.a[0],b.b[0]);});
        for(size_t i=0;i<edges.size();++i)for(size_t j=i+1;j<edges.size();++j) {
            Work();const auto& a=edges[i];const auto& b=edges[j];
            if(std::min(b.a[0],b.b[0])>std::max(a.a[0],a.b[0]))break;
            if(a.ring==b.ring && ((a.index+1)%a.count==b.index || (b.index+1)%b.count==a.index))continue;
            if(std::max(a.a[1],a.b[1])<std::min(b.a[1],b.b[1]) || std::max(b.a[1],b.b[1])<std::min(a.a[1],a.b[1]))continue;
            double c1=Cross(a.a,a.b,b.a),c2=Cross(a.a,a.b,b.b),c3=Cross(b.a,b.b,a.a),c4=Cross(b.a,b.b,a.b);
            if(((c1<=0 && c2>=0)||(c2<=0 && c1>=0)) && ((c3<=0 && c4>=0)||(c4<=0 && c3>=0)))
                Fail("contours/offset bands intersect or touch");
        }
    }
    void Holes(const SDFPolygon& poly) {
        for(size_t i=1;i<poly.size();++i) {
            if(!Inside(poly[i][0],poly[0]))Fail("hole escapes outer contour");
            for(size_t j=1;j<poly.size();++j)if(i!=j && Inside(poly[i][0],poly[j]))Fail("nested/overlapping holes in one group");
        }
    }
    uint32_t Vertex(P p,double d) {
        if(out.distances.size()>=opt.maxVertices)Fail("maxVertices exceeded");
        uint32_t i=static_cast<uint32_t>(out.distances.size());
        out.vertices.push_back(static_cast<float>(p[0]));out.vertices.push_back(static_cast<float>(p[1]));
        out.distances.push_back(static_cast<float>(d));
        out.uvs.push_back(static_cast<float>((p[0]-out.bounds[0])/(out.bounds[2]-out.bounds[0])));
        out.uvs.push_back(static_cast<float>((p[1]-out.bounds[1])/(out.bounds[3]-out.bounds[1])));
        return i;
    }
    double Triangle(uint32_t a,uint32_t b,uint32_t c,bool core=false) {
        Work();double cross=Cross({out.vertices[2*a],out.vertices[2*a+1]},
            {out.vertices[2*b],out.vertices[2*b+1]},{out.vertices[2*c],out.vertices[2*c+1]});
        if(core && cross==0)return 0;
        if(!(cross>0))Fail("folded/collapsed triangle");
        out.indices.insert(out.indices.end(),{a,b,c});return cross*.5;
    }
    void Group(const SDFPolygon& input) {
        if(input.empty())Fail("empty group");
        SDFPolygon original,inner,outer;
        for(size_t r=0;r<input.size();++r) {
            auto ring=Normalize(input[r],r!=0);size_t n=ring.size();
            if(n>(opt.maxVertices-out.distances.size())/3)Fail("maxVertices exceeded");
            SDFRing in,ex,normals;
            for(size_t i=0;i<n;++i) {
                auto a=ring[i],b=ring[(i+1)%n];double dx=b[0]-a[0],dy=b[1]-a[1],len=std::hypot(dx,dy);
                if(len<1e-6)Fail("edge below coordinate resolution");normals.push_back({-dy/len,dx/len});
            }
            for(size_t i=0;i<n;++i) {
                auto a=normals[(i+n-1)%n],b=normals[i];double denom=1+a[0]*b[0]+a[1]*b[1];
                if(denom<1e-10)Fail("near reversal");
                P m{(a[0]+b[0])/denom,(a[1]+b[1])/denom};
                if(std::hypot(m[0],m[1])>limit+1e-6)Fail("miterLimit exceeded");
                in.push_back(Position({ring[i][0]+m[0]*opt.innerRange,ring[i][1]+m[1]*opt.innerRange}));
                ex.push_back(Position({ring[i][0]-m[0]*opt.outerRange,ring[i][1]-m[1]*opt.outerRange}));
            }
            if(Area(in)*Area(ring)<=0 || Area(ex)*Area(ring)<=0)Fail("offset ring collapsed/reversed");
            out.stats.inputEdges+=n;original.push_back(std::move(ring));inner.push_back(std::move(in));outer.push_back(std::move(ex));
        }
        // Only local validity tests, never a union or distance envelope. Groups
        // remain caller-owned disjoint regions; their AA bands can still meet.
        SDFPolygon layers=original;layers.insert(layers.end(),inner.begin(),inner.end());layers.insert(layers.end(),outer.begin(),outer.end());
        Crossings(layers);Holes(original);Holes(inner);Holes(outer);
        for(const auto& ring:inner)for(auto p:ring)if(!Filled(p,original))Fail("inner band leaves filled region");
        for(const auto& ring:outer)for(auto p:ring)if(Filled(p,original))Fail("outer band enters filled region");
        std::vector<uint32_t> coreVertices;
        for(size_t r=0;r<original.size();++r) {
            size_t n=original[r].size();std::vector<uint32_t> b(n),in(n),ex(n);
            for(size_t i=0;i<n;++i) {
                b[i]=Vertex(original[r][i],0);in[i]=Vertex(inner[r][i],-opt.innerRange);ex[i]=Vertex(outer[r][i],opt.outerRange);
                coreVertices.push_back(in[i]);
            }
            for(size_t i=0;i<n;++i) {
                size_t j=(i+1)%n;
                Triangle(b[i],b[j],in[j]);Triangle(b[i],in[j],in[i]);
                Triangle(b[i],ex[i],ex[j]);Triangle(b[i],ex[j],b[j]);
            }
        }
        auto indices=mapbox::earcut<uint32_t>(inner);
        if(indices.empty())Fail("core triangulation failed");
        double expected=0,actual=0;for(const auto& r:inner)expected+=Area(r);
        if(!(expected>0))Fail("core disappeared");
        for(size_t i=0;i<indices.size();i+=3)actual+=Triangle(coreVertices[indices[i]],coreVertices[indices[i+1]],coreVertices[indices[i+2]],true);
        if(std::abs(actual-expected)>std::max(1e-7,expected*1e-8))Fail("core triangulation area mismatch");
    }
};
}
bool BuildLocalStrokeMesh(const std::vector<SDFPolygon>& groups,const SDFOptions& options,
                          double miterLimit,SDFMesh& mesh,std::string& error) {
    mesh=SDFMesh();auto start=std::chrono::steady_clock::now();
    try {
        if(!(options.innerRange>0) || !(options.outerRange>0) || !std::isfinite(options.innerRange) ||
            !std::isfinite(options.outerRange) || !std::isfinite(miterLimit) || miterLimit<=0)
            throw std::runtime_error("earcut innerStroke requires positive finite ranges/miterLimit");
        bool first=true;
        for(const auto& group:groups)for(const auto& ring:group)for(auto p:ring) {
            if(!std::isfinite(p[0]) || !std::isfinite(p[1]))throw std::runtime_error("earcut innerStroke non-finite input");
            if(first){mesh.bounds={p[0],p[1],p[0],p[1]};first=false;}
            mesh.bounds[0]=std::min(mesh.bounds[0],p[0]);mesh.bounds[1]=std::min(mesh.bounds[1],p[1]);
            mesh.bounds[2]=std::max(mesh.bounds[2],p[0]);mesh.bounds[3]=std::max(mesh.bounds[3],p[1]);
        }
        if(first || mesh.bounds[0]>=mesh.bounds[2] || mesh.bounds[1]>=mesh.bounds[3])throw std::runtime_error("earcut innerStroke degenerate bounds");
        {Builder b{options,miterLimit,mesh};for(const auto& group:groups)b.Group(group);}
        mesh.stats.uniqueVertices=mesh.distances.size();mesh.stats.triangles=mesh.indices.size()/3;
        mesh.stats.totalMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        mesh.stats.outputBytes=(mesh.vertices.size()+mesh.uvs.size()+mesh.distances.size())*sizeof(float)+mesh.indices.size()*sizeof(uint32_t);
        return true;
    }catch(const std::exception& e){error=e.what();mesh=SDFMesh();return false;}
}
}
