#include "baritone/process/GetToBlockProcess.h"

#include <algorithm>

#include "baritone/Baritone.h"
#include "baritone/Settings.h"
#include "baritone/pathing/movement/MovementHelper.h"

namespace baritone {

void GetToBlockProcess::getToBlock(BlockFilter filter) {
    onLostControl();
    filter_ = std::move(filter);
    start_ = baritone_.playerFeet();
    blacklist_.clear();
    tickCount_ = 0;
    arrivalTickCount_ = 0;
    clicked_ = false;
    rescan();
}

void GetToBlockProcess::onLostControl() {
    filter_.reset();
    knownLocations_.reset();
    blacklist_.clear();
    scan_.wait();
}

void GetToBlockProcess::rescan() {
    if (!filter_ || !baritone_.world()) return;
    auto positions = searchWorld(*baritone_.world(), *filter_, baritone_.playerFeet(), 64,
                                      Settings::get().maxScanChunkRadius);
    std::erase_if(positions, [&](const BlockPos& p) { return std::ranges::find(blacklist_, p) != blacklist_.end(); });
    knownLocations_ = std::move(positions);
}

// Java walkIntoInsteadOfAdjacent: a nether portal with enterPortal
bool GetToBlockProcess::walkIntoInsteadOfAdjacent() const {
    return Settings::get().enterPortal && filter_->matchesName("minecraft:portal");
}

// Java rightClickOnArrival: containers are opened with rightClickContainerOnArrival
bool GetToBlockProcess::rightClickOnArrival() const {
    if (!Settings::get().rightClickContainerOnArrival) return false;
    for (std::string_view b : {"minecraft:crafting_table", "minecraft:furnace", "minecraft:lit_furnace", "minecraft:blast_furnace",
                               "minecraft:lit_blast_furnace", "minecraft:ender_chest", "minecraft:chest", "minecraft:trapped_chest"}) {
        if (filter_->matchesName(std::string(b))) return true;
    }
    return false;
}

// Java blockOnTopMustBeRemoved: a chest only opens with nothing solid on top
bool GetToBlockProcess::blockOnTopMustBeRemoved() const {
    if (!rightClickOnArrival()) return false;
    return filter_->matchesName("minecraft:ender_chest") || filter_->matchesName("minecraft:chest") ||
           filter_->matchesName("minecraft:trapped_chest");
}

GoalPtr GetToBlockProcess::createGoal(const BlockPos& pos) {
    if (walkIntoInsteadOfAdjacent()) return std::make_shared<GoalTwoBlocks>(pos);
    if (blockOnTopMustBeRemoved() && baritone_.liveContext() &&
        helper::isBlockNormalCube(baritone_.liveContext()->props(pos.x, pos.y + 1, pos.z))) {
        return std::make_shared<GoalBlock>(pos.above());
    }
    return std::make_shared<GoalGetToBlock>(pos);
}

// Java rightClick: look at a known instance and right click it (a real click: opens it
// whatever is held). Done once clicked, or after 20 ticks.
bool GetToBlockProcess::rightClick() {
    CalculationContext* calc = baritone_.liveContext();
    if (!calc) return true;
    ExecContext ec{baritone_.player(), *calc, baritone_.inventory(), baritone_.playerFeet()};
    for (const BlockPos& pos : *knownLocations_) {
        auto reach = movement_helper::reachable(ec, pos);
        if (!reach) continue;
        baritone_.controls().setTarget({*reach, true});
        if (ec.isLookingAt(pos) && !clicked_) {
            baritone_.player().useHeldItem();
            clicked_ = true;
        }
        if (clicked_ && arrivalTickCount_ >= 5) return true;  // the screen had the time to open
        if (arrivalTickCount_++ > 20) {
            baritone_.logDirect("Clic droit : delai depasse", true);
            return true;
        }
        return false;  // trying to right click, next tick or so
    }
    baritone_.logDirect("Arrive mais impossible d'ouvrir en clic droit", true);
    return true;
}

bool GetToBlockProcess::blacklistClosest() {
    if (!knownLocations_ || knownLocations_->empty()) return false;
    BlockPos feet = baritone_.playerFeet();
    auto closest = *std::ranges::min_element(*knownLocations_, [&](const BlockPos& a, const BlockPos& b) {
        return a.distSq(feet) < b.distSq(feet);
    });
    // the closest one and every known instance connected to it, block by block (same vein
    // or structure), like Java's blacklistClosest
    std::vector<BlockPos> toBlacklist{closest};
    std::erase(*knownLocations_, closest);
    for (bool grew = true; grew;) {
        grew = false;
        for (const BlockPos& known : *knownLocations_) {
            bool adjacent = std::ranges::any_of(toBlacklist, [&](const BlockPos& b) {
                return std::abs(known.x - b.x) + std::abs(known.y - b.y) + std::abs(known.z - b.z) == 1;
            });
            if (adjacent) {
                toBlacklist.push_back(known);
                std::erase(*knownLocations_, known);
                grew = true;
                break;
            }
        }
    }
    for (const BlockPos& p : toBlacklist) {
        blacklist_.push_back(p);
        std::erase(*knownLocations_, p);
    }
    return true;
}

std::optional<PathingCommand> GetToBlockProcess::onTick(bool calcFailed, bool isSafeToCancel) {
    if (!filter_) return std::nullopt;
    if (auto result = scan_.poll()) {
        std::erase_if(*result, [&](const BlockPos& p) { return std::ranges::find(blacklist_, p) != blacklist_.end(); });
        knownLocations_ = std::move(*result);
    }
    if (!knownLocations_) rescan();
    if (knownLocations_->empty()) {
        if (Settings::get().exploreForBlocks && !calcFailed) {
            return PathingCommand{std::make_shared<GoalRunAway>(1, std::nullopt, std::vector<BlockPos>{start_}, true),
                                  PathingCommandType::ForceRevalidateGoalAndPath};
        }
        baritone_.logDirect("Aucun " + filter_->toString() + " connu, #goto annule", true);
        if (isSafeToCancel) onLostControl();
        return PathingCommand{nullptr, PathingCommandType::CancelAndSetGoal};
    }
    std::vector<GoalPtr> goals;
    for (const BlockPos& p : *knownLocations_) goals.push_back(createGoal(p));
    GoalPtr goal = std::make_shared<GoalComposite>(std::move(goals));
    if (calcFailed) {
        if (Settings::get().blacklistClosestOnFailure) {
            baritone_.logDirect("Pas de chemin vers " + filter_->toString() + ", le plus proche est ignore", true);
            blacklistClosest();
            return onTick(false, isSafeToCancel);
        }
        baritone_.logDirect("Pas de chemin vers " + filter_->toString() + ", #goto annule", true);
        if (isSafeToCancel) onLostControl();
        return PathingCommand{goal, PathingCommandType::CancelAndSetGoal};
    }
    int interval = Settings::get().mineGoalUpdateInterval;
    if (interval != 0 && tickCount_++ % interval == 0) {
        scan_.start(baritone_.world(), *filter_, baritone_.playerFeet(), 64, Settings::get().maxScanChunkRadius);
    }
    if (goal->isInGoal(baritone_.playerFeet()) && goal->isInGoal(baritone_.pathing().pathStart()) && isSafeToCancel) {
        if (rightClickOnArrival()) {
            if (!rightClick()) return PathingCommand{nullptr, PathingCommandType::RequestPause};
        }
        baritone_.logDirect("Arrive a " + filter_->toString(), false);
        onLostControl();
        return PathingCommand{nullptr, PathingCommandType::CancelAndSetGoal};
    }
    return PathingCommand{goal, PathingCommandType::RevalidateGoalAndPath};
}

std::string GetToBlockProcess::displayName() const {
    return "Aller a " + (filter_ ? filter_->toString() : std::string());
}

}  // namespace baritone
