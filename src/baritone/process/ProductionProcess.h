#pragma once
#include <future>
#include "baritone/process/Process.h"
#include "baritone/production/RecipePlanner.h"

namespace baritone {
class ProductionProcess : public Process {
public:
    using Process::Process;
    ~ProductionProcess() { shutdown(); }
    void start(const RecipeBook& book, BlockPos station, std::string item, int additional, bool execute);
    void shutdown();
    bool isActive() override;
    bool executing() const { return active_ && execute_; }
    bool succeeded() const { return succeeded_; }
    const std::string& failure() const { return failure_; }
    const std::optional<ProductionPlan>& plan() const { return plan_; }
    bool isTemporary() const override { return true; }
    double priority() const override { return 3; }
    void onLostControl() override;
    std::optional<PathingCommand> onTick(bool failed, bool safe) override;
    std::string displayName() const override;
private:
    std::map<std::string, int> inventory() const;
    std::future<ProductionPlan> planning_;
    std::map<std::string, int> initial_;
    std::optional<ProductionPlan> plan_;
    BlockPos station_{}, origin_{};
    std::string failure_;
    std::size_t next_ = 0;
    int ticks_ = 0, childTarget_ = 0, finalTarget_ = 0;
    bool active_ = false, execute_ = false, child_ = false, succeeded_ = false;
};
}
