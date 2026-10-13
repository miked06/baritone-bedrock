#pragma once
#include "baritone/process/Process.h"

namespace baritone {

// #goto / #goal / #path (Baritone's CustomGoalProcess).
class CustomGoalProcess : public Process {
public:
    using Process::Process;

    void setGoal(GoalPtr goal);
    void path();
    void setGoalAndPath(GoalPtr goal) {
        setGoal(std::move(goal));
        path();
    }
    GoalPtr goal() const { return goal_; }
    GoalPtr mostRecentGoal() const { return mostRecentGoal_; }

    bool isActive() override { return state_ != State::None; }
    std::optional<PathingCommand> onTick(bool calcFailed, bool isSafeToCancel) override;
    void onLostControl() override {
        state_ = State::None;
        goal_.reset();
        retries_ = 0;
    }
    std::string displayName() const override;

private:
    enum class State { None, GoalSet, PathRequested, Executing };
    State state_ = State::None;
    GoalPtr goal_, mostRecentGoal_;
    int retries_ = 0;  // calculations failed in a row (retried with more time)
};

}  // namespace baritone
