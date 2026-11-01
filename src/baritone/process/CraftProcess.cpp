#include "baritone/process/CraftProcess.h"
#include "baritone/Baritone.h"
#include "baritone/cache/WorldScanner.h"
namespace baritone {
void CraftProcess::start(BlockPos station,std::string item,int additional) {
    auto& player=baritone_.player();
    if (active_ || player.containerOpen() || player.inventoryMovePending()) throw std::invalid_argument("Close the current container and wait for pending inventory actions first");
    item=BlockFilter::normalize(std::move(item));
    if (additional<1 || additional>2304 || player.countItem(item)+additional>2304) throw std::invalid_argument("Crafting target must fit within 2304 inventory items");
    onLostControl(); item_=std::move(item); target_=player.countItem(item_)+additional; station_=station; origin_=baritone_.playerFeet();
    ticks_=attempts_=recipeWait_=0; waiting_=closing_=succeeded_=false; failure_.clear(); active_=true;
}
bool CraftProcess::isActive() { return active_ && !baritone_.pauser().paused(); }
void CraftProcess::onLostControl() { active_=waiting_=false; baritone_.player().closeOwnedContainer(); }
std::string CraftProcess::displayName() const { return std::format("Crafting {}: inventory target {}",item_,target_); }
std::optional<PathingCommand> CraftProcess::onTick(bool failed,bool safe) {
    if (!active_) return {};
    auto& player=baritone_.player();
    auto fail=[&](std::string reason)->std::optional<PathingCommand> { failure_=std::move(reason); baritone_.logDirect(failure_,true); onLostControl(); return PathingCommand{nullptr,PathingCommandType::CancelAndSetGoal}; };
    if (failed || ++ticks_>24000) return fail("Crafting timed out or the station is unreachable");
    if (closing_) {
        player.closeOwnedContainer();
        if (player.containerOpen() || player.inventoryMovePending()) return PathingCommand{nullptr,PathingCommandType::RequestPause};
        if (origin_.distSq(baritone_.playerFeet())>1) return PathingCommand{std::make_shared<GoalNear>(origin_,1),PathingCommandType::RevalidateGoalAndPath};
        succeeded_=true; active_=false; baritone_.logDirect("Crafting output confirmed; returned to the task's start position");
        return PathingCommand{nullptr,PathingCommandType::CancelAndSetGoal};
    }
    if (!player.ownedContainerReady()) {
        if (attempts_) return fail("Crafting screen closed before the output was confirmed");
        if (station_.distSq(baritone_.playerFeet())>9) return PathingCommand{std::make_shared<GoalGetToBlock>(station_),PathingCommandType::RevalidateGoalAndPath};
        if (safe) player.openContainerAt(station_);
        if (ticks_>160) return fail("Could not open the registered crafting table");
        return PathingCommand{nullptr,PathingCommandType::RequestPause};
    }
    if (waiting_) {
        if (player.craftState()==TransferState::Rejected) return fail(player.craftError());
        if (player.craftState()==TransferState::Accepted) waiting_=false;
        return PathingCommand{nullptr,PathingCommandType::RequestPause};
    }
    if (player.countItem(item_)>=target_) { closing_=true; return PathingCommand{nullptr,PathingCommandType::RequestPause}; }
    if (player.inventoryMovePending() || ticks_%6!=0) return PathingCommand{nullptr,PathingCommandType::RequestPause};
    auto choices=player.recipes(item_);
    if (choices.empty()) {
        // Large recipe collections populate across native UI frames after the
        // manager appears. An empty first frame is not proof of a missing recipe.
        if (++recipeWait_<20) return PathingCommand{nullptr,PathingCommandType::RequestPause};
        return fail("Recipe is not available in the current crafting interface; check the recipe filter, unlocks and station");
    }
    recipeWait_=0;
    if (++attempts_>2304) return fail("Crafting operation limit reached");
    if (!player.craftRecipe(choices.front())) return fail(player.craftError().empty() ? "The game could not craft this recipe; check ingredients and free inventory space" : player.craftError());
    waiting_=true;
    return PathingCommand{nullptr,PathingCommandType::RequestPause};
}
}
