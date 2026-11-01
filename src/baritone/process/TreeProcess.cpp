#include "baritone/process/TreeProcess.h"
#include "baritone/Baritone.h"
#include "baritone/Settings.h"
#include "baritone/utils/RayTrace.h"
#include "baritone/world/Protection.h"
#include <algorithm>
#include <set>
#include <unordered_map>
namespace baritone {
namespace {
const std::unordered_map<std::string,std::string> saplings{
    {"minecraft:oak_log","minecraft:oak_sapling"},{"minecraft:spruce_log","minecraft:spruce_sapling"},
    {"minecraft:birch_log","minecraft:birch_sapling"},{"minecraft:jungle_log","minecraft:jungle_sapling"},
    {"minecraft:acacia_log","minecraft:acacia_sapling"},{"minecraft:dark_oak_log","minecraft:dark_oak_sapling"},
    {"minecraft:cherry_log","minecraft:cherry_sapling"},{"minecraft:pale_oak_log","minecraft:pale_oak_sapling"}
};
bool soil(const std::string& name) {
    return name=="minecraft:dirt" || name=="minecraft:grass_block" || name=="minecraft:podzol" || name=="minecraft:coarse_dirt" || name=="minecraft:rooted_dirt" || name=="minecraft:moss_block";
}
const BlockProps* live(const WorldSnapshot& world,BlockPos p) {
    auto c=world.chunk(p.x>>4,p.z>>4); if (!c || p.y<world.minY() || p.y>=world.maxY()) return nullptr;
    return &BlockRegistry::get().props(c->get(p.x&15,p.y,p.z&15));
}
}
std::optional<TreePlan> identifyTree(const WorldSnapshot& world,const Selection& selection,BlockPos root) {
    const auto* base=live(world,root); const auto* ground=live(world,root.below());
    if (!base || !ground || !saplings.contains(base->name) || !soil(ground->name)) return {};
    TreePlan tree; tree.log=base->name; tree.sapling=saplings.at(base->name);
    std::set<BlockPos> visited{root}, canopy;
    std::vector<BlockPos> pending{root};
    for (std::size_t i=0;i<pending.size();++i) {
        const auto p=pending[i]; if (!selection.contains(p) || pending.size()>128) return {};
        if (auto protection=world.protection(); protection && !protection->permitsModify(p)) return {};
        tree.logs.push_back(p);
        if (const auto* b=live(world,p.below()); b && soil(b->name)) tree.roots.push_back(p);
        for (int dx=-1;dx<=1;++dx) for (int dy=-1;dy<=1;++dy) for (int dz=-1;dz<=1;++dz) {
            auto q=p.offset(dx,dy,dz); const auto* b=live(world,q); if (!b) return {};
            if (b->type==BlockType::Leaves && (b->state("persistent_bit")=="0" || b->state("persistent_bit")=="false")) canopy.insert(q);
        }
        for (Direction d : {Direction::Down,Direction::Up,Direction::North,Direction::South,Direction::West,Direction::East}) {
            auto q=p.offset(d); const auto* b=live(world,q); if (!b) return {};
            if (b->name==tree.log && visited.insert(q).second) pending.push_back(q);
        }
    }
    if (tree.logs.size()<3 || canopy.size()<3 || tree.roots.empty() || tree.roots.size()>4) return {};
    std::ranges::sort(tree.logs,[](BlockPos a,BlockPos b) { return a.y>b.y; });
    return tree;
}
void TreeProcess::start(Selection selection,int count) {
    const auto lo=selection.min(),hi=selection.max();
    if (count<1 || count>64 || int64_t(hi.x)-lo.x>=64 || int64_t(hi.y)-lo.y>=64 || int64_t(hi.z)-lo.z>=64) throw std::invalid_argument("Tree farming: count 1..64 and selection sides at most 64 blocks");
    auto s=selection.size(); if (int64_t(s.x)*s.y*s.z>32768) throw std::invalid_argument("Tree selection volume must be at most 32768 blocks");
    if (!Settings::get().allowBreak || !Settings::get().allowPlace) throw std::invalid_argument("Tree farming requires allowBreak and allowPlace");
    onLostControl(); selection_=selection; requested_=count; completed_=waiting_=timeout_=0; cursor_=0; active_=true;
}
std::string TreeProcess::displayName() const { return std::format("Tree farming {}/{}",completed_,requested_); }
std::optional<PathingCommand> TreeProcess::onTick(bool failed,bool safe) {
    if (!active_ || !baritone_.liveContext()) return {};
    auto stop=[&](const std::string& reason,bool error) -> std::optional<PathingCommand> { baritone_.logDirect(reason,error); onLostControl(); return PathingCommand{nullptr,PathingCommandType::CancelAndSetGoal}; };
    if (failed) return stop("No safe path to the selected tree",true);
    if (completed_>=requested_) return stop("Tree harvesting and replanting complete",false);
    if (++timeout_>24000) return stop("Tree task timed out; inspect reachability and supplies",true);
    auto& world=*baritone_.world(); auto& player=baritone_.player();
    if (!plan_) {
        auto s=selection_.size(),lo=selection_.min(); const uint64_t volume=uint64_t(s.x)*s.y*s.z;
        for (int i=0;i<256 && cursor_<volume;++i,++cursor_) {
            BlockPos p=lo.offset(int(cursor_%s.x),int(cursor_/(s.x*s.z)),int((cursor_/s.x)%s.z));
            const auto* block=live(world,p); if (!block) return stop("Load every chunk in the tree selection first",true);
            if (!saplings.contains(block->name)) continue;
            auto candidate=identifyTree(world,selection_,p); if (!candidate) continue;
            if (player.countItem(candidate->sapling)<static_cast<int>(candidate->roots.size())) return stop("Keep enough matching saplings in inventory to replant before harvesting",true);
            plan_=std::move(candidate); break;
        }
        if (!plan_) {
            if (cursor_>=volume) return stop("No eligible complete tree with natural leaves remains in the selection",false);
            return PathingCommand{nullptr,PathingCommandType::RequestPause};
        }
    }
    auto& plan=*plan_; auto& ctx=*baritone_.liveContext(); ExecContext ec{player,ctx,baritone_.inventory(),baritone_.playerFeet()};
    std::erase_if(plan.logs,[&](BlockPos p) { const auto* b=live(world,p); return b && b->name!=plan.log; });
    if (!plan.logs.empty()) {
        BlockPos target=plan.logs.front();
        // If a high branch cannot be reached, remove a lower reachable log first.
        for (auto p : plan.logs) if (auto aim=movement_helper::reachable(ec,p)) {
            if (!ctx.permitsModify(p) || !safe) continue;
            baritone_.controls().setTarget({*aim,true}); movement_helper::switchToBestToolFor(ec,ctx.props(p.x,p.y,p.z));
            if (ec.isLookingAt(p)) baritone_.controls().setInput(Input::ClickLeft,true);
            return PathingCommand{nullptr,PathingCommandType::RequestPause};
        }
        return PathingCommand{std::make_shared<GoalBreak>(target),PathingCommandType::RevalidateGoalAndPath};
    }
    if (++waiting_<40) return PathingCommand{nullptr,PathingCommandType::RequestPause};
    while (!plan.roots.empty()) {
        const auto root=plan.roots.back(); const auto* current=live(world,root);
        if (!current) return stop("Tree replanting chunk unloaded",true);
        if (current->name==plan.sapling) { plan.roots.pop_back(); continue; }
        if (current->type!=BlockType::Air && !current->replaceable) return stop("The replanting position is occupied; task stopped",true);
        if (!ctx.permitsModify(root) || !ctx.permitsModify(root.below())) return stop("Tree replanting is protected",true);
        if (!baritone_.inventory().selectItem([&](const HotbarStack& item) { return item.name==plan.sapling; },true)) return stop("Missing saplings while replanting",true);
        const auto ground=root.below();
        auto aim=rotation_utils::calcRotationFromVec3d(ec.playerHead(),{root.x+0.5,double(root.y),root.z+0.5},ec.playerRotations());
        auto hit=rayTrace(ctx.bsi,ec.playerHead(),aim,player.blockReachDistance());
        if (hit && hit->pos==ground && hit->face==Direction::Up && safe) {
            baritone_.controls().setTarget({aim,true});
            if (ec.isLookingAt(ground)) baritone_.controls().setInput(Input::ClickRight,true);
            return PathingCommand{nullptr,PathingCommandType::RequestPause};
        }
        return PathingCommand{std::make_shared<GoalNear>(root,2),PathingCommandType::RevalidateGoalAndPath};
    }
    ++completed_; plan_.reset(); cursor_=0; waiting_=timeout_=0;
    return PathingCommand{nullptr,PathingCommandType::RequestPause};
}
}
