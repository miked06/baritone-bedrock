#pragma once
#include <atomic>
#include <memory>
#include <optional>
#include <vector>

#include "baritone/utils/BlockPos.h"
#include "baritone/world/WorldSnapshot.h"

namespace baritone::elytra {

// A path through the air for elytra flight (Baritone uses the native nether-pathfinder;
// this is a plain A* on blocks): every node has `clearance` free blocks around it.
// Chunks never seen count as air, so long paths go straight and are recalculated as the
// world loads during the flight.
struct FlightPathResult {
    std::vector<BlockPos> path;  // start .. end
    bool reachesGoal = false;    // false: best partial path (node limit or time out)
    int nodes = 0;
};

class ElytraPathFinder {
public:
    struct Options {
        int clearance = 1;           // free blocks around the path (elytraAllowTightSpaces: 0)
        std::optional<int> maxY;     // highest y (default: under the build limit / nether roof)
        bool useCache = true;        // elytraUseCache: blocks of the long-distance cache count
    };
    ElytraPathFinder(std::shared_ptr<const WorldSnapshot> world, int clearance = 1)
        : world_(std::move(world)), clearance_(clearance) {}
    ElytraPathFinder(std::shared_ptr<const WorldSnapshot> world, Options options)
        : world_(std::move(world)), clearance_(options.clearance), maxY_(options.maxY), useCache_(options.useCache) {}

    // Path from `start` to within `goalRadius` blocks of `goal`. Thread-safe (own block access).
    FlightPathResult calculate(const BlockPos& start, const BlockPos& goal, double goalRadius, long timeoutMs,
                               int maxNodes = 400000) const;
    void cancel() { cancelled_ = true; }

private:
    std::shared_ptr<const WorldSnapshot> world_;
    int clearance_;
    std::optional<int> maxY_;
    bool useCache_ = true;
    mutable std::atomic<bool> cancelled_{false};
};

}  // namespace baritone::elytra
