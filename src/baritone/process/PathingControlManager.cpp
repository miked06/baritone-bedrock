#include "baritone/process/PathingControlManager.h"

#include <algorithm>

#include "baritone/Baritone.h"
#include "baritone/Settings.h"

namespace baritone {

std::optional<PathingCommand> PathingControlManager::executeProcesses() {
    for (Process* p : processes_) {
        bool listed = std::ranges::find(active_, p) != active_.end();
        if (p->isActive()) {
            if (!listed) active_.insert(active_.begin(), p);  // newly active: front of the list
        } else if (listed) {
            std::erase(active_, p);
        }
    }
    // ties are broken by which became active first
    std::ranges::stable_sort(active_, [](Process* a, Process* b) { return a->priority() > b->priority(); });

    PathingBehavior& pathing = baritone_.pathing();
    for (std::size_t i = 0; i < active_.size(); ++i) {
        Process* proc = active_[i];
        bool calcFailed = proc == inControlLastTick_ && pathing.calcFailedLastTick();
        auto exec = proc->onTick(calcFailed, pathing.isSafeToCancel());
        if (!exec) continue;  // reports itself inactive
        if (exec->type != PathingCommandType::Defer) {
            inControlThisTick_ = proc;
            if (!proc->isTemporary()) {
                for (std::size_t j = i + 1; j < active_.size(); ++j) active_[j]->onLostControl();
            }
            return exec;
        }
    }
    return std::nullopt;
}

void PathingControlManager::preTick() {
    inControlLastTick_ = inControlThisTick_;
    inControlThisTick_ = nullptr;
    PathingBehavior& p = baritone_.pathing();
    command_ = executeProcesses();
    if (!command_) {
        p.cancelSegmentIfSafe();
        p.secretInternalSetGoal(nullptr);
        return;
    }
    if (inControlThisTick_ != inControlLastTick_ && command_->type != PathingCommandType::RequestPause &&
        inControlLastTick_ && !inControlLastTick_->isTemporary()) {
        // control went from one real process to another: drop the previous process' path
        p.cancelSegmentIfSafe();
    }
    switch (command_->type) {
        case PathingCommandType::SetGoalAndPause:
            p.secretInternalSetGoalAndPath(command_->goal);
            [[fallthrough]];
        case PathingCommandType::RequestPause: p.requestPause(); break;
        case PathingCommandType::CancelAndSetGoal:
            p.secretInternalSetGoal(command_->goal);
            p.cancelSegmentIfSafe();
            break;
        case PathingCommandType::ForceRevalidateGoalAndPath:
        case PathingCommandType::RevalidateGoalAndPath:
            if (!p.isPathing() && !p.hasInProgress()) p.secretInternalSetGoalAndPath(command_->goal);
            break;
        case PathingCommandType::SetGoalAndPath:
            if (command_->goal) p.secretInternalSetGoalAndPath(command_->goal);
            break;
        case PathingCommandType::Defer: break;
    }
}

void PathingControlManager::postTick() {
    // replanning happens after the tick's movement so the time between ticks is used for it
    if (!command_) return;
    PathingBehavior& p = baritone_.pathing();
    switch (command_->type) {
        case PathingCommandType::ForceRevalidateGoalAndPath:
            if (!command_->goal || forceRevalidate(command_->goal) || revalidateGoal(command_->goal)) p.softCancelIfSafe();
            p.secretInternalSetGoalAndPath(command_->goal);
            break;
        case PathingCommandType::RevalidateGoalAndPath:
            if (Settings::get().cancelOnGoalInvalidation && (!command_->goal || revalidateGoal(command_->goal))) p.softCancelIfSafe();
            p.secretInternalSetGoalAndPath(command_->goal);
            break;
        default: break;
    }
}

bool PathingControlManager::forceRevalidate(const GoalPtr& newGoal) {
    auto current = baritone_.pathing().currentPathInfo();
    if (!current) return false;
    if (newGoal->isInGoal(current->first)) return false;
    return !current->second || !newGoal->sameAs(*current->second);
}

bool PathingControlManager::revalidateGoal(const GoalPtr& newGoal) {
    auto current = baritone_.pathing().currentPathInfo();
    if (!current) return false;
    const auto& [end, intended] = *current;
    // the path used to end in the goal, but the goal changed and it no longer does
    return intended && intended->isInGoal(end) && !newGoal->isInGoal(end);
}

void PathingControlManager::cancelEverything() {
    for (Process* p : processes_) p->onLostControl();
    active_.clear();
    inControlThisTick_ = nullptr;
    command_.reset();
}

}  // namespace baritone
