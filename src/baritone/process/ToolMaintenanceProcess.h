#pragma once
#include "baritone/process/Process.h"
#include "baritone/api/PlayerContext.h"
namespace baritone {
class ToolMaintenanceProcess : public Process {
public:
    using Process::Process;
    void enable(int remainingPercent);
    bool enabled() const { return enabled_; }
    int threshold() const { return enabled_ ? percent_ : 0; }
    bool permits(const HotbarStack& stack) const;
    bool isActive() override;
    bool isTemporary() const override { return true; }
    double priority() const override { return 4.7; }
    void onLostControl() override { enabled_ = false; waiting_ = -1; }
    std::optional<PathingCommand> onTick(bool failed, bool safe) override;
    std::string displayName() const override { return "Protecting and replacing worn tools"; }
private:
    bool enabled_ = false;
    int percent_ = 10, waiting_ = -1, ticks_ = 0;
    HotbarStack expected_;
};
}
