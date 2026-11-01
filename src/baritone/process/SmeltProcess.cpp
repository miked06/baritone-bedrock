#include "baritone/process/SmeltProcess.h"
#include "baritone/Baritone.h"
#include "baritone/cache/WorldScanner.h"
#include <algorithm>
namespace baritone {
void SmeltProcess::start(BlockPos furnace,std::string input,std::string output,int count,std::string fuel,int fuelCount) {
    auto& player=baritone_.player();
    if (active_ || player.containerOpen() || player.inventoryMovePending()) throw std::invalid_argument("Close the current container and stop the current workstation task first");
    if (count<1 || count>64 || fuelCount<1 || fuelCount>64) throw std::invalid_argument("Smelting batch and fuel quantities must be 1..64");
    input_=BlockFilter::normalize(std::move(input)); output_=BlockFilter::normalize(std::move(output)); fuel_=BlockFilter::normalize(std::move(fuel));
    if (input_==output_ || input_==fuel_) throw std::invalid_argument("Input, output and fuel must identify the intended distinct smelting items");
    bool haveInput=false,haveFuel=false;
    for (int i=0;i<36;++i) { auto item=player.hotbar(i); haveInput |= item.name==input_ && item.count>=count; haveFuel |= item.name==fuel_ && item.count>=fuelCount; }
    if (!haveInput || !haveFuel) throw std::invalid_argument("Keep the whole batch in one input stack and the selected fuel in one fuel stack");
    onLostControl(); furnace_=furnace; origin_=baritone_.playerFeet(); count_=count; fuelCount_=fuelCount;
    stage_=ticks_=0; waiting_=false; succeeded_=false; failure_.clear(); active_=true;
}
bool SmeltProcess::isActive() { return active_ && !baritone_.pauser().paused(); }
void SmeltProcess::onLostControl() { active_=false; waiting_=false; baritone_.player().closeOwnedContainer(); }
std::string SmeltProcess::displayName() const { return std::format("Smelting {} x{}: stage {}",output_,count_,stage_); }
std::optional<PathingCommand> SmeltProcess::onTick(bool failed,bool safe) {
    if (!active_) return {};
    auto& player=baritone_.player();
    auto fail=[&](std::string reason)->std::optional<PathingCommand> { failure_=std::move(reason); baritone_.logDirect(failure_,true); onLostControl(); return PathingCommand{nullptr,PathingCommandType::CancelAndSetGoal}; };
    if (failed || ++ticks_>24000) return fail("Smelting timed out or the workstation is unreachable; inspect its remaining items before retrying");
    if (stage_==5) {
        player.closeOwnedContainer();
        if (player.containerOpen() || player.inventoryMovePending()) return PathingCommand{nullptr,PathingCommandType::RequestPause};
        if (origin_.distSq(baritone_.playerFeet())>1) return PathingCommand{std::make_shared<GoalNear>(origin_,1),PathingCommandType::RevalidateGoalAndPath};
        succeeded_=true; active_=false; baritone_.logDirect("Smelting output collected; returned to the task's start position");
        return PathingCommand{nullptr,PathingCommandType::CancelAndSetGoal};
    }
    if (!player.ownedContainerReady()) {
        if (stage_!=0) return fail("The furnace was closed during smelting; inspect it before retrying");
        if (furnace_.distSq(baritone_.playerFeet())>9) return PathingCommand{std::make_shared<GoalGetToBlock>(furnace_),PathingCommandType::RevalidateGoalAndPath};
        if (safe) player.openContainerAt(furnace_);
        if (ticks_>160) return fail("Could not open the registered furnace");
        return PathingCommand{nullptr,PathingCommandType::RequestPause};
    }
    if (waiting_) {
        if (player.transferState()==TransferState::Rejected) return fail(player.transferError());
        if (player.transferState()==TransferState::Accepted) { waiting_=false; ++stage_; }
        return PathingCommand{nullptr,PathingCommandType::RequestPause};
    }
    auto slots=player.containerSlots();
    auto input=std::ranges::find_if(slots,[](const ContainerSlot& s) { return !s.playerInventory && (s.networkName==25 || s.networkName==45 || s.networkName==46); });
    auto fuel=std::ranges::find_if(slots,[](const ContainerSlot& s) { return !s.playerInventory && s.networkName==24; });
    auto output=std::ranges::find_if(slots,[](const ContainerSlot& s) { return !s.playerInventory && s.networkName==26; });
    if (input==slots.end() || fuel==slots.end() || output==slots.end()) return fail("The registered workstation does not expose furnace input, fuel and output slots");
    if (stage_==0) {
        if (!input->stack.empty() || !fuel->stack.empty() || !output->stack.empty()) return fail("Use an empty furnace so existing items and fuel remain untouched");
        if (!std::ranges::any_of(slots,[](const ContainerSlot& s) { return s.playerInventory && s.stack.empty(); })) return fail("Keep an empty inventory slot for the smelted output");
        stage_=1;
    }
    if (stage_==1 || stage_==2) {
        const auto& name=stage_==1 ? input_ : fuel_; const int amount=stage_==1 ? count_ : fuelCount_;
        auto source=std::ranges::find_if(slots,[&](const ContainerSlot& s) { return s.playerInventory && s.stack.name==name && s.stack.count>=amount; });
        if (source==slots.end()) return fail("The required input or fuel stack changed before submission");
        if (player.transferContainerItem(*source,stage_==1 ? *input : *fuel,amount)) waiting_=true;
        return PathingCommand{nullptr,PathingCommandType::RequestPause};
    }
    if (stage_==3) {
        if (!output->stack.empty() && output->stack.name!=output_) return fail("Furnace output differs from the requested recipe; inspect it before retrying");
        if (output->stack.count<count_) return PathingCommand{nullptr,PathingCommandType::RequestPause};
        auto target=std::ranges::find_if(slots,[](const ContainerSlot& s) { return s.playerInventory && s.stack.empty(); });
        if (target==slots.end()) return fail("No empty inventory slot for smelting output");
        if (player.transferContainerItem(*output,*target,count_)) waiting_=true;
    } else if (stage_==4) {
        if (!fuel->stack.empty()) {
            auto target=std::ranges::find_if(slots,[](const ContainerSlot& s) { return s.playerInventory && s.stack.empty(); });
            if (target==slots.end()) return fail("No empty inventory slot to return unused furnace fuel");
            if (player.transferContainerItem(*fuel,*target,fuel->stack.count)) waiting_=true;
        } else stage_=5;
    }
    return PathingCommand{nullptr,PathingCommandType::RequestPause};
}
}
