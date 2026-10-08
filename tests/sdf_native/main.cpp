#include "sdf_builder.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace Geometry2D;
static double Exact(const std::vector<SDFPolygon>& groups,double x,double y) {
    double distance=1e30;bool inside=false;
    for(const auto& group:groups) {
        bool in=false;
        for(const auto& ring:group)for(size_t i=0,j=ring.size()-1;i<ring.size();j=i++) {
            auto a=ring[j],b=ring[i];double dx=b[0]-a[0],dy=b[1]-a[1];
            double t=std::fmax(0,std::fmin(1,((x-a[0])*dx+(y-a[1])*dy)/(dx*dx+dy*dy)));
            distance=std::fmin(distance,std::hypot(x-a[0]-t*dx,y-a[1]-t*dy));
            if((a[1]>y)!=(b[1]>y) && x<(b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0])in=!in;
        }
        inside=inside||in;
    }
    return inside?-distance:distance;
}
int main() {
    std::vector<std::vector<SDFPolygon>> cases={
        {{{{0,0},{200,0},{200,200},{0,200}}}},
        {{{{0,0},{200,0},{200,80},{80,80},{80,200},{0,200}}}},
        {{{{0,0},{200,0},{200,3},{0,3}}}},
        {{{{0,0},{200,0},{200,200},{0,200}},{{60,60},{140,60},{140,140},{60,140}}}},
        {{{{0,0},{200,0},{200,200},{0,200}},{{60,60},{140,60},{140,140},{60,140}}},{{{70,70},{130,70},{130,130},{70,130}}}},
        {{{{0,0},{99,0},{99,200},{0,200}}},{{{100,0},{200,0},{200,200},{100,200}}}},
        {{{{0,0},{200,0},{200,200},{90,200},{90,80},{80,80},{80,200},{0,200}}}},
        {{{{0,0},{200,0},{1,2}}}},
        {{{{0,0},{60,0},{60,28},{100,28},{100,0},{160,0},{160,60},{100,60},{100,32},{60,32},{60,60},{0,60}}}},
    };
    SDFRing star;
    for(int i=0;i<40;++i) {double r=i%2?55:95,a=i*3.141592653589793/20;star.push_back({100+r*std::cos(a),100+r*std::sin(a)});}
    cases.push_back({{star}});
    for(int n:{16,64,128,256}) {
        SDFRing r;
        for(int i=0;i<n;++i){double a=6.283185307179586*i/n,radius=i%2?90:100;r.push_back({radius*std::cos(a),radius*std::sin(a)});}
        cases.push_back({{r}});
    }
    SDFPolygon holes{{{0,0},{100,0},{100,100},{0,100}}};
    for(int i=0;i<16;++i){double x=i%4*20+8,y=i/4*20+8;holes.push_back({{x,y},{x+8,y},{x+8,y+8},{x,y+8}});}
    cases.push_back({holes});
    for (auto groups : cases) {
        SDFMesh m;SDFOptions o;std::string error;
        auto original=groups;
        bool ok=BuildDistanceMesh(groups,o,m,error);
        if(!ok){std::cerr<<error<<"\n";return 1;}
        assert(groups==original);
        double worst=0;size_t tested=0;
        for(size_t k=0;k<m.indices.size();k+=3) {
            double x=0,y=0,d=0;
            for(size_t j=k;j<k+3;++j){auto i=m.indices[j];x+=m.vertices[2*i]/3.0;y+=m.vertices[2*i+1]/3.0;d+=m.distances[i]/3.0;}
            double expected=std::fmax(-o.innerRange,Exact(groups,x,y));
            if(std::abs(d-expected)>o.distanceTolerance+.001) {
                std::cerr<<"centroid distance "<<d<<" expected "<<expected<<" at "<<x<<","<<y<<"\n";return 1;
            }
        }
        for(double y=-1.31;y<202;y+=2.317) for(double x=-1.27;x<202;x+=2.173) {
            double exact=1e30;bool inside=false;
            for(const auto& group:groups) {
              bool groupInside=false;
              for (const auto& ring:group) {
               for(size_t i=0,j=ring.size()-1;i<ring.size();j=i++) {
                auto a=ring[j],b=ring[i];double dx=b[0]-a[0],dy=b[1]-a[1];
                double t=std::fmax(0,std::fmin(1,((x-a[0])*dx+(y-a[1])*dy)/(dx*dx+dy*dy)));
                exact=std::fmin(exact,std::hypot(x-a[0]-t*dx,y-a[1]-t*dy));
                if((a[1]>y)!=(b[1]>y) && x<(b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0]) groupInside=!groupInside;
               }
              }
              inside=inside||groupInside;
            }
            double expected=inside?-std::fmin(exact,o.innerRange):exact;
            size_t hits=0;
            for(size_t k=0;k<m.indices.size();k+=3) {
                auto a=m.indices[k],b=m.indices[k+1],c=m.indices[k+2];
                double ax=m.vertices[2*a],ay=m.vertices[2*a+1],bx=m.vertices[2*b],by=m.vertices[2*b+1],cx=m.vertices[2*c],cy=m.vertices[2*c+1];
                double det=(by-cy)*(ax-cx)+(cx-bx)*(ay-cy);
                double u=((by-cy)*(x-cx)+(cx-bx)*(y-cy))/det;
                double v=((cy-ay)*(x-cx)+(ax-cx)*(y-cy))/det,w=1-u-v;
                if(u>1e-7 && v>1e-7 && w>1e-7) {
                    ++hits;double d=u*m.distances[a]+v*m.distances[b]+w*m.distances[c];
                    worst=std::fmax(worst,std::abs(d-expected));++tested;
                }
            }
            if(hits>1 || ((inside || exact<o.outerRange-.01) && hits!=1)) {
                std::cerr<<"coverage "<<hits<<" at "<<x<<","<<y<<"\n";return 1;
            }
        }
        std::cout<<"SDF edges="<<m.stats.inputEdges<<" vertices="<<m.distances.size()<<" triangles="<<m.indices.size()/3<<" ms="<<m.stats.totalMs<<" samples="<<tested<<" worst="<<worst<<"\n";
        assert(worst<=o.distanceTolerance+1e-3);
    }
    for(auto geometry:{SDFGeometry::Fill,SDFGeometry::FillAA}) for(const auto& groups:cases) {
        SDFOptions o;o.geometry=geometry;SDFMesh m;std::string error;
        auto original=groups;
        if(!BuildDistanceMesh(groups,o,m,error)){std::cerr<<SDFGeometryName(geometry)<<": "<<error<<"\n";return 1;}
        assert(original==groups);
        assert((geometry==SDFGeometry::Fill)==m.distances.empty());
        assert(m.stats.uniqueVertices==m.vertices.size()/2);
        // Every triangle's interior has the correct unsigned distance; body
        // triangles stay wholly in the filled region without distance splits.
        for(size_t k=0;k<m.indices.size();k+=3) {
            double x=0,y=0,d=0;
            for(size_t j=k;j<k+3;++j) {
                auto i=m.indices[j];x+=m.vertices[2*i]/3.0;y+=m.vertices[2*i+1]/3.0;
                if(!m.distances.empty())d+=m.distances[i]/3.0;
            }
            double exact=Exact(groups,x,y);
            if(geometry==SDFGeometry::Fill)assert(exact<=1e-5);
            else assert(std::abs(d-std::fmax(exact,0))<=o.distanceTolerance+.001);
        }
        for(double y=-1.31;y<202;y+=7.317)for(double x=-1.27;x<202;x+=7.173) {
            double exact=Exact(groups,x,y);size_t hits=0;
            for(size_t k=0;k<m.indices.size();k+=3) {
                auto a=m.indices[k],b=m.indices[k+1],c=m.indices[k+2];
                double ax=m.vertices[2*a],ay=m.vertices[2*a+1],bx=m.vertices[2*b],by=m.vertices[2*b+1],cx=m.vertices[2*c],cy=m.vertices[2*c+1];
                double det=(by-cy)*(ax-cx)+(cx-bx)*(ay-cy);
                double u=((by-cy)*(x-cx)+(cx-bx)*(y-cy))/det;
                double v=((cy-ay)*(x-cx)+(ax-cx)*(y-cy))/det;
                if(u>1e-7 && v>1e-7 && 1-u-v>1e-7)++hits;
            }
            assert(hits<=1);
            if(exact<-.001 || (geometry==SDFGeometry::FillAA && exact<o.outerRange-.01))assert(hits==1);
            if(geometry==SDFGeometry::Fill && exact>.001)assert(hits==0);
        }
    }
    std::cout<<"SDF_LIGHT_NATIVE PASS fill/fillAA coverage, distances and input invariance (15 fixtures each)\n";
    for(auto transform:std::vector<std::array<double,6>>{{.2,0,0,5,31,27},{-2,.2,.1,.5,0,0}}) {
        SDFOptions o;o.transform=transform;SDFMesh m;std::string error;
        assert(BuildDistanceMesh(cases[1],o,m,error));
        auto transformed=cases[1];
        for(auto& group:transformed)for(auto& ring:group)for(auto& p:ring){auto q=p;p={transform[0]*q[0]+transform[2]*q[1]+transform[4],transform[1]*q[0]+transform[3]*q[1]+transform[5]};}
        for(size_t k=0;k<m.indices.size();k+=3) {
            double x=0,y=0,d=0;
            for(size_t j=k;j<k+3;++j){auto i=m.indices[j];x+=m.vertices[2*i]/3.0;y+=m.vertices[2*i+1]/3.0;d+=m.distances[i]/3.0;}
            double expected=std::fmax(-o.innerRange,Exact(transformed,transform[0]*x+transform[2]*y+transform[4],transform[1]*x+transform[3]*y+transform[5]));
            assert(std::abs(d-expected)<=o.distanceTolerance+.001);
        }
    }
    for(auto bad:std::vector<SDFOptions>{[](){SDFOptions o;o.innerRange=1e38;return o;}(),[](){SDFOptions o;o.maxWork=1;return o;}()}) {
        SDFMesh m;std::string error;assert(!BuildDistanceMesh(cases[0],bad,m,error));assert(m.vertices.empty() && !error.empty());
    }
    std::cout<<"SDF_NATIVE PASS distance/coverage/input invariance/affine transforms/failure contracts\n";
}
