#pragma once
#include <optional>
#include <vector>

#include "baritone/cache/AsyncScan.h"
#include "baritone/pathing/movement/CalculationContext.h"
#include "baritone/process/Process.h"

namespace baritone {

// #goto <block>: go next to the closest block of a kind (Baritone's GetToBlockProcess).
class GetToBlockProcess : public Process {
public:
    using Process::Process;

    void getToBlock(BlockFilter filter);
    bool isActive() override { return filter_.has_value(); }
    std::optional<PathingCommand> onTick(bool calcFailed, bool isSafeToCancel) override;
    void onLostControl() override;
    std::string displayName() const override;
    void customizeContext(CalculationContext& ctx) override { ctx.ignoreBreakRestrictions = true; }
    void shutdown() { scan_.wait(); }
    // #blacklist: ignore the closest instance (and the blocks touching it).
    bool blacklistClosest();

private:
    GoalPtr createGoal(const BlockPos& pos);
    void rescan();
    bool walkIntoInsteadOfAdjacent() const;
    bool rightClickOnArrival() const;
    bool blockOnTopMustBeRemoved() const;
    bool rightClick();
    int arrivalTickCount_ = 0;
    bool clicked_ = false;

    std::optional<BlockFilter> filter_;
    std::optional<std::vector<BlockPos>> knownLocations_;
    std::vector<BlockPos> blacklist_;
    BlockPos start_;
    int tickCount_ = 0;
    AsyncScan scan_;
};

}  // namespace baritone
