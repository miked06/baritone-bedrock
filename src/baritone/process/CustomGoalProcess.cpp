#include "baritone/process/CustomGoalProcess.h"

#include <cmath>
#include <format>

#include "baritone/Baritone.h"
#include "baritone/Settings.h"

namespace baritone {

void CustomGoalProcess::setGoal(GoalPtr goal) {
    goal_ = goal;
    mostRecentGoal_ = goal;
    settledTicks_ = 0;
    // Java: a new goal during an elytra flight changes the flight's destination
    if (goal && baritone_.elytra().isActive()) {
        if (auto error = baritone_.elytra().pathTo(goal)) baritone_.logDirect("Cannot change the elytra goal: " + *error, true);
    }
    if (state_ == State::None) state_ = State::GoalSet;
    if (state_ == State::Executing) state_ = State::PathRequested;
}

void CustomGoalProcess::path() {
    state_ = State::PathRequested;
    retries_ = 0;
    settledTicks_ = 0;
    baritone_.pathing().setTimeoutMultiplier(1);
}

std::optional<PathingCommand> CustomGoalProcess::onTick(bool calcFailed, bool isSafeToCancel) {
    switch (state_) {
        case State::GoalSet: return PathingCommand{goal_, PathingCommandType::CancelAndSetGoal};
        case State::PathRequested: {
            // force a replan towards the new goal, once
            state_ = State::Executing;
            return PathingCommand{goal_, PathingCommandType::ForceRevalidateGoalAndPath};
        }
        case State::Executing: {
            PathingBehavior& p = baritone_.pathing();
            const BlockPos feet = baritone_.playerFeet();
            // Bedrock addition: standing on the edge of the goal's support (e.g. the 3 pixel top of
            // a ladder) the feet can be just outside the goal block. The path calculation then
            // starts in the goal and does nothing: that is arrived too, once on the ground.
            if (goal_ && goal_->isInGoal(p.pathStart()) && (goal_->isInGoal(feet) || baritone_.player().onGround())) {
                // Still in the air (e.g. climbing out of a ladder onto its top): let the last
                // movement finish. Cancelling now drops us back down and starts over.
                if (!baritone_.player().onGround() && p.isPathing()) {
                    settledTicks_ = 0;
                    return PathingCommand{goal_, PathingCommandType::SetGoalAndPath};
                }
                // Release path input and let residual movement settle. If the
                // player crosses the boundary, resume pathing to the same goal.
                const auto velocity = baritone_.player().velocity();
                if (!isSafeToCancel || velocity.x * velocity.x + velocity.z * velocity.z > 0.02 * 0.02 ||
                    (!baritone_.player().onGround() && std::abs(velocity.y) > 0.04))
                    settledTicks_ = 0;
                else
                    ++settledTicks_;
                if (settledTicks_ < 3)
                    return PathingCommand{goal_, PathingCommandType::CancelAndSetGoal};

                baritone_.logDirect("Arrived! " + goal_->toString(), false);
                if (Settings::get().disconnectOnArrival && !baritone_.player().requestDisconnect())
                    baritone_.logDirect("Could not queue disconnect on arrival", true);
                if (Settings::get().notificationOnPathComplete) baritone_.logNotification("Journey complete", false);
                GoalPtr arrived = goal_;
                onLostControl();
                return PathingCommand{std::move(arrived), PathingCommandType::CancelAndSetGoal};
            }
            settledTicks_ = 0;
            if (calcFailed) {
                // retry with more time before giving up: long trips through complex terrain
                // can need more than failureTimeoutMS
                if (retries_ < 2) {
                    ++retries_;
                    baritone_.pathing().setTimeoutMultiplier(1 + 2 * retries_);
                    baritone_.logDirect(std::format("No path; retrying with more time ({}/2)", retries_), true);
                    return PathingCommand{goal_, PathingCommandType::ForceRevalidateGoalAndPath};
                }
                baritone_.logDirect("No path found to " + (goal_ ? goal_->toString() : std::string("?")), true);
                baritone_.pathing().setTimeoutMultiplier(1);
                onLostControl();
                return PathingCommand{goal_, PathingCommandType::CancelAndSetGoal};
            }
            if (retries_ > 0 && baritone_.pathing().isPathing()) {  // found one: back to normal
                retries_ = 0;
                baritone_.pathing().setTimeoutMultiplier(1);
            }
            if (!goal_) {
                onLostControl();
                return PathingCommand{nullptr, PathingCommandType::CancelAndSetGoal};
            }
            return PathingCommand{goal_, PathingCommandType::SetGoalAndPath};
        }
        case State::None: break;
    }
    return std::nullopt;
}

std::string CustomGoalProcess::displayName() const {
    return "Goto " + (goal_ ? goal_->toString() : std::string("(none)"));
}

}  // namespace baritone
