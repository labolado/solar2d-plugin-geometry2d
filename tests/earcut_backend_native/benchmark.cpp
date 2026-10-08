#include "earcut_stroke_builder.h"
#include <chrono>
#include <cmath>
#include <iostream>

using namespace Geometry2D;
int main() {
    for(int count:{4,32,128,256}) {
        SDFRing ring;
        for(int i=0;i<count;++i){double a=6.283185307179586*i/count;ring.push_back({100*std::cos(a),100*std::sin(a)});}
        std::vector<SDFPolygon> groups{{ring}};
        for(bool local:{false,true}) {
            double sum=0;SDFMesh m;std::string error;SDFOptions options;
            for(int repeat=0;repeat<6;++repeat) {
                auto start=std::chrono::steady_clock::now();
                bool ok=local?BuildLocalStrokeMesh(groups,options,2.4,m,error):BuildDistanceMesh(groups,options,m,error);
                if(!ok){std::cerr<<error<<"\n";return 1;}
                double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                if(repeat)sum+=ms;
            }
            std::cout<<"LOCAL_STROKE_BENCH points="<<count<<" backend="<<(local?"earcut":"robust")<<" mean_ms="<<sum/5
                <<" vertices="<<m.vertices.size()/2<<" triangles="<<m.indices.size()/3
                <<" indexed_bytes="<<(m.vertices.size()+m.uvs.size()+m.distances.size())*4+m.indices.size()*2<<"\n";
        }
    }
}
