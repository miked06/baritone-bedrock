#pragma once
#include <optional>
#include <vector>

#include "baritone/cache/AsyncScan.h"
#include "baritone/process/Process.h"

namespace baritone {

class CalculationContext;

// #farm [range]: harvest ripe crops, replant, pick up the drops (Baritone's FarmProcess).
class FarmProcess : public Process {
public:
    using Process::Process;

    void farm(int range, BlockPos center);
    bool isActive() override { return active_; }
    std::optional<PathingCommand> onTick(bool calcFailed, bool isSafeToCancel) override;
    void onLostControl() override {
        active_ = false;
        locations_.reset();
        scan_.wait();
    }
    std::string displayName() const override { return "Farm"; }
    void shutdown() { scan_.wait(); }

    static bool isPlantable(const std::string& item);

private:
    bool readyForHarvest(CalculationContext& ctx, const BlockPos& pos, const BlockProps& p);

    bool active_ = false;
    int range_ = 0;
    BlockPos center_;
    int tickCount_ = 0;
    std::optional<std::vector<BlockPos>> locations_;
    AsyncScan scan_;
};

}  // namespace baritone
