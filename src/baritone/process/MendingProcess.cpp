#include "baritone/process/MendingProcess.h"
#include "baritone/Baritone.h"
#include <format>
namespace baritone {
namespace {
bool exactStack(const HotbarStack& a, const HotbarStack& b) { return a.empty() && b.empty() || a.count == b.count && sameRepairItem(a,b); }
}
void MendingProcess::start(BlockPos source, int slot, int percent, int timeoutSeconds) {
    auto& p=baritone_.player();
    if(active_ || !p.valid() || p.containerOpen() || p.inventoryMovePending() || slot<0 || slot>=36 || p.selectedSlot()<0 || p.selectedSlot()>8)
        throw std::invalid_argument("Mending: enter a world, close interfaces, choose slot 0..35, target 1..100 percent and timeout 30..1800 seconds");
    const auto tool=p.hotbar(slot);
    validateMendingItem(tool,percent,timeoutSeconds);
    source_=source; origin_=baritone_.playerFeet(); slot_=slot; selected_=p.selectedSlot(); held_=slot<9?slot:selected_;
    target_=percent; deadline_=timeoutSeconds*20; ticks_=progressTick_=stable_=0; lastDamage_=tool.damage; health_=p.health();
    tool_=tool; borrowed_=p.hotbar(held_); phase_=Phase::Travel; moved_=succeeded_=false; failure_.clear(); active_=true;
}
bool MendingProcess::isActive() { return active_ && !baritone_.pauser().paused(); }
void MendingProcess::onLostControl() {
    if(active_ && moved_) baritone_.logDirect(std::format("Mending stopped; inspect hotbar slot {} and inventory slot {} after any pending swap settles",held_,slot_));
    active_=false;
}
std::optional<PathingCommand> MendingProcess::onTick(bool failed, bool safe) {
    if(!active_)return {};
    auto& p=baritone_.player(); const PathingCommand pause{nullptr,PathingCommandType::RequestPause};
    auto fail=[&](std::string message)->std::optional<PathingCommand>{failure_=std::move(message);baritone_.logDirect(failure_,true);onLostControl();return PathingCommand{nullptr,PathingCommandType::CancelAndSetGoal};};
    if(failed || ++ticks_>deadline_)return fail("Mending stopped: path failure or job deadline; no XP or repair was simulated");
    if(p.health()<health_)return fail("Mending stopped because health decreased; inspect the XP source");
    if(p.inventoryMovePending() || p.slotChangePending() || !safe)return pause;
    if(p.containerOpen())return fail("Mending interrupted by an inventory/container interface");
    if(phase_==Phase::Travel){
        if(source_.distSq(baritone_.playerFeet())>1)return PathingCommand{std::make_shared<GoalNear>(source_,1),PathingCommandType::RevalidateGoalAndPath};
        phase_=Phase::Equip;return pause;
    }
    if(phase_==Phase::Equip){
        if(!exactStack(p.hotbar(slot_),tool_) || (slot_>=9 && !exactStack(p.hotbar(held_),borrowed_)))return fail("Mending inventory changed before equipping");
        if(slot_>=9){if(!p.swapSlots(slot_,held_))return fail("Could not submit the Mending equip swap");moved_=true;phase_=Phase::EquipReceipt;return pause;}
        phase_=Phase::EquipReceipt;
    }
    if(phase_==Phase::EquipReceipt){
        if(!sameRepairItem(p.hotbar(held_),tool_,false) || p.hotbar(held_).count!=1)return fail("Mending equip swap was not confirmed");
        p.selectSlot(held_);phase_=Phase::Wait;progressTick_=ticks_;
        baritone_.logDirect("Mending tool equipped; waiting for real XP repair. Other Mending equipment may share the available XP");return pause;
    }
    if(phase_==Phase::Wait){
        if(source_.distSq(baritone_.playerFeet())>4)return fail("Mending source position changed; job cancelled");
        const auto current=p.hotbar(held_);
        if(!sameRepairItem(current,tool_,false) || current.count!=1 || current.damage<0 || current.damage>lastDamage_)
            return fail("Mending tool changed, disappeared or took additional damage");
        if(p.selectedSlot()!=held_)p.selectSlot(held_);
        if(current.damage<lastDamage_){lastDamage_=current.damage;progressTick_=ticks_;stable_=0;}
        if(int64_t(current.maxDamage-current.damage)*100>=int64_t(current.maxDamage)*target_){if(++stable_>=3){tool_=current;phase_=Phase::Restore;}return pause;}
        if(ticks_-progressTick_>20*60)return fail("No Mending durability progress for sixty seconds; check the XP source and other Mending equipment");
        return pause;
    }
    if(phase_==Phase::Restore){
        if(moved_){
            if(!exactStack(p.hotbar(slot_),borrowed_) || !sameRepairItem(p.hotbar(held_),tool_,false))return fail("Mending inventory changed before hotbar restoration");
            if(!p.swapSlots(held_,slot_))return fail("Could not submit the hotbar restoration swap");
            phase_=Phase::RestoreReceipt;return pause;
        }
        phase_=Phase::RestoreReceipt;
    }
    if(phase_==Phase::RestoreReceipt){
        if((moved_ && (!exactStack(p.hotbar(held_),borrowed_) || !sameRepairItem(p.hotbar(slot_),tool_,false))) ||
            (!moved_ && !sameRepairItem(p.hotbar(held_),tool_,false)))return fail("Mending hotbar restoration was not confirmed");
        moved_=false;p.selectSlot(selected_);phase_=Phase::Return;return pause;
    }
    if(phase_==Phase::Return){
        if(origin_.distSq(baritone_.playerFeet())>1)return PathingCommand{std::make_shared<GoalNear>(origin_,1),PathingCommandType::RevalidateGoalAndPath};
        active_=false;succeeded_=true;baritone_.logDirect("Mending target confirmed from observed durability; hotbar restored and returned to the start");
        return PathingCommand{nullptr,PathingCommandType::CancelAndSetGoal};
    }
    return pause;
}
}
