#include "baritone/schematic/BuildAssistance.h"
#include "baritone/schematic/Orientation.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace baritone {
namespace {
std::string itemFor(const std::string& name) {
    if(name=="minecraft:wall_torch")return "minecraft:torch";
    if(name=="minecraft:lit_furnace")return "minecraft:furnace";
    return name;
}
bool technical(const std::string& name) {
    for(const auto* part:{"redstone","repeater","comparator","piston","observer","hopper","dispenser","dropper","lever","pressure_plate","tripwire","daylight_detector","rail"})
        if(name.find(part)!=std::string::npos)return true;
    return false;
}
}
std::optional<BuildHint> nextBuildHint(const SchematicWorkspace& placement,const WorldSnapshot& world,BlockPos player,int radius) {
    if(!placement.loaded())return {};
    if(radius<1 || radius>16)throw std::invalid_argument("Manual build hint radius must be 1..16");
    std::optional<BuildHint> best;int64_t distance=std::numeric_limits<int64_t>::max();
    for(const auto& cell:placement.preview(world,player,radius,512)) {
        if(cell.difference!=SchematicWorkspace::Difference::Missing)continue;
        if(auto policy=world.protection();policy && (!policy->permitsModify(cell.pos) || !policy->permitsEntry(cell.pos)))continue;
        const auto local=cell.pos.offset(-placement.origin().x,-placement.origin().y,-placement.origin().z);
        auto desired=placement.schematic()->desired(local.x,local.y,local.z,BlockProps{});
        if(desired.kind!=DesiredBlock::Kind::Block || technical(desired.name))continue;
        // Do not imply automatic support for states the builder cannot place.
        if(std::ranges::any_of(desired.states,[](const auto& state){return !isOrientationState(state.first);}))continue;
        bool supported=false;
        for(Direction direction:{Direction::Down,Direction::Up,Direction::North,Direction::South,Direction::East,Direction::West}) {
            const auto against=cell.pos.offset(direction);const auto chunk=world.chunk(against.x>>4,against.z>>4);
            if(!chunk || against.y<world.minY() || against.y>=world.maxY())continue;
            const auto& block=BlockRegistry::get().props(chunk->get(against.x&15,against.y,against.z&15));
            if(block.canPlaceAgainst && !block.hasFluid())supported=true;
        }
        if(!supported)continue;
        const auto d=player.distSq(cell.pos);
        if(d>=distance)continue;
        std::string guidance="Place the highlighted block manually";
        for(const auto& [key,value]:desired.states)guidance+="; "+key+"="+value;
        best=BuildHint{cell.pos,desired,itemFor(desired.name),std::move(guidance)};distance=d;
    }
    return best;
}
TechnicalAudit auditTechnicalPlacement(const Schematic& source,BlockPos origin) {
    const auto size=source.size();const auto volume=schematicVolume(size);
    if(volume>262144)throw std::invalid_argument("Technical audit supports at most 262144 cells");
    TechnicalAudit result;
    for(int x=0;x<size.x;++x)for(int y=0;y<size.y;++y)for(int z=0;z<size.z;++z) {
        ++result.cells;const auto desired=source.desired(x,y,z,BlockProps{});
        if(desired.kind!=DesiredBlock::Kind::Block)continue;
        const bool component=technical(desired.name);if(component)++result.technical;
        auto issue=[&](std::string reason){if(result.issues.size()<512)result.issues.push_back({origin.offset(x,y,z),desired.name,std::move(reason)});else result.truncated=true;};
        if(component)issue("Technical component: validate connections, placement and circuit behavior manually; no circuit simulation is performed");
        for(const auto& [key,value]:desired.states) {
            ++result.stateChecks;
            if(!isOrientationState(key))issue("State is not reproduced by the current placement controller: "+key+"="+value);
        }
        if(desired.name.find("chest")!=std::string::npos || desired.name.find("shulker")!=std::string::npos || desired.name.find("sign")!=std::string::npos)
            issue("Block-entity contents/text are not reproduced by ordinary block placement");
    }
    return result;
}
}
