#include "baritone/pathing/calc/AStarPathFinder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <stdexcept>
#include <thread>

#include "baritone/Settings.h"
#include "baritone/pathing/calc/BinaryHeapOpenSet.h"

namespace baritone {

// ---- Favoring ----
Favoring::Favoring(const PathPtr& previous, double backtrackCoefficient) {
    if (backtrackCoefficient == 1.0 || !previous) return;
    for (const BlockPos& p : previous->positions()) favorings_[p.longHash()] = backtrackCoefficient;
}

void Favoring::avoidSphere(const BlockPos& c, int radius, double coefficient) {
    for (int x = -radius; x <= radius; ++x)
        for (int y = -radius; y <= radius; ++y)
            for (int z = -radius; z <= radius; ++z)
                if (x * x + y * y + z * z <= radius * radius) {
                    favorings_[BlockPos::longHash(c.x + x, c.y + y, c.z + z)] *= coefficient;
                }
}

// ---- AStarPathFinder ----
AStarPathFinder::AStarPathFinder(BlockPos start, GoalPtr goal, std::unique_ptr<CalculationContext> context,
                                 Favoring favoring)
    : start_(start), goal_(std::move(goal)), context_(std::move(context)), favoring_(std::move(favoring)) {
    // Java AbstractNodeCostSearch: the node map's initial size and load factor
    const Settings& s = Settings::get();
    map_.max_load_factor(static_cast<float>(std::clamp(s.pathingMapLoadFactor, 0.1, 4.0)));
    map_.reserve(static_cast<std::size_t>(std::max(16, s.pathingMapDefaultSize)));
}

AStarPathFinder::~AStarPathFinder() = default;

PathNode* AStarPathFinder::getNode(int x, int y, int z, int64_t hash) {
    auto [it, inserted] = map_.try_emplace(hash, nullptr);
    if (!inserted) return it->second;
    if (blocks_.empty() || blockUsed_ == kBlockSize) {
        blocks_.push_back(std::make_unique<PathNode[]>(kBlockSize));
        blockUsed_ = 0;
    }
    PathNode* node = &blocks_.back()[blockUsed_++];
    node->init(x, y, z, *goal_);
    it->second = node;
    return node;
}

double AStarPathFinder::distFromStartSq(const PathNode* n) const {
    double dx = n->x - start_.x, dy = n->y - start_.y, dz = n->z - start_.z;
    return dx * dx + dy * dy + dz * dz;
}

PathCalculationResult AStarPathFinder::calculate(long primaryTimeoutMs, long failureTimeoutMs) {
    struct FinishGuard {
        std::atomic<bool>& f;
        ~FinishGuard() { f = true; }
    } guard{finished_};

    try {
        using Clock = std::chrono::steady_clock;
        const Settings& settings = Settings::get();
        CalculationContext& ctx = *context_;
        const int minY = ctx.bsi.minY();
        const int maxY = ctx.bsi.maxY();

        startNode_ = getNode(start_.x, start_.y, start_.z, start_.longHash());
        startNode_->cost = 0;
        startNode_->combinedCost = startNode_->estimatedCostToGoal;
        BinaryHeapOpenSet openSet;
        openSet.insert(startNode_);

        std::array<double, kCoefficients.size()> bestHeuristicSoFar{};
        for (std::size_t i = 0; i < kCoefficients.size(); ++i) {
            bestHeuristicSoFar[i] = startNode_->estimatedCostToGoal;
            bestSoFar_[i] = startNode_;
        }

        const bool slowPath = settings.slowPath;
        const auto startTime = Clock::now();
        const auto primaryTimeout = startTime + std::chrono::milliseconds(slowPath ? settings.slowPathTimeoutMS : primaryTimeoutMs);
        const auto failureTimeout = startTime + std::chrono::milliseconds(slowPath ? settings.slowPathTimeoutMS : failureTimeoutMs);
        const int maxChunkBorderFetch = settings.pathingMaxChunkBorderFetch;
        const double minimumImprovement = settings.minimumImprovementRepropagation ? kMinImprovement : 0;
        const bool isFavoring = !favoring_.empty();

        bool failing = true;
        int numNodes = 0;
        int numEmptyChunk = 0;
        MoveResult res;
        const auto& moves = allMoves();

        while (!openSet.empty() && numEmptyChunk < maxChunkBorderFetch && !cancelRequested_) {
            if ((numNodes & 63) == 0) {  // checking the clock every node is too slow
                auto now = Clock::now();
                if (now >= failureTimeout || (!failing && now >= primaryTimeout)) break;
            }
            if (slowPath) std::this_thread::sleep_for(std::chrono::milliseconds(settings.slowPathTimeDelayMS));

            PathNode* current = openSet.removeLowest();
            mostRecentConsidered_.store(current, std::memory_order_relaxed);
            numNodes++;
            if (goal_->isInGoal(current->x, current->y, current->z)) {
                return postProcess(Path::fromNodes(startNode_, current, goal_, numNodes, ctx), numNodes);
            }

            for (const Move& move : moves) {
                int newX = current->x + move.dx;
                int newZ = current->z + move.dz;
                if (((newX >> 4) != (current->x >> 4) || (newZ >> 4) != (current->z >> 4)) && !ctx.isLoaded(newX, newZ)) {
                    // only check other chunks; unloaded = edge of what we know
                    if (!move.dynamicXZ) numEmptyChunk++;
                    continue;
                }
                int newY = current->y + move.dy;
                if (newY >= maxY || newY < minY) continue;

                res.reset();
                move.apply(move, ctx, current->x, current->y, current->z, res);
                double actionCost = res.cost;
                if (actionCost >= costs::COST_INF) continue;
                if (actionCost <= 0 || std::isnan(actionCost)) {
                    throw std::logic_error(std::string(move.name) + " calculated an invalid cost");
                }
                if (!move.dynamicXZ && (res.x != newX || res.z != newZ)) {
                    throw std::logic_error(std::string(move.name) + " moved to an unexpected x/z");
                }
                if (!move.dynamicY && res.y != current->y + move.dy) {
                    throw std::logic_error(std::string(move.name) + " moved to an unexpected y");
                }
                int64_t hash = BlockPos::longHash(res.x, res.y, res.z);
                if (isFavoring) actionCost *= favoring_.calculate(hash);

                PathNode* neighbor = getNode(res.x, res.y, res.z, hash);
                double tentativeCost = current->cost + actionCost;
                if (neighbor->cost - tentativeCost > minimumImprovement) {
                    neighbor->previous = current;
                    neighbor->cost = tentativeCost;
                    neighbor->combinedCost = tentativeCost + neighbor->estimatedCostToGoal;
                    if (neighbor->isOpen()) openSet.update(neighbor);
                    else openSet.insert(neighbor);
                    for (std::size_t i = 0; i < kCoefficients.size(); ++i) {
                        double heuristic = neighbor->estimatedCostToGoal + neighbor->cost / kCoefficients[i];
                        if (bestHeuristicSoFar[i] - heuristic > minimumImprovement) {
                            bestHeuristicSoFar[i] = heuristic;
                            bestSoFar_[i] = neighbor;
                            if (failing && distFromStartSq(neighbor) > kMinDistPath * kMinDistPath) failing = false;
                        }
                    }
                }
            }
        }
        if (cancelRequested_) return {PathResultType::Cancellation, nullptr};
        if (auto best = bestSoFar(numNodes)) return postProcess(*best, numNodes);
        return {PathResultType::Failure, nullptr,
                std::format("{} noeuds, open set {}, bords de chunks {}", numNodes, openSet.empty() ? "vide" : "non vide",
                            numEmptyChunk),
                numNodes};
    } catch (const std::exception& e) {
        return {PathResultType::Exception, nullptr, e.what()};
    }
}

// AbstractNodeCostSearch.calculate: cut the path at the loaded chunks (cutoffAtLoadBoundary)
// and statically (pathCutoffFactor), then success to goal or segment.
PathCalculationResult AStarPathFinder::postProcess(PathPtr path, int numNodes) {
    if (cancelRequested_) return {PathResultType::Cancellation, nullptr};
    const Settings& s = Settings::get();
    if (s.cutoffAtLoadBoundary) path = path->cutoffAtLoadedChunks(*context_);
    path = path->staticCutoff(goal_.get(), s.pathCutoffFactor, s.pathCutoffMinimumLength);
    PathResultType type = goal_->isInGoal(path->dest()) ? PathResultType::SuccessToGoal : PathResultType::SuccessSegment;
    return {type, path, {}, numNodes};
}

std::optional<PathPtr> AStarPathFinder::bestSoFar(int numNodes) {
    if (!startNode_) return std::nullopt;
    for (PathNode* node : bestSoFar_) {
        if (!node) continue;
        if (distFromStartSq(node) > kMinDistPath * kMinDistPath) {
            return Path::fromNodes(startNode_, node, goal_, numNodes, *context_);
        }
    }
    // no path went further than kMinDistPath: we're stuck
    return std::nullopt;
}

std::vector<BlockPos> AStarPathFinder::walkBack(const PathNode* n) {
    std::vector<BlockPos> out;
    // previous pointers always lead to cheaper nodes, but they're read while the search
    // writes them: cap the walk anyway
    for (int i = 0; n && i < 100000; ++i, n = n->previous) out.push_back({n->x, n->y, n->z});
    std::reverse(out.begin(), out.end());
    return out;
}

std::vector<BlockPos> AStarPathFinder::bestPathSoFar() const {
    const PathNode* best = nullptr;
    for (PathNode* node : bestSoFar_) {
        if (node && distFromStartSq(node) > kMinDistPath * kMinDistPath) {
            best = node;
            break;
        }
    }
    return walkBack(best);
}

std::vector<BlockPos> AStarPathFinder::pathToMostRecentNodeConsidered() const {
    return walkBack(mostRecentConsidered_.load(std::memory_order_relaxed));
}

}  // namespace baritone
