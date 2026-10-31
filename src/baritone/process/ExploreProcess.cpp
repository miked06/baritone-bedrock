#include "baritone/process/ExploreProcess.h"

#include <format>
#include <limits>
#include <algorithm>

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
    return origin_ ? std::format("Exploring around {} {}", origin_->x, origin_->z) : "Exploring";
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
    if (filter_ && invertFilter_) {
        // A sparse allow-list may name chunks thousands of chunks away. Do not
        // enumerate every intervening diamond (or falsely stop at distance 9999).
        struct Candidate { int cx, cz; int64_t distance; };
        std::vector<Candidate> candidates;
        for (int64_t key : *filter_) {
            const int cx = static_cast<int>(key >> 32), cz = static_cast<int>(static_cast<uint32_t>(key));
            if (!world.wasExplored(cx, cz)) candidates.push_back({cx, cz,
                std::abs(static_cast<int64_t>(cx) - chunkX) + std::abs(static_cast<int64_t>(cz) - chunkZ)});
        }
        std::ranges::sort(candidates, [](const auto& a, const auto& b) {
            return std::tie(a.distance, a.cx, a.cz) < std::tie(b.distance, b.cx, b.cz);
        });
        if (candidates.size() > count) candidates.resize(count);
        std::vector<GoalPtr> goals;
        for (const auto& c : candidates) {
            const int64_t x = static_cast<int64_t>(c.cx) * 16 + 8 + (c.cx < chunkX ? -offset : offset);
            const int64_t z = static_cast<int64_t>(c.cz) * 16 + 8 + (c.cz < chunkZ ? -offset : offset);
            if (x >= INT32_MIN && x <= INT32_MAX && z >= INT32_MIN && z <= INT32_MAX)
                goals.push_back(createGoal(static_cast<int>(x), static_cast<int>(z)));
        }
        return goals;
    }
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
        baritone_.logDirect("Exploration: no path; stopping", true);
        if (Settings::get().notificationOnExploreFinished) baritone_.logNotification("Exploration failed", true);
        onLostControl();
        return std::nullopt;
    }
    if (!Settings::get().disableCompletionCheck && countRemain() == 0) {
        baritone_.logDirect("Everything has been explored", false);
        if (Settings::get().notificationOnExploreFinished) baritone_.logNotification("All chunks have been explored", false);
        onLostControl();
        return std::nullopt;
    }
    auto goals = closestUnexploredChunks();
    if (goals.empty()) {
        baritone_.logDirect("Everything has been explored", false);
        if (Settings::get().notificationOnExploreFinished) baritone_.logNotification("All chunks have been explored", false);
        onLostControl();
        return std::nullopt;
    }
    return PathingCommand{std::make_shared<GoalComposite>(std::move(goals)), PathingCommandType::ForceRevalidateGoalAndPath};
}

}  // namespace baritone
