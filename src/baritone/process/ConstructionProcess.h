#pragma once
#include "baritone/process/Process.h"
#include "baritone/schematic/ConstructionPlan.h"
namespace baritone {
class ConstructionProcess : public Process {
public:
    using Process::Process;
    void start(SchematicPtr source, BlockPos origin, std::string support, int depth, bool permanent, bool execute);
    bool isActive() override;
    bool isTemporary() const override { return true; }
    double priority() const override { return 3.7; }
    void onLostControl() override;
    std::optional<PathingCommand> onTick(bool failed, bool safe) override;
    std::string displayName() const override;
    const ConstructionPlan& plan() const { return plan_; }
private:
    ConstructionPlan plan_;
    bool active_ = false, execute_ = false, child_ = false;
    int phase_ = 0, ticks_ = 0;
    uint64_t diagnostic_ = 0;
};
}
