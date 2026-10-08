#include "region.h"
#include "tiles.h"
#include "export.h"
#include "earcut_stroke_builder.h"
#include <chrono>
#include <iomanip>
#include <iostream>
using namespace Geometry2D;
int main(int argc,char** argv) {
    size_t count;if(!(std::cin>>count))return 2;
    for(size_t c=0;c<count;++c) {
        std::string label;size_t ng;std::cin>>label>>ng;
        std::vector<SDFPolygon> groups(ng);
        for(auto& g:groups){size_t nr;std::cin>>nr;g.resize(nr);
            for(auto& ring:g){size_t np;std::cin>>np;ring.resize(np);for(auto& p:ring)std::cin>>p[0]>>p[1];}}
        if(!std::cin)return 2;
        auto original=groups;
        for(auto method:{"local","partition","region","tiles4x8","tiles6x6","tilesShared11"})for(int run=0;run<3;++run) {
            std::string error;SDFOptions opt;opt.innerRange=4;opt.outerRange=2;
            SDFMesh mesh;Prototype::RegionResult region;Prototype::TileResult tiles;
            auto t=std::chrono::steady_clock::now();bool ok;
            if(std::string(method)=="local")ok=BuildLocalStrokeMesh(groups,opt,2.4,mesh,error);
            else if(std::string(method)=="partition")ok=BuildDistanceMesh(groups,opt,mesh,error);
            else if(std::string(method)=="region")ok=Prototype::Region(groups,4,region,error);
            else if(std::string(method)=="tiles4x8")ok=Prototype::Tiles(groups,tiles,error,4,8);
            else if(std::string(method)=="tiles6x6")ok=Prototype::Tiles(groups,tiles,error,6,6);
            else ok=Prototype::Tiles(groups,tiles,error,11,11);
            double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();
            size_t tileStorage=tiles.output.capacity()*sizeof(Prototype::Tile);
            for(const auto& tile:tiles.output)tileStorage+=(tile.distanceEdges.capacity()+tile.signEdges.capacity())*sizeof(std::array<float,4>);
            if(groups!=original)return 3;
            if(argc==2 && ok && run==2 && std::string(method)=="tilesShared11")Prototype::Export(argv[1],label,tiles,groups);
            std::cout<<std::setprecision(12)<<"{\"case\":"<<std::quoted(label)<<",\"method\":"<<std::quoted(method)
                <<",\"run\":"<<run<<",\"warmup\":"<<(run==0?"true":"false")<<",\"success\":"<<(ok?"true":"false")
                <<",\"totalMs\":"<<ms<<",\"vertices\":"<<(mesh.vertices.size()+region.vertices.size())/2
                <<",\"triangles\":"<<(mesh.indices.size()+region.indices.size())/3
                <<",\"normalizeMs\":"<<region.normalizeMs<<",\"offsetMs\":"<<region.offsetMs
                <<",\"triangulateMs\":"<<region.triangulateMs<<",\"validateMs\":"<<region.validateMs
                <<",\"coverageDifference\":"<<region.coverageDifference<<",\"overlapArea\":"<<region.overlapArea
                <<",\"failedMaterial\":"<<region.failedMaterial<<",\"failedOriginalCross\":"<<region.failedOriginalCross<<",\"failedFloatCross\":"<<region.failedFloatCross
                <<",\"coreRegions\":"<<region.coreRegions<<",\"tileCells\":"<<tiles.cells<<",\"tileLeaves\":"<<tiles.leaves
                <<",\"tileDepth\":"<<tiles.maxDepth<<",\"tileWork\":"<<tiles.work<<",\"tileProbes\":"<<tiles.probes
                <<",\"tileAuditWork\":"<<tiles.auditWork<<",\"tileAuditMs\":"<<tiles.auditMs
                <<",\"tileGenerationMs\":"<<(tiles.cells?ms-tiles.auditMs:0)
                <<",\"tileOutputVertices\":"<<tiles.output.size()*4<<",\"tileOutputTriangles\":"<<tiles.output.size()*2
                <<",\"tileOutputBytes\":"<<tiles.output.size()*(4*52*sizeof(float)+6*sizeof(uint16_t))
                <<",\"tileStorageCapacityBytes\":"<<tileStorage
                <<",\"tileDeepInside\":"<<tiles.deepInside<<",\"tileEmptyOutside\":"<<tiles.emptyOutside
                <<",\"tileMaxDistanceError\":"<<tiles.maxDistanceError<<",\"error\":"<<std::quoted(error)<<"}\n";
        }
    }
    std::cout<<"DIAGNOSTIC_COMPLETE (not all geometries passed)\n";
}
