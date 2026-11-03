#pragma once
#include "baritone/process/Process.h"
#include "baritone/inventory/ToolRepair.h"
namespace baritone {
class RepairProcess : public Process {
public:
    using Process::Process;
    void start(std::string method, BlockPos station, int toolSlot, int materialSlot, int count, int maxLevels, bool execute);
    bool isActive() override;
    bool isTemporary() const override { return true; }
    double priority() const override { return 4.1; }
    void onLostControl() override;
    std::optional<PathingCommand> onTick(bool failed, bool safe) override;
    std::string displayName() const override { return "Repairing at " + method_; }
    bool succeeded() const { return succeeded_; }
    const std::string& failure() const { return failure_; }
private:
    enum class Phase { Open, Tool, ToolReceipt, Material, MaterialReceipt, Preview, Result, Close, Return };
    Phase phase_ = Phase::Open;
    bool active_ = false, execute_ = false, succeeded_ = false;
    std::string method_, failure_;
    BlockPos station_, origin_;
    int toolSlot_ = 0, materialSlot_ = 0, count_ = 0, maxLevels_ = 0, ticks_ = 0, phaseTicks_ = 0;
    int originalTools_ = 0, originalMaterials_ = 0;
    HotbarStack tool_, material_;
    RepairPreview expected_;
    std::optional<ContainerSlot> inputSlot(bool material) const;
    int matchingTools() const;
    void next(Phase phase) { phase_ = phase; phaseTicks_ = 0; }
};
}
