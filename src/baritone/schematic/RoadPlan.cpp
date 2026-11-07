#include "baritone/schematic/RoadPlan.h"
#include "baritone/cache/WorldScanner.h"
#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace baritone {
namespace {
class RoadSchematic : public Schematic {
public:
    RoadSchematic(BlockPos size,std::map<BlockPos,DesiredBlock> cells):size_(size),cells_(std::move(cells)){}
    BlockPos size() const override{return size_;}
    bool restrictOutside() const override{return true;}
    bool mayBreak(int x,int y,int z) const override{return cells_.contains({x,y,z});}
    bool mayPlace(int x,int y,int z) const override {const auto at=cells_.find({x,y,z});return at!=cells_.end() && at->second.kind==DesiredBlock::Kind::Block;}
    DesiredBlock desired(int x,int y,int z,const BlockProps&) const override {const auto at=cells_.find({x,y,z});return at==cells_.end()?DesiredBlock::any():at->second;}
private: BlockPos size_;std::map<BlockPos,DesiredBlock> cells_;
};
}
RoadPlacement planRoad(BlockPos from,BlockPos to,int width,std::string material,int lightInterval) {
    const int64_t dx=int64_t(to.x)-from.x,dz=int64_t(to.z)-from.z,dy=int64_t(to.y)-from.y;
    const int64_t length=std::abs(dx)+std::abs(dz);
    if(length<1 || length>256 || std::abs(dy)*4>length || width<1 || width>9 || width%2==0 || lightInterval<0 || lightInterval>64 || (lightInterval>0 && (lightInterval<4 || width<3)))
        throw std::invalid_argument("Roads: horizontal length 1..256, odd width 1..9, grade at most 1:4, lights off (0) or every 4..64 blocks with width >=3");
    material=BlockFilter::normalize(std::move(material));
    if(material!="minecraft:stone" && material!="minecraft:cobblestone" && material!="minecraft:dirt")
        throw std::invalid_argument("Road paving must be stone, cobblestone or dirt");
    const int half=width/2;
    for(const auto p:{from,to})if(std::abs(int64_t(p.x))>30000000-half || std::abs(int64_t(p.z))>30000000-half || p.y<-2048 || p.y>2045)throw std::invalid_argument("Road coordinates exceed supported bounds");
    std::map<BlockPos,DesiredBlock> cells;
    const auto paving=DesiredBlock::block(material);
    const int sx=dx>=0?1:-1,sz=dz>=0?1:-1;
    BlockPos center=from;
    std::vector<BlockPos> centers;
    for(int step=0;step<=length;++step) {
        center.y=from.y+int((std::abs(dy)*step/length)*(dy>=0?1:-1));centers.push_back(center);
        // Square footprint also fills the inside corner and connects slopes.
        for(int ox=-half;ox<=half;++ox)for(int oz=-half;oz<=half;++oz) {
            const auto pos=center.offset(ox,0,oz);
            auto at=cells.find(pos);if(at==cells.end() || at->second.kind==DesiredBlock::Kind::Air)cells[pos]=paving;
        }
        if(step<std::abs(dx))center.x+=sx;else center.z+=sz;
    }
    // Clear walking headroom without erasing a higher paving cell on a slope.
    const auto floors=cells;
    for(const auto& [pos,block]:floors)for(int up=1;up<=2;++up)if(!cells.contains(pos.above(up)))cells[pos.above(up)]=DesiredBlock::air();
    if(lightInterval)for(std::size_t i=0;i<centers.size();i+=std::size_t(lightInterval)) {
        const auto pos=centers[i].offset(-half,1,-half);
        if(cells.contains(pos) && cells[pos].kind==DesiredBlock::Kind::Air && cells.contains(pos.below()) && cells[pos.below()].kind==DesiredBlock::Kind::Block)cells[pos]=DesiredBlock::block("minecraft:torch");
    }
    BlockPos low=from,high=from;
    std::size_t pavingCount=0,lights=0;
    for(const auto& [pos,block]:cells) {
        low={std::min(low.x,pos.x),std::min(low.y,pos.y),std::min(low.z,pos.z)};high={std::max(high.x,pos.x),std::max(high.y,pos.y),std::max(high.z,pos.z)};
        if(block.kind==DesiredBlock::Kind::Block){if(block.name=="minecraft:torch")++lights;else ++pavingCount;}
    }
    const BlockPos size{high.x-low.x+1,high.y-low.y+1,high.z-low.z+1};
    if(schematicVolume(size)>262144)throw std::invalid_argument("Road bounding volume exceeds construction limits; split the route");
    std::map<BlockPos,DesiredBlock> local;
    for(auto& [pos,block]:cells)local[{pos.x-low.x,pos.y-low.y,pos.z-low.z}]=std::move(block);
    return {std::make_shared<RoadSchematic>(size,std::move(local)),low,pavingCount,lights};
}
}
