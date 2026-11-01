#include "baritone/process/SurvivalProcess.h"
#include "baritone/Baritone.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace baritone {
namespace {
const std::unordered_map<std::string, int> foods{
    {"minecraft:cooked_beef", 8}, {"minecraft:cooked_porkchop", 8}, {"minecraft:cooked_mutton", 6},
    {"minecraft:cooked_chicken", 6}, {"minecraft:cooked_salmon", 6}, {"minecraft:cooked_cod", 5},
    {"minecraft:baked_potato", 5}, {"minecraft:bread", 5}, {"minecraft:carrot", 3}, {"minecraft:apple", 4},
    {"minecraft:golden_carrot", 6}, {"minecraft:beetroot", 1}, {"minecraft:melon_slice", 2}, {"minecraft:sweet_berries", 2}
};
}
void SurvivalProcess::setEnabled(bool enabled) { onLostControl(); enabled_ = enabled; }
void SurvivalProcess::onLostControl() {
    baritone_.player().holdUseItem(false);
    if (previousSlot_ >= 0 && baritone_.player().valid()) baritone_.player().selectSlot(previousSlot_);
    previousSlot_ = -1; eating_ = recovering_ = false; eatingTicks_ = 0; escape_.reset(); mode_.clear();
}
void SurvivalProcess::mode(std::string next) {
    if (mode_ != next) { mode_ = std::move(next); baritone_.logDirect("Survival: " + mode_); }
}
std::vector<BlockPos> SurvivalProcess::threats() const {
    static const std::unordered_set<std::string> hostile{
        "minecraft:zombie", "minecraft:husk", "minecraft:drowned", "minecraft:skeleton", "minecraft:stray",
        "minecraft:wither_skeleton", "minecraft:creeper", "minecraft:witch", "minecraft:pillager", "minecraft:vindicator",
        "minecraft:evoker", "minecraft:ravager", "minecraft:warden", "minecraft:phantom", "minecraft:blaze", "minecraft:piglin_brute"
    };
    std::vector<BlockPos> result;
    const auto position = baritone_.player().position();
    for (const auto& entity : baritone_.player().entities()) if (hostile.contains(entity.type) && (entity.position - position).lengthSq() < 64)
        result.push_back({int(std::floor(entity.position.x)), int(std::floor(entity.position.y)), int(std::floor(entity.position.z))});
    return result;
}
bool SurvivalProcess::isActive() {
    if (!enabled_ || !baritone_.player().valid() || baritone_.player().isCreative() || baritone_.player().containerOpen() || baritone_.pauser().paused()) return false;
    if (eating_ || recovering_ || baritone_.player().foodLevel() < eatBelow || baritone_.player().health() <= retreatBelow) return true;
    if (home_ && needsSupplies()) return true;
    return !threats().empty();
}
bool SurvivalProcess::needsSupplies() const {
    int empty = 0, food = 0, usableTools = 0, wornTools = 0;
    for (int i = 0; i < 36; ++i) {
        auto stack = baritone_.player().hotbar(i);
        if (stack.empty()) { ++empty; continue; }
        if (foods.contains(stack.name)) food += stack.count;
        if (stack.tool != Tool::None && stack.maxDamage > 0) {
            if (stack.maxDamage - stack.damage <= 10) ++wornTools; else ++usableTools;
        }
    }
    return empty == 0 || food < 2 || (wornTools > 0 && usableTools == 0);
}
std::optional<PathingCommand> SurvivalProcess::onTick(bool failed, bool safe) {
    auto& player = baritone_.player(); ++ticks_;
    if (!safe) return PathingCommand{nullptr, PathingCommandType::Defer};
    auto danger = threats();
    if (!danger.empty()) {
        player.holdUseItem(false); eating_ = false;
        mode("retreating from nearby hostile mobs");
        if (failed) { mode("no retreat path; paused"); return PathingCommand{nullptr, PathingCommandType::RequestPause}; }
        if (!escape_ || ticks_ % 10 == 0) escape_ = std::make_shared<GoalRunAway>(16, std::nullopt, std::move(danger));
        return PathingCommand{escape_, PathingCommandType::RevalidateGoalAndPath};
    }
    escape_.reset();
    if (home_ && needsSupplies()) {
        player.holdUseItem(false); eating_ = false;
        if (home_->distSq(baritone_.playerFeet()) > 4 && !failed) {
            mode("returning home for supplies or unloading");
            return PathingCommand{std::make_shared<GoalNear>(*home_, 2), PathingCommandType::RevalidateGoalAndPath};
        }
        mode("supplies need attention; paused at home"); return PathingCommand{nullptr, PathingCommandType::RequestPause};
    }
    if (player.health() <= retreatBelow) recovering_ = true;
    if (recovering_ && player.health() > retreatBelow + 4) recovering_ = false;
    if (recovering_ && home_ && home_->distSq(baritone_.playerFeet()) > 4) {
        player.holdUseItem(false); mode("returning home to recover");
        if (failed) return PathingCommand{nullptr, PathingCommandType::RequestPause};
        return PathingCommand{std::make_shared<GoalNear>(*home_, 2), PathingCommandType::RevalidateGoalAndPath};
    }
    if (player.foodLevel() < eatBelow || (recovering_ && player.foodLevel() < 20) || eating_) {
        if (player.foodLevel() >= 20) {
            player.holdUseItem(false); eating_ = false; eatingTicks_ = 0;
            if (previousSlot_ >= 0) { player.selectSlot(previousSlot_); previousSlot_ = -1; }
        } else {
            if (previousSlot_ < 0) previousSlot_ = player.selectedSlot();
            int best = 0;
            for (int i = 0; i < 36; ++i) { auto stack = player.hotbar(i); if (!stack.empty()) if (auto food = foods.find(stack.name); food != foods.end()) best = std::max(best, food->second); }
            if (!best || !baritone_.inventory().selectItem([best](const HotbarStack& stack) { auto found = foods.find(stack.name); return found != foods.end() && found->second == best; }, true)) {
                player.holdUseItem(false); mode("no safe food available; paused"); return PathingCommand{nullptr, PathingCommandType::RequestPause};
            }
            if (player.slotChangePending() || player.inventoryMovePending()) return PathingCommand{nullptr, PathingCommandType::RequestPause};
            mode("eating"); eating_ = true;
            // Aim away from containers and animals so using food cannot interact
            // with a targeted block/entity instead of eating.
            baritone_.controls().setTarget({Rotation{player.rotation().yaw, -90}, true});
            if (player.rotation().pitch > -80) { player.holdUseItem(false); return PathingCommand{nullptr, PathingCommandType::RequestPause}; }
            if (!player.holdUseItem(true)) { mode("held-use input unavailable; paused"); return PathingCommand{nullptr, PathingCommandType::RequestPause}; }
            if (++eatingTicks_ > 200) { player.holdUseItem(false); eating_ = false; enabled_ = false; mode("eating timed out; assistant stopped"); }
            return PathingCommand{nullptr, PathingCommandType::RequestPause};
        }
    }
    if (recovering_) { mode("waiting for health regeneration"); return PathingCommand{nullptr, PathingCommandType::RequestPause}; }
    onLostControl(); return PathingCommand{nullptr, PathingCommandType::Defer};
}
}
