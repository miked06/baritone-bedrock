#pragma once
#include <optional>
#include <vector>

#include "baritone/process/Process.h"

namespace baritone {

// Picks which process controls the pathing each tick (Baritone's PathingControlManager).
class PathingControlManager {
public:
    explicit PathingControlManager(Baritone& baritone) : baritone_(baritone) {}

    void registerProcess(Process* process) { processes_.push_back(process); }
    const std::vector<Process*>& processes() const { return processes_; }

    void preTick();
    void postTick();
    void cancelEverything();  // every process loses control

    Process* mostRecentInControl() const { return inControlThisTick_; }
    const std::optional<PathingCommand>& mostRecentCommand() const { return command_; }

private:
    std::optional<PathingCommand> executeProcesses();
    bool forceRevalidate(const GoalPtr& newGoal);
    bool revalidateGoal(const GoalPtr& newGoal);

    Baritone& baritone_;
    std::vector<Process*> processes_;
    std::vector<Process*> active_;
    Process* inControlLastTick_ = nullptr;
    Process* inControlThisTick_ = nullptr;
    std::optional<PathingCommand> command_;
};

}  // namespace baritone
