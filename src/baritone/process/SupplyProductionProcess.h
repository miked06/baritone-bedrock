#pragma once
#include "baritone/process/Process.h"
#include "baritone/production/RecipePlanner.h"
#include <future>

namespace baritone {
// Coordinates existing processes. A quantity is additional output, while every
// acquisition child receives an absolute inventory target to avoid double work.
class SupplyProductionProcess : public Process {
public:
    using Process::Process;
    ~SupplyProductionProcess() { shutdown(); }
    void shutdown() { onLostControl(); if(planning_.valid())planning_.wait(); }
    void start(const RecipeBook& book, BlockPos station, std::string item, int additional,
               std::string supply = {}, std::string delivery = {});
    bool isActive() override;
    bool succeeded() const { return succeeded_; }
    const std::string& failure() const { return failure_; }
    bool isTemporary() const override { return true; }
    double priority() const override { return 3.8; }
    void onLostControl() override;
    std::optional<PathingCommand> onTick(bool failed, bool safe) override;
    std::string displayName() const override;
    static std::vector<std::string> harvestBlocks(const std::string& item);
private:
    enum class Phase { Plan, Supply, Harvest, Craft, Deliver, Return };
    Phase phase_ = Phase::Plan;
    RecipeBook book_;
    std::future<ProductionPlan> planning_;
    std::map<std::string,int> planningStock_;
    BlockPos station_{}, origin_{};
    std::string item_, supply_, delivery_, harvest_, failure_;
    int additional_ = 0, target_ = 0, harvestTarget_ = 0, ticks_ = 0, rounds_ = 0;
    uint64_t diagnostic_ = 0;
    bool active_ = false, succeeded_ = false, supplied_ = false;
};
}
