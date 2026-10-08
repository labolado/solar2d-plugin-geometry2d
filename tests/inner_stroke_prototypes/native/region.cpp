#include "region.h"
#include "clipper2/clipper.h"
#include "mapbox/earcut.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
namespace Prototype {
namespace {
using namespace Clipper2Lib;
using Clock=std::chrono::steady_clock;
double Ms(Clock::time_point t) {return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
PathsD Boolean(ClipType type,const PathsD& a,const PathsD& b={}) {
    ClipperD c(6);c.AddSubject(a);c.AddClip(b);PathsD out;
    if(!c.Execute(type,FillRule::NonZero,out)||c.ErrorCode())throw std::runtime_error("region boolean failed");
    return out;
}
PathsD Normalize(const std::vector<Geometry2D::SDFPolygon>& input) {
    PathsD all;
    for(const auto& group:input) {
        PathsD outer,holes;
        for(size_t i=0;i<group.size();++i) {
            PathD ring;for(auto p:group[i])ring.emplace_back(p[0],p[1]);
            if(Area(ring)<0)std::reverse(ring.begin(),ring.end());
            (i?holes:outer).push_back(std::move(ring));
        }
        auto region=Boolean(ClipType::Difference,outer,holes);
        all.insert(all.end(),region.begin(),region.end());
    }
    return Boolean(ClipType::Union,all);
}
double Cross(double ax,double ay,double bx,double by,double cx,double cy) {
    return (bx-ax)*(cy-ay)-(by-ay)*(cx-ax);
}
void Emit(const PathsD& paths,unsigned label,RegionResult& out,PathsD& triangles) {
    ClipperD c(6);c.AddSubject(paths);PolyTreeD tree;
    if(!c.Execute(ClipType::Union,FillRule::NonZero,tree)||c.ErrorCode())throw std::runtime_error("region tree failed");
    auto visit=[&](auto&& self,const PolyPathD& node)->void {
        if(!node.Polygon().empty()&&!node.IsHole()) {
            if(!label)++out.coreRegions;
            Geometry2D::SDFPolygon polygon;
            auto add=[&](const PathD& path){Geometry2D::SDFRing ring;for(auto p:path)ring.push_back({p.x,p.y});polygon.push_back(std::move(ring));};
            add(node.Polygon());for(const auto& hole:node)add(hole->Polygon());
            auto ids=mapbox::earcut<unsigned>(polygon);
            if(ids.empty())throw std::runtime_error("region triangulation empty");
            std::vector<Geometry2D::SDFPoint> points;
            for(auto& ring:polygon)points.insert(points.end(),ring.begin(),ring.end());
            if(out.vertices.size()/2+points.size()>1000000)throw std::runtime_error("region vertex budget exceeded");
            unsigned base=unsigned(out.vertices.size()/2);
            for(auto p:points){out.vertices.push_back(float(p[0]));out.vertices.push_back(float(p[1]));}
            for(size_t i=0;i<ids.size();i+=3) {
                auto a=points[ids[i]],b=points[ids[i+1]],d=points[ids[i+2]];
                double exact=Cross(a[0],a[1],b[0],b[1],d[0],d[1]);
                auto point=[&](unsigned id){return PointD(double(out.vertices[2*(base+id)]),double(out.vertices[2*(base+id)+1]));};
                auto fa=point(ids[i]),fb=point(ids[i+1]),fc=point(ids[i+2]);
                double floating=Cross(fa.x,fa.y,fb.x,fb.y,fc.x,fc.y);
                if(!(exact>0)||!(floating>0)) {
                    out.failedMaterial=int(label);out.failedOriginalCross=exact;out.failedFloatCross=floating;
                }
                if(!(exact>0))throw std::runtime_error("region original reversed/collapsed triangle");
                if(!(floating>0))throw std::runtime_error("region float32 reversed/collapsed triangle");
                triangles.push_back({fa,fb,fc});
                out.indices.insert(out.indices.end(),{base+ids[i],base+ids[i+1],base+ids[i+2]});out.labels.push_back(label);
            }
        }
        for(const auto& child:node)self(self,*child);
    };visit(visit,tree);
}
}
Clipper2Lib::PathsD NormalizedPaths(const std::vector<Geometry2D::SDFPolygon>& input) {return Normalize(input);}
bool Region(const std::vector<Geometry2D::SDFPolygon>& input,double width,RegionResult& out,std::string& error) {
    out={};
    try {
        if(!(width>0)||!std::isfinite(width))throw std::runtime_error("positive width required");
        auto t=Clock::now();auto shape=Normalize(input);out.normalizeMs=Ms(t);
        if(shape.empty())throw std::runtime_error("empty normalized input");
        t=Clock::now();
        // Round offset; arc tolerance in local units. Offset result is clipped
        // to the source before the complementary stroke region is formed.
        auto core=Boolean(ClipType::Intersection,InflatePaths(shape,-width,JoinType::Round,EndType::Polygon,2,6,.05),shape);
        auto stroke=Boolean(ClipType::Difference,shape,core);out.offsetMs=Ms(t);
        t=Clock::now();PathsD coreTris,strokeTris;
        Emit(core,0,out,coreTris);Emit(stroke,1,out,strokeTris);out.triangulateMs=Ms(t);
        t=Clock::now();
        PathsD all=coreTris;all.insert(all.end(),strokeTris.begin(),strokeTris.end());
        auto united=Boolean(ClipType::Union,all);
        out.coverageDifference=std::abs(Area(Boolean(ClipType::Xor,shape,united)));
        // Sum of absolute triangle areas vs union catches duplicated coverage,
        // including overlaps within one material region.
        double sum=0;for(auto& tri:all)sum+=std::abs(Area(tri));
        out.overlapArea=std::max(0.0,sum-std::abs(Area(united)));
        out.validateMs=Ms(t);
        // Diagnostics only: positive differences are NOT declared repaired.
        if(out.coverageDifference>1e-6 || out.overlapArea>1e-6)
            throw std::runtime_error("region float32 coverage/overlap audit failed");
        return true;
    } catch(const std::exception& e){error=e.what();return false;}
}
}
