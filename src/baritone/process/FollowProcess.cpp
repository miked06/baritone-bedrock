#include "baritone/process/FollowProcess.h"

#include <cmath>

#include "baritone/Baritone.h"
#include "baritone/Settings.h"

namespace baritone {

void FollowProcess::follow(Filter filter, std::string description) {
    filter_ = std::move(filter);
    description_ = std::move(description);
    into_ = false;
}

// Java pickup: walk into the dropped items (any, or of the given names)
void FollowProcess::pickup(std::function<bool(const std::string& item)> items, std::string description) {
    filter_ = [items = std::move(items)](const EntityInfo& e) { return e.isItem && items(e.itemName); };
    description_ = std::move(description);
    into_ = true;
}

void FollowProcess::scanWorld() {
    cache_.clear();
    if (!filter_) return;
    Vec3d me = baritone_.player().position();
    int maxDist = Settings::get().followTargetMaxDistance;
    for (const EntityInfo& e : baritone_.player().entities()) {
        if (!filter_(e)) continue;
        if (maxDist != 0 && (e.position - me).lengthSq() > static_cast<double>(maxDist) * maxDist) continue;
        cache_.push_back(e);
    }
}

bool FollowProcess::isActive() {
    if (!filter_) return false;
    scanWorld();
    return !cache_.empty();
}

GoalPtr FollowProcess::towards(const EntityInfo& e) const {
    const Settings& s = Settings::get();
    BlockPos pos;
    if (s.followOffsetDistance == 0 || into_) {
        pos = {static_cast<int>(std::floor(e.position.x)), static_cast<int>(std::floor(e.position.y)),
               static_cast<int>(std::floor(e.position.z))};
    } else {
        auto g = GoalXZ::fromDirection(e.position.x, e.position.z, static_cast<float>(s.followOffsetDirection),
                                       s.followOffsetDistance);
        pos = {g->x(), static_cast<int>(std::floor(e.position.y)), g->z()};
    }
    if (into_) return std::make_shared<GoalBlock>(pos);
    return std::make_shared<GoalNear>(pos, s.followRadius);
}

std::optional<PathingCommand> FollowProcess::onTick(bool, bool) {
    scanWorld();
    if (cache_.empty()) return PathingCommand{nullptr, PathingCommandType::RequestPause};  // target out of sight: wait
    std::vector<GoalPtr> goals;
    for (const EntityInfo& e : cache_) goals.push_back(towards(e));
    return PathingCommand{std::make_shared<GoalComposite>(std::move(goals)), PathingCommandType::RevalidateGoalAndPath};
}

}  // namespace baritone
