#pragma once
#include "baritone/process/Process.h"
#include "baritone/inventory/ToolRepair.h"
namespace baritone {
class MendingProcess : public Process {
public:
    using Process::Process;
    void start(BlockPos source, int slot, int targetPercent, int timeoutSeconds);
    bool isActive() override;
    bool isTemporary() const override { return true; }
    double priority() const override { return 4.1; }
    void onLostControl() override;
    std::optional<PathingCommand> onTick(bool failed, bool safe) override;
    std::string displayName() const override { return "Mending tool at the registered XP source"; }
    bool succeeded() const { return succeeded_; }
    const std::string& failure() const { return failure_; }
private:
    enum class Phase { Travel, Equip, EquipReceipt, Wait, Restore, RestoreReceipt, Return };
    Phase phase_ = Phase::Travel;
    bool active_ = false, moved_ = false, succeeded_ = false;
    int slot_ = 0, held_ = 0, selected_ = 0, target_ = 100, deadline_ = 0, ticks_ = 0, progressTick_ = 0, lastDamage_ = 0, stable_ = 0;
    float health_ = 0;
    HotbarStack tool_, borrowed_;
    BlockPos source_, origin_;
    std::string failure_;
};
}
