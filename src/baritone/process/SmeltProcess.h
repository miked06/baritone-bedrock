#pragma once
#include "baritone/process/Process.h"
namespace baritone {
class SmeltProcess : public Process {
public:
    using Process::Process;
    void start(BlockPos furnace,std::string input,std::string output,int count,std::string fuel,int fuelCount);
    bool isActive() override;
    bool isTemporary() const override { return true; }
    double priority() const override { return 2; }
    void onLostControl() override;
    std::optional<PathingCommand> onTick(bool failed,bool safe) override;
    std::string displayName() const override;
    bool succeeded() const { return succeeded_; }
    const std::string& failure() const { return failure_; }
private:
    bool active_=false, waiting_=false, succeeded_=false;
    int stage_=0, ticks_=0, count_=0, fuelCount_=0;
    BlockPos furnace_,origin_;
    std::string input_,output_,fuel_,failure_;
};
}
