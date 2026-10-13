#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "baritone/pathing/calc/Path.h"
#include "baritone/pathing/calc/PathNode.h"
#include "baritone/pathing/movement/CalculationContext.h"

namespace baritone {

// Cost multipliers for positions (Baritone's Favoring): the previous path is cheaper so the
// bot doesn't flip between equivalent paths; mobs/spawners can be made more expensive.
class Favoring {
public:
    Favoring() = default;
    Favoring(const PathPtr& previous, double backtrackCoefficient);
    void avoidSphere(const BlockPos& center, int radius, double coefficient);
    double calculate(int64_t hash) const {
        if (favorings_.empty()) return 1.0;
        auto it = favorings_.find(hash);
        return it == favorings_.end() ? 1.0 : it->second;
    }
    bool empty() const { return favorings_.empty(); }

private:
    std::unordered_map<int64_t, double> favorings_;
};

enum class PathResultType { SuccessToGoal, SuccessSegment, Failure, Cancellation, Exception };

struct PathCalculationResult {
    PathResultType type;
    PathPtr path;
    std::string error;  // Exception: what went wrong
    int nodes = 0;
};

// A* search (Baritone's AbstractNodeCostSearch + AStarPathFinder). One instance per
// calculation, run on a worker thread; cancel() and the render getters are thread-safe.
class AStarPathFinder {
public:
    AStarPathFinder(BlockPos start, GoalPtr goal, std::unique_ptr<CalculationContext> context, Favoring favoring);
    ~AStarPathFinder();

    PathCalculationResult calculate(long primaryTimeoutMs, long failureTimeoutMs);
    void cancel() { cancelRequested_ = true; }
    bool isFinished() const { return finished_; }

    const GoalPtr& goal() const { return goal_; }
    const BlockPos& start() const { return start_; }

    // For rendering the calculation in progress (copies, thread-safe).
    std::vector<BlockPos> bestPathSoFar() const;
    std::vector<BlockPos> pathToMostRecentNodeConsidered() const;

    static constexpr std::array<double, 7> kCoefficients{1.5, 2, 2.5, 3, 4, 5, 10};
    static constexpr int kMinDistPath = 5;
    static constexpr double kMinImprovement = 0.01;

private:
    PathNode* getNode(int x, int y, int z, int64_t hash);
    std::optional<PathPtr> bestSoFar(int numNodes);
    PathCalculationResult postProcess(PathPtr path, int numNodes);
    double distFromStartSq(const PathNode* n) const;
    static std::vector<BlockPos> walkBack(const PathNode* n);

    BlockPos start_;
    GoalPtr goal_;
    std::unique_ptr<CalculationContext> context_;
    Favoring favoring_;

    // Nodes live in fixed-size blocks so pointers stay valid.
    std::vector<std::unique_ptr<PathNode[]>> blocks_;
    std::size_t blockUsed_ = 0;
    static constexpr std::size_t kBlockSize = 4096;
    std::unordered_map<int64_t, PathNode*> map_;

    PathNode* startNode_ = nullptr;
    std::array<PathNode*, kCoefficients.size()> bestSoFar_{};
    std::atomic<PathNode*> mostRecentConsidered_{nullptr};
    std::atomic<bool> cancelRequested_{false};
    std::atomic<bool> finished_{false};
};

}  // namespace baritone
