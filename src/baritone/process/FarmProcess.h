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
    void harvest(std::string crop,int additional,int range,BlockPos center);
    static std::string cropDrop(const std::string& crop);
    bool succeeded() const{return succeeded_;}
    const auto& failure() const{return failure_;}
    bool isActive() override { return active_; }
    std::optional<PathingCommand> onTick(bool calcFailed, bool isSafeToCancel) override;
    void onLostControl() override {
        active_ = false;
        locations_.reset();
        scan_.wait();
    }
    std::string displayName() const override { return "Farming"; }
    void shutdown() { scan_.wait(); }

    static bool isPlantable(const std::string& item);

private:
    bool readyForHarvest(CalculationContext& ctx, const BlockPos& pos, const BlockProps& p);

    bool active_ = false;
    bool succeeded_ = false;
    std::string harvestBlock_,harvestDrop_,failure_;
    int harvestTarget_ = 0;
    int range_ = 0;
    BlockPos center_;
    int tickCount_ = 0;
    std::optional<std::vector<BlockPos>> locations_;
    AsyncScan scan_;
};

}  // namespace baritone
