#pragma once
#include <optional>
#include <string>

#include "baritone/pathing/goals/Goal.h"

namespace baritone {

class Baritone;

enum class PathingCommandType {
    RequestPause,                // don't path this tick (the process acts by itself)
    CancelAndSetGoal,            // stop the current path, remember the goal
    RevalidateGoalAndPath,       // path to the goal; replan if the current path no longer ends in it
    ForceRevalidateGoalAndPath,  // same, but replan as soon as the goal object changes
    SetGoalAndPath,              // path to the goal if not already pathing
    SetGoalAndPause,
    Defer,                       // let a lower priority process decide
};

struct PathingCommand {
    GoalPtr goal;
    PathingCommandType type;
};

// Something the bot can be doing (Baritone's IBaritoneProcess): goto, mine, follow...
// The PathingControlManager asks every active process for a command each tick; the one
// with the highest priority that doesn't defer gets control.
class Process {
public:
    explicit Process(Baritone& baritone) : baritone_(baritone) {}
    virtual ~Process() = default;

    virtual bool isActive() = 0;
    // nullopt = no longer active
    virtual std::optional<PathingCommand> onTick(bool calcFailed, bool isSafeToCancel) = 0;
    virtual bool isTemporary() const { return false; }
    virtual void onLostControl() = 0;
    virtual double priority() const { return -1; }
    virtual std::string displayName() const = 0;
    // Lets the process in control restrict the path calculations (game thread).
    virtual void customizeContext(class CalculationContext&) {}

protected:
    Baritone& baritone_;
};

}  // namespace baritone
