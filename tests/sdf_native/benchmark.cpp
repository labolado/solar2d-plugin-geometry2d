// Test-only v1 baseline: earcut body plus unchanged ExpandFill skirt.
#include "sdf_builder.h"
#include "fringe.h"
#include "mapbox/earcut.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <set>

static size_t liveBytes=0,peakBytes=0;
struct alignas(std::max_align_t) Allocation {size_t size;};
void* operator new(size_t n) {
    auto p=static_cast<Allocation*>(std::malloc(n+sizeof(Allocation)));
    if(!p) throw std::bad_alloc();p->size=n;liveBytes+=n;peakBytes=std::max(peakBytes,liveBytes);return p+1;
}
void operator delete(void* p) noexcept {if(p){auto h=static_cast<Allocation*>(p)-1;liveBytes-=h->size;std::free(h);}}
void* operator new[](size_t n) {return ::operator new(n);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete(void* p,size_t) noexcept {::operator delete(p);}
void operator delete[](void* p,size_t) noexcept {::operator delete(p);}

using namespace Geometry2D;
using Clock=std::chrono::steady_clock;
static double ms(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
static SDFMesh Legacy(const std::vector<SDFPolygon>& groups) {
    SDFMesh m;auto start=Clock::now();
    for(const auto& poly:groups) {
        auto stage=Clock::now();
        auto tri=mapbox::earcut<uint32_t>(poly);auto base=m.distances.size();
        m.stats.triangulateMs+=ms(stage);
        std::vector<Fringe::FillRing> rings;
        for(size_t r=0;r<poly.size();++r) {
            Fringe::FillRing ring;ring.hole=r?1:0;
            for(auto p:poly[r]) {m.vertices.push_back(p[0]);m.vertices.push_back(p[1]);m.distances.push_back(0);ring.points.push_back({float(p[0]),float(p[1])});}
            rings.push_back(std::move(ring));
        }
        for(auto i:tri)m.indices.push_back(uint32_t(base+i));
        std::vector<Fringe::Vertex> skirt;
        stage=Clock::now();
        Fringe::ExpandFill(rings,2,Fringe::JOIN_ROUND,2.4,.25,skirt);
        m.stats.partitionMs+=ms(stage);
        for(auto v:skirt) {m.indices.push_back(uint32_t(m.distances.size()));m.vertices.push_back(v.x);m.vertices.push_back(v.y);m.distances.push_back(-2*v.u);}
    }
    m.stats.totalMs=ms(start);m.stats.uniqueVertices=m.distances.size();m.stats.triangles=m.indices.size()/3;
    m.stats.prepareMs=m.stats.totalMs-m.stats.partitionMs-m.stats.triangulateMs;
    return m;
}
int main(){
    for(const char* kind:{"points","holes","groups"}) for(int n:{4,16,64,128,256}) {
        std::vector<SDFPolygon> groups;
        if(std::string(kind)=="points") {
            SDFRing ring;for(int i=0;i<n;++i){double a=6.283185307179586*i/n,r=i%2?90:100;ring.push_back({r*std::cos(a),r*std::sin(a)});}groups.push_back({ring});
        } else if(std::string(kind)=="holes") {
            int side=int(std::ceil(std::sqrt(n)));double extent=side*20+10;
            SDFPolygon p{{{0,0},{extent,0},{extent,extent},{0,extent}}};
            for(int i=0;i<n;++i){double x=i%side*20+8,y=i/side*20+8;p.push_back({{x,y},{x+8,y},{x+8,y+8},{x,y+8}});}groups.push_back(p);
        } else {
            for(int i=0;i<n;++i){double x=i%16*30,y=i/16*30;groups.push_back({{{x,y},{x+20,y},{x+20,y+20},{x,y+20}}});}
        }
        for(bool legacy:{true,false}) {
            std::vector<double> times;SDFStats stats;size_t peak=0,bytes=0,indices=0,vertices=0,unique=0;
            double firstMs=0;
            std::string error;bool failed=false;
            for(int repeat=0;repeat<6;++repeat) {
                size_t base=liveBytes;peakBytes=base;
                SDFMesh m;auto t=Clock::now();
                if(legacy) m=Legacy(groups);
                else {
                    SDFOptions options;
                    if(n==256 && std::string(kind)!="points") options.maxWork=20000000;
                    if(!BuildDistanceMesh(groups,options,m,error)){failed=true;break;}
                }
                double elapsed=ms(t);
                peak=std::max(peak,peakBytes-base);stats=m.stats;
                indices=m.indices.size();vertices=m.distances.size();
                bytes=vertices*(legacy?12:20)+indices*2;
                if(repeat)times.push_back(elapsed);else firstMs=elapsed;
                if(repeat==5) {
                    std::set<std::array<float,3>> attributes;
                    for(size_t i=0;i<vertices;++i)attributes.insert({m.vertices[2*i],m.vertices[2*i+1],m.distances[i]});
                    unique=attributes.size();
                }
            }
            if(failed){std::cout<<"SDF_BENCH "<<kind<<" n="<<n<<" new FAIL "<<error<<"\n";continue;}
            std::sort(times.begin(),times.end());double sum=0;for(auto v:times)sum+=v;
            std::cout<<"SDF_BENCH "<<kind<<" n="<<n<<" "<<(legacy?"old":"new")
                <<" work_limit="<<((n==256 && std::string(kind)!="points")?20000000:2000000)
                <<" first_ms="<<firstMs<<" mean_ms="<<sum/times.size()<<" p95_ms="<<times.back()
                <<" prepare_ms="<<stats.prepareMs<<" partition_ms="<<stats.partitionMs<<" triangulate_ms="<<stats.triangulateMs
                <<" unique_attributes="<<unique<<" pool_vertices="<<vertices<<" list_vertices="<<indices<<" indices="<<indices<<" triangles="<<indices/3
                <<" indexed_bytes="<<bytes<<" call_peak_live_bytes="<<peak<<"\n";
        }
        for(auto geometry:{SDFGeometry::Fill,SDFGeometry::FillAA}) {
            SDFOptions options;options.geometry=geometry;
            if(n==256 && std::string(kind)!="points")options.maxWork=20000000;
            double first=0,sum=0;size_t peak=0;SDFStats stats;std::string error;
            size_t bytes=0;
            for(int repeat=0;repeat<6;++repeat) {
                SDFMesh m;size_t base=liveBytes;peakBytes=base;auto start=Clock::now();
                if(!BuildDistanceMesh(groups,options,m,error)){std::cerr<<error<<"\n";return 1;}
                double elapsed=ms(start);if(repeat)sum+=elapsed;else first=elapsed;
                peak=std::max(peak,peakBytes-base);stats=m.stats;
                bytes=(m.vertices.size()+m.uvs.size()+m.distances.size())*4+m.indices.size()*2;
            }
            std::cout<<"SDF_LIGHT_BENCH "<<kind<<" n="<<n<<" geometry="<<SDFGeometryName(geometry)
                <<" first_ms="<<first<<" mean_ms="<<sum/5
                <<" prepare_ms="<<stats.prepareMs<<" partition_ms="<<stats.partitionMs<<" triangulate_ms="<<stats.triangulateMs
                <<" vertices="<<stats.uniqueVertices<<" triangles="<<stats.triangles<<" indexed_bytes="<<bytes
                <<" call_peak_live_bytes="<<peak<<"\n";
        }
    }
}
