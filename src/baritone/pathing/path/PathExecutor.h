#pragma once
#include <memory>
#include <optional>
#include <vector>

#include "baritone/pathing/calc/Path.h"
#include "baritone/pathing/movement/Movement.h"

namespace baritone {

// Follows one path segment, movement by movement (Baritone's PathExecutor).
class PathExecutor {
public:
    explicit PathExecutor(PathPtr path);

    // One game tick. Returns true when it is safe to switch to another path now.
    bool onTick(ExecContext& ctx);

    const PathPtr& path() const { return path_; }
    std::size_t position() const { return pathPosition_; }
    bool finished() const { return pathPosition_ >= path_->length(); }
    bool failed() const { return failed_; }
    std::optional<std::string> failReason() const { return failReason_; }
    Movement* currentMovement() const {
        return pathPosition_ < movements_.size() ? movements_[pathPosition_].get() : nullptr;
    }
    // Inputs/rotation to apply this tick (empty when paused or done).
    const MovementState& output() const { return output_; }
    double ticksRemainingFrom(std::size_t position) const;
    // Whether the player sprints next tick (Java isSprinting / shouldSprintNextTick).
    bool isSprinting() const { return sprintNextTick_; }
    // Java toBreak() / toPlace() / toWalkInto(): the blocks of the rest of the path (rendering)
    const std::vector<BlockPos>& toBreak() const { return toBreak_; }
    const std::vector<BlockPos>& toPlace() const { return toPlace_; }
    const std::vector<BlockPos>& toWalkInto() const { return toWalkInto_; }

    // Regardless of the current path position, jump to the player's feet if they are on the
    // path (Java snipsnapifpossible).
    bool snipsnapIfPossible(ExecContext& ctx);
    // Splices `next` onto this path when it starts where this one ends, else trims the
    // history (Java trySplice / cutIfTooLong). Returns the executor to keep using.
    static std::unique_ptr<PathExecutor> trySplice(std::unique_ptr<PathExecutor> current, const PathExecutor* next);

private:
    void cancel(std::string reason);
    void clearKeys() { output_ = MovementState{}; }
    void onChangeInPathPosition();
    void updateBlockSets(ExecContext& ctx);
    std::pair<double, BlockPos> closestPathPos(ExecContext& ctx);
    bool possiblyOffPath(ExecContext& ctx, const std::pair<double, BlockPos>& status, double leniency);
    bool shouldPause(ExecContext& ctx);
    bool shouldSprintNextTick(ExecContext& ctx, bool requested);
    std::optional<std::pair<Vec3d, BlockPos>> overrideFall(ExecContext& ctx, Movement* fall);
    static std::unique_ptr<PathExecutor> cutIfTooLong(std::unique_ptr<PathExecutor> current);

    PathPtr path_;
    std::vector<std::unique_ptr<Movement>> movements_;
    std::size_t pathPosition_ = 0;
    int ticksAway_ = 0;
    int ticksOnCurrent_ = 0;
    std::optional<std::size_t> costEstimateIndex_;
    double currentMovementOriginalCostEstimate_ = 0;
    bool failed_ = false;
    bool sprintNextTick_ = false;
    std::optional<std::string> failReason_;
    MovementState output_;
    std::vector<BlockPos> toBreak_, toPlace_, toWalkInto_;
    int depth_ = 0;  // re-entrant onTick calls in one tick

    static constexpr double kMaxMaxDistFromPath = 3;
    static constexpr double kMaxDistFromPath = 2;
    static constexpr int kMaxTicksAway = 200;
};

}  // namespace baritone
