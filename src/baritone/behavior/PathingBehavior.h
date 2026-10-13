#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "baritone/behavior/InventoryBehavior.h"
#include "baritone/pathing/calc/AStarPathFinder.h"
#include "baritone/pathing/path/PathExecutor.h"

namespace baritone {

// Messages for the chat (Baritone's logDirect / PathEvent).
using Notify = std::function<void(const std::string& message, bool error)>;

// Calculates and follows paths towards a goal, segment by segment, planning the next
// segment while walking the current one (Baritone's PathingBehavior). Driven every tick
// by the PathingControlManager through the secretInternal* methods, like in Baritone.
class PathingBehavior {
public:
    PathingBehavior(PlayerContext& player, InventoryBehavior& inventory, Notify notify);
    ~PathingBehavior();

    // Game thread, in this order every tick: beginTick, (processes), tickPath.
    void beginTick(std::shared_ptr<const WorldSnapshot> world);
    void tickPath();

    // ---- used by the PathingControlManager ----
    void secretInternalSetGoal(GoalPtr goal);
    bool secretInternalSetGoalAndPath(GoalPtr goal);
    void requestPause() { pauseRequested_ = true; }
    // Applied to every new path calculation context (set each tick from the process in control).
    void setContextCustomizer(std::function<void(CalculationContext&)> fn) { customizer_ = std::move(fn); }
    // Called when a path arrives at its goal (Java PathEvent.AT_GOAL).
    void setOnArrival(std::function<void()> fn) { onArrival_ = std::move(fn); }
    // More time for the next calculations (a retry after a failure).
    void setTimeoutMultiplier(int m) { timeoutMultiplier_ = m < 1 ? 1 : m; }
    void cancelSegmentIfSafe();
    void softCancelIfSafe();
    void secretInternalSegmentCancel();
    bool calcFailedLastTick() const { return calcFailedLastTick_; }

    GoalPtr goal() const;
    bool isPathing() const;
    bool hasInProgress() const;
    bool isSafeToCancel() const { return safeToCancel_; }
    // dest and goal of the path being executed
    std::optional<std::pair<BlockPos, GoalPtr>> currentPathInfo() const;
    void cancelEverything();
    void shutdown();  // waits for the calculation thread (unload)

    // Inputs/rotation the path wants this tick; valid when producedOutput().
    const MovementState& output() const { return output_; }
    bool producedOutput() const { return producedOutput_; }
    bool pausedThisTick() const { return pausedThisTick_; }

    BlockPos pathStart();
    std::optional<double> ticksRemainingInSegment() const;
    // Java estimatedTicksToGoal: extrapolated from the progress of the heuristic since the
    // current path started executing
    std::optional<double> estimatedTicksToGoal();
    // Blocks the movement being executed breaks (BackfillProcess.partOfCurrentMovement).
    std::vector<BlockPos> currentMovementToBreak() const;
    std::string debugState() const;

    // Rendering (copies)
    std::vector<BlockPos> currentPathPositions() const;
    std::vector<BlockPos> nextPathPositions() const;
    std::vector<BlockPos> calculationInProgress() const;
    std::vector<BlockPos> mostRecentConsidered() const;  // Java pathToMostRecentNodeConsidered
    std::size_t currentPathPosition() const;
    // the current executor's toBreak / toPlace / toWalkInto
    struct BlockSets {
        std::vector<BlockPos> toBreak, toPlace, toWalkInto;
    };
    BlockSets currentBlockSets() const;

    CalculationContext* liveContext() { return liveCalc_.get(); }
    const std::shared_ptr<const WorldSnapshot>& world() const { return world_; }

private:
    void findPathInNewThread(const BlockPos& start, bool talkAboutIt);  // holds planLock_ and calcLock_

    PlayerContext& player_;
    InventoryBehavior& inventory_;
    Notify notify_;

    mutable std::recursive_mutex planLock_;  // current_, next_, goal_, expectedSegmentStart_
    mutable std::mutex calcLock_;            // inProgress_
    std::unique_ptr<PathExecutor> current_, next_;
    std::shared_ptr<AStarPathFinder> inProgress_;
    std::thread calcThread_;
    GoalPtr goal_;
    BlockPos expectedSegmentStart_;

    bool safeToCancel_ = true;
    bool pauseRequested_ = false;
    bool pausedThisTick_ = false;
    bool unpausedLastTick_ = true;
    bool cancelRequested_ = false;
    std::atomic<bool> calcFailed_{false};  // set by the calc thread
    bool calcFailedLastTick_ = false;

    MovementState output_;
    std::optional<BlockPos> etaStart_;  // Java startPosition
    int ticksElapsedSoFar_ = 0;
    bool producedOutput_ = false;

    std::shared_ptr<const WorldSnapshot> world_;
    std::unique_ptr<CalculationContext> liveCalc_;
    std::function<void(CalculationContext&)> customizer_;
    std::function<void()> onArrival_;
    int timeoutMultiplier_ = 1;
};

}  // namespace baritone
