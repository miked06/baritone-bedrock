#include "baritone/process/RailProcess.h"

#include <algorithm>
#include <cmath>

#include "baritone/Baritone.h"
#include "baritone/pathing/RailPath.h"

namespace baritone {

void RailProcess::start(BlockPos from, BlockPos to) {
    auto& player = baritone_.player();
    auto world = baritone_.world();
    if (!world || !player.valid() || player.containerOpen() || player.ridingState().value_or(true))
        throw std::invalid_argument("Stand on foot near the departure station and close any container first");
    if (from == to) throw std::invalid_argument("Choose two different rail stations");
    if ((Vec3d::center(from) - player.position()).lengthSq() > 64)
        throw std::invalid_argument("Move within eight blocks of the departure station");
    if (!railHasStoppingPlatform(*world, to))
        throw std::invalid_argument("Destination needs a flat unpowered golden rail and clear dismount space on both sides");
    auto route = findRailPath(*world, from, to);
    if (!route.error.empty()) throw std::invalid_argument(route.error);

    uint64_t cart = 0;
    for (const auto& entity : player.entities()) {
        if (entity.type != "minecraft:minecart" || !entity.id || entity.dead || railBelow(*world, entity.position) != from) continue;
        if (cart) throw std::invalid_argument("Leave just one passenger minecart at the departure station");
        cart = entity.id;
    }
    if (!cart) throw std::invalid_argument("Place an empty passenger minecart on the departure rail first");

    path_ = std::move(route.points);
    target_ = cart;
    phase_ = Phase::Board;
    current_ = 0;
    ticks_ = stationary_ = offRoute_ = exitTicks_ = 0;
    interacted_ = false;
}

bool RailProcess::isActive() { return phase_ != Phase::Idle && !baritone_.pauser().paused(); }

void RailProcess::onLostControl() {
    phase_ = Phase::Idle;
    path_.clear();
    target_ = 0;
}

void RailProcess::customizeContext(CalculationContext& context) {
    context.allowBreak = false;
    context.allowBreakAnyway.clear();
    context.allowPlace = false;
}

std::string RailProcess::displayName() const {
    if (phase_ == Phase::Board) return "Boarding minecart";
    if (phase_ == Phase::Exit) return "Dismounting at rail station";
    if (phase_ == Phase::Ride) return std::format("Minecart route: rail {}/{}", current_ + 1, path_.size());
    return "Rail trip idle";
}

std::optional<PathingCommand> RailProcess::onTick(bool failed, bool safe) {
    if (!isActive()) return std::nullopt;
    const PathingCommand pause{nullptr, PathingCommandType::RequestPause};
    auto stop = [&](std::string message, bool error = true) -> PathingCommand {
        baritone_.logDirect(message, error);
        onLostControl();
        return {nullptr, PathingCommandType::CancelAndSetGoal};
    };
    auto& player = baritone_.player();
    const auto riding = player.ridingState();
    if (!riding || failed || !baritone_.world() || player.containerOpen() || ++ticks_ > 12000)
        return stop("Rail trip stopped: riding, path or world state is unavailable");
    const auto& world = *baritone_.world();
    if (phase_ == Phase::Exit && !*riding) return stop("Rail destination reached and dismount confirmed", false);
    const auto cart = player.entityPosition(target_);
    if (!cart) return stop("The selected minecart is no longer loaded");
    const auto at = railBelow(world, *cart);

    if (phase_ == Phase::Board) {
        const double distance = (*cart - player.position()).lengthSq();
        if (*riding) {
            if (!interacted_ || !(distance < 2.25)) return stop("Could not confirm boarding the selected minecart");
            // A powered departure can cross into the next rail while the
            // riding state is waiting for its next observation.
            if (at == path_[1]) current_ = 1;
            else if (at != path_.front()) return stop("The minecart left the departure segment before boarding was confirmed");
            phase_ = Phase::Ride;
            previous_ = *cart;
            return pause;
        }
        if (at != path_.front()) return stop("The minecart moved away from the departure rail");
        if (ticks_ > 200) return stop("Minecart boarding was not confirmed");
        if (distance > 4) return PathingCommand{std::make_shared<GoalNear>(*at, 1), PathingCommandType::RevalidateGoalAndPath};
        if (safe && ticks_ % 10 == 0 && player.interactEntity(target_)) interacted_ = true;
        return pause;
    }

    if (!*riding) return stop("Rail trip stopped because the player left the cart before arrival");
    if (!((*cart - player.position()).lengthSq() < 2.25)) return stop("The player is no longer aligned with the selected minecart");
    const double speedSquared = (*cart - previous_).lengthSq();
    previous_ = *cart;
    const auto nextLimit = std::min(path_.size(), current_ + 4);
    auto next = current_;
    while (next < nextLimit && at != path_[next]) ++next;
    if (next == nextLimit) {
        if (++offRoute_ > 3) return stop("The minecart left the planned rail route");
        return pause;
    }
    if (current_ != next || speedSquared >= .0009) stationary_ = 0;
    else ++stationary_;
    current_ = next;
    offRoute_ = 0;

    if (!railHasStoppingPlatform(world, path_.back())) return stop("The destination brake rail or dismount platform changed");
    if (current_ + 1 == path_.size()) {
        if (stationary_ >= 5) {
            if (++exitTicks_ > 40) return stop("Minecart dismount was not confirmed");
            phase_ = Phase::Exit;
            baritone_.controls().setInput(Input::Sneak, true);
        }
        return pause;
    }
    for (auto i = current_; i < std::min(current_ + 3, path_.size() - 1); ++i)
        if (!railsConnected(world, path_[i], path_[i + 1])) return stop("The planned rail ahead changed or became unloaded");
    if (stationary_ > 100) return stop("The minecart stalled; check the powered rails or departure switch");
    if (stationary_ > 2) {
        const auto target = Vec3d::center(path_[current_ + 1]);
        const auto aim = rotation_utils::calcRotationFromVec3d(*cart, {target.x, cart->y, target.z}, player.rotation());
        baritone_.controls().setTarget({aim, true});
        if (std::abs(std::remainder(player.rotation().yaw - aim.yaw, 360.f)) < 25)
            baritone_.controls().setInput(Input::MoveForward, true);
    }
    return pause;
}

}  // namespace baritone
