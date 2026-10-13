#include "baritone/pathing/calc/Path.h"

#include <algorithm>
#include <unordered_map>

#include "baritone/pathing/calc/PathNode.h"
#include "baritone/pathing/movement/CalculationContext.h"

namespace baritone {

namespace {

// Finds the move that goes from src to dest and recomputes its cost now (Baritone's
// Path.runBackwards). nullopt if it became impossible.
std::optional<MovementSpec> runBackwards(CalculationContext& ctx, const BlockPos& src, const BlockPos& dest,
                                         double nodeCost) {
    // Java: the first move whose destination is `dest`, cost min(now, as calculated); a move
    // that became impossible keeps the calculated cost (the executor notices it later)
    MoveResult res;
    for (const Move& move : allMoves()) {
        res.reset();
        move.apply(move, ctx, src.x, src.y, src.z, res);
        int rx = res.x, ry = res.y, rz = res.z;
        if (res.cost >= costs::COST_INF && !move.dynamicXZ && !move.dynamicY) {
            rx = src.x + move.dx, ry = src.y + move.dy, rz = src.z + move.dz;  // fixed destination
        }
        if (rx != dest.x || ry != dest.y || rz != dest.z) continue;
        MovementType type = move.type;
        if (type == MovementType::Descend && ry != src.y - 1) type = MovementType::Fall;
        return MovementSpec{type, src, dest, std::min(res.cost, nodeCost), ctx.isLoadedLive(dest.x, dest.z)};
    }
    return std::nullopt;
}

}  // namespace

std::shared_ptr<Path> Path::fromNodes(const PathNode* start, const PathNode* end, GoalPtr goal, int numNodes,
                                      CalculationContext& ctx) {
    std::vector<const PathNode*> nodes;
    for (const PathNode* n = end; n; n = n->previous) {
        nodes.push_back(n);
        if (n == start) break;
    }
    std::reverse(nodes.begin(), nodes.end());

    auto path = std::make_shared<Path>();
    path->goal_ = std::move(goal);
    path->numNodes_ = numNodes;
    path->positions_.push_back({nodes[0]->x, nodes[0]->y, nodes[0]->z});
    for (std::size_t i = 1; i < nodes.size(); ++i) {
        BlockPos src{nodes[i - 1]->x, nodes[i - 1]->y, nodes[i - 1]->z};
        BlockPos dest{nodes[i]->x, nodes[i]->y, nodes[i]->z};
        auto spec = runBackwards(ctx, src, dest, nodes[i]->cost - nodes[i - 1]->cost);
        if (!spec) break;  // movement became impossible during the calculation: cut here
        path->movements_.push_back(*spec);
        path->positions_.push_back(dest);
    }
    return path;
}

std::shared_ptr<Path> Path::fromParts(std::vector<BlockPos> positions, std::vector<MovementSpec> movements,
                                      GoalPtr goal, int numNodes) {
    auto path = std::make_shared<Path>();
    path->positions_ = std::move(positions);
    path->movements_ = std::move(movements);
    path->goal_ = std::move(goal);
    path->numNodes_ = numNodes;
    return path;
}

double Path::ticksRemainingFrom(std::size_t pathPosition) const {
    double sum = 0;
    for (std::size_t i = pathPosition; i < movements_.size(); ++i) sum += movements_[i].cost;
    return sum;
}

std::shared_ptr<Path> Path::staticCutoff(const Goal* destination, double factor, int min) const {
    // Java PathBase.staticCutoff
    if (static_cast<int>(length()) < min) return std::make_shared<Path>(*this);
    if (!destination || destination->isInGoal(dest())) return std::make_shared<Path>(*this);
    int newLength = static_cast<int>((static_cast<int>(length()) - min) * factor) + min - 1;
    return cutoffAt(static_cast<std::size_t>(std::max(newLength, 0)));
}

std::shared_ptr<Path> Path::cutoffAtLoadedChunks(CalculationContext& ctx) const {
    // Java PathBase.cutoffAtLoadedChunks (cutoffAtLoadBoundary)
    for (std::size_t i = 0; i < positions_.size(); ++i) {
        if (!ctx.isLoadedLive(positions_[i].x, positions_[i].z)) return cutoffAt(i);
    }
    return std::make_shared<Path>(*this);
}

std::shared_ptr<Path> Path::cutoffAt(std::size_t index) const {
    index = std::min(index, length() - 1);
    auto path = std::make_shared<Path>();
    path->positions_.assign(positions_.begin(), positions_.begin() + index + 1);
    path->movements_.assign(movements_.begin(), movements_.begin() + index);
    path->goal_ = goal_;
    path->numNodes_ = numNodes_;
    return path;
}

PathPtr splicePaths(const PathPtr& first, const PathPtr& second) {
    if (!first || !second) return nullptr;
    if (first->dest() != second->src()) return nullptr;
    // the first position of `first` that is also in `second`: the loop (if any) in between is dropped
    std::unordered_map<BlockPos, std::size_t> secondIndex;
    for (std::size_t i = 0; i < second->length(); ++i) secondIndex.try_emplace(second->positions()[i], i);
    for (std::size_t i = 0; i < first->length(); ++i) {
        auto it = secondIndex.find(first->positions()[i]);
        if (it == secondIndex.end()) continue;
        std::size_t j = it->second;
        std::vector<BlockPos> positions(first->positions().begin(), first->positions().begin() + i);
        std::vector<MovementSpec> movements(first->movements().begin(), first->movements().begin() + i);
        positions.insert(positions.end(), second->positions().begin() + j, second->positions().end());
        movements.insert(movements.end(), second->movements().begin() + j, second->movements().end());
        return Path::fromParts(std::move(positions), std::move(movements), second->goal(),
                               first->numNodesConsidered() + second->numNodesConsidered());
    }
    return nullptr;
}

}  // namespace baritone
