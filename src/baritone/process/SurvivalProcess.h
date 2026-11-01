#pragma once
#include "baritone/process/Process.h"
namespace baritone {
class SurvivalProcess : public Process {
public:
    using Process::Process;
    void setEnabled(bool enabled);
    bool enabled() const { return enabled_; }
    void setHome(BlockPos home) { home_ = home; }
    void resetWorld() { setEnabled(false); home_.reset(); }
    bool isActive() override;
    bool isTemporary() const override { return true; }
    double priority() const override { return 5; } // inventory's stationary pause must run first
    std::optional<PathingCommand> onTick(bool failed, bool safe) override;
    void onLostControl() override;
    std::string displayName() const override { return "Survival: " + mode_; }
    int eatBelow = 16;
    float retreatBelow = 8;
private:
    bool enabled_ = false, eating_ = false, recovering_ = false;
    std::optional<BlockPos> home_;
    std::string mode_;
    int previousSlot_ = -1, eatingTicks_ = 0, ticks_ = 0;
    GoalPtr escape_;
    std::vector<BlockPos> threats() const;
    bool needsSupplies() const;
    void mode(std::string next);
};
}
