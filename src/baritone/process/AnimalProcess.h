#pragma once
#include <chrono>
#include <unordered_map>
#include "baritone/process/Process.h"
namespace baritone {
class AnimalProcess : public Process {
public:
    using Process::Process;
    void start(std::string species, bool harvest, int count, int keep, int radius);
    bool isActive() override { return active_; }
    void onLostControl() override { active_ = false; target_ = 0; attempted_ = false; }
    std::optional<PathingCommand> onTick(bool failed, bool safe) override;
    std::string displayName() const override;
private:
    bool active_ = false, harvest_ = false, attempted_ = false;
    std::string species_;
    int requested_ = 0, completed_ = 0, keep_ = 2, radius_ = 16, actionTicks_ = 0;
    BlockPos center_{};
    uint64_t target_ = 0;
    std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> cooldowns_;
};
}
