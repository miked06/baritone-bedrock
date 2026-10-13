#pragma once
#include <bitset>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

#include "baritone/api/PlayerContext.h"
#include "baritone/pathing/calc/Path.h"
#include "baritone/pathing/movement/CalculationContext.h"
#include "baritone/utils/RayTrace.h"

namespace baritone {

class InventoryBehavior;

enum class MovementStatus { Prepping, Waiting, Running, Success, Unreachable, Failed, Canceled };

inline bool isComplete(MovementStatus s) {
    return s == MovementStatus::Success || s == MovementStatus::Unreachable || s == MovementStatus::Failed ||
           s == MovementStatus::Canceled;
}

enum class Input { MoveForward, MoveBack, MoveLeft, MoveRight, ClickLeft, ClickRight, Jump, Sneak, Sprint, Count };

struct MovementTarget {
    std::optional<Rotation> rotation;
    bool forceRotations = false;
};

class MovementState {
public:
    MovementStatus status = MovementStatus::Prepping;
    MovementTarget target;
    std::bitset<static_cast<std::size_t>(Input::Count)> inputs;

    MovementState& setStatus(MovementStatus s) {
        status = s;
        return *this;
    }
    MovementState& setTarget(MovementTarget t) {
        target = t;
        return *this;
    }
    MovementState& setInput(Input i, bool forced) {
        inputs.set(static_cast<std::size_t>(i), forced);
        return *this;
    }
    bool input(Input i) const { return inputs.test(static_cast<std::size_t>(i)); }
};

// What a movement needs to act, rebuilt every tick by the PathExecutor.
struct ExecContext {
    PlayerContext& player;
    CalculationContext& calc;  // over the live snapshot (refreshed around the player each tick)
    InventoryBehavior& inventory;
    BlockPos pathStart;  // PathingBehavior.pathStart()
    // best path so far of the calculation in progress (PathExecutor.shouldPause), if any
    const std::vector<BlockPos>* inProgressBest = nullptr;

    BlockStateInterface& bsi() { return calc.bsi; }
    BlockPos playerFeet();
    Vec3d playerHead() const { return player.eyePosition(); }
    Rotation playerRotations() const { return player.rotation(); }
    const BlockProps& props(const BlockPos& p) { return calc.props(p.x, p.y, p.z); }
    bool isLookingAt(const BlockPos& pos);
    std::optional<BlockHit> objectMouseOver();

    bool canWalkThrough(const BlockPos& p);
    bool canWalkOn(const BlockPos& p);
    bool isLiquid(const BlockPos& p) { return props(p).hasFluid(); }
};

namespace movement_helper {

enum class PlaceResult { ReadyToPlace, Attempting, NoOption };

void moveTowards(ExecContext& ctx, MovementState& state, const BlockPos& pos);
// Movement keys (forward/back/left/right) closest to idealYaw without turning (MovementOption).
void moveTowardsWithoutRotation(ExecContext& ctx, MovementState& state, float idealYaw);
// Turns at most 22.5 degrees, the keys do the rest (Java moveTowardsWithSlightRotation).
void moveTowardsWithSlightRotation(ExecContext& ctx, MovementState& state, const BlockPos& dest);
// The blocks under the player's bounding box (Java MovementHelper.steppingOnBlocks).
std::vector<BlockPos> steppingOnBlocks(ExecContext& ctx);
// Sneak on magma with allowWalkOnMagmaBlocks.
bool steppingOnMagma(ExecContext& ctx);
// RotationUtils.reachable: a rotation that makes the crosshair hit `pos`.
// reach < 0: the player's block reach distance
std::optional<Rotation> reachable(ExecContext& ctx, const BlockPos& pos, bool wouldSneak = false, double reach = -1);
PlaceResult attemptToPlaceABlock(ExecContext& ctx, MovementState& state, const BlockPos& placeAt, bool preferDown,
                                 bool wouldSneak);
void switchToBestToolFor(ExecContext& ctx, const BlockProps& block);
// The game refuses a block that would intersect the player's bounding box.
bool intersectsPlayer(const PlayerContext& player, const BlockPos& pos);

}  // namespace movement_helper

// One step of a path being executed (Baritone's Movement and subclasses).
class Movement {
public:
    Movement(MovementType type, BlockPos src, BlockPos dest, std::vector<BlockPos> toBreak,
             std::optional<BlockPos> toPlace);
    virtual ~Movement() = default;

    static std::unique_ptr<Movement> create(const MovementSpec& spec);

    MovementStatus update(ExecContext& ctx);
    MovementType type() const { return type_; }
    const BlockPos& src() const { return src_; }
    const BlockPos& dest() const { return dest_; }
    BlockPos direction() const { return dest_ - src_; }

    double getCost() const { return cost_; }
    void overrideCost(double cost) { cost_ = cost; }
    double calculateCost(CalculationContext& ctx) const;
    double recalculateCost(CalculationContext& ctx) {
        cost_ = calculateCost(ctx);
        return cost_;
    }

    const std::unordered_set<BlockPos>& validPositions();
    bool playerInValidPosition(ExecContext& ctx);
    // Java Movement.safeToCancel: true unless the movement says otherwise
    virtual bool safeToCancel(ExecContext&) const { return true; }
    // MovementDescend only (PathExecutor's sprint logic)
    virtual bool descendSafeMode(ExecContext&) { return false; }
    virtual bool descendSkipToAscend(ExecContext&) { return false; }
    virtual void forceSafeMode() {}
    bool isFinished() const { return isComplete(currentState_.status); }
    virtual void reset();

    std::vector<BlockPos> toBreak(CalculationContext& ctx);
    // Every position this movement may break, broken already or not (toBreakAll).
    const std::vector<BlockPos>& toBreakAll() const { return positionsToBreak_; }
    std::vector<BlockPos> toPlace(CalculationContext& ctx);
    virtual std::vector<BlockPos> toWalkInto(CalculationContext&) { return {}; }

    // The state produced by the last update: inputs and rotation to apply this tick.
    const MovementState& state() const { return lastOutput_; }
    bool calculatedWhileLoaded = true;

protected:
    virtual MovementState& updateState(ExecContext& ctx, MovementState& state);
    virtual bool prepared(ExecContext& ctx, MovementState& state);
    virtual std::unordered_set<BlockPos> calculateValidPositions() const = 0;

    MovementType type_;
    BlockPos src_, dest_;
    std::vector<BlockPos> positionsToBreak_;
    std::optional<BlockPos> positionToPlace_;
    double cost_ = costs::COST_INF;
    MovementState currentState_;
    MovementState lastOutput_;

private:
    std::optional<std::unordered_set<BlockPos>> validPositions_;
};

}  // namespace baritone
