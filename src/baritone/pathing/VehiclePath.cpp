#include "baritone/pathing/VehiclePath.h"
#include "baritone/world/Protection.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <queue>
#include <unordered_map>
namespace baritone {
namespace {
const BlockProps* live(const WorldSnapshot& world,BlockPos p) {
    auto c=world.chunk(p.x>>4,p.z>>4); if (!c || p.y<world.minY() || p.y>=world.maxY()) return nullptr;
    return &BlockRegistry::get().props(c->get(p.x&15,p.y,p.z&15));
}
bool hazard(const BlockProps& b) { return b.type==BlockType::Lava || b.type==BlockType::Fire || b.type==BlockType::Magma || b.type==BlockType::Cactus || b.type==BlockType::SweetBerryBush || b.type==BlockType::PowderSnow || b.type==BlockType::Cobweb; }
double heuristic(BlockPos a,BlockPos b) { return std::abs(double(a.x)-b.x)+std::abs(double(a.z)-b.z)+std::abs(double(a.y)-b.y); }
}
bool vehicleNodeClear(const WorldSnapshot& world,BlockPos p,VehicleKind kind) {
    if (p.x<=INT32_MIN+2 || p.z<=INT32_MIN+2 || p.x>=INT32_MAX-2 || p.z>=INT32_MAX-2 || p.y<=world.minY() || p.y+3>=world.maxY()) return false;
    if (auto policy=world.protection(); policy && (!policy->permitsEntry(p) || !policy->permitsEntry(p.above(2)))) return false;
    // A conservative three-block corridor accounts for a wide vehicle and
    // steering error. A player-sized route cannot be reused for a boat/horse.
    for (int dx=-1;dx<=1;++dx) for (int dz=-1;dz<=1;++dz) {
        auto at=p.offset(dx,0,dz); const auto* floor=live(world,at.below());
        if (!floor || hazard(*floor)) return false;
        if (kind==VehicleKind::Boat ? floor->fluid!=Fluid::Water : !floor->fullCube && !floor->solidTop) return false;
        for (int dy=0;dy<(kind==VehicleKind::Boat ? 2 : 3);++dy) {
            auto q=at.above(dy); const auto* b=live(world,q);
            if (!b || b->collides || b->hasFluid() || hazard(*b)) return false;
            if (auto policy=world.protection(); policy && !policy->permitsEntry(q)) return false;
        }
    }
    return true;
}
VehiclePath findVehiclePath(const WorldSnapshot& world,BlockPos from,BlockPos to,VehicleKind kind,const std::atomic<bool>* cancel) {
    if (heuristic(from,to)>256 || std::abs(double(from.x)-to.x)>128 || std::abs(double(from.z)-to.z)>128) return {{},"Vehicle trips are limited to 128 horizontal blocks per segment"};
    if (!vehicleNodeClear(world,from,kind) || !vehicleNodeClear(world,to,kind)) return {{},"Start/destination needs a loaded, wide, safe vehicle corridor"};
    struct Node { BlockPos p; double score; bool operator<(const Node& other) const { return score>other.score; } };
    std::priority_queue<Node> open; std::unordered_map<BlockPos,double> costs; std::unordered_map<BlockPos,BlockPos> previous;
    open.push({from,heuristic(from,to)}); costs[from]=0;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(300);
    std::size_t expanded=0;
    while (!open.empty() && expanded++<32768) {
        if ((expanded&63)==0 && ((cancel && cancel->load()) || std::chrono::steady_clock::now()>deadline)) return {{},"Vehicle path calculation cancelled or exceeded its fixed time budget"};
        auto current=open.top(); open.pop();
        if (current.p==to) {
            VehiclePath result; for (auto p=to;;p=previous.at(p)) { result.points.push_back(p); if (p==from) break; }
            std::ranges::reverse(result.points); return result;
        }
        const double cost=costs.at(current.p);
        if (current.score>cost+heuristic(current.p,to)+.001) continue;
        for (auto d:kHorizontals) for (int dy=kind==VehicleKind::Boat ? 0 : -1;dy<=(kind==VehicleKind::Boat ? 0 : 1);++dy) {
            auto next=current.p.offset(d).above(dy);
            if (std::abs(double(next.x)-from.x)>128 || std::abs(double(next.z)-from.z)>128 || !vehicleNodeClear(world,next,kind)) continue;
            if (auto policy=world.protection(); policy && !policy->permitsSegment(current.p,next)) continue;
            double candidate=cost+1+std::abs(dy)*.5;
            if (costs.contains(next) && candidate>=costs.at(next)) continue;
            costs[next]=candidate; previous[next]=current.p; open.push({next,candidate+heuristic(next,to)});
        }
    }
    return {{},"No loaded safe vehicle route; choose a nearer destination or a wider corridor"};
}
}
