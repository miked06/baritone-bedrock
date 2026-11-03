#include "baritone/process/ProductionProcess.h"
#include "baritone/Baritone.h"
#include <algorithm>
#include <chrono>
#include <format>

namespace baritone {
std::map<std::string, int> ProductionProcess::inventory() const {
    std::map<std::string, int> stock;
    for (int slot = 0; slot < 36; ++slot) {
        const auto item = baritone_.player().hotbar(slot);
        if (!item.empty()) stock[item.name] += item.count;
    }
    return stock;
}
void ProductionProcess::start(const RecipeBook& book, BlockPos station, std::string item, int additional, bool execute) {
    if (!baritone_.player().valid()) throw std::invalid_argument("Enter a world first");
    if (planning_.valid()) {
        if (planning_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            throw std::invalid_argument("Wait for the previous ingredient calculation to finish");
        try { (void)planning_.get(); } catch (const std::exception&) { /* Discard a cancelled calculation. */ }
    }
    if (active_ || (execute && (baritone_.craft().isActive() || baritone_.smelt().isActive() || baritone_.player().containerOpen())))
        throw std::invalid_argument("Stop existing production and close its container first");
    if (additional < 1 || additional > 2304) throw std::invalid_argument("Additional production target must be 1..2304");
    initial_ = inventory(); plan_.reset(); failure_.clear();
    active_ = true; execute_ = execute; child_ = succeeded_ = false; next_ = 0; ticks_ = 0;
    station_ = station; origin_ = baritone_.playerFeet();
    planning_ = std::async(std::launch::async, [book, stock = initial_, item = std::move(item), additional] {
        return book.plan(item, additional, stock);
    });
}
bool ProductionProcess::isActive() { return active_ && !baritone_.pauser().paused(); }
void ProductionProcess::onLostControl() {
    if (active_ && failure_.empty()) failure_ = "Production cancelled";
    active_ = false;
    if (child_) baritone_.craft().onLostControl();
    child_ = false;
}
void ProductionProcess::shutdown() { onLostControl(); if (planning_.valid()) planning_.wait(); }
std::string ProductionProcess::displayName() const {
    return planning_.valid() ? "Calculating ingredient dependencies" : std::format("Recursive crafting step {}/{}", next_, plan_ ? plan_->steps.size() : 0);
}
std::optional<PathingCommand> ProductionProcess::onTick(bool failed, bool safe) {
    if (!active_) return {};
    auto finish = [&](std::string text, bool error) -> std::optional<PathingCommand> {
        if (error) failure_ = text;
        baritone_.logDirect(text, error);
        active_ = false;
        if (child_) baritone_.craft().onLostControl();
        child_ = false;
        return PathingCommand{nullptr, execute_ ? PathingCommandType::CancelAndSetGoal : PathingCommandType::Defer};
    };
    if (failed || ++ticks_ > 20 * 60 * 60) return finish("Recursive production stopped: path failure or job deadline", true);
    if (planning_.valid()) {
        if (planning_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return PathingCommand{nullptr, execute_ ? PathingCommandType::RequestPause : PathingCommandType::Defer};
        try { plan_ = planning_.get(); }
        catch (const std::exception& error) { return finish(error.what(), true); }
        baritone_.logDirect(std::format("Ingredient plan: {} additional {}, {} crafting steps, {} missing item types",
            plan_->additional, plan_->item, plan_->steps.size(), plan_->missing.size()));
        for (std::size_t i = 0; i < std::min<std::size_t>(32, plan_->steps.size()); ++i)
            baritone_.logDirect(std::format("{}: craft {} x{} ({} batches)", i + 1, plan_->steps[i].item, plan_->steps[i].amount, plan_->steps[i].batches));
        for (const auto& [item, count] : plan_->missing) baritone_.logDirect(std::format("Missing {} x{}", item, count));
        if (!execute_) { succeeded_ = true; return finish("Ingredient comparison complete; inventory was not changed", false); }
        if (!plan_->ready()) return finish("Production needs the missing resources before any crafting can start", true);
        if (inventory() != initial_) return finish("Inventory changed during planning; request a fresh production plan", true);
        finalTarget_ = baritone_.player().countItem(plan_->item) + plan_->additional;
    }
    if (!plan_) return finish("Production plan is unavailable", true);
    if (child_) {
        if (baritone_.craft().isActive()) return PathingCommand{nullptr, PathingCommandType::Defer};
        const auto& step = plan_->steps.at(next_);
        if (!baritone_.craft().succeeded() || baritone_.player().countItem(step.item) < childTarget_)
            return finish("Intermediate crafting was not confirmed: " + baritone_.craft().failure(), true);
        child_ = false; ++next_;
    }
    if (next_ < plan_->steps.size()) {
        if (!safe || baritone_.player().containerOpen() || baritone_.player().inventoryMovePending())
            return PathingCommand{nullptr, PathingCommandType::RequestPause};
        const auto& step = plan_->steps[next_];
        childTarget_ = baritone_.player().countItem(step.item) + step.amount;
        try { baritone_.craft().start(station_, step.item, step.amount, false); }
        catch (const std::exception& error) { return finish(error.what(), true); }
        child_ = true;
        return PathingCommand{nullptr, PathingCommandType::Defer};
    }
    if (baritone_.player().countItem(plan_->item) < finalTarget_) return finish("Final production output was not observed", true);
    if (origin_.distSq(baritone_.playerFeet()) > 1)
        return PathingCommand{std::make_shared<GoalNear>(origin_, 1), PathingCommandType::RevalidateGoalAndPath};
    succeeded_ = true;
    return finish("Recursive production complete; final output confirmed and returned to the start", false);
}
}
