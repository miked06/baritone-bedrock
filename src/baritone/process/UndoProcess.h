#pragma once
#include "baritone/process/Process.h"
#include "baritone/task/ActionHistory.h"
namespace baritone {
class UndoProcess : public Process {
public:
    using Process::Process;
    void start(std::vector<ActionHistory::Entry> entries);
    bool isActive() override;
    bool isTemporary() const override { return true; }
    double priority() const override { return 3.8; }
    void onLostControl() override;
    std::optional<PathingCommand> onTick(bool failed, bool safe) override;
    std::string displayName() const override { return "Conditionally restoring observed block changes"; }
private:
    std::vector<ActionHistory::Entry> entries_;
    std::size_t next_ = 0;
    bool active_ = false, child_ = false;
    int ticks_ = 0, restored_ = 0, skipped_ = 0;
    uint64_t diagnostic_ = 0;
};
}
