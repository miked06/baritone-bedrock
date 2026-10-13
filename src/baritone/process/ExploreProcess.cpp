#include "baritone/process/ExploreProcess.h"

#include <format>
#include <limits>

#include "baritone/Baritone.h"
#include "baritone/Settings.h"

namespace baritone {

namespace {

// Java ExploreProcess.createGoal: a GoalXZ that also keeps exploreMaintainY
class GoalExploreXZ : public GoalXZ {
public:
    GoalExploreXZ(int x, int z, int y) : GoalXZ(x, z), y_(y) {}
    double heuristic(int x, int y, int z) const override { return GoalXZ::heuristic(x, y, z) + GoalYLevel::calculate(y_, y); }

private:
    int y_;
};

GoalPtr createGoal(int x, int z) {
    int y = Settings::get().exploreMaintainY;
    if (y == -1) return std::make_shared<GoalXZ>(x, z);
    return std::make_shared<GoalExploreXZ>(x, z, y);
}

}  // namespace

// Java IChunkFilter.countRemain: only an inverted chunk list ends; then the listed chunks
// not explored yet (counted up to exploreChunkSetMinimumSize)
int ExploreProcess::countRemain() const {
    if (!filter_ || !invertFilter_ || !baritone_.world()) return std::numeric_limits<int>::max();
    int remain = 0;
    for (int64_t key : *filter_) {
        int cx = static_cast<int>(key >> 32), cz = static_cast<int>(static_cast<uint32_t>(key));
        if (!baritone_.world()->wasExplored(cx, cz)) {
            if (++remain >= Settings::get().exploreChunkSetMinimumSize) return remain;
        }
    }
    return remain;
}

void ExploreProcess::explore(int centerX, int centerZ) {
    origin_ = BlockPos{centerX, 0, centerZ};
    distanceCompleted_ = 0;
}

std::string ExploreProcess::displayName() const {
    return origin_ ? std::format("Explorer autour de {} {}", origin_->x, origin_->z) : "Explorer";
}

bool ExploreProcess::alreadyExplored(const WorldSnapshot& world, int cx, int cz) const {
    // JsonChunkFilter: listed (or, inverted, not listed) chunks are explored; otherwise ask the cache
    if (filter_ && (filter_->contains(WorldSnapshot::key(cx, cz)) != invertFilter_)) return true;
    return world.wasExplored(cx, cz);
}

std::vector<GoalPtr> ExploreProcess::closestUnexploredChunks() {
    const WorldSnapshot& world = *baritone_.world();
    int chunkX = origin_->x >> 4, chunkZ = origin_->z >> 4;
    std::size_t count = static_cast<std::size_t>(std::max(1, std::min(countRemain(), Settings::get().exploreChunkSetMinimumSize)));
    int offset = Settings::get().worldExploringChunkOffset << 4;
    std::vector<BlockPos> centers;
    for (int dist = distanceCompleted_; dist < 10000; ++dist) {
        for (int dx = -dist; dx <= dist; ++dx) {
            int zval = dist - std::abs(dx);
            for (int mult = 0; mult < 2; ++mult) {
                int dz = (mult * 2 - 1) * zval;  // -zval or zval
                if (zval == 0 && mult == 1) continue;  // same chunk twice
                if (alreadyExplored(world, chunkX + dx, chunkZ + dz)) continue;
                // worldExploringChunkOffset: aim further away from the origin
                int centerX = ((chunkX + dx) << 4) + 8 + (dx < 0 ? -offset : offset);
                int centerZ = ((chunkZ + dz) << 4) + 8 + (dz < 0 ? -offset : offset);
                centers.push_back({centerX, 0, centerZ});
            }
        }
        if (centers.size() >= count) break;
        if (centers.empty()) distanceCompleted_ = dist + 1;  // everything up to dist is explored
    }
    std::vector<GoalPtr> goals;
    for (const BlockPos& c : centers) goals.push_back(createGoal(c.x, c.z));
    return goals;
}

std::optional<PathingCommand> ExploreProcess::onTick(bool calcFailed, bool) {
    if (!origin_ || !baritone_.world()) return std::nullopt;
    if (calcFailed) {
        baritone_.logDirect("Exploration : aucun chemin, arret", true);
        if (Settings::get().notificationOnExploreFinished) baritone_.logNotification("Exploration echouee", true);
        onLostControl();
        return std::nullopt;
    }
    if (!Settings::get().disableCompletionCheck && countRemain() == 0) {
        baritone_.logDirect("Tout est explore", false);
        if (Settings::get().notificationOnExploreFinished) baritone_.logNotification("Tous les chunks sont explores", false);
        onLostControl();
        return std::nullopt;
    }
    auto goals = closestUnexploredChunks();
    if (goals.empty()) {
        baritone_.logDirect("Tout est explore", false);
        if (Settings::get().notificationOnExploreFinished) baritone_.logNotification("Tous les chunks sont explores", false);
        onLostControl();
        return std::nullopt;
    }
    return PathingCommand{std::make_shared<GoalComposite>(std::move(goals)), PathingCommandType::ForceRevalidateGoalAndPath};
}

}  // namespace baritone
