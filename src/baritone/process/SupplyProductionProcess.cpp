#include "baritone/process/SupplyProductionProcess.h"
#include "baritone/Baritone.h"
#include <algorithm>
#include <format>
#include <chrono>

namespace baritone {
std::vector<std::string> SupplyProductionProcess::harvestBlocks(const std::string& item) {
    // Only deterministic ordinary drops. In particular, never treat raw ores as
    // ingots, or mine silk-touch ores while expecting their normal drops.
    if (item == "minecraft:coal") return {"coal_ore", "deepslate_coal_ore"};
    if (item == "minecraft:raw_iron") return {"iron_ore", "deepslate_iron_ore"};
    if (item == "minecraft:raw_copper") return {"copper_ore", "deepslate_copper_ore"};
    if (item == "minecraft:raw_gold") return {"gold_ore", "deepslate_gold_ore"};
    if (item == "minecraft:diamond") return {"diamond_ore", "deepslate_diamond_ore"};
    if (item == "minecraft:lapis_lazuli") return {"lapis_ore", "deepslate_lapis_ore"};
    if (item == "minecraft:redstone") return {"redstone_ore", "deepslate_redstone_ore"};
    if (item == "minecraft:cobblestone") return {"stone", "cobblestone"};
    if (item == "minecraft:cobbled_deepslate") return {"deepslate", "cobbled_deepslate"};
    for (const std::string wood : {"oak", "spruce", "birch", "jungle", "acacia", "dark_oak", "mangrove", "cherry", "pale_oak"})
        if (item == "minecraft:" + wood + "_log") return {wood + "_log"};
    if (item == "minecraft:dirt" || item == "minecraft:sand" || item == "minecraft:netherrack" || item == "minecraft:blackstone") return {item};
    return {};
}
void SupplyProductionProcess::start(const RecipeBook& book, BlockPos station, std::string item, int additional,
                                    std::string supply, std::string delivery) {
    if (!baritone_.player().valid() || active_ || baritone_.player().containerOpen() || baritone_.player().inventoryMovePending())
        throw std::invalid_argument("Enter a world, close containers and finish the current inventory job first");
    for (const auto& location : {supply, delivery})
        if (!location.empty() && !baritone_.storage().locations().contains(location)) throw std::invalid_argument("Unknown registered supply/delivery location: " + location);
    if(additional<1 || additional>2304)throw std::invalid_argument("Additional production target must be 1..2304");
    if(planning_.valid()) {
        if(planning_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)throw std::invalid_argument("Wait for the previous ingredient calculation to finish");
        try{(void)planning_.get();}catch(const std::exception&){}
    }
    book_=book; item_=BlockFilter::normalize(std::move(item)); additional_=additional; target_=baritone_.player().countItem(item_)+additional;
    if (target_>2304) throw std::invalid_argument("Final output target exceeds inventory capacity");
    station_=station; origin_=baritone_.playerFeet(); supply_=std::move(supply); delivery_=std::move(delivery);
    ticks_=rounds_=0; failure_.clear(); harvest_.clear(); phase_=Phase::Plan; supplied_=succeeded_=false; active_=true;
}
bool SupplyProductionProcess::isActive() { return active_ && !baritone_.pauser().paused(); }
void SupplyProductionProcess::onLostControl() {
    if (!active_) return;
    if (phase_==Phase::Supply) baritone_.logistics().onLostControl();
    if (phase_==Phase::Harvest) baritone_.mine().onLostControl();
    if (phase_==Phase::Craft) baritone_.production().onLostControl();
    if (phase_==Phase::Deliver) baritone_.storage().onLostControl();
    active_=false; if (failure_.empty()) failure_="Production objective cancelled; completed transfers and crafting are preserved";
}
std::string SupplyProductionProcess::displayName() const {
    static constexpr const char* names[]={"Planning", "Restocking", "Gathering", "Crafting", "Delivering", "Returning"};
    return std::format("{} production objective: {} additional {}", names[int(phase_)],additional_,item_);
}
std::optional<PathingCommand> SupplyProductionProcess::onTick(bool failed,bool safe) {
    if (!active_) return {};
    auto stop=[&](std::string reason)->std::optional<PathingCommand> {
        failure_=std::move(reason); onLostControl(); baritone_.logDirect(failure_,true);
        return PathingCommand{nullptr,PathingCommandType::CancelAndSetGoal};
    };
    if (failed || ++ticks_>20*60*60) return stop("Production objective stopped: path failure or one-hour deadline");
    try {
    auto& p=baritone_.player();
    if (phase_==Phase::Supply) {
        if (baritone_.logistics().isActive()) return PathingCommand{nullptr,PathingCommandType::Defer};
        // A confirmed partial withdrawal is useful. Replan from actual inventory,
        // then gather supported resources rather than resending the withdrawal.
        if (!baritone_.logistics().succeeded()) baritone_.logDirect("Supply pass ended: "+baritone_.logistics().failure());
        phase_=Phase::Plan;
    }
    if (phase_==Phase::Harvest) {
        if (p.countItem(harvest_)>=harvestTarget_) { baritone_.mine().onLostControl(); phase_=Phase::Plan; }
        else if (!baritone_.mine().isActive()) return stop("Gathering stopped before the required inventory quantity of "+harvest_);
        else if (!baritone_.diagnostics().errorsAfter(diagnostic_).empty()) return stop("Gathering failed: "+baritone_.diagnostics().errorsAfter(diagnostic_));
        else return PathingCommand{nullptr,PathingCommandType::Defer};
    }
    if (phase_==Phase::Craft) {
        if (baritone_.production().isActive()) return PathingCommand{nullptr,PathingCommandType::Defer};
        if (!baritone_.production().succeeded() || p.countItem(item_)<target_)
            return stop("Production output was not confirmed: "+baritone_.production().failure());
        if (!delivery_.empty()) {
            baritone_.storage().start(delivery_,item_,additional_,false); phase_=Phase::Deliver;
            return PathingCommand{nullptr,PathingCommandType::Defer};
        }
        phase_=Phase::Return;
    }
    if (phase_==Phase::Deliver) {
        if (baritone_.storage().isActive()) return PathingCommand{nullptr,PathingCommandType::Defer};
        if (!baritone_.storage().succeeded()) return stop("Delivery stopped: "+baritone_.storage().failure());
        phase_=Phase::Return;
    }
    if (phase_==Phase::Return) {
        if (origin_.distSq(baritone_.playerFeet())>1) return PathingCommand{std::make_shared<GoalNear>(origin_,1),PathingCommandType::RevalidateGoalAndPath};
        active_=false; succeeded_=true; baritone_.logDirect("Production objective complete; output confirmed and returned to the start");
        return PathingCommand{nullptr,PathingCommandType::CancelAndSetGoal};
    }
    if (!safe || p.containerOpen() || p.inventoryMovePending()) return PathingCommand{nullptr,PathingCommandType::RequestPause};
    std::map<std::string,int> stock;
    for (int slot=0; slot<36; ++slot) { const auto s=p.hotbar(slot); if (!s.empty()) stock[s.name]+=s.count; }
    const int remaining=target_-p.countItem(item_);
    if (remaining<=0) {
        if (!delivery_.empty()) { baritone_.storage().start(delivery_,item_,additional_,false); phase_=Phase::Deliver; }
        else phase_=Phase::Return;
        return PathingCommand{nullptr,PathingCommandType::Defer};
    }
    if(!planning_.valid()) {
        if(++rounds_>64)return stop("Production acquisition exceeded 64 planning passes");
        planningStock_=stock;
        planning_=std::async(std::launch::async,[book=book_,item=item_,remaining,stock=std::move(stock)] {return book.plan(item,remaining,stock);});
        return PathingCommand{nullptr,PathingCommandType::RequestPause};
    }
    if(planning_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return PathingCommand{nullptr,PathingCommandType::RequestPause};
    ProductionPlan plan;
    try{plan=planning_.get();}catch(const std::exception& error){return stop(error.what());}
    if(stock!=planningStock_)return PathingCommand{nullptr,PathingCommandType::RequestPause};
    if (plan.ready()) {
        baritone_.production().start(book_,station_,item_,remaining,true); phase_=Phase::Craft;
        return PathingCommand{nullptr,PathingCommandType::Defer};
    }
    if (!supply_.empty() && !supplied_) {
        std::map<std::string,int> targets;
        for (const auto& [id,count]:plan.missing) targets[id]=p.countItem(id)+count;
        baritone_.logistics().start(std::move(targets),supply_); supplied_=true; phase_=Phase::Supply;
        return PathingCommand{nullptr,PathingCommandType::Defer};
    }
    // Check all missing types before allowing a destructive acquisition pass.
    for (const auto& [id,count]:plan.missing)
        if (harvestBlocks(id).empty()) return stop("No automatic gathering policy for "+id+"; supply it in inventory or a registered source chest");
    const auto& [id,count]=*plan.missing.begin();
    const bool ordinaryDrops=id=="minecraft:coal" || id=="minecraft:diamond" || id=="minecraft:raw_iron" || id=="minecraft:raw_gold" || id=="minecraft:raw_copper" || id=="minecraft:redstone" || id=="minecraft:lapis_lazuli" || id=="minecraft:cobblestone" || id=="minecraft:cobbled_deepslate";
    if (ordinaryDrops) for (int slot=0;slot<36;++slot)
        if (p.hotbar(slot).silkTouch && p.hotbar(slot).tool==Tool::Pickaxe)
            return stop("Put Silk Touch pickaxes in storage before gathering ordinary drops; tool selection may otherwise choose one");
    harvest_=id; harvestTarget_=p.countItem(id)+count;
    if (harvestTarget_>2304) return stop("Gathering target exceeds inventory capacity");
    diagnostic_=baritone_.diagnostics().sequence();
    const BlockFilter filter(harvestBlocks(id));
    int aggregate=count;for(const auto& drop:filter.dropNames())aggregate+=p.countItem(drop);
    baritone_.mine().mine(aggregate,filter); phase_=Phase::Harvest;
    baritone_.logDirect(std::format("Gathering {} to inventory target {}",id,harvestTarget_));
    return PathingCommand{nullptr,PathingCommandType::Defer};
    }catch(const std::exception& error){return stop(std::string("Production objective stopped: ")+error.what());}
}
}
