#include "baritone/process/AnimalProcess.h"
#include "baritone/Baritone.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <unordered_map>
namespace baritone {
namespace {
const std::unordered_map<std::string, std::vector<std::string>> diets{
    {"minecraft:cow", {"minecraft:wheat"}}, {"minecraft:mooshroom", {"minecraft:wheat"}},
    {"minecraft:sheep", {"minecraft:wheat"}}, {"minecraft:chicken", {"minecraft:wheat_seeds", "minecraft:beetroot_seeds", "minecraft:melon_seeds", "minecraft:pumpkin_seeds"}},
    {"minecraft:pig", {"minecraft:carrot", "minecraft:potato", "minecraft:beetroot"}},
    {"minecraft:rabbit", {"minecraft:carrot", "minecraft:golden_carrot", "minecraft:dandelion"}}
};
}
void AnimalProcess::start(std::string species, bool harvest, int count, int keep, int radius) {
    if (species.find(':') == std::string::npos) species = "minecraft:" + species;
    if (!diets.contains(species)) throw std::invalid_argument("Supported animals: cow, mooshroom, sheep, chicken, pig, rabbit");
    if (count < 1 || count > 64 || keep < 2 || keep > 256 || radius < 1 || radius > 64) throw std::invalid_argument("Count 1..64, minimum kept adults 2..256, radius 1..64");
    onLostControl(); species_ = std::move(species); harvest_ = harvest; requested_ = count; keep_ = keep; radius_ = radius;
    completed_ = 0; actionTicks_ = 0; center_ = baritone_.playerFeet(); active_ = true;
}
std::string AnimalProcess::displayName() const { return std::format("{} {}: {}/{}", harvest_ ? "Harvesting" : "Breeding", species_, completed_, requested_); }
std::optional<PathingCommand> AnimalProcess::onTick(bool failed, bool safe) {
    if (!active_) return std::nullopt;
    auto stop = [&](const std::string& reason, bool error) -> std::optional<PathingCommand> {
        baritone_.logDirect(reason, error); onLostControl(); return PathingCommand{nullptr, PathingCommandType::CancelAndSetGoal};
    };
    auto entities = baritone_.player().entities();
    std::erase_if(entities, [&](const EntityInfo& e) {
        const BlockPos p{int(std::floor(e.position.x)), int(std::floor(e.position.y)), int(std::floor(e.position.z))};
        auto* ctx = baritone_.liveContext();
        return e.type != species_ || p.distSq(center_) > int64_t(radius_) * radius_ ||
            (ctx && !ctx->permitsModify(p)) || (!baritone_.selections().all().empty() && !baritone_.selections().contains(p));
    });
    int adults = static_cast<int>(std::ranges::count_if(entities, [](const EntityInfo& e) { return e.baby.has_value() && !*e.baby && !e.dead; }));
    if (completed_ >= requested_) return stop("Animal task complete", false);
    auto target = std::ranges::find_if(entities, [&](const EntityInfo& e) { return e.id == target_; });
    if (target_ && target == entities.end()) return stop("Animal target disappeared; completion was not confirmed", true);
    if (target != entities.end()) {
        if (harvest_ && (target->dead || (target->health && *target->health <= 0))) {
            ++completed_; target_ = 0; attempted_ = false; actionTicks_ = 0;
            return PathingCommand{nullptr, PathingCommandType::RequestPause};
        }
        if (!harvest_ && attempted_ && target->inLove.value_or(false)) {
            cooldowns_[target_] = std::chrono::steady_clock::now() + std::chrono::minutes(5);
            ++completed_; target_ = 0; attempted_ = false; actionTicks_ = 0;
            return PathingCommand{nullptr, PathingCommandType::RequestPause};
        }
    } else {
        if (harvest_ && adults <= keep_) return stop("Adult population reached the configured minimum; harvesting stopped", false);
        const auto now = std::chrono::steady_clock::now();
        std::erase_if(cooldowns_, [&](const auto& entry) { return entry.second <= now; });
        double nearest = 1e100;
        for (auto it = entities.begin(); it != entities.end(); ++it) {
            if (!it->baby || *it->baby || it->dead || cooldowns_.contains(it->id)) continue;
            if (!harvest_ && (!it->inLove || *it->inLove)) continue;
            auto distance = (it->position - baritone_.player().position()).lengthSq();
            if (distance < nearest) { nearest = distance; target = it; }
        }
        if (target == entities.end()) return stop("No eligible adults with known age/state are available", false);
        target_ = target->id; attempted_ = false; actionTicks_ = 0;
    }
    if (failed) return stop("No path to the selected animal", true);
    if (target->baby.value_or(true)) return stop("Animal age changed or could not be verified; task stopped", true);
    if (harvest_ && adults <= keep_) return stop("Adult population reached the configured minimum; harvesting stopped", false);
    const auto distance = (target->position - baritone_.player().position()).lengthSq();
    if (distance > 4) {
        auto p = target->position;
        return PathingCommand{std::make_shared<GoalNear>(BlockPos{int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z))}, 1), PathingCommandType::RevalidateGoalAndPath};
    }
    if (!safe) return PathingCommand{nullptr, PathingCommandType::Defer};
    auto& player = baritone_.player();
    if (!harvest_) {
        const auto& food = diets.at(species_);
        if (!baritone_.inventory().selectItem([&](const HotbarStack& item) { return std::ranges::find(food, item.name) != food.end(); }, true))
            return stop("Missing animal feed", true);
        if (player.slotChangePending() || player.inventoryMovePending()) return PathingCommand{nullptr, PathingCommandType::RequestPause};
        if (!attempted_) attempted_ = player.interactEntity(target_);
        if (++actionTicks_ > 100) return stop("Feeding was not confirmed by the animal's breeding state", true);
    } else {
        if (actionTicks_ % 10 == 0) player.attackEntity(target_);
        if (++actionTicks_ > 400) return stop("Animal harvest timed out; no kill was assumed", true);
    }
    return PathingCommand{nullptr, PathingCommandType::RequestPause};
}
}
