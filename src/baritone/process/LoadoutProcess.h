#pragma once
#include "baritone/inventory/Loadouts.h"
#include "baritone/process/Process.h"

namespace baritone {
class LoadoutProcess : public Process {
public:
    using Process::Process;
    void start(std::string name, LoadoutStore::Profile rules);
    bool isActive() override;
    bool isTemporary() const override { return true; }
    double priority() const override { return 3; }
    void onLostControl() override { active_ = false; }
    std::string displayName() const override { return "Equipping loadout " + name_; }
    std::optional<PathingCommand> onTick(bool failed, bool safe) override;
    const std::string& failure() const { return failure_; }
    bool succeeded() const { return succeeded_; }
private:
    LoadoutStore::Profile rules_;
    std::string name_, failure_;
    std::optional<LoadoutRule> waiting_;
    int ticks_ = 0;
    bool active_ = false, succeeded_ = false;
};
}
