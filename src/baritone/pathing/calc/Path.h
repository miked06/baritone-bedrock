#pragma once
#include <memory>
#include <optional>
#include <vector>

#include "baritone/pathing/goals/Goal.h"
#include "baritone/pathing/movement/Moves.h"

namespace baritone {

struct PathNode;
class CalculationContext;

// One step of a path. The executor turns it into a game-side movement.
struct MovementSpec {
    MovementType type;
    BlockPos src;
    BlockPos dest;
    double cost;  // cost at calculation time (used to detect cost increases)
    bool calculatedWhileLoaded = true;  // false: planned from the long-distance cache
};

// A calculated path (Baritone's IPath / Path).
class Path {
public:
    // Builds the path ending at `end` and its movements. If a movement became impossible
    // between the search and now, the path is cut just before it.
    static std::shared_ptr<Path> fromNodes(const PathNode* start, const PathNode* end, GoalPtr goal, int numNodes,
                                           CalculationContext& ctx);
    static std::shared_ptr<Path> fromParts(std::vector<BlockPos> positions, std::vector<MovementSpec> movements,
                                           GoalPtr goal, int numNodes);

    const std::vector<BlockPos>& positions() const { return positions_; }
    const std::vector<MovementSpec>& movements() const { return movements_; }
    const GoalPtr& goal() const { return goal_; }
    int numNodesConsidered() const { return numNodes_; }
    std::size_t length() const { return positions_.size(); }
    const BlockPos& src() const { return positions_.front(); }
    const BlockPos& dest() const { return positions_.back(); }

    double ticksRemainingFrom(std::size_t pathPosition) const;
    double totalTicks() const { return ticksRemainingFrom(0); }

    // Java staticCutoff: a segment not ending in the goal is cut to its first
    // (length - min) * factor + min positions (pathCutoffFactor, pathCutoffMinimumLength).
    std::shared_ptr<Path> staticCutoff(const Goal* destination, double factor, int min) const;
    // Java cutoffAtLoadedChunks: cut where the path leaves the loaded chunks.
    std::shared_ptr<Path> cutoffAtLoadedChunks(CalculationContext& ctx) const;
    // Drops the end of the path after `index` (inclusive end at index).
    std::shared_ptr<Path> cutoffAt(std::size_t index) const;

private:
    std::vector<BlockPos> positions_;
    std::vector<MovementSpec> movements_;
    GoalPtr goal_;
    int numNodes_ = 0;
};

using PathPtr = std::shared_ptr<const Path>;

// Joins two paths that share a position (Baritone's SplicedPath).
PathPtr splicePaths(const PathPtr& first, const PathPtr& second);

}  // namespace baritone
