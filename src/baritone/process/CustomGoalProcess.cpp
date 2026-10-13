#include "baritone/process/CustomGoalProcess.h"

#include <format>

#include "baritone/Baritone.h"
#include "baritone/Settings.h"

namespace baritone {

void CustomGoalProcess::setGoal(GoalPtr goal) {
    goal_ = goal;
    mostRecentGoal_ = goal;
    // Java: a new goal during an elytra flight changes the flight's destination
    if (goal && baritone_.elytra().isActive()) {
        if (auto error = baritone_.elytra().pathTo(goal)) baritone_.logDirect("Impossible de changer le goal de l'elytre : " + *error, true);
    }
    if (state_ == State::None) state_ = State::GoalSet;
    if (state_ == State::Executing) state_ = State::PathRequested;
}

void CustomGoalProcess::path() {
    state_ = State::PathRequested;
    retries_ = 0;
    baritone_.pathing().setTimeoutMultiplier(1);
}

std::optional<PathingCommand> CustomGoalProcess::onTick(bool calcFailed, bool) {
    switch (state_) {
        case State::GoalSet: return PathingCommand{goal_, PathingCommandType::CancelAndSetGoal};
        case State::PathRequested: {
            // force a replan towards the new goal, once
            state_ = State::Executing;
            return PathingCommand{goal_, PathingCommandType::ForceRevalidateGoalAndPath};
        }
        case State::Executing: {
            if (calcFailed) {
                // retry with more time before giving up: long trips through complex terrain
                // can need more than failureTimeoutMS
                if (retries_ < 2) {
                    ++retries_;
                    baritone_.pathing().setTimeoutMultiplier(1 + 2 * retries_);
                    baritone_.logDirect(std::format("Pas de chemin, nouvel essai avec plus de temps ({}/2)", retries_), true);
                    return PathingCommand{goal_, PathingCommandType::ForceRevalidateGoalAndPath};
                }
                baritone_.logDirect("Aucun chemin trouve vers " + (goal_ ? goal_->toString() : std::string("?")), true);
                baritone_.pathing().setTimeoutMultiplier(1);
                onLostControl();
                return PathingCommand{goal_, PathingCommandType::CancelAndSetGoal};
            }
            if (retries_ > 0 && baritone_.pathing().isPathing()) {  // found one: back to normal
                retries_ = 0;
                baritone_.pathing().setTimeoutMultiplier(1);
            }
            PathingBehavior& p = baritone_.pathing();
            BlockPos feet = baritone_.playerFeet();
            if (!goal_ || (goal_->isInGoal(feet) && goal_->isInGoal(p.pathStart()))) {
                baritone_.logDirect("Arrive ! " + (goal_ ? goal_->toString() : std::string()), false);
                // Java disconnectOnArrival leaves the server; Bedrock: no safe way to leave the world
                // from here, the player is told instead
                if (Settings::get().disconnectOnArrival) baritone_.logNotification("Arrive au goal ! (disconnectOnArrival)", false);
                if (Settings::get().notificationOnPathComplete) baritone_.logNotification("Trajet termine", false);
                GoalPtr g = goal_;
                onLostControl();
                return PathingCommand{g, PathingCommandType::CancelAndSetGoal};
            }
            return PathingCommand{goal_, PathingCommandType::SetGoalAndPath};
        }
        case State::None: break;
    }
    return std::nullopt;
}

std::string CustomGoalProcess::displayName() const {
    return "Goto " + (goal_ ? goal_->toString() : std::string("(rien)"));
}

}  // namespace baritone
