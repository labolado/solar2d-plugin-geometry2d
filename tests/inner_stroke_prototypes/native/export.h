#pragma once
#include "tiles.h"
#include <fstream>
#include <filesystem>
#include <cstdint>
#include <iomanip>
namespace Prototype {
template<class T> void Save(const std::filesystem::path& path,const std::vector<T>& data) {
    std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(data.data()),data.size()*sizeof(T));
    if(!f)throw std::runtime_error("prototype export failed");
}
inline void Export(const std::filesystem::path& dir,const std::string& name,const TileResult& result,
    const std::vector<Geometry2D::SDFPolygon>& input) {
    if(result.output.empty())return;
    if(result.output.size()*4>65535)throw std::runtime_error("prototype export indexed capacity exceeded");
    std::filesystem::create_directories(dir);
    std::array<float,4> bounds=result.output.front().bounds;
    for(const auto& tile:result.output) {
        if(tile.distanceEdges.size()+tile.signEdges.size()>11)throw std::runtime_error("shared-slot export overflow");
        bounds[0]=std::min(bounds[0],tile.bounds[0]);bounds[1]=std::min(bounds[1],tile.bounds[1]);
        bounds[2]=std::max(bounds[2],tile.bounds[2]);bounds[3]=std::max(bounds[3],tile.bounds[3]);
    }
    std::vector<float> positions,uvs,attributes;std::vector<uint16_t> indices;
    for(const auto& tile:result.output) {
        uint16_t base=uint16_t(positions.size()/2);
        const auto& b=tile.bounds;
        for(auto p:{std::array<float,2>{b[0],b[1]},{b[2],b[1]},{b[2],b[3]},{b[0],b[3]}}) {
            positions.insert(positions.end(),{p[0],p[1]});
            uvs.insert(uvs.end(),{(p[0]-bounds[0])/(bounds[2]-bounds[0]),(p[1]-bounds[1])/(bounds[3]-bounds[1])});
            std::array<float,48> values{};size_t index=0;
            for(auto e:tile.distanceEdges)for(float v:e)values[index++]=v;
            for(auto e:tile.signEdges)for(float v:e)values[index++]=v;
            if(index>44)throw std::runtime_error("shared-slot export overflow");
            values[44]=float(tile.distanceEdges.size());values[45]=float(tile.signEdges.size());values[46]=float(tile.constant);
            attributes.insert(attributes.end(),values.begin(),values.end());
        }
        for(unsigned i:{0u,1u,2u,0u,2u,3u})indices.push_back(uint16_t(base+i));
    }
    Save(dir/(name+"-positions.bin"),positions);Save(dir/(name+"-uvs.bin"),uvs);
    Save(dir/(name+"-indices.bin"),indices);Save(dir/(name+"-attributes.bin"),attributes);
    std::ofstream meta(dir/(name+".json"));
    meta<<std::setprecision(9)<<"{\"vertices\":"<<positions.size()/2<<",\"indices\":"<<indices.size()<<",\"bounds\":["
        <<bounds[0]<<","<<bounds[1]<<","<<bounds[2]<<","<<bounds[3]<<"],\"rings\":[";
    bool first=true;
    for(const auto& ring:NormalizedPaths(input)) {
        if(!first)meta<<",";first=false;meta<<"[";
        for(size_t i=0;i<ring.size();++i){if(i)meta<<",";meta<<ring[i].x<<","<<ring[i].y;}
        meta<<"]";
    }
    meta<<"],\"tiles\":[";first=true;
    for(const auto& tile:result.output) {
        if(!first)meta<<",";first=false;const auto& b=tile.bounds;
        meta<<"["<<b[0]<<","<<b[1]<<","<<b[2]<<","<<b[3]<<"]";
    }
    meta<<"]}";
    if(!meta)throw std::runtime_error("prototype manifest export failed");
}
}
