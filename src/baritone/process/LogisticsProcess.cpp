#include "baritone/process/LogisticsProcess.h"
#include "baritone/Baritone.h"
#include "baritone/cache/WorldScanner.h"
#include <algorithm>
#include <format>
namespace baritone {
void LogisticsProcess::begin(std::string source) {
    if (active_ || baritone_.storage().isActive() || baritone_.production().executing() ||
        baritone_.player().containerOpen() || baritone_.player().inventoryMovePending())
        throw std::invalid_argument("Finish current container/production actions before starting logistics");
    std::vector<std::string> sources;
    for (const auto& [name, pos] : baritone_.storage().locations()) if (source.empty() || source == name) sources.push_back(name);
    if (sources.empty()) throw std::invalid_argument("Register a supply container with #storage add first");
    origin_ = baritone_.playerFeet();
    std::ranges::stable_sort(sources, [&](const auto& a, const auto& b) {
        return baritone_.storage().locations().at(a).distSq(origin_) < baritone_.storage().locations().at(b).distSq(origin_);
    });
    sources_ = std::move(sources); targets_.clear(); scanned_.clear(); skipped_.clear(); equip_.clear(); failure_.clear();
    active_ = true; succeeded_ = child_ = equipping_ = survey_ = false; ticks_ = actions_ = 0;
}
void LogisticsProcess::start(std::map<std::string, int> targets, std::string source, std::string equip) {
    if (targets.empty() || targets.size() > 36) throw std::invalid_argument("Restocking needs 1..36 item targets");
    std::map<std::string, int> normalized; int total = 0;
    for (const auto& [item, count] : targets) {
        if (count < 1 || count > 2304 || (total += count) > 2304) throw std::invalid_argument("Restock targets must fit within 2304 items");
        normalized[BlockFilter::normalize(item)] = count;
    }
    if (!equip.empty()) (void)baritone_.loadouts().profile(equip);
    begin(std::move(source)); targets_ = std::move(normalized); equip_ = std::move(equip);
}
void LogisticsProcess::survey(std::string source) { begin(std::move(source)); survey_ = true; }
bool LogisticsProcess::isActive() { return active_ && !baritone_.pauser().paused(); }
void LogisticsProcess::onLostControl() {
    if (active_ && failure_.empty()) failure_ = "Logistics cancelled";
    active_ = false;
    if (child_) baritone_.storage().onLostControl();
    if (equipping_) baritone_.loadout().onLostControl();
    child_ = equipping_ = false;
}
std::string LogisticsProcess::displayName() const { return survey_ ? "Surveying registered storage" : "Restocking inventory targets"; }
std::optional<PathingCommand> LogisticsProcess::onTick(bool failed, bool safe) {
    if (!active_) return {};
    auto finish = [&](std::string message, bool error) -> std::optional<PathingCommand> {
        if (error) { failure_ = message; onLostControl(); }
        else { succeeded_ = true; active_ = false; }
        baritone_.logDirect(message, error);
        return PathingCommand{nullptr, PathingCommandType::CancelAndSetGoal};
    };
    if (failed || ++ticks_ > 20 * 60 * 30 || actions_ > 512) return finish("Logistics stopped: path failure or bounded job limit", true);
    auto& storage = baritone_.storage();
    if (child_) {
        if (storage.isActive()) return PathingCommand{nullptr, PathingCommandType::Defer};
        child_ = false;
        if (!storage.succeeded()) {
            skipped_.insert(childSource_);
            baritone_.logDirect("Supply location skipped: " + childSource_ + ": " + storage.failure(), false);
        }
    }
    if (equipping_) {
        if (baritone_.loadout().isActive()) return PathingCommand{nullptr, PathingCommandType::Defer};
        equipping_ = false;
        if (!baritone_.loadout().succeeded()) return finish(baritone_.loadout().failure(), true);
        equip_.clear();
    }
    std::string item; int missing = 0;
    for (const auto& [name, target] : targets_) if (const int need = target - baritone_.player().countItem(name); need > 0) { item = name; missing = need; break; }
    const bool complete = !survey_ && item.empty();
    if (!safe || baritone_.player().containerOpen() || baritone_.player().inventoryMovePending())
        return PathingCommand{nullptr, PathingCommandType::RequestPause};
    if (!complete) {
        // Each source is freshly inspected once per job. A changed container can
        // never trigger a blind retry of an accepted partial transfer.
        for (const auto& source : sources_) {
            if (skipped_.contains(source) || !storage.locations().contains(source)) continue;
            try {
                if (!scanned_.contains(source)) {
                    storage.scan(source, false); scanned_.insert(source); childSource_ = source; child_ = true; ++actions_;
                    return PathingCommand{nullptr, PathingCommandType::Defer};
                }
                if (!survey_) {
                    const auto observed = storage.observations().find(source);
                    if (observed == storage.observations().end()) continue;
                    const auto available = observed->second.items.find(item);
                    if (available == observed->second.items.end() || available->second <= 0) continue;
                    storage.start(source, item, std::min(missing, available->second), true, false);
                    childSource_ = source; child_ = true; ++actions_;
                    return PathingCommand{nullptr, PathingCommandType::Defer};
                }
            } catch (const std::exception& e) { skipped_.insert(source); baritone_.logDirect(source + ": " + e.what(), false); }
        }
        if (!survey_) return finish(std::format("Supplies exhausted or unavailable: {} more {} needed; completed transfers are retained", missing, item), true);
    }
    if (!equip_.empty()) {
        try { baritone_.loadout().start(equip_, baritone_.loadouts().profile(equip_)); equipping_ = true; }
        catch (const std::exception& e) { return finish(e.what(), true); }
        return PathingCommand{nullptr, PathingCommandType::Defer};
    }
    if (origin_.distSq(baritone_.playerFeet()) > 1) return PathingCommand{std::make_shared<GoalNear>(origin_, 1), PathingCommandType::RevalidateGoalAndPath};
    if (survey_ && !skipped_.empty()) return finish("Storage survey incomplete: some locations were inaccessible; inspect the last-seen results", true);
    return finish(survey_ ? "Storage survey complete; last-seen contents indexed" : "Restock targets confirmed; returned to the interrupted task", false);
}
}
