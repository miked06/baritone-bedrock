#include "baritone/process/FurnacePoolProcess.h"
#include "baritone/Baritone.h"
#include "baritone/inventory/StorageTransfers.h"
#include <algorithm>
#include <format>
#include <set>

namespace baritone {
void FurnacePoolProcess::start(std::vector<BlockPos> positions,std::string input,std::string output,int count,std::string fuel,int fuelCount) {
    auto& p=baritone_.player();
    if(active_ || !p.valid() || p.containerOpen() || p.inventoryMovePending())throw std::invalid_argument("Close containers and finish inventory actions before starting the furnace pool");
    if(positions.empty() || positions.size()>16 || count<int(positions.size()) || count>int(positions.size())*64 || fuelCount<int(positions.size()) || fuelCount>int(positions.size())*64 || std::set<BlockPos>(positions.begin(),positions.end()).size()!=positions.size())
        throw std::invalid_argument("Furnace pool: 1..16 distinct stations, with 1..64 input/fuel items allocated per station");
    input=BlockFilter::normalize(std::move(input));output=BlockFilter::normalize(std::move(output));fuel=BlockFilter::normalize(std::move(fuel));
    if(input==output || input==fuel || output==fuel || p.countItem(input)<count || p.countItem(fuel)<fuelCount || p.countItem(output)>2304-count)
        throw std::invalid_argument("Keep enough distinct input/fuel items and room for the requested one-to-one output in inventory");
    input_=std::move(input);output_=std::move(output);fuel_=std::move(fuel);finalTarget_=p.countItem(output_)+count;stations_.clear();
    for(std::size_t i=0;i<positions.size();++i)stations_.push_back({positions[i],count/int(positions.size())+(i<std::size_t(count%positions.size())?1:0),fuelCount/int(positions.size())+(i<std::size_t(fuelCount%positions.size())?1:0)});
    phase_=Phase::Survey;pending_=Pending::None;cursor_=0;ticks_=openTicks_=pendingTicks_=actions_=idle_=0;failure_.clear();opened_=succeeded_=false;active_=true;origin_=baritone_.playerFeet();
}
bool FurnacePoolProcess::isActive(){return active_ && !baritone_.pauser().paused();}
void FurnacePoolProcess::onLostControl(){if(active_ && failure_.empty())failure_="Furnace pool cancelled; inspect stations before retrying";active_=false;pending_=Pending::None;opened_=false;baritone_.player().closeOwnedContainer();}
std::string FurnacePoolProcess::displayName() const{return std::format("Furnace pool: station {}/{}, {} collected",std::min(cursor_+1,stations_.size()),stations_.size(),std::ranges::count_if(stations_,[](const Station& s){return s.done;}));}
std::optional<PathingCommand> FurnacePoolProcess::onTick(bool failed,bool safe) {
    if(!active_)return {};
    auto& p=baritone_.player();
    auto pause=[] {return PathingCommand{nullptr,PathingCommandType::RequestPause};};
    auto stop=[&](std::string reason)->std::optional<PathingCommand>{failure_=std::move(reason)+"; inspect registered furnaces before retrying, completed transfers are preserved";baritone_.logDirect(failure_,true);onLostControl();return PathingCommand{nullptr,PathingCommandType::CancelAndSetGoal};};
    auto close=[&]{p.closeOwnedContainer();opened_=false;openTicks_=0;};
    if(failed || ++ticks_>20*60*60 || actions_>512)return stop("Furnace pool stopped: path failure or action/deadline limit");
    if(idle_>0){--idle_;return pause();}
    if(phase_==Phase::Return) {
        if(p.containerOpen() || p.inventoryMovePending()){close();return pause();}
        if(p.countItem(output_)<finalTarget_)return stop("Furnace pool output quantity was not observed");
        if(origin_.distSq(baritone_.playerFeet())>1)return PathingCommand{std::make_shared<GoalNear>(origin_,1),PathingCommandType::RevalidateGoalAndPath};
        active_=false;succeeded_=true;baritone_.logDirect("Furnace pool output and unused fuel collected; returned to the start");return PathingCommand{nullptr,PathingCommandType::CancelAndSetGoal};
    }
    if(cursor_>=stations_.size()) {
        cursor_=0;
        if(phase_==Phase::Survey)phase_=Phase::Input;
        else if(phase_==Phase::Input)phase_=Phase::Collect;
        else if(std::ranges::all_of(stations_,[](const Station& s){return s.done;}))phase_=Phase::Return;
        else idle_=100;
        return pause();
    }
    auto& station=stations_[cursor_];
    if(phase_==Phase::Collect && station.done){++cursor_;return pause();}
    if(!p.ownedContainerReady()) {
        if(opened_)return stop("Furnace interface closed during an unfinished action");
        if(station.pos.distSq(baritone_.playerFeet())>9)return PathingCommand{std::make_shared<GoalGetToBlock>(station.pos),PathingCommandType::RevalidateGoalAndPath};
        if(safe && !p.containerOpen())p.openContainerAt(station.pos);
        if(++openTicks_>160)return stop("Could not open a registered furnace");
        return pause();
    }
    opened_=true;
    if(pending_!=Pending::None) {
        if(p.transferState()==TransferState::Rejected)return stop("Furnace transfer rejected: "+p.transferError());
        if(++pendingTicks_>140)return stop("Furnace transfer was not confirmed; it will not be resent");
        if(p.transferState()!=TransferState::Accepted)return pause();
        switch(pending_) {
        case Pending::Input:station.inputSubmitted+=pendingCount_;if(station.inputSubmitted==station.count)phase_=Phase::Fuel;break;
        case Pending::Fuel:station.fuelSubmitted+=pendingCount_;if(station.fuelSubmitted==station.fuel){close();++cursor_;phase_=Phase::Input;}break;
        case Pending::Output:station.collected=true;phase_=Phase::ReturnFuel;break;
        case Pending::UnusedFuel:station.done=true;close();++cursor_;phase_=Phase::Collect;break;
        default:break;
        }
        pending_=Pending::None;return pause();
    }
    if(p.inventoryMovePending() || !safe)return pause();
    const auto slots=p.containerSlots();
    auto input=std::ranges::find_if(slots,[](const ContainerSlot& s){return !s.playerInventory && (s.networkName==25 || s.networkName==45 || s.networkName==46);});
    auto fuel=std::ranges::find_if(slots,[](const ContainerSlot& s){return !s.playerInventory && s.networkName==24;});
    auto output=std::ranges::find_if(slots,[](const ContainerSlot& s){return !s.playerInventory && s.networkName==26;});
    if(input==slots.end() || fuel==slots.end() || output==slots.end())return stop("The registered station has no supported furnace input/fuel/output models");
    if(phase_==Phase::Survey || (phase_==Phase::Input && station.inputSubmitted==0)) {
        if(!input->stack.empty() || !fuel->stack.empty() || !output->stack.empty())return stop("Furnace pool requires initially empty stations; a station changed since survey");
        if(phase_==Phase::Survey){close();++cursor_;return pause();}
    }
    auto submit=[&](const ContainerSlot& from,const ContainerSlot& to,int amount,Pending kind) {
        if(!p.transferContainerItem(from,to,amount))return false;
        ++actions_;pending_=kind;pendingCount_=amount;pendingTicks_=0;return true;
    };
    if(phase_==Phase::Input || phase_==Phase::Fuel) {
        const bool loadingInput=phase_==Phase::Input;const auto& item=loadingInput?input_:fuel_;const auto& destination=loadingInput?*input:*fuel;
        const int remaining=loadingInput?station.count-station.inputSubmitted:station.fuel-station.fuelSubmitted;
        auto source=std::ranges::find_if(slots,[&](const ContainerSlot& s){return s.playerInventory && s.stack.name==item && !s.stack.empty() && mergeRoom(s.stack,destination.stack)>0;});
        if(source==slots.end())return stop("Missing input/fuel stack or unsupported stack merge at the workstation");
        const int amount=std::min({remaining,source->stack.count,mergeRoom(source->stack,destination.stack)});
        if(!submit(*source,destination,amount,loadingInput?Pending::Input:Pending::Fuel))return stop("The furnace transfer could not be submitted");
        return pause();
    }
    if(phase_==Phase::Collect) {
        if(!output->stack.empty() && output->stack.name!=output_)return stop("A furnace produced an unexpected item");
        if(output->stack.count>station.count)return stop("A furnace output was changed externally");
        if(output->stack.count<station.count){close();++cursor_;return pause();}
        if(!input->stack.empty())return stop("A furnace still contains unexpected input after its full output appeared");
        auto target=std::ranges::find_if(slots,[](const ContainerSlot& s){return s.playerInventory && s.stack.empty();});
        if(target==slots.end())return stop("No empty inventory slot for furnace output");
        if(!submit(*output,*target,station.count,Pending::Output))return stop("Could not collect furnace output");
    } else if(phase_==Phase::ReturnFuel) {
        if(fuel->stack.empty()){station.done=true;close();++cursor_;phase_=Phase::Collect;return pause();}
        auto target=std::ranges::find_if(slots,[](const ContainerSlot& s){return s.playerInventory && s.stack.empty();});
        if(target==slots.end())return stop("No empty inventory slot to return remaining fuel");
        if(!submit(*fuel,*target,fuel->stack.count,Pending::UnusedFuel))return stop("Could not return unused furnace fuel");
    }
    return pause();
}
}
