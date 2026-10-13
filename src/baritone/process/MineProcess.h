#pragma once
#include <chrono>
#include <optional>
#include <unordered_map>
#include <vector>

#include "baritone/cache/AsyncScan.h"
#include "baritone/process/Process.h"

namespace baritone {

class CalculationContext;

// #mine [quantity] <blocks...> (Baritone's MineProcess): paths into the closest matching
// blocks (breaking them on the way), picks up the drops, explores when none is known.
class MineProcess : public Process {
public:
    using Process::Process;

    void mine(int quantity, BlockFilter filter);
    void cancel() { onLostControl(); }
    bool isActive() override { return filter_.has_value(); }
    std::optional<PathingCommand> onTick(bool calcFailed, bool isSafeToCancel) override;
    void onLostControl() override;
    std::string displayName() const override;
    void shutdown() { scan_.wait(); }

private:
    std::optional<PathingCommand> updateGoal();
    int branchY() const;
    bool addNearby();
    static bool isNextToAir(CalculationContext& ctx, const BlockPos& pos);
    void warnIfNoPickaxe();
    std::vector<BlockPos> prune(CalculationContext& ctx, std::vector<BlockPos> locs, const std::vector<BlockPos>& dropped);
    bool plausibleToBreak(CalculationContext& ctx, const BlockPos& pos);
    bool internalMiningGoal(const BlockPos& pos, CalculationContext& ctx, const std::vector<BlockPos>& locs);
    GoalPtr coalesce(const BlockPos& loc, const std::vector<BlockPos>& locs, CalculationContext& ctx);
    std::vector<BlockPos> droppedItemsScan();
    void updateLoucaSystem();
    int countHave();

    std::optional<BlockFilter> filter_;
    int desiredQuantity_ = 0;
    std::vector<BlockPos> knownOreLocations_;
    std::vector<BlockPos> blacklist_;
    std::unordered_map<BlockPos, std::chrono::steady_clock::time_point> anticipatedDrops_;
    std::optional<BlockPos> branchPoint_;  // where branch mining started, at the mining level
    bool announcedDescent_ = false;
    int tickCount_ = 0;
    AsyncScan scan_;
};

}  // namespace baritone
