#pragma once
#include <map>
#include <set>
#include "baritone/process/Process.h"
#include "baritone/inventory/Loadouts.h"
namespace baritone {
// A temporary parent: storage children yield control back without cancelling
// the interrupted builder. Quantities are absolute inventory targets.
class LogisticsProcess : public Process {
public:
    using Process::Process;
    void start(std::map<std::string, int> targets, std::string source = {}, std::string equip = {});
    void survey(std::string source = {});
    bool isActive() override;
    bool isTemporary() const override { return true; }
    double priority() const override { return 3.5; }
    std::optional<PathingCommand> onTick(bool failed, bool safe) override;
    void onLostControl() override;
    std::string displayName() const override;
    bool succeeded() const { return succeeded_; }
    const std::string& failure() const { return failure_; }
private:
    void begin(std::string source);
    bool active_ = false, survey_ = false, child_ = false, equipping_ = false, succeeded_ = false;
    std::map<std::string, int> targets_;
    std::vector<std::string> sources_;
    std::set<std::string> scanned_, skipped_;
    std::string childSource_, equip_, failure_;
    BlockPos origin_{};
    int ticks_ = 0, actions_ = 0;
};
}
