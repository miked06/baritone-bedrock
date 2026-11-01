#include "baritone/schematic/QuarryPlan.h"
#include "baritone/world/Protection.h"
namespace baritone {
QuarryPlan::QuarryPlan(Selection area, bool lighting) : origin_(area.min()), lighting_(lighting) {
    const auto hi=area.max();
    if (int64_t(hi.x)-origin_.x >= 128 || int64_t(hi.z)-origin_.z >= 128 || int64_t(hi.y)-origin_.y >= 1024) throw std::invalid_argument("Quarry selection is too large");
    size_=area.size();
    if (size_.x < 5 || size_.z < 5 || size_.x > 128 || size_.z > 128 || size_.y < 1 || size_.y > 2*(size_.x+size_.z)-4 || schematicVolume(size_) > 1024*1024)
        throw std::invalid_argument("Quarry: width/length 5..128, depth at most one perimeter lap, volume at most one million blocks");
    if (origin_.x == INT32_MIN || origin_.z == INT32_MIN || int64_t(origin_.x)+size_.x >= INT32_MAX || int64_t(origin_.z)+size_.z >= INT32_MAX)
        throw std::invalid_argument("Quarry boundary coordinates overflow");
}
int QuarryPlan::rampFloor(int x,int z) const {
    int step;
    if (z==0) step=x;
    else if (x==size_.x-1) step=size_.x-1+z;
    else if (z==size_.z-1) step=size_.x+size_.z-2+(size_.x-1-x);
    else if (x==0) step=2*size_.x+size_.z-3+(size_.z-1-z);
    else return -1;
    return size_.y-1-step;
}
DesiredBlock QuarryPlan::desired(int x,int y,int z,const BlockProps&) const {
    const int floor = rampFloor(x,z);
    if (floor>=0 && y<=floor) return DesiredBlock::any();
    if (lighting_ && floor>=0 && y==floor+1 && (size_.y-1-floor)%8==0) return DesiredBlock::block("minecraft:torch");
    return DesiredBlock::air();
}
void QuarryPreflight::start(std::shared_ptr<const QuarryPlan> plan) {
    if (!plan) throw std::invalid_argument("Missing quarry plan"); plan_=std::move(plan); cursor_=blocks_=0; ready_=false; failure_.clear();
}
bool QuarryPreflight::step(const WorldSnapshot& world,std::size_t budget) {
    if (!plan_ || ready_ || !failure_.empty()) return true;
    budget=std::min<std::size_t>(budget,4096);
    auto size=plan_->size(), origin=plan_->origin();
    const uint64_t width=size.x+2, depth=size.z+2, volume=width*depth*size.y;
    for (std::size_t n=0;n<budget && cursor_<volume;++n,++cursor_) {
        int x=int(cursor_%width)-1,z=int((cursor_/width)%depth)-1,y=int(cursor_/(width*depth));
        auto pos=origin.offset(x,y,z); auto chunk=world.chunk(pos.x>>4,pos.z>>4);
        if (!chunk || pos.y<world.minY() || pos.y>=world.maxY()) { failure_="Load the entire quarry and boundary before starting"; return true; }
        const auto& block=BlockRegistry::get().props(chunk->get(pos.x&15,pos.y,pos.z&15));
        if (block.type==BlockType::Water || block.type==BlockType::Lava) { failure_="Liquid detected in the quarry or boundary; contain it before excavating"; return true; }
        if (x<0 || z<0 || x>=size.x || z>=size.z) continue;
        const auto wanted=plan_->desired(x,y,z,block);
        if (wanted.kind==DesiredBlock::Kind::Any) {
            if (y==plan_->rampFloor(x,z) && (!block.fullCube || block.falling || block.type==BlockType::Magma || block.type==BlockType::Cactus)) { failure_="The access ramp needs safe, solid existing terrain at every step"; return true; }
            continue;
        }
        if (auto policy=world.protection(); policy && (!policy->permitsModify(pos) || !policy->permitsEntry(pos))) { failure_="Quarry intersects a protected area"; return true; }
        if (block.unbreakable && block.type!=BlockType::Air) { failure_="Unbreakable block inside the excavation"; return true; }
        if (block.type!=BlockType::Air) ++blocks_;
    }
    ready_=cursor_==volume; return ready_;
}
}
