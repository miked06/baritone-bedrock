#pragma once
#include "baritone/process/Process.h"
namespace baritone {
class CraftProcess : public Process {
public:
    using Process::Process;
    void start(BlockPos station,std::string item,int additional);
    bool isActive() override;
    bool isTemporary() const override { return true; }
    double priority() const override { return 2; }
    void onLostControl() override;
    std::optional<PathingCommand> onTick(bool failed,bool safe) override;
    std::string displayName() const override;
    bool succeeded() const { return succeeded_; }
    const std::string& failure() const { return failure_; }
private:
    bool active_=false,waiting_=false,closing_=false,succeeded_=false;
    int target_=0,ticks_=0,attempts_=0,recipeWait_=0;
    BlockPos station_,origin_;
    std::string item_,failure_;
};
}
