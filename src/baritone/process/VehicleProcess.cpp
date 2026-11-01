#include "baritone/process/VehicleProcess.h"
#include "baritone/Baritone.h"
#include <algorithm>
#include <cmath>
namespace baritone {
namespace {
bool eligible(const EntityInfo& e,VehicleKind kind) {
    if (e.dead || e.isPlayer) return false;
    return kind==VehicleKind::Boat ? e.type=="minecraft:boat" || e.type=="minecraft:chest_boat" :
        (e.type=="minecraft:horse" || e.type=="minecraft:donkey" || e.type=="minecraft:mule") && e.tamed.value_or(false) && e.saddled.value_or(false) && !e.baby.value_or(true);
}
}
void VehicleProcess::shutdown() { onLostControl(); if (calculation_.valid()) calculation_.wait(); }
void VehicleProcess::board(VehicleKind kind) {
    if (baritone_.player().ridingState().value_or(true)) throw std::invalid_argument("Already riding, or the riding state is unknown");
    onLostControl(); kind_=kind; target_=0; double nearest=64;
    for (const auto& e:baritone_.player().entities()) if (eligible(e,kind)) {
        double distance=(e.position-baritone_.player().position()).lengthSq(); if (distance<nearest) { nearest=distance; target_=e.id; }
    }
    if (!target_) throw std::invalid_argument("No nearby eligible boat or tamed saddled adult mount");
    mode_=Mode::Board; ticks_=0;
}
void VehicleProcess::navigate(VehicleKind kind,BlockPos destination) {
    if (!baritone_.player().ridingState().value_or(false)) throw std::invalid_argument("Board the vehicle first");
    if (calculation_.valid()) {
        if (calculation_.wait_for(std::chrono::seconds(0))!=std::future_status::ready) throw std::invalid_argument("Wait for the previous vehicle calculation to stop");
        calculation_.get();
    }
    auto world=baritone_.world(); if (!world) throw std::invalid_argument("Enter a world first");
    auto start=baritone_.playerFeet(); bool found=false;
    for (int down=0;down<=3;++down) if (vehicleNodeClear(*world,start.below(down),kind)) { start=start.below(down); found=true; break; }
    if (!found) throw std::invalid_argument("Current vehicle position has no safe, loaded corridor");
    onLostControl(); cancel_=false; kind_=kind; ticks_=stuck_=0; last_=baritone_.player().position(); next_=0; mode_=Mode::Navigate;
    calculation_=std::async(std::launch::async,[this,world,start,destination,kind] { return findVehiclePath(*world,start,destination,kind,&cancel_); });
}
void VehicleProcess::dismount() {
    if (!baritone_.player().ridingState().value_or(false)) throw std::invalid_argument("Not riding, or riding state unknown");
    onLostControl(); mode_=Mode::Dismount; ticks_=0;
}
std::string VehicleProcess::displayName() const { return mode_==Mode::Board ? "Boarding vehicle" : mode_==Mode::Dismount ? "Dismounting" : "Vehicle navigation"; }
std::optional<PathingCommand> VehicleProcess::onTick(bool failed,bool safe) {
    if (!isActive()) return {};
    auto stop=[&](const std::string& reason,bool error)->std::optional<PathingCommand> { baritone_.logDirect(reason,error); onLostControl(); return PathingCommand{nullptr,PathingCommandType::CancelAndSetGoal}; };
    auto& player=baritone_.player(); auto riding=player.ridingState();
    if (!riding) return stop("Vehicle riding state is unavailable",true);
    if (++ticks_>24000 || failed) return stop("Vehicle task timed out or its approach failed",true);
    if (mode_==Mode::Board) {
        if (*riding) return stop("Boarding confirmed",false);
        auto entities=player.entities(); auto e=std::ranges::find_if(entities,[&](const EntityInfo& v) { return v.id==target_; });
        if (e==entities.end() || !eligible(*e,kind_)) return stop("The selected vehicle is no longer eligible",true);
        if ((e->position-player.position()).lengthSq()>4) {
            BlockPos p{int(std::floor(e->position.x)),int(std::floor(e->position.y)),int(std::floor(e->position.z))};
            return PathingCommand{std::make_shared<GoalNear>(p,1),PathingCommandType::RevalidateGoalAndPath};
        }
        if (safe && ticks_%10==0) player.interactEntity(target_);
        if (ticks_>200) return stop("Boarding was not confirmed",true);
        return PathingCommand{nullptr,PathingCommandType::RequestPause};
    }
    if (mode_==Mode::Dismount) {
        if (!*riding) return stop("Dismount confirmed",false);
        baritone_.controls().setInput(Input::Sneak,true);
        if (ticks_>40) return stop("Dismount was not confirmed",true);
        return PathingCommand{nullptr,PathingCommandType::RequestPause};
    }
    if (!*riding) return stop("Vehicle navigation stopped because the player is no longer riding",true);
    if (calculation_.valid()) {
        if (calculation_.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return PathingCommand{nullptr,PathingCommandType::RequestPause};
        auto result=calculation_.get(); if (!result.error.empty()) return stop(result.error,true); path_=std::move(result.points);
    }
    auto position=player.position();
    while (next_<path_.size()) { auto p=path_[next_]; double dx=p.x+.5-position.x,dz=p.z+.5-position.z; if (dx*dx+dz*dz>.5) break; ++next_; }
    if (next_>=path_.size()) return stop("Vehicle destination reached",false);
    auto target=path_[next_];
    if (!vehicleNodeClear(*baritone_.world(),target,kind_)) return stop("Vehicle corridor changed or unloaded; navigation stopped",true);
    if ((position-last_).lengthSq()<.0004) ++stuck_; else { stuck_=0; last_=position; }
    if (stuck_>100) return stop("Vehicle is not making progress; stopped before repeated steering",true);
    if ((Vec3d::center(target)-position).lengthSq()>36) return stop("Vehicle drifted too far from its planned corridor",true);
    auto aim=rotation_utils::calcRotationFromVec3d(position,{target.x+.5,position.y,target.z+.5},player.rotation());
    baritone_.controls().setTarget({aim,true});
    if (std::abs(std::remainder(player.rotation().yaw-aim.yaw,360.f))<25) baritone_.controls().setInput(Input::MoveForward,true);
    if (kind_==VehicleKind::Horse && next_ && target.y>path_[next_-1].y) baritone_.controls().setInput(Input::Jump,true);
    return PathingCommand{nullptr,PathingCommandType::RequestPause};
}
}
