#include "mesh_builder.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <algorithm>

// mesh_builder's only dependency on the Lua parser is this flatten helper.
// The real input parser and descriptors are exercised in Simulator separately.
namespace Geometry2D {
void FlattenPolygon(const Polygon& poly,std::vector<Point>& coords) {
    for(const auto& ring:poly)coords.insert(coords.end(),ring.begin(),ring.end());
}
}
using namespace Geometry2D;
int main() {
    for (float aa : {0.0f,1.0f}) {
        std::vector<Fringe::Vertex> out;
        assert(!Fringe::ExpandStroke({{{0,0},{100,0}}},false,10,aa,
            Fringe::CAP_ROUND,Fringe::JOIN_ROUND,2.4f,1e-8f,out));
        assert(out.empty());
        assert(Fringe::ExpandStroke({{{0,0},{100,0}}},false,10,aa,
            Fringe::CAP_ROUND,Fringe::JOIN_ROUND,2.4f,.25f,out));
    }
    std::vector<Polygon> groups={{{{0,0},{120,0},{120,120},{0,120}},
        {{40,40},{80,40},{80,80},{40,80}}},{{{160,0},{180,0},{180,20},{160,20}}}};
    auto original=groups;
    for(bool triangles:{false,true})for(auto join:{Fringe::JOIN_MITER,Fringe::JOIN_BEVEL,Fringe::JOIN_ROUND}) {
        MeshOptions options;options.triangles=triangles;options.join=join;options.fringe=2;
        options.sdf.geometry=SDFGeometry::FillAA;
        MeshResult fast;std::string error;
        assert(BuildFillMesh(groups,options,false,fast,error));
        assert(fast.fillResult && !fast.sdfResult && fast.fringeWidth==2);
        assert(fast.uvs.size()==fast.vertices.size());
        for(size_t i=0;i<fast.VertexCount();++i) {
            assert(std::abs(fast.vertices[2*i]-fast.uvs[2*i]*180)<.0001);
            assert(std::abs(fast.vertices[2*i+1]-fast.uvs[2*i+1]*120)<.0001);
            assert(fast.values[i]>=0 && fast.values[i]<=1);
        }
        assert(groups==original);
        options.sdf.maxVertices=12;
        MeshResult rejected;assert(!BuildFillMesh(groups,options,false,rejected,error));
    }
    for(bool triangles:{false,true}) {
        MeshOptions options;options.vertexAA=false;
        options.triangles=triangles;
        // Internal options deliberately contain an unusable round tolerance:
        // fill must not enter any fringe generation/subdivision path.
        options.join=Fringe::JOIN_ROUND;options.tessTol=1e-30f;
        MeshResult fill;std::string error;
        assert(BuildFillMesh(groups,options,false,fill,error));
        assert(fill.valueName==nullptr && fill.values.empty() && fill.values.capacity()==0);
        assert(fill.fringeWidth==0 && fill.fillResult);
        double area=0;
        size_t count=triangles?fill.VertexCount():fill.indices.size();
        for(size_t i=0;i<count;i+=3) {
            auto at=[&](size_t j){return triangles?j:size_t(fill.indices[j]);};
            auto a=at(i)*2,b=at(i+1)*2,c=at(i+2)*2;const auto& v=fill.vertices;
            area+=std::abs((double(v[b])-v[a])*(double(v[c+1])-v[a+1])-(double(v[b+1])-v[a+1])*(double(v[c])-v[a]))*.5;
        }
        assert(std::abs(area-13200)<.001);
        assert(fill.uvs.size()==fill.vertices.size() && groups==original);
    }
    MeshOptions options;options.earcutBackend=true;options.sdf.geometry=SDFGeometry::FillAA;
    options.join=Fringe::JOIN_ROUND;options.tessTol=1e-30f;
    MeshResult rejected;std::string error;
    assert(!BuildFillMesh(groups,options,false,rejected,error));
    std::vector<std::vector<Polygon>> fixtures={
        {{{{0,0},{200,0},{200,200},{0,200}}}},
        {{{{0,0},{200,0},{200,80},{80,80},{80,200},{0,200}}}},
        {{{{0,0},{200,0},{200,200},{0,200}},{{60,60},{140,60},{140,140},{60,140}}}},
        {{{{0,0},{200,0},{200,200},{0,200}},{{60,60},{140,60},{140,140},{60,140}}},{{{80,80},{120,80},{120,120},{80,120}}}},
    };
    for(auto shape:fixtures) {
        auto originalShape=shape;
        MeshOptions o;o.earcutBackend=true;
        MeshResult m,list;
        if(!BuildFillMesh(shape,o,true,m,error)){std::cerr<<error<<"\n";return 1;}
        assert(shape==originalShape && m.values.size()==m.VertexCount());
        assert(m.VertexCount()==3*m.sdfStats.inputEdges);
        for(float d:m.values)assert(d>=-8 && d<=2);
        o.triangles=true;assert(BuildFillMesh(shape,o,true,list,error));
        for(size_t i=0;i<m.indices.size();++i) {
            auto id=m.indices[i];assert(list.values[i]==m.values[id]);
            assert(list.vertices[2*i]==m.vertices[2*id] && list.vertices[2*i+1]==m.vertices[2*id+1]);
        }
        size_t probes=0;double maxDistanceError=0;
        for(double y=.317;y<200;y+=3.713)for(double x=.137;x<200;x+=3.917) {
            bool filled=false;
            for(const auto& group:shape) {
                bool region=false;
                for(const auto& ring:group)for(size_t i=0,j=ring.size()-1;i<ring.size();j=i++) {
                    auto a=ring[j],b=ring[i];
                    if((a[1]>y)!=(b[1]>y) && x<(b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0])region=!region;
                }
                filled=filled||region;
            }
            double exact=1e30;
            for(const auto& group:shape)for(const auto& ring:group)for(size_t i=0;i<ring.size();++i) {
                auto a=ring[i],b=ring[(i+1)%ring.size()];double dx=b[0]-a[0],dy=b[1]-a[1];
                double t=std::clamp(((x-a[0])*dx+(y-a[1])*dy)/(dx*dx+dy*dy),0.0,1.0);
                exact=std::min(exact,std::hypot(x-a[0]-t*dx,y-a[1]-t*dy));
            }
            double target=filled?-std::min(exact,8.0):exact;
            size_t hits=0;
            for(size_t i=0;i<m.indices.size();i+=3) {
                auto ia=m.indices[i],ib=m.indices[i+1],ic=m.indices[i+2];
                double ax=m.vertices[2*ia],ay=m.vertices[2*ia+1],bx=m.vertices[2*ib],by=m.vertices[2*ib+1],cx=m.vertices[2*ic],cy=m.vertices[2*ic+1];
                double den=(by-cy)*(ax-cx)+(cx-bx)*(ay-cy);assert(den>0);
                double u=((by-cy)*(x-cx)+(cx-bx)*(y-cy))/den,v=((cy-ay)*(x-cx)+(ax-cx)*(y-cy))/den;
                if(u>1e-8 && v>1e-8 && 1-u-v>1e-8) {
                    ++hits;double d=u*m.values[ia]+v*m.values[ib]+(1-u-v)*m.values[ic];
                    assert(filled?d<=1e-5:d>=-1e-5);
                    maxDistanceError=std::max(maxDistanceError,std::abs(d-target));
                }
            }
            assert(hits<=1);if(filled)assert(hits==1);++probes;
        }
        std::cout<<"LOCAL_STROKE probes="<<probes<<" vertices="<<m.VertexCount()<<" triangles="<<m.indices.size()/3<<" max_approx_distance_error="<<maxDistanceError<<"\n";
        for(auto& group:shape)for(auto& ring:group)std::reverse(ring.begin(),ring.end());
        MeshResult reversed;o.triangles=false;assert(BuildFillMesh(shape,o,true,reversed,error));
        assert(reversed.vertices==m.vertices && reversed.values==m.values && reversed.indices==m.indices);
    }
    for(int failure=0;failure<4;++failure) {
        MeshOptions o;o.earcutBackend=true;MeshResult m;
        if(failure==0)o.sdf.innerRange=120;
        if(failure==1)o.sdf.maxWork=1;
        if(failure==2)o.sdf.maxVertices=5;
        if(failure==3)o.miterLimit=1;
        assert(!BuildFillMesh(fixtures[0],o,true,m,error));assert(m.vertices.empty());
    }
    for(auto difficult:std::vector<std::vector<Polygon>>{
        {{{{0,0},{200,0},{200,3},{0,3}}}},
        {{{{0,0},{120,0},{120,120},{0,120}},{{59,59},{61,59},{61,61},{59,61}}}},
        {{{{0,0},{60,0},{60,28},{100,28},{100,0},{160,0},{160,60},{100,60},{100,32},{60,32},{60,60},{0,60}}}}
    }) {
        MeshOptions o;o.earcutBackend=true;MeshResult m;
        assert(!BuildFillMesh(difficult,o,true,m,error));assert(m.vertices.empty());
    }
    std::cout<<"LOCAL_STROKE_NATIVE PASS coverage, parity, winding, input invariance and bounded failures\n";
    std::cout<<"FILL_LOCAL_NATIVE PASS none/vertex fill area, indexed/triangles, UV, alpha, input invariance and budgets\n";
}
