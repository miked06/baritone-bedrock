#pragma once
#include "baritone/process/Process.h"
namespace baritone {
class FurnacePoolProcess : public Process {
public:
    using Process::Process;
    void start(std::vector<BlockPos> stations,std::string input,std::string output,int count,std::string fuel,int fuelCount);
    bool isActive() override;
    bool isTemporary() const override{return true;}
    double priority() const override{return 3.6;}
    void onLostControl() override;
    std::optional<PathingCommand> onTick(bool failed,bool safe) override;
    std::string displayName() const override;
    bool succeeded() const{return succeeded_;}
    const auto& failure() const{return failure_;}
private:
    struct Station {BlockPos pos;int count=0,fuel=0,inputSubmitted=0,fuelSubmitted=0;bool collected=false,done=false;};
    enum class Phase {Survey,Input,Fuel,Collect,ReturnFuel,Return};
    enum class Pending {None,Input,Fuel,Output,UnusedFuel};
    std::vector<Station> stations_;
    std::string input_,output_,fuel_,failure_;
    BlockPos origin_{};
    Phase phase_=Phase::Survey;Pending pending_=Pending::None;
    std::size_t cursor_=0;
    int ticks_=0,openTicks_=0,pendingTicks_=0,pendingCount_=0,actions_=0,idle_=0,finalTarget_=0;
    bool active_=false,opened_=false,succeeded_=false;
};
}
