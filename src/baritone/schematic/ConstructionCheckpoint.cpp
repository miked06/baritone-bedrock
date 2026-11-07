#include "baritone/schematic/ConstructionPlan.h"
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace baritone {
namespace {
class RecoveredSource : public Schematic {
public:
    RecoveredSource(BlockPos size,BlockPos origin,const std::map<BlockPos,DesiredBlock>& targets):size_(size) {
        for(const auto& [pos,block]:targets)cells_[{pos.x-origin.x,pos.y-origin.y,pos.z-origin.z}]=block;
    }
    BlockPos size() const override{return size_;}
    bool restrictOutside() const override{return true;}
    bool mayBreak(int x,int y,int z) const override{return cells_.contains({x,y,z});}
    bool mayPlace(int x,int y,int z) const override{const auto at=cells_.find({x,y,z});return at!=cells_.end() && at->second.kind==DesiredBlock::Kind::Block;}
    DesiredBlock desired(int x,int y,int z,const BlockProps&) const override{const auto at=cells_.find({x,y,z});return at==cells_.end()?DesiredBlock::any():at->second;}
private:BlockPos size_;std::map<BlockPos,DesiredBlock> cells_;
};
bool coordinate(BlockPos p){return p.x>=-30000000 && p.x<=30000000 && p.z>=-30000000 && p.z<=30000000 && p.y>=-2048 && p.y<=2048;}
bool token(const std::string& text){return !text.empty() && text.size()<=128 && text.find_first_of("\r\n\0",0,3)==std::string::npos;}
}
std::string ConstructionPlan::checkpoint(const WorldIdentity& identity,int phase) const {
    if(!ready_ || !failure_.empty() || phase<0 || phase>3)throw std::invalid_argument("Only a ready construction plan can be checkpointed");
    const auto sourceSize=source_->size();
    std::ostringstream out;
    out<<"BARITONE_CONSTRUCTION 1 "<<std::quoted(identity.name)<<' '<<identity.dimension<<' '<<phase<<' '<<permanent_<<' '<<depth_<<' '<<std::quoted(support_)<<' '
       <<origin_.x<<' '<<origin_.y<<' '<<origin_.z<<' '<<phaseOrigin_.x<<' '<<phaseOrigin_.y<<' '<<phaseOrigin_.z<<' '
       <<sourceSize.x<<' '<<sourceSize.y<<' '<<sourceSize.z<<' '<<final_.size()<<' '<<supports_.size()<<'\n';
    for(const auto* entries:{&final_,&supports_})for(const auto& [pos,block]:*entries) {
        out<<pos.x<<' '<<pos.y<<' '<<pos.z<<' '<<int(block.kind)<<' '<<std::quoted(block.name)<<' '<<block.states.size();
        for(const auto& [key,value]:block.states)out<<' '<<std::quoted(key)<<' '<<std::quoted(value);
        out<<'\n';
    }
    auto result=out.str();if(result.size()>8*1024*1024)throw std::invalid_argument("Construction checkpoint exceeds eight MiB");
    return result;
}
ConstructionRecovery ConstructionPlan::recover(const std::string& text) {
    if(text.size()>8*1024*1024)throw std::invalid_argument("Construction checkpoint exceeds eight MiB");
    ConstructionRecovery result;auto& plan=result.plan;std::istringstream in(text);std::string magic;int version=0,permanent=0;std::size_t targets=0,supports=0;BlockPos sourceSize;
    if(!(in>>magic>>version>>std::quoted(result.identity.name)>>result.identity.dimension>>result.phase>>permanent>>plan.depth_>>std::quoted(plan.support_)
         >>plan.origin_.x>>plan.origin_.y>>plan.origin_.z>>plan.phaseOrigin_.x>>plan.phaseOrigin_.y>>plan.phaseOrigin_.z>>sourceSize.x>>sourceSize.y>>sourceSize.z>>targets>>supports)
       || magic!="BARITONE_CONSTRUCTION" || version!=1 || result.identity.name.empty() || result.identity.name.size()>1024 || result.identity.dimension<0 || result.identity.dimension>2
       || result.phase<0 || result.phase>3 || permanent<0 || permanent>1 || plan.depth_<1 || plan.depth_>16 || !coordinate(plan.origin_) || !coordinate(plan.phaseOrigin_)
       || plan.phaseOrigin_.x!=plan.origin_.x || plan.phaseOrigin_.z!=plan.origin_.z || plan.phaseOrigin_.y>plan.origin_.y || plan.phaseOrigin_.y<plan.origin_.y-plan.depth_
       || (plan.support_!="minecraft:cobblestone" && plan.support_!="minecraft:stone" && plan.support_!="minecraft:dirt") || targets>262144 || supports>65536 || schematicVolume(sourceSize)>262144)
        throw std::invalid_argument("Invalid construction checkpoint header");
    if(int64_t(plan.origin_.x)+sourceSize.x>30000001 || int64_t(plan.origin_.z)+sourceSize.z>30000001 || int64_t(plan.origin_.y)+sourceSize.y>2049)throw std::invalid_argument("Construction checkpoint bounds overflow");
    plan.size_=sourceSize;plan.size_.y+=plan.origin_.y-plan.phaseOrigin_.y;
    if(schematicVolume(plan.size_)>262144 || targets>schematicVolume(sourceSize))throw std::invalid_argument("Construction checkpoint volume exceeds its limits");
    for(int group=0;group<2;++group)for(std::size_t i=0;i<(group?supports:targets);++i) {
        BlockPos pos;DesiredBlock block;int kind=0;std::size_t states=0;
        if(!(in>>pos.x>>pos.y>>pos.z>>kind>>std::quoted(block.name)>>states) || kind<int(DesiredBlock::Kind::Air) || kind>int(DesiredBlock::Kind::Block) || states>32 || !coordinate(pos))throw std::invalid_argument("Invalid construction checkpoint block");
        block.kind=DesiredBlock::Kind(kind);
        if((kind==int(DesiredBlock::Kind::Air) && (!block.name.empty() || states)) || (kind==int(DesiredBlock::Kind::Block) && (!token(block.name) || block.name.find(':')==block.name.npos)))throw std::invalid_argument("Invalid construction block identity");
        for(std::size_t s=0;s<states;++s){std::string key,value;if(!(in>>std::quoted(key)>>std::quoted(value)) || !token(key) || !token(value))throw std::invalid_argument("Invalid construction block state");for(const auto& state:block.states)if(state.first==key)throw std::invalid_argument("Duplicate construction block state");block.states.emplace_back(std::move(key),std::move(value));}
        const auto origin=group?plan.phaseOrigin_:plan.origin_;const auto size=group?plan.size_:sourceSize;
        if(pos.x<origin.x || pos.y<origin.y || pos.z<origin.z || int64_t(pos.x)>=int64_t(origin.x)+size.x || int64_t(pos.y)>=int64_t(origin.y)+size.y || int64_t(pos.z)>=int64_t(origin.z)+size.z)throw std::invalid_argument("Construction checkpoint cell is outside its bounds");
        if(group && (block.kind!=DesiredBlock::Kind::Block || block.name!=plan.support_ || !block.states.empty()))throw std::invalid_argument("Invalid construction support identity");
        auto& entries=group?plan.supports_:plan.final_;
        if(!entries.emplace(pos,std::move(block)).second)throw std::invalid_argument("Duplicate construction checkpoint cell");
        if(group){const auto at=plan.final_.find(pos);if(at!=plan.final_.end() && at->second.kind==DesiredBlock::Kind::Block)throw std::invalid_argument("Support overlaps a target block");}
    }
    if(in>>std::ws && in.peek()!=EOF)throw std::invalid_argument("Trailing construction checkpoint data");
    plan.source_=std::make_shared<RecoveredSource>(sourceSize,plan.origin_,plan.final_);plan.permanent_=permanent!=0;plan.collected_=plan.ready_=true;
    return result;
}
}
