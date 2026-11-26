#include "baritone/pathing/movement/Movement.h"

#include <algorithm>
#include <numbers>
#include <cmath>
#include <iterator>
#include <ranges>

#include "baritone/Settings.h"
#include "baritone/utils/DebugLog.h"
#include "baritone/behavior/InventoryBehavior.h"
#include "baritone/pathing/movement/MovementHelper.h"
#include "baritone/pathing/movement/CornerJump.h"

namespace baritone {

using namespace costs;
using T = BlockType;
namespace ru = rotation_utils;

namespace {
// In the ladder we jumped to: its column, at its height or already higher (the jump can carry
// the feet into the cell above, and pushing into the wall keeps climbing).
bool holdingLadder(ExecContext& ctx, const BlockPos& dest) {
    const BlockPos feet = ctx.playerFeet();
    return feet.x == dest.x && feet.z == dest.z && feet.y >= dest.y && feet.y <= dest.y + 2 &&
           helper::isClimbable(ctx.props(feet)) && helper::isClimbable(ctx.props(dest));
}

// Bedrock addition: getting ready to jump from the thin top of a ladder (LadderHop, and an
// Ascend from it). Takes off from the end of the plate on the destination's side, the feet past
// the ladder's cell: with the feet above it, the Bedrock client takes the jump for a climb (0.2
// up, no boost) while the server jumps, and corrects us in mid-air. Towards a ladder further from
// the wall, or up into one on the same wall (the body has to pass beside its plate), from the
// outer corner of the plate. Sneaking stops us at its end. The client also ignores the sneaking
// there and gets to the end long before the server: keep pushing until the server is there too,
// then stand still while a correction of the steps arrives (and the sprint jump does not start
// as the sneaking stops).
class LadderTakeoff {
public:
    void reset() {
        settled_ = pushed_ = stuck_ = 0;
        lastPos_.reset();
    }

    // false while getting there (the state holds the inputs); true: jump now
    bool ready(ExecContext& ctx, MovementState& state, const BlockPos& src, const BlockPos& dest, float jumpYaw) {
        const auto wall = helper::ladderWall(ctx.props(src.below()));
        if (!wall) return true;
        const Vec3d pos = ctx.player.position();
        const BlockPos d = dest - src;
        double tx = d.x - (d.x * wall->x + d.z * wall->z) * wall->x;
        double tz = d.z - (d.x * wall->x + d.z * wall->z) * wall->z;
        if (std::hypot(tx, tz) < 1e-3) {  // straight out from (or into) the wall: either end
            tx = -wall->z;
            tz = wall->x;
        }
        const double along = std::hypot(tx, tz);
        const double offset = ((pos.x - src.x - 0.5) * tx + (pos.z - src.z - 0.5) * tz) / along;
        const double towardsWall = (pos.x - src.x - 0.5) * wall->x + (pos.z - src.z - 0.5) * wall->z;
        const auto destWall = helper::ladderWall(ctx.props(dest));
        const bool besidePlate = dest.y > src.y && destWall && *destWall == *wall;
        const bool awayFromWall = besidePlate || d.x * wall->x + d.z * wall->z < 0;
        const bool reached = offset >= 0.6 && (!awayFromWall || towardsWall <= 0.08);
        const bool moved = lastPos_ && std::hypot(pos.x - lastPos_->x, pos.z - lastPos_->z) >= 0.003;
        lastPos_ = pos;
        const float wallYaw = static_cast<float>(std::atan2(-wall->x, wall->z) * 180 / std::numbers::pi);
        const bool facingWall = std::abs(std::remainder(ctx.playerRotations().yaw - wallYaw, 360.f)) <= 10;
        // stopped short of it (something along the wall): as far as it goes, until we move again
        if (moved) stuck_ = 0;
        else if (!reached && facingWall && pushed_ == 0) ++stuck_;
        if (reached || stuck_ >= 10) ++pushed_;
        else if (pushed_ > 0 && pushed_ < kPushTicks) pushed_ = 0;  // pulled back
        if (pushed_ < kPushTicks) {
            settled_ = 0;
            state.setTarget({Rotation{wallYaw, ctx.playerRotations().pitch}, false});
            state.setInput(Input::Sneak, true);
            // turned to the wall first: the keys move us relative to where we look
            if (!facingWall) return false;
            // only sideways (and away from the wall when needed): pushing into the wall on a
            // ladder top climbs it in Bedrock
            if (awayFromWall) state.setInput(Input::MoveBack, true);
            // the destination side, seen facing the wall: right is (-wall.z, wall.x)
            const double right = -wall->z * tx + wall->x * tz;
            state.setInput(right > 0 ? Input::MoveRight : Input::MoveLeft, true);
            return false;
        }
        if (!reached && stuck_ < 10) {  // corrected back by the server: again
            pushed_ = 0;
            return false;
        }
        settled_ = moved ? 0 : settled_ + 1;
        return settled_ >= kSettleTicks && std::abs(std::remainder(ctx.playerRotations().yaw - jumpYaw, 360.f)) <= 15;
    }

private:
    static constexpr int kSettleTicks = 12;  // the server corrects a gap over 0.3 that lasts 500 ms
    static constexpr int kPushTicks = 16;    // the server sneaks the length of the plate meanwhile
    int settled_ = 0, pushed_ = 0, stuck_ = 0;
    std::optional<Vec3d> lastPos_;
};
}  // namespace

// ===========================================================================
// ExecContext
// ===========================================================================

BlockPos ExecContext::playerFeet() {
    Vec3d p = player.position();
    BlockPos feet{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y + 0.1251)),
                  static_cast<int>(std::floor(p.z))};
    // standing on top of a bottom slab counts as the block above
    const auto& support = props(feet);
    if (support.type == T::Slab && !support.doubleSlab) return feet.above();
    // on top of a horizontal end rod or a candle: the node is the block above, as for slabs
    if ((support.isHorizontalRod() || support.isCandle()) && std::abs(p.y - (feet.y + support.height)) < 0.02 &&
        player.velocity().y <= 0.01) return feet.above();
    return feet;
}

std::optional<BlockHit> ExecContext::objectMouseOver() {
    return rayTrace(bsi(), playerHead(), playerRotations(), player.blockReachDistance());
}

bool ExecContext::isLookingAt(const BlockPos& pos) {
    auto hit = objectMouseOver();
    return hit && hit->pos == pos;
}

bool ExecContext::canWalkThrough(const BlockPos& p) {
    return helper::canWalkThrough(calc, p.x, p.y, p.z);
}

bool ExecContext::canWalkOn(const BlockPos& p) {
    return helper::canWalkOn(calc, p.x, p.y, p.z);
}

// ===========================================================================
// MovementHelper (execution side)
// ===========================================================================

namespace movement_helper {

// Where to stand in a block: its middle, or against the wall when it is the thin top of a
// ladder (3 pixels against the block it hangs on: from the middle we would fall through).
Vec3d standPoint(ExecContext& ctx, const BlockPos& pos) {
    Vec3d point = Vec3d::center(pos);
    if (helper::isClimbable(ctx.props(pos))) return point;
    if (auto wall = helper::ladderWall(ctx.props(pos.below()))) {
        point.x += wall->x * 0.12;
        point.z += wall->z * 0.12;
    }
    return point;
}

void moveTowards(ExecContext& ctx, MovementState& state, const BlockPos& pos) {
    Rotation r = ru::calcRotationFromVec3d(ctx.playerHead(), standPoint(ctx, pos), ctx.playerRotations())
                     .withPitch(ctx.playerRotations().pitch);
    state.setTarget({r, false}).setInput(Input::MoveForward, true);
}

void moveTowardsWithoutRotation(ExecContext& ctx, MovementState& state, float idealYaw) {
    constexpr float kDegToRad = 3.14159265358979f / 180.f;
    constexpr float kSprint = 1.3f;
    float yaw = ctx.playerRotations().yaw;
    float mx = std::sin(yaw * kDegToRad), mz = std::cos(yaw * kDegToRad);
    bool sprint = Settings::get().allowSprint;
    float fx = sprint ? mx * kSprint : mx, fz = sprint ? mz * kSprint : mz;
    struct Option {
        Input a;
        std::optional<Input> b;
        float x, z;
    };
    const Option options[] = {
        {Input::MoveForward, std::nullopt, fx, fz},
        {Input::MoveBack, std::nullopt, -mx, -mz},
        {Input::MoveLeft, std::nullopt, -mz, mx},
        {Input::MoveRight, std::nullopt, mz, -mx},
        {Input::MoveForward, Input::MoveLeft, fx - mz, fz + mx},
        {Input::MoveForward, Input::MoveRight, fx + mz, fz - mx},
        {Input::MoveBack, Input::MoveLeft, -mx - mz, -mz + mx},
        {Input::MoveBack, Input::MoveRight, -mx + mz, -mz - mx},
    };
    float ix = std::sin(idealYaw * kDegToRad), iz = std::cos(idealYaw * kDegToRad);
    const Option* best = nullptr;
    float bestDist = 0;
    for (const Option& o : options) {
        float d = std::fabs(o.x - ix) + std::fabs(o.z - iz);
        if (!best || d < bestDist) {
            best = &o;
            bestDist = d;
        }
    }
    state.setInput(best->a, true);
    if (best->b) state.setInput(*best->b, true);
}

void moveTowardsWithSlightRotation(ExecContext& ctx, MovementState& state, const BlockPos& dest) {
    float idealYaw = ru::calcRotationFromVec3d(ctx.playerHead(), standPoint(ctx, dest), ctx.playerRotations()).yaw;
    float yaw = ctx.playerRotations().yaw;
    float distance = std::fmod(Rotation::yawDistanceFromOffset(yaw, idealYaw), 45.f);
    float newYaw = distance > 0.f ? (distance > 22.5f ? distance - 45.f : distance)
                                  : (distance < -22.5f ? distance + 45.f : distance);
    state.setTarget({Rotation{yaw - newYaw, ctx.playerRotations().pitch}, true});
    moveTowardsWithoutRotation(ctx, state, idealYaw);
}

std::vector<BlockPos> steppingOnBlocks(ExecContext& ctx) {
    std::vector<BlockPos> out;
    Vec3d p = ctx.player.position();
    BlockPos at{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z))};
    constexpr double half = 0.3;
    for (int x = -1; x <= 1; ++x)
        for (int z = -1; z <= 1; ++z) {
            double minX = at.x + x, minZ = at.z + z;
            if (p.x - half < minX + 1 && p.x + half > minX && p.z - half < minZ + 1 && p.z + half > minZ) {
                out.push_back({at.x + x, at.y - 1, at.z + z});
            }
        }
    return out;
}

bool steppingOnMagma(ExecContext& ctx) {
    if (!Settings::get().allowWalkOnMagmaBlocks) return false;
    for (const BlockPos& b : steppingOnBlocks(ctx))
        if (ctx.props(b).type == T::Magma) return true;
    return false;
}

namespace {

std::optional<Rotation> reachableOffset(ExecContext& ctx, const BlockPos& pos, const Vec3d& offsetPos, bool wouldSneak,
                                        double reach) {
    Vec3d eyes = ctx.playerHead();
    if (wouldSneak) eyes.y -= 0.3;  // TODO(bedrock): measure the sneaking eye height
    Rotation rotation = ru::calcRotationFromVec3d(eyes, offsetPos, ctx.playerRotations());
    auto hit = rayTrace(ctx.bsi(), eyes, rotation, reach < 0 ? ctx.player.blockReachDistance() : reach);
    if (hit) {
        if (hit->pos == pos) return rotation;
        if (ctx.props(pos).type == T::Fire && hit->pos == pos.below()) return rotation;
    }
    return std::nullopt;
}

}  // namespace

std::optional<Rotation> reachable(ExecContext& ctx, const BlockPos& pos, bool wouldSneak, double reach) {
    // Java remainWithExistingLookDirection: already looking at it, keep the look (not when
    // sneaking would move the eyes: then a rotation is computed from the sneaking eyes)
    if (Settings::get().remainWithExistingLookDirection && !wouldSneak && ctx.isLookingAt(pos)) {
        Rotation current = ctx.playerRotations();
        return Rotation{current.yaw, current.pitch + 0.0001f};
    }
    if (auto r = reachableOffset(ctx, pos, blockCenter(ctx.bsi(), pos), wouldSneak, reach)) return r;
    auto box = outlineBox(ctx.props(pos)).value_or(LocalBox{0, 0, 0, 1, 1, 1});
    static constexpr double sides[6][3] = {{0.5, 0, 0.5}, {0.5, 1, 0.5}, {0.5, 0.5, 0},
                                           {0.5, 0.5, 1}, {0, 0.5, 0.5}, {1, 0.5, 0.5}};
    for (const auto& s : sides) {
        double x = box.minX * s[0] + box.maxX * (1 - s[0]);
        double y = box.minY * s[1] + box.maxY * (1 - s[1]);
        double z = box.minZ * s[2] + box.maxZ * (1 - s[2]);
        if (auto r = reachableOffset(ctx, pos, {pos.x + x, pos.y + y, pos.z + z}, wouldSneak, reach)) return r;
    }
    return std::nullopt;
}

bool intersectsPlayer(const PlayerContext& player, const BlockPos& b) {
    constexpr double half = 0.3, height = 1.8;
    Vec3d p = player.position();
    return p.x - half < b.x + 1 && p.x + half > b.x && p.y < b.y + 1 && p.y + height > b.y && p.z - half < b.z + 1 &&
           p.z + half > b.z;
}

PlaceResult attemptToPlaceABlock(ExecContext& ctx, MovementState& state, const BlockPos& placeAt, bool preferDown,
                                 bool wouldSneak) {
    bool found = false;
    if (auto direct = reachable(ctx, placeAt, wouldSneak)) {
        state.setTarget({*direct, true});
        found = true;
    }
    for (Direction d : kPlaceAgainstDirs) {
        BlockPos against = placeAt.offset(d);
        if (!helper::canPlaceAgainst(ctx.props(against))) continue;
        if (!ctx.inventory.selectThrowawayForLocation(false, placeAt.x, placeAt.y, placeAt.z)) {
            state.setStatus(MovementStatus::Unreachable);  // no blocks to place
            return PlaceResult::NoOption;
        }
        Vec3d face{(placeAt.x + against.x + 1.0) * 0.5, (placeAt.y + against.y + 0.5) * 0.5,
                   (placeAt.z + against.z + 1.0) * 0.5};
        Vec3d eyes = ctx.playerHead();
        if (wouldSneak) eyes.y -= 0.3;
        Rotation place = ru::calcRotationFromVec3d(eyes, face, ctx.playerRotations());
        auto hit = rayTrace(ctx.bsi(), eyes, place, ctx.player.blockReachDistance());
        if (hit && hit->pos == against && hit->pos.offset(hit->face) == placeAt) {
            state.setTarget({place, true});
            found = true;
            if (!preferDown) break;  // preferDown: keep the last option (down)
        }
    }
    if (auto hit = ctx.objectMouseOver()) {
        if (hit->pos == placeAt ||
            (helper::canPlaceAgainst(ctx.props(hit->pos)) && hit->pos.offset(hit->face) == placeAt)) {
            if (wouldSneak) state.setInput(Input::Sneak, true);
            ctx.inventory.selectThrowawayForLocation(true, placeAt.x, placeAt.y, placeAt.z);
            return PlaceResult::ReadyToPlace;
        }
    }
    if (found) {
        if (wouldSneak) state.setInput(Input::Sneak, true);
        ctx.inventory.selectThrowawayForLocation(true, placeAt.x, placeAt.y, placeAt.z);
        return PlaceResult::Attempting;
    }
    return PlaceResult::NoOption;
}

void switchToBestToolFor(ExecContext& ctx, const BlockProps& block) {
    ctx.inventory.switchToBestToolFor(block);
}

}  // namespace movement_helper

namespace mh = movement_helper;

// ===========================================================================
// Movement base
// ===========================================================================

Movement::Movement(MovementType type, BlockPos src, BlockPos dest, std::vector<BlockPos> toBreak,
                   std::optional<BlockPos> toPlace)
    : type_(type), src_(src), dest_(dest), positionsToBreak_(std::move(toBreak)), positionToPlace_(toPlace) {}

double Movement::calculateCost(CalculationContext& ctx) const {
    MoveResult res;
    switch (type_) {
        case MovementType::Traverse: return movement_costs::traverse(ctx, src_.x, src_.y, src_.z, dest_.x, dest_.z);
        case MovementType::Ascend: return movement_costs::ascend(ctx, src_.x, src_.y, src_.z, dest_.x, dest_.z);
        case MovementType::Descend:
        case MovementType::Fall:
            movement_costs::descend(ctx, src_.x, src_.y, src_.z, dest_.x, dest_.z, res);
            return (res.x == dest_.x && res.y == dest_.y && res.z == dest_.z) ? res.cost : COST_INF;
        case MovementType::Diagonal:
            movement_costs::diagonal(ctx, src_.x, src_.y, src_.z, dest_.x, dest_.z, res);
            return res.y == dest_.y ? res.cost : COST_INF;
        case MovementType::Pillar: return movement_costs::pillar(ctx, src_.x, src_.y, src_.z);
        case MovementType::Downward: return movement_costs::downward(ctx, src_.x, src_.y, src_.z);
        case MovementType::ParkourCorner: return COST_INF;
        case MovementType::ParkourShulker: return movement_costs::shulkerClimb(ctx, src_, dest_);
        case MovementType::LadderHop: return movement_costs::ladderHop(ctx, src_, dest_);
        case MovementType::LadderShimmy: return movement_costs::ladderShimmy(ctx, src_, dest_);
        case MovementType::MomentumJump: {
            auto jump = movement_costs::momentumJump(ctx, src_, dest_);
            return jump ? jump->cost : COST_INF;
        }
        case MovementType::EdgeWalk: {
            auto walk = movement_costs::edgeWalk(ctx, src_, dest_);
            return walk ? walk->cost : COST_INF;
        }
        case MovementType::Parkour: {
            if (auto jump = movement_costs::parkourJump(ctx, src_, dest_)) return jump->cost;
            BlockPos d = direction();
            if (d.x != 0 && d.z != 0) {
                movement_costs::parkourDiagonal(ctx, src_.x, src_.y, src_.z, d.x > 0 ? 1 : -1, d.z > 0 ? 1 : -1, res);
                return (res.x == dest_.x && res.y == dest_.y && res.z == dest_.z) ? res.cost : COST_INF;
            }
            Direction dir = d.x > 0 ? Direction::East : d.x < 0 ? Direction::West : d.z > 0 ? Direction::South
                                                                                              : Direction::North;
            movement_costs::parkour(ctx, src_.x, src_.y, src_.z, dir, res);
            if (res.x == dest_.x && res.y == dest_.y && res.z == dest_.z) return res.cost;
            res.reset();
            movement_costs::ladderJump(ctx, src_.x, src_.y, src_.z, dir, res);
            return (res.x == dest_.x && res.y == dest_.y && res.z == dest_.z) ? res.cost : COST_INF;
        }
    }
    return COST_INF;
}

const std::unordered_set<BlockPos>& Movement::validPositions() {
    if (!validPositions_) validPositions_ = calculateValidPositions();
    return *validPositions_;
}

bool Movement::playerInValidPosition(ExecContext& ctx) {
    return validPositions().contains(ctx.playerFeet()) || validPositions().contains(ctx.pathStart);
}

void Movement::reset() {
    currentState_ = MovementState{};
}

std::vector<BlockPos> Movement::toBreak(CalculationContext& c) {
    std::vector<BlockPos> result;
    for (const BlockPos& p : positionsToBreak_)
        if (!helper::canWalkThrough(c, p.x, p.y, p.z)) result.push_back(p);
    return result;
}

std::vector<BlockPos> Movement::toPlace(CalculationContext& c) {
    std::vector<BlockPos> result;
    if (positionToPlace_ && !helper::canWalkOn(c, positionToPlace_->x, positionToPlace_->y, positionToPlace_->z)) {
        result.push_back(*positionToPlace_);
    }
    return result;
}

MovementStatus Movement::update(ExecContext& ctx) {
    MovementState state = currentState_;
    state.inputs.reset();
    state.target = {};
    updateState(ctx, state);
    if (ctx.isLiquid(ctx.playerFeet()) && ctx.player.position().y < dest_.y + 0.6) {
        state.setInput(Input::Jump, true);  // swim up
    }
    // Escape a solid block around the player's eyes only when the hit can be mined.
    if (ctx.calc.allowBreak || !ctx.calc.allowBreakAnyway.empty()) {
        Vec3d eyes = ctx.playerHead();
        BlockPos eyeBlock{static_cast<int>(std::floor(eyes.x)), static_cast<int>(std::floor(eyes.y)),
                          static_cast<int>(std::floor(eyes.z))};
        const BlockProps& in = ctx.props(eyeBlock);
        if (helper::isBlockNormalCube(in) && !in.hasFluid()) {
            if (auto hit = ctx.objectMouseOver(); hit &&
                ctx.calc.breakCostMultiplierAt(hit->pos.x, hit->pos.y, hit->pos.z, ctx.props(hit->pos)) < COST_INF) {
                mh::switchToBestToolFor(ctx, ctx.props(hit->pos));
                state.setInput(Input::ClickLeft, true);
            }
        }
    }
    currentState_ = state;
    lastOutput_ = state;
    if (isComplete(state.status)) lastOutput_.inputs.reset();
    return state.status;
}

bool Movement::prepared(ExecContext& ctx, MovementState& state) {
    if (state.status == MovementStatus::Waiting) return true;
    const auto obstruction = std::ranges::find_if(positionsToBreak_, [&](const BlockPos& pos) {
        return !ctx.canWalkThrough(pos);
    });
    if (obstruction == positionsToBreak_.end()) return true;
    const BlockPos& pos = *obstruction;
    const BlockProps& block = ctx.props(pos);
    if (ctx.calc.breakCostMultiplierAt(pos.x, pos.y, pos.z, block) >= COST_INF) return false;

    // Reading the native entity list is expensive, so only check while preparing to mine.
    if (Settings::get().pauseMiningForFallingBlocks) {
        const auto miningCells = std::ranges::subrange(positionsToBreak_.begin(), std::next(obstruction));
        for (const Vec3d& f : ctx.player.fallingBlockPositions()) {
            for (const BlockPos& target : miningCells) {
                if (f.x + 0.49 > target.x && f.x - 0.49 < target.x + 1 &&
                    f.z + 0.49 > target.z && f.z - 0.49 < target.z + 1 &&
                    f.y + 0.98 > target.y && f.y < target.y + 1.1) return false;
            }
        }
    }
    mh::switchToBestToolFor(ctx, block);
    if (auto reach = mh::reachable(ctx, pos)) {
        state.setTarget({*reach, true});
        if (ctx.isLookingAt(pos) || ctx.playerRotations().isReallyCloseTo(*reach)) {
            state.setInput(Input::ClickLeft, true);
        }
        return false;
    }
    // Aim at the center when no face is visible yet.
    state.setTarget({ru::calcRotationFromVec3d(ctx.playerHead(), Vec3d::center(pos), ctx.playerRotations()), true});
    state.setInput(Input::ClickLeft, true);
    return false;
}

MovementState& Movement::updateState(ExecContext& ctx, MovementState& state) {
    if (!prepared(ctx, state)) return state.setStatus(MovementStatus::Prepping);
    if (state.status == MovementStatus::Prepping) state.setStatus(MovementStatus::Waiting);
    if (state.status == MovementStatus::Waiting) state.setStatus(MovementStatus::Running);
    return state;
}

// ===========================================================================
// Traverse
// ===========================================================================

class MovementTraverse : public Movement {
public:
    MovementTraverse(BlockPos src, BlockPos dest)
        : Movement(MovementType::Traverse, src, dest, {dest.above(), dest}, dest.below()) {}

    bool safeToCancel(ExecContext& ctx) const override {
        // safe while breaking blocks or when the bridge block is already there
        return currentState_.status != MovementStatus::Running || ctx.canWalkOn(dest_.below());
    }

protected:
    std::unordered_set<BlockPos> calculateValidPositions() const override { return {src_, dest_}; }

    bool prepared(ExecContext& ctx, MovementState& state) override {
        BlockPos feet = ctx.playerFeet();
        if (feet == src_ || feet == src_.below()) {
            if (helper::isClimbable(ctx.props(src_.below()))) state.setInput(Input::Sneak, true);
        }
        return Movement::prepared(ctx, state);
    }

    MovementState& updateState(ExecContext& ctx, MovementState& state) override {
        Movement::updateState(ctx, state);
        const BlockProps& pb0 = ctx.props(positionsToBreak_[0]);
        const BlockProps& pb1 = ctx.props(positionsToBreak_[1]);
        Vec3d pos = ctx.player.position();
        if (state.status != MovementStatus::Running) {
            // walk while breaking the blocks in front
            if (!Settings::get().walkWhileBreaking || state.status != MovementStatus::Prepping) return state;
            if (helper::avoidWalkingInto(pb0) || helper::avoidWalkingInto(pb1)) return state;
            double dist = std::max(std::fabs(pos.x - (dest_.x + 0.5)), std::fabs(pos.z - (dest_.z + 0.5)));
            if (dist < 0.83) return state;
            if (!state.target.rotation) return state;
            float yawToDest = ru::calcRotationFromVec3d(ctx.playerHead(), blockCenter(ctx.bsi(), dest_), ctx.playerRotations()).yaw;
            float pitchToBreak = state.target.rotation->pitch;
            if (helper::isBlockNormalCube(pb0) || (pb0.type == T::Air && (helper::isBlockNormalCube(pb1) || pb1.type == T::Air))) {
                pitchToBreak = 26;  // break efficiently at this angle until we're against the block
            }
            return state.setTarget({Rotation{yawToDest, pitchToBreak}, true})
                .setInput(Input::MoveForward, true)
                .setInput(Input::Sprint, true);
        }

        const BlockProps& fd = ctx.props(src_.below());
        bool ladder = helper::isClimbable(fd);

        state.setInput(Input::Sneak, mh::steppingOnMagma(ctx));

        // closed doors / gates: open them
        if ((pb1.type == T::Door && !pb1.open) || (pb0.type == T::Door && !pb0.open)) {
            BlockPos door = pb1.type == T::Door ? positionsToBreak_[1] : positionsToBreak_[0];
            return state.setTarget({ru::calcRotationFromVec3d(ctx.playerHead(), blockCenter(ctx.bsi(), door), ctx.playerRotations()), true})
                .setInput(Input::ClickRight, true);
        }
        if ((pb1.type == T::FenceGate && !pb1.open) || (pb0.type == T::FenceGate && !pb0.open)) {
            BlockPos gate = pb1.type == T::FenceGate ? positionsToBreak_[1] : positionsToBreak_[0];
            if (auto r = mh::reachable(ctx, gate)) return state.setTarget({*r, true}).setInput(Input::ClickRight, true);
        }

        bool isTheBridgeBlockThere = ctx.canWalkOn(*positionToPlace_) || ladder ||
                                     helper::canUseFrostWalker(ctx.calc, ctx.props(*positionToPlace_));
        BlockPos feet = ctx.playerFeet();
        if (feet.y != dest_.y && !ladder) {
            if (feet.y < dest_.y) return state.setInput(Input::Jump, true);
            return state;
        }

        if (isTheBridgeBlockThere) {
            if (feet == dest_) return state.setStatus(MovementStatus::Success);
            BlockPos dir = direction();
            if (Settings::get().overshootTraverse && (feet == dest_ + dir || feet == dest_ + dir + dir)) {
                return state.setStatus(MovementStatus::Success);
            }
            const BlockProps& low = ctx.props(src_);
            const BlockProps& high = ctx.props(src_.above());
            if (pos.y > src_.y + 0.1 && !ctx.player.onGround() && (helper::isClimbable(low) || helper::isClimbable(high))) {
                return state;  // pressing forward could climb the ladder instead
            }
            BlockPos into = dest_ + dir;
            const BlockProps& intoBelow = ctx.props(into);
            const BlockProps& intoAbove = ctx.props(into.above());
            if (wasTheBridgeBlockAlwaysThere_ && (!ctx.isLiquid(feet) || Settings::get().sprintInWater) &&
                (!helper::avoidWalkingInto(intoBelow) || helper::isWater(intoBelow)) && !helper::avoidWalkingInto(intoAbove)) {
                state.setInput(Input::Sprint, true);
            }
            const BlockProps& destDown = ctx.props(dest_.below());
            if (feet.y != dest_.y && ladder && helper::isClimbable(destDown)) state.setInput(Input::Jump, true);
            mh::moveTowards(ctx, state, positionsToBreak_[0]);
            return state;
        }

        // bridging: place a block under the destination
        wasTheBridgeBlockAlwaysThere_ = false;
        const BlockProps& standingOn = ctx.props(feet.below());
        if (standingOn.type == T::SoulSand || standingOn.type == T::Slab) {
            double dist = std::max(std::fabs(dest_.x + 0.5 - pos.x), std::fabs(dest_.z + 0.5 - pos.z));
            if (dist < 0.85) {
                mh::moveTowards(ctx, state, dest_);
                return state.setInput(Input::MoveForward, false).setInput(Input::MoveBack, true);
            }
        }
        double dist1 = std::max(std::fabs(pos.x - (dest_.x + 0.5)), std::fabs(pos.z - (dest_.z + 0.5)));
        bool safeWalk = Settings::get().assumeSafeWalk;
        auto p = mh::attemptToPlaceABlock(ctx, state, dest_.below(), false, !safeWalk);
        if ((p == mh::PlaceResult::ReadyToPlace || dist1 < 0.6) && !safeWalk) state.setInput(Input::Sneak, true);
        switch (p) {
            case mh::PlaceResult::ReadyToPlace:
                if (ctx.player.isSneaking() || safeWalk) state.setInput(Input::ClickRight, true);
                return state;
            case mh::PlaceResult::Attempting:
                if (dist1 > 0.83) {
                    float yaw = ru::calcRotationFromVec3d(ctx.playerHead(), Vec3d::center(dest_), ctx.playerRotations()).yaw;
                    if (state.target.rotation && std::fabs(state.target.rotation->yaw - yaw) < 0.1f) {
                        return state.setInput(Input::MoveForward, true);
                    }
                } else if (state.target.rotation && ctx.playerRotations().isReallyCloseTo(*state.target.rotation)) {
                    return state.setInput(Input::ClickLeft, true);  // something is in the way
                }
                return state;
            default: break;
        }
        if (feet == dest_) {
            // sneaking over air: place against the block we just walked off of
            double faceX = (dest_.x + src_.x + 1.0) * 0.5;
            double faceY = (dest_.y + src_.y - 1.0) * 0.5;
            double faceZ = (dest_.z + src_.z + 1.0) * 0.5;
            BlockPos goalLook = src_.below();
            Rotation backToFace = ru::calcRotationFromVec3d(ctx.playerHead(), {faceX, faceY, faceZ}, ctx.playerRotations());
            double dist2 = std::max(std::fabs(pos.x - faceX), std::fabs(pos.z - faceZ));
            if (dist2 < 0.29) {
                float yaw = ru::calcRotationFromVec3d(Vec3d::center(dest_), ctx.playerHead(), ctx.playerRotations()).yaw;
                state.setTarget({Rotation{yaw, backToFace.pitch}, true});
                state.setInput(Input::MoveBack, true);
            } else {
                state.setTarget({backToFace, true});
            }
            if (ctx.isLookingAt(goalLook)) return state.setInput(Input::ClickRight, true);
            if (ctx.playerRotations().isReallyCloseTo(*state.target.rotation)) state.setInput(Input::ClickLeft, true);
            return state;
        }
        mh::moveTowardsWithSlightRotation(ctx, state, dest_);
        return state;
    }

private:
    bool wasTheBridgeBlockAlwaysThere_ = true;
};

// ===========================================================================
// Ascend
// ===========================================================================

class MovementAscend : public Movement {
public:
    MovementAscend(BlockPos src, BlockPos dest)
        : Movement(MovementType::Ascend, src, dest, {src.above(2), dest, dest.above()}, dest.below()) {}

    bool safeToCancel(ExecContext&) const override {
        return currentState_.status != MovementStatus::Running || ticksWithoutPlacement_ == 0;
    }

protected:
    std::unordered_set<BlockPos> calculateValidPositions() const override {
        BlockPos prior = (src_ - direction()).above();  // backing up to place, sprint ascends
        std::unordered_set<BlockPos> set{src_, src_.above(), dest_, prior, prior.above()};
        // from a ladder top, the takeoff is at an end of its plate, over the next cell
        for (Direction d : kHorizontals) set.insert(src_.offset(d));
        return set;
    }

    void reset() override {
        Movement::reset();
        ladderTakeoff_.reset();
    }

    // Java headBonkClear: nothing around the head to bonk on, so jumping right away is safe
    bool headBonkClear(ExecContext& ctx) {
        BlockPos startUp = src_.above(2);
        for (Direction d : kHorizontals)
            if (!ctx.canWalkThrough(startUp.offset(d))) return false;
        return true;
    }

    MovementState& updateState(ExecContext& ctx, MovementState& state) override {
        if (ctx.playerFeet().y < src_.y) return state.setStatus(MovementStatus::Unreachable);
        Movement::updateState(ctx, state);
        if (state.status != MovementStatus::Running) return state;

        BlockPos feet = ctx.playerFeet();
        if (feet == dest_ || feet == dest_ + direction().below()) return state.setStatus(MovementStatus::Success);

        const BlockProps& jumpingOnto = ctx.props(*positionToPlace_);
        if (!ctx.canWalkOn(*positionToPlace_)) {
            ticksWithoutPlacement_++;
            if (mh::attemptToPlaceABlock(ctx, state, dest_.below(), false, true) == mh::PlaceResult::ReadyToPlace) {
                state.setInput(Input::Sneak, true);
                if (ctx.player.isSneaking()) state.setInput(Input::ClickRight, true);
            }
            if (ticksWithoutPlacement_ > 10) state.setInput(Input::MoveBack, true);  // we may be in the way
            return state;
        }
        mh::moveTowards(ctx, state, dest_);
        state.setInput(Input::Sneak, Settings::get().allowWalkOnMagmaBlocks && jumpingOnto.type == T::Magma);
        if (helper::isBottomSlab(jumpingOnto) && !helper::isBottomSlab(ctx.props(src_.below()))) {
            return state;  // walk onto the slab, don't jump
        }
        if (Settings::get().assumeStep || feet == src_.above()) return state;  // step up / already jumping
        // Bedrock addition: out of a ladder onto the block beside it, keep climbing (jump held)
        // while heading for it: let go and we slide out of its bottom
        if (helper::isClimbable(ctx.props(src_)) && helper::isClimbable(ctx.props(ctx.playerFeet())))
            return state.setInput(Input::Jump, true);
        // Bedrock addition: from the top of a ladder onto a block, jump from the end of its plate
        // (above the ladder's cell the jump is a climb), then straight at the destination. Into
        // another ladder, the climb is what it takes.
        if (helper::isClimbable(ctx.props(src_.below())) && !helper::isClimbable(ctx.props(dest_)) &&
            !helper::isClimbable(ctx.props(dest_.below())) && ctx.player.onGround()) {
            const Vec3d pos = ctx.player.position(), aim = Vec3d::center(dest_);
            const float yaw = static_cast<float>(std::atan2(-(aim.x - pos.x), aim.z - pos.z) * 180 / std::numbers::pi);
            if (!ladderTakeoff_.ready(ctx, state.setInput(Input::MoveForward, false), src_, dest_, yaw)) return state;
            state.setTarget({Rotation{yaw, ctx.playerRotations().pitch}, false});
            // a sprint jump: the end of the plate leaves us beside the block, not against it
            return state.setInput(Input::MoveForward, true).setInput(Input::Jump, true).setInput(Input::Sprint, true);
        }

        int xAxis = std::abs(src_.x - dest_.x), zAxis = std::abs(src_.z - dest_.z);
        Vec3d pos = ctx.player.position();
        Vec3d vel = ctx.player.velocity();
        double flatDistToNext = xAxis * std::fabs(dest_.x + 0.5 - pos.x) + zAxis * std::fabs(dest_.z + 0.5 - pos.z);
        double sideDist = zAxis * std::fabs(dest_.x + 0.5 - pos.x) + xAxis * std::fabs(dest_.z + 0.5 - pos.z);
        double lateralMotion = xAxis * vel.z + zAxis * vel.x;
        if (std::fabs(lateralMotion) > 0.1) return state;
        if (headBonkClear(ctx)) return state.setInput(Input::Jump, true);
        if (flatDistToNext > 1.2 || sideDist > 0.2) return state;
        return state.setInput(Input::Jump, true);
    }

private:
    int ticksWithoutPlacement_ = 0;
    LadderTakeoff ladderTakeoff_;
};

// ===========================================================================
// Descend
// ===========================================================================

class MovementDescend : public Movement {
public:
    MovementDescend(BlockPos src, BlockPos dest)
        : Movement(MovementType::Descend, src, dest, {dest.above(2), dest.above(), dest}, dest.below()) {}

protected:
    std::unordered_set<BlockPos> calculateValidPositions() const override { return {src_, dest_.above(), dest_}; }

    bool skipToAscend(ExecContext& ctx) {
        BlockPos into = dest_ - src_.below() + dest_;
        return !ctx.canWalkThrough(into) && ctx.canWalkThrough(into.above()) && ctx.canWalkThrough(into.above(2));
    }

    bool descendSafeMode(ExecContext& ctx) override { return safeMode(ctx); }
    bool descendSkipToAscend(ExecContext& ctx) override { return skipToAscend(ctx); }
    void forceSafeMode() override { forceSafeMode_ = true; }

    bool safeMode(ExecContext& ctx) {
        if (forceSafeMode_) return true;
        if (skipToAscend(ctx)) return true;
        BlockPos into = dest_ - src_.below() + dest_;
        for (int y = 0; y <= 2; ++y)
            if (helper::avoidWalkingInto(ctx.props(into.above(y)))) return true;
        return false;
    }

    MovementState& updateState(ExecContext& ctx, MovementState& state) override {
        Movement::updateState(ctx, state);
        if (state.status != MovementStatus::Running) return state;

        BlockPos feet = ctx.playerFeet();
        BlockPos fakeDest{dest_.x * 2 - src_.x, dest_.y, dest_.z * 2 - src_.z};
        Vec3d pos = ctx.player.position();
        if ((feet == dest_ || feet == fakeDest) && (ctx.isLiquid(dest_) || pos.y - dest_.y < 0.5)) {
            return state.setStatus(MovementStatus::Success);
        }
        Rotation cur = ctx.playerRotations();
        if (safeMode(ctx)) {
            double destX = (src_.x + 0.5) * 0.17 + (dest_.x + 0.5) * 0.83;
            double destZ = (src_.z + 0.5) * 0.17 + (dest_.z + 0.5) * 0.83;
            float yaw = ru::calcRotationFromVec3d(ctx.playerHead(), {destX, static_cast<double>(dest_.y), destZ}, cur).yaw;
            return state.setTarget({Rotation{yaw, cur.pitch}, false}).setInput(Input::MoveForward, true);
        }
        double diffX = pos.x - (dest_.x + 0.5), diffZ = pos.z - (dest_.z + 0.5);
        double ab = std::sqrt(diffX * diffX + diffZ * diffZ);
        double x = pos.x - (src_.x + 0.5), z = pos.z - (src_.z + 0.5);
        double fromStart = std::sqrt(x * x + z * z);
        {
            Vec3d p = ctx.player.position();
            BlockPos under{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)) - 1,
                           static_cast<int>(std::floor(p.z))};
            state.setInput(Input::Sneak, Settings::get().allowWalkOnMagmaBlocks && ctx.props(under).type == T::Magma);
        }
        if (feet != dest_ || ab > 0.25) {
            if (numTicks_++ < 20 && fromStart < 1.25) mh::moveTowards(ctx, state, fakeDest);
            else mh::moveTowards(ctx, state, dest_);
        }
        return state;
    }

private:
    int numTicks_ = 0;
    bool forceSafeMode_ = false;
};

// ===========================================================================
// Fall
// ===========================================================================

class MovementFall : public Movement {
public:
    MovementFall(BlockPos src, BlockPos dest) : Movement(MovementType::Fall, src, dest, buildBreak(src, dest), std::nullopt) {}

    bool safeToCancel(ExecContext& ctx) const override {
        return ctx.playerFeet() == src_ || currentState_.status != MovementStatus::Running;
    }

protected:
    static std::vector<BlockPos> buildBreak(BlockPos src, BlockPos dest) {
        int diffX = src.x - dest.x, diffZ = src.z - dest.z, diffY = std::abs(src.y - dest.y);
        std::vector<BlockPos> out;
        for (int i = 0; i < diffY + 2; ++i) out.push_back({src.x - diffX, src.y + 1 - i, src.z - diffZ});
        return out;
    }

    std::unordered_set<BlockPos> calculateValidPositions() const override {
        std::unordered_set<BlockPos> set{src_};
        for (int y = src_.y - dest_.y; y >= 0; --y) set.insert(dest_.above(y));
        return set;
    }

    void reset() override {
        Movement::reset();
        bucketUsed_ = false;
    }

    bool bucketUsed_ = false;  // the water bucket was emptied during this fall

    // only the first blocks matter before jumping off (Java MovementFall.prepared)
    bool prepared(ExecContext& ctx, MovementState& state) override {
        if (state.status == MovementStatus::Waiting) return true;
        for (std::size_t i = 0; i < 4 && i < positionsToBreak_.size(); ++i) {
            if (!ctx.canWalkThrough(positionsToBreak_[i])) return Movement::prepared(ctx, state);
        }
        return true;
    }

    // facing of the first ladder below us: stay against it while falling
    std::optional<BlockPos> avoid(ExecContext& ctx) {
        BlockPos feet = ctx.playerFeet();
        for (int i = 0; i < 15; ++i) {
            const BlockProps& p = ctx.props(feet.below(i));
            if (p.type != T::Ladder) continue;
            std::string_view dir = p.state("facing_direction");  // 2 north, 3 south, 4 west, 5 east
            if (dir == "2") return BlockPos{0, 0, -1};
            if (dir == "3") return BlockPos{0, 0, 1};
            if (dir == "4") return BlockPos{-1, 0, 0};
            if (dir == "5") return BlockPos{1, 0, 0};
        }
        return std::nullopt;
    }

    MovementState& updateState(ExecContext& ctx, MovementState& state) override {
        Movement::updateState(ctx, state);
        if (state.status != MovementStatus::Running) return state;

        BlockPos feet = ctx.playerFeet();
        Vec3d pos = ctx.player.position();
        Vec3d vel = ctx.player.velocity();
        Rotation toDest = ru::calcRotationFromVec3d(ctx.playerHead(), Vec3d::center(dest_), ctx.playerRotations());
        std::optional<Rotation> targetRotation;
        bool isWater = helper::isWater(ctx.props(dest_));
        if (ctx.props(dest_.below()).type == T::Magma) {
            bool allAir = true;
            for (const BlockPos& b : mh::steppingOnBlocks(ctx)) allAir = allAir && ctx.canWalkThrough(b);
            if (allAir) state.setInput(Input::Sneak, true);
        }
        bool willPlaceBucket = src_.y - dest_.y > Settings::get().maxFallHeightNoWater + 1;  // see the cost
        if (!isWater && willPlaceBucket && feet != dest_) {
            int bucket = ctx.inventory.findSlot("minecraft:water_bucket");
            if (bucket < 0 && !ctx.player.onGround() && ctx.player.hotbar(ctx.player.selectedSlot()).name == "minecraft:bucket") {
                // just emptied: the water is placed but not read back yet (Java sees it at once)
                ctx.player.refreshWorld(dest_, 1);
                bucket = ctx.player.selectedSlot();
                bucketUsed_ = true;
            }
            if (bucket < 0 || ctx.calc.bsi.world().dimension() == 1) return state.setStatus(MovementStatus::Unreachable);
            // Bedrock: the click waits a few ticks after a slot change (the server must know the
            // held item) and the camera must be down when it comes: hold the bucket and look down
            // as soon as the fall starts, not only within reach like Java
            if (!ctx.player.onGround() && pos.y < src_.y - 0.5) {
                ctx.player.selectSlot(bucket);
                targetRotation = Rotation{toDest.yaw, 90.f};
                // Java clicks once within reach; falling ~4 blocks per tick, the reach can be
                // crossed between two ticks, so the click is armed every frame from 2 ticks before
                double nextFall = std::fabs((vel.y - 0.08) * 0.98);
                if (!bucketUsed_ && ctx.player.selectedSlot() == bucket && ctx.playerRotations().pitch > 75 &&
                    pos.y - dest_.y < ctx.player.blockReachDistance() + 2 * nextFall + 2)
                    ctx.player.useHeldItemWhenTargeting();
            }
        }
        state.setTarget(targetRotation ? MovementTarget{targetRotation, true} : MovementTarget{toDest, false});
        // Bedrock: a player floating in water has its feet about a block lower than in Java
        bool floatingInDest = isWater && feet == dest_.below() && helper::isWater(ctx.props(feet));
        if ((feet == dest_ || floatingInDest) && (pos.y - feet.y < 0.094 || isWater)) {
            if (isWater) {
                int empty = ctx.inventory.findSlot("minecraft:bucket");
                if (empty >= 0 && willPlaceBucket) {
                    ctx.player.selectSlot(empty);
                    if (vel.y >= 0) {
                        return state.setTarget({Rotation{toDest.yaw, 90.f}, true}).setInput(Input::ClickRight, true);
                    }
                    return state;
                }
                if (vel.y >= 0) return state.setStatus(MovementStatus::Success);
            } else {
                return state.setStatus(MovementStatus::Success);
            }
        }
        Vec3d destCenter = Vec3d::center(dest_);
        if (std::fabs(pos.x + vel.x - destCenter.x) > 0.1 || std::fabs(pos.z + vel.z - destCenter.z) > 0.1) {
            if (!ctx.player.onGround() && std::fabs(vel.y) > 0.4) state.setInput(Input::Sneak, true);
            state.setInput(Input::MoveForward, true);
        }
        BlockPos avoidDir = src_ - dest_;
        if (auto ladder = avoid(ctx)) {
            avoidDir = *ladder;
            double dist = std::fabs(avoidDir.x * (destCenter.x - avoidDir.x / 2.0 - pos.x)) +
                          std::fabs(avoidDir.z * (destCenter.z - avoidDir.z / 2.0 - pos.z));
            if (dist < 0.6) state.setInput(Input::MoveForward, true);
            else if (!ctx.player.onGround()) state.setInput(Input::Sneak, false);
        }
        if (!targetRotation) {
            Vec3d offset{destCenter.x + 0.125 * avoidDir.x, destCenter.y, destCenter.z + 0.125 * avoidDir.z};
            state.setTarget({ru::calcRotationFromVec3d(ctx.playerHead(), offset, ctx.playerRotations()), false});
        }
        return state;
    }
};

// ===========================================================================
// Diagonal
// ===========================================================================

class MovementDiagonal : public Movement {
public:
    MovementDiagonal(BlockPos src, BlockPos dest)
        : Movement(MovementType::Diagonal, src, dest,
                   {{src.x, src.y, dest.z}, {src.x, src.y + 1, dest.z}, {dest.x, src.y, src.z}, {dest.x, src.y + 1, src.z},
                    dest, dest.above()},
                   std::nullopt) {}

    bool safeToCancel(ExecContext& ctx) const override {
        // Java MovementDiagonal.safeToCancel: not while cutting a corner over nothing
        BlockPos feet = ctx.playerFeet();
        if (feet == src_) return true;
        if (ctx.canWalkOn({src_.x, src_.y - 1, dest_.z}) && ctx.canWalkOn({dest_.x, src_.y - 1, src_.z})) return true;
        if (feet == BlockPos{src_.x, src_.y, dest_.z} || feet == BlockPos{dest_.x, src_.y, src_.z}) {
            Vec3d p = ctx.player.position();
            double y = p.y - 1, o = 0.25;
            auto on = [&](double x, double z) {
                return ctx.canWalkOn({static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)),
                                      static_cast<int>(std::floor(z))});
            };
            return on(p.x + o, p.z + o) || on(p.x + o, p.z - o) || on(p.x - o, p.z + o) || on(p.x - o, p.z - o);
        }
        return true;
    }

    std::vector<BlockPos> toWalkInto(CalculationContext& c) override {
        std::vector<BlockPos> out;
        for (int i = 0; i < 4; ++i)
            if (!helper::canWalkThrough(c, positionsToBreak_[i].x, positionsToBreak_[i].y, positionsToBreak_[i].z)) {
                out.push_back(positionsToBreak_[i]);
            }
        return out;
    }

protected:
    bool prepared(ExecContext&, MovementState&) override { return true; }

    std::unordered_set<BlockPos> calculateValidPositions() const override {
        BlockPos diagA{src_.x, src_.y, dest_.z}, diagB{dest_.x, src_.y, src_.z};
        if (dest_.y < src_.y) return {src_, dest_.above(), diagA, diagB, dest_, diagA.below(), diagB.below()};
        if (dest_.y > src_.y) return {src_, src_.above(), diagA, diagB, dest_, diagA.above(), diagB.above()};
        return {src_, dest_, diagA, diagB};
    }

    bool sprint(ExecContext& ctx) {
        if (ctx.isLiquid(ctx.playerFeet()) && !Settings::get().sprintInWater) return false;
        for (int i = 0; i < 4; ++i)
            if (!ctx.canWalkThrough(positionsToBreak_[i])) return false;
        return true;
    }

    MovementState& updateState(ExecContext& ctx, MovementState& state) override {
        Movement::updateState(ctx, state);
        if (state.status != MovementStatus::Running) return state;
        BlockPos feet = ctx.playerFeet();
        if (feet == dest_) return state.setStatus(MovementStatus::Success);
        if (!playerInValidPosition(ctx) && !(ctx.isLiquid(src_) && validPositions().contains(feet.above()))) {
            return state.setStatus(MovementStatus::Unreachable);
        }
        // TODO(bedrock): horizontalCollision -> jump for diagonal ascends
        if (dest_.y > src_.y && ctx.player.position().y < src_.y + 0.1) state.setInput(Input::Jump, true);
        if (sprint(ctx)) state.setInput(Input::Sprint, true);
        state.setInput(Input::Sneak, mh::steppingOnMagma(ctx));
        mh::moveTowards(ctx, state, dest_);
        return state;
    }
};

// ===========================================================================
// Pillar
// ===========================================================================

class MovementPillar : public Movement {
public:
    MovementPillar(BlockPos src, BlockPos dest) : Movement(MovementType::Pillar, src, dest, {src.above(2)}, src) {}

protected:
    std::unordered_set<BlockPos> calculateValidPositions() const override { return {src_, dest_}; }

    bool prepared(ExecContext& ctx, MovementState& state) override {
        BlockPos feet = ctx.playerFeet();
        if ((feet == src_ || feet == src_.below()) && helper::isClimbable(ctx.props(src_.below()))) {
            state.setInput(Input::Sneak, true);
        }
        if (helper::isWater(ctx.props(dest_.above()))) return true;
        return Movement::prepared(ctx, state);
    }

    MovementState& updateState(ExecContext& ctx, MovementState& state) override {
        Movement::updateState(ctx, state);
        if (state.status != MovementStatus::Running) return state;
        BlockPos feet = ctx.playerFeet();
        if (feet.y < src_.y) return state.setStatus(MovementStatus::Unreachable);

        const BlockProps& fromDown = ctx.props(src_);
        Vec3d pos = ctx.player.position();
        if (helper::isWater(fromDown) && helper::isWater(ctx.props(dest_))) {
            state.setTarget({ru::calcRotationFromVec3d(ctx.playerHead(), Vec3d::center(dest_), ctx.playerRotations()), false});
            Vec3d c = Vec3d::center(dest_);
            if (std::fabs(pos.x - c.x) > 0.2 || std::fabs(pos.z - c.z) > 0.2) state.setInput(Input::MoveForward, true);
            if (feet == dest_) return state.setStatus(MovementStatus::Success);
            return state;
        }
        // on a ladder, or under one that starts at head height (jump into it)
        bool ladder = helper::isClimbable(fromDown) || helper::isClimbable(ctx.props(dest_));
        Rotation rotation = ru::calcRotationFromVec3d(ctx.playerHead(), Vec3d::center(*positionToPlace_), ctx.playerRotations());
        if (!ladder) state.setTarget({ctx.playerRotations().withPitch(rotation.pitch), true});

        bool blockIsThere = ctx.canWalkOn(src_) || ladder;
        if (ladder) {
            // the top of a ladder (no ladder in the destination): done once standing on its thin
            // top; until then keep pushing over it towards the wall, or we slide back down
            const bool top = !helper::isClimbable(ctx.props(dest_));
            if (feet == dest_ && (!top || ctx.player.onGround())) return state.setStatus(MovementStatus::Success);
            const BlockProps& climbing = helper::isClimbable(fromDown) ? fromDown : ctx.props(dest_);
            if (auto wall = helper::ladderWall(climbing)) {
                // Bedrock addition: face the block the ladder hangs on and push into it. That
                // climbs, and at the top the feet end on the thin top of the ladder, against the
                // wall, where the bot can stand and jump to the next one.
                const float yaw = static_cast<float>(std::atan2(-wall->x, wall->z) * 180 / std::numbers::pi);
                state.setTarget({Rotation{yaw, ctx.playerRotations().pitch}, false});
                const double lateral = (pos.x - src_.x - 0.5) * -wall->z + (pos.z - src_.z - 0.5) * wall->x;
                state.setInput(Input::MoveForward, true);
                state.setInput(Input::MoveLeft, lateral > 0.12);
                state.setInput(Input::MoveRight, lateral < -0.12);
            } else {
                mh::moveTowards(ctx, state, dest_);
            }
            state.setInput(Input::Jump, true);
            return state;
        }

        if (!ctx.inventory.selectThrowawayForLocation(true, src_.x, src_.y, src_.z)) {
            return state.setStatus(MovementStatus::Unreachable);
        }
        state.setInput(Input::Sneak, true);
        double diffX = pos.x - (dest_.x + 0.5), diffZ = pos.z - (dest_.z + 0.5);
        double dist = std::sqrt(diffX * diffX + diffZ * diffZ);
        Vec3d vel = ctx.player.velocity();
        double flatMotion = std::sqrt(vel.x * vel.x + vel.z * vel.z);
        if (dist > 0.17) {
            state.setInput(Input::MoveForward, true);
            state.setTarget({rotation, true});
        } else if (flatMotion < 0.05) {
            state.setInput(Input::Jump, pos.y < dest_.y);
        }
        if (!blockIsThere) {
            const BlockProps& fr = ctx.props(src_);
            if (!(fr.type == T::Air || fr.replaceable)) {
                if (auto r = mh::reachable(ctx, src_)) state.setTarget({*r, true});
                state.setInput(Input::Jump, false);  // breaking while jumping is 5x slower
                state.setInput(Input::ClickLeft, true);
                blockIsThere = false;
            } else if (ctx.player.isSneaking() && (ctx.isLookingAt(src_.below()) || ctx.isLookingAt(src_)) &&
                       pos.y > dest_.y + 0.1) {
                state.setInput(Input::ClickRight, true);
            }
        }
        if (feet == dest_ && blockIsThere) return state.setStatus(MovementStatus::Success);
        return state;
    }
};

// ===========================================================================
// Downward
// ===========================================================================

class MovementDownward : public Movement {
public:
    MovementDownward(BlockPos src, BlockPos dest) : Movement(MovementType::Downward, src, dest, {dest}, std::nullopt) {}

protected:
    std::unordered_set<BlockPos> calculateValidPositions() const override { return {src_, dest_}; }

    MovementState& updateState(ExecContext& ctx, MovementState& state) override {
        Movement::updateState(ctx, state);
        if (state.status != MovementStatus::Running) return state;
        if (ctx.playerFeet() == dest_) return state.setStatus(MovementStatus::Success);
        if (!playerInValidPosition(ctx)) return state.setStatus(MovementStatus::Unreachable);
        Vec3d pos = ctx.player.position();
        double diffX = pos.x - (dest_.x + 0.5), diffZ = pos.z - (dest_.z + 0.5);
        double ab = std::sqrt(diffX * diffX + diffZ * diffZ);
        if (numTicks_++ < 10 && ab < 0.2) return state;
        mh::moveTowards(ctx, state, positionsToBreak_[0]);
        return state;
    }

private:
    int numTicks_ = 0;
};

// ===========================================================================
// Parkour
// ===========================================================================

class MovementParkour : public Movement {
public:
    MovementParkour(BlockPos src, BlockPos dest) : Movement(MovementType::Parkour, src, dest, {}, std::nullopt) {
        BlockPos d = dest - src;
        dist_ = std::max(std::abs(d.x), std::abs(d.z));
        stepX_ = (d.x > 0) - (d.x < 0);
        stepZ_ = (d.z > 0) - (d.z < 0);
        ascend_ = dest.y > src.y;
        diagonal_ = stepX_ != 0 && stepZ_ != 0;
        const double distance = std::hypot(d.x, d.z);
        directionX_ = d.x / distance;
        directionZ_ = d.z / distance;
    }

    bool safeToCancel(ExecContext&) const override { return currentState_.status != MovementStatus::Running; }
    void reset() override {
        Movement::reset();
        planned_ = false;
        jump_.reset();
        launched_ = false;
        jumped_ = false;
    }

protected:
    std::unordered_set<BlockPos> calculateValidPositions() const override {
        std::unordered_set<BlockPos> set;
        const BlockPos d = direction();
        for (int i = 0; i <= dist_ * 4; ++i) {
            const double t = static_cast<double>(i) / (dist_ * 4);
            const double x = src_.x + 0.5 + d.x * t, z = src_.z + 0.5 + d.z * t;
            for (int bx = static_cast<int>(std::floor(x - 0.3)); bx <= std::floor(x + 0.3); ++bx) {
                for (int bz = static_cast<int>(std::floor(z - 0.3)); bz <= std::floor(z + 0.3); ++bz) {
                    for (int y = std::min(src_.y, dest_.y) - 1; y <= src_.y + 1; ++y) set.insert({bx, y, bz});
                }
            }
        }
        for (int y = dest_.y; y <= dest_.y + 2; ++y) set.insert({dest_.x, y, dest_.z});  // a ladder we climb on
        return set;
    }

    MovementState& updateState(ExecContext& ctx, MovementState& state) override {
        Movement::updateState(ctx, state);
        if (state.status != MovementStatus::Running) return state;
        if (!planned_) {
            jump_ = movement_costs::parkourJump(ctx.calc, src_, dest_);
            planned_ = true;
        }
        if (jump_) return executeJump(ctx, state);

        BlockPos feet = ctx.playerFeet();
        Vec3d pos = ctx.player.position();
        if (feet.y < src_.y) return state.setStatus(MovementStatus::Unreachable);  // we fell
        // Bedrock addition: still arriving (top of a ladder, end of an ascend or of a jump):
        // land first. Walking on in the air took us over the edge without a jump.
        if (!jumped_ && !ctx.player.onGround()) return state;
        if (dist_ >= 4 || ascend_ || diagonal_) state.setInput(Input::Sprint, true);
        if (Settings::get().allowWalkOnMagmaBlocks && ctx.props(feet.below()).type == T::Magma) state.setInput(Input::Sneak, true);
        mh::moveTowards(ctx, state, dest_);
        BlockPos step{stepX_, 0, stepZ_};
        if (!ctx.player.onGround() && pos.y - src_.y > 0.0001 && feet != src_) {
            // in the air (Bedrock carries a bit further than Java): keep facing the jump
            // direction, stop pushing once over the landing block, step back if past its middle
            // predict where we land without input (air drag 0.91, gravity), then push or brake
            // along the jump direction (a unit vector: 45 degrees on a diagonal)
            const double norm = diagonal_ ? std::numbers::sqrt2 : 1.0;
            double along = ((pos.x - (dest_.x + 0.5)) * stepX_ + (pos.z - (dest_.z + 0.5)) * stepZ_) / norm;
            Vec3d vel = ctx.player.velocity();
            double v = (vel.x * stepX_ + vel.z * stepZ_) / norm, vy = vel.y, y = pos.y, travel = 0;
            for (int t = 0; t < 40; ++t) {
                vy = (vy - 0.08) * 0.98;
                y += vy;
                v *= 0.91;
                travel += v;
                if (vy < 0 && y <= dest_.y) break;
            }
            double landing = along + travel;  // relative to the middle of the landing block
            float yaw = static_cast<float>(std::atan2(-stepX_, stepZ_) * 180.0 / 3.14159265358979);
            state.setTarget({Rotation{yaw, ctx.playerRotations().pitch}, false});
            if (helper::isClimbable(ctx.props(dest_))) {
                // jumping into a ladder: the wall behind it stops us, pressing into it climbs
                state.setInput(Input::MoveForward, true);
                state.setInput(Input::MoveBack, false);
                state.setInput(Input::Sprint, false);
            } else {
                state.setInput(Input::MoveForward, landing < -0.35);
                state.setInput(Input::MoveBack, landing > 0.05);
                if (landing >= -0.35) state.setInput(Input::Sprint, false);
            }
            if (!diagonal_) {
                // Bedrock addition: stay on the line of the jump (a sideways drift from the
                // run-up misses one block wide platforms)
                const double lateral = (pos.x - src_.x - 0.5) * -stepZ_ + (pos.z - src_.z - 0.5) * stepX_;
                const double drift = lateral + (vel.x * -stepZ_ + vel.z * stepX_) * 3;
                state.setInput(Input::MoveLeft, drift > 0.08);
                state.setInput(Input::MoveRight, drift < -0.08);
            }
        }
        if (holdingLadder(ctx, dest_)) return state.setStatus(MovementStatus::Success);
        if (feet == dest_) {
            if (ctx.player.onGround() && pos.y - feet.y < 0.094) state.setStatus(MovementStatus::Success);
        } else if (diagonal_ && ctx.player.onGround() && pos.y - src_.y < 0.0001) {
            // diagonal: run to the corner of the block and jump once the body is past it (the
            // player crosses the corner cells, so the feet block can't tell when to jump)
            double out = ((pos.x - (src_.x + 0.5)) * stepX_ + (pos.z - (src_.z + 0.5)) * stepZ_) / std::numbers::sqrt2;
            if (out > 0.62) {
                state.setInput(Input::Jump, true);
                jumped_ = true;
            }
            else if (out < -0.2) {
                state.setInput(Input::Sprint, false);
                mh::moveTowards(ctx, state, src_);
            }
        } else if (feet != src_ && !diagonal_) {
            if (feet == src_ + step || (jumped_ && pos.y - src_.y > 0.0001)) {
                if (Settings::get().allowPlace && ctx.inventory.hasGenericThrowaway() && !ctx.canWalkOn(dest_.below()) &&
                    !ctx.player.onGround() &&
                    mh::attemptToPlaceABlock(ctx, state, dest_.below(), true, false) == mh::PlaceResult::ReadyToPlace) {
                    state.setInput(Input::ClickRight, true);
                }
                if (dist_ == 3 && !ascend_) {  // 2 block gap: don't jump too early
                    double xDiff = (src_.x + 0.5) - pos.x, zDiff = (src_.z + 0.5) - pos.z;
                    if (std::max(std::fabs(xDiff), std::fabs(zDiff)) < 0.7) return state;
                }
                state.setInput(Input::Jump, true);
                if (ctx.player.onGround()) jumped_ = true;
            } else if (feet != dest_ - step) {
                state.setInput(Input::Sprint, false);
                if (feet == src_ - step) mh::moveTowards(ctx, state, src_);
                else mh::moveTowards(ctx, state, src_ - step);
            }
        }
        return state;
    }

private:
    MovementState& executeJump(ExecContext& ctx, MovementState& state) {
        const Vec3d pos = ctx.player.position(), velocity = ctx.player.velocity();
        const BlockProps& source = ctx.props(src_.below()), &landing = ctx.props(dest_.below());
        const double startY = src_.y - 1 + source.height, landY = dest_.y - 1 + landing.height;
        const double along = (pos.x - src_.x - 0.5) * directionX_ + (pos.z - src_.z - 0.5) * directionZ_;
        const double side = (pos.x - src_.x - 0.5) * directionZ_ - (pos.z - src_.z - 0.5) * directionX_;
        const double speed = velocity.x * directionX_ + velocity.z * directionZ_;
        const double sideSpeed = velocity.x * directionZ_ - velocity.z * directionX_;
        const bool grounded = ctx.player.onGround();
        if (grounded && ctx.playerFeet() == dest_ && std::abs(pos.y - landY) < 0.03)
            return state.setStatus(MovementStatus::Success);
        if (pos.y < std::min(startY, landY) - 0.12 || (launched_ && grounded && ctx.playerFeet() != dest_))
            return state.setStatus(MovementStatus::Unreachable);

        const float yaw = static_cast<float>(std::atan2(-directionX_, directionZ_) * 180 / std::numbers::pi);
        const auto view = ctx.playerRotations();
        state.setTarget({Rotation{yaw, view.pitch}, false});
        if (grounded) {
            if (std::abs(pos.y - startY) > 0.05) return state.setStatus(MovementStatus::Unreachable);
            const double alignment = side + sideSpeed;
            if (std::abs(alignment) > 0.16 && along < jump_->takeoff) {
                state.setInput(Input::MoveLeft, alignment < 0).setInput(Input::MoveRight, alignment > 0);
                return state;
            }
            state.setInput(Input::MoveForward, true);
            state.setInput(Input::Sprint, jump_->sprint);
            const float yawError = std::remainder(view.yaw - yaw, 360.f);
            const double launchAt = jump_->takeoff - std::clamp(speed * 0.45, 0.0, 0.08);
            if (std::abs(yawError) < 20 && along >= launchAt) state.setInput(Input::Jump, true);
            return state;
        }

        launched_ = true;
        // The Bedrock motion vector is already integrated with gravity and drag for
        // the next tick. Predict the remaining displacement without steering input.
        double y = pos.y, vy = velocity.y, drift = 0, drag = 1;
        for (int tick = 0; tick < 20; ++tick) {
            y += vy;
            drift += drag;
            if (vy < 0 && y <= landY) break;
            vy = (vy - 0.08) * 0.98;
            drag *= 0.91;
        }
        const double remaining = (dest_.x + 0.5 - pos.x) * directionX_ +
                                 (dest_.z + 0.5 - pos.z) * directionZ_;
        const double error = remaining - speed * drift;
        const double lateral = side + sideSpeed * drift;
        const bool narrow = landing.supportHalfX() < 0.4 || landing.supportHalfZ() < 0.4;
        const double tolerance = narrow ? 0.09 : 0.18;
        state.setInput(Input::MoveForward, error > tolerance);
        state.setInput(Input::MoveBack, error < -tolerance);
        state.setInput(Input::MoveLeft, lateral < -tolerance);
        state.setInput(Input::MoveRight, lateral > tolerance);
        state.setInput(Input::Sprint, jump_->sprint && error > tolerance);
        return state;
    }

    int dist_, stepX_, stepZ_;
    double directionX_, directionZ_;
    bool ascend_;
    bool diagonal_ = false;  // Bedrock addition: a jump along a diagonal
    bool planned_ = false, launched_ = false;
    bool jumped_ = false;  // the legacy jump: we pressed jump from the ground
    std::optional<movement_costs::ParkourJump> jump_;
};

// ===========================================================================

class MovementParkourCorner : public Movement {
public:
    MovementParkourCorner(BlockPos src, BlockPos dest) : Movement(MovementType::ParkourCorner, src, dest, {}, std::nullopt) {}

    double calculateCost(CalculationContext& calc) const override {
        if (!movement_costs::isCornerJump(calc, src_, dest_)) return COST_INF;
        if (!route_) route_ = movement_costs::planCornerJump(calc, src_, dest_);
        return route_ ? 16.0 + route_->inputs.size() + calc.jumpPenalty : COST_INF;
    }

    bool safeToCancel(ExecContext& ctx) const override { return !playing_ || ctx.player.onGround(); }

    void reset() override {
        Movement::reset();
        playing_ = false;
        route_.reset();
        tick_ = 0;
    }

protected:
    std::unordered_set<BlockPos> calculateValidPositions() const override {
        std::unordered_set<BlockPos> positions;
        for (int x = std::min(src_.x, dest_.x) - 1; x <= std::max(src_.x, dest_.x) + 1; ++x)
            for (int z = std::min(src_.z, dest_.z) - 1; z <= std::max(src_.z, dest_.z) + 1; ++z)
                for (int y = std::min(src_.y, dest_.y); y <= src_.y + 1; ++y) positions.insert({x,y,z});
        return positions;
    }

    MovementState& updateState(ExecContext& ctx, MovementState& state) override {
        Movement::updateState(ctx, state);
        if (state.status != MovementStatus::Running) return state;
        const Vec3d pos = ctx.player.position(), velocity = ctx.player.velocity();
        if (pos.y < std::min(src_.y, dest_.y) - 0.08) return state.setStatus(MovementStatus::Unreachable);
        const auto view = ctx.playerRotations();
        auto pointAt = [&](float yaw) { state.setTarget({Rotation{yaw, 0}, false}); };
        auto centerOn = [&](Vec3d target) {
            const double dx = target.x - pos.x - velocity.x * 1.9, dz = target.z - pos.z - velocity.z * 1.9;
            const float yaw = static_cast<float>(std::atan2(-dx, dz) * 180 / std::numbers::pi);
            pointAt(yaw);
            if (std::hypot(dx, dz) > 0.028 && std::abs(std::remainder(view.yaw - yaw, 360.f)) < 14) {
                state.setInput(Input::Sneak, true).setInput(Input::MoveForward, true);
            }
        };

        if (!route_ && calculateCost(ctx.calc) >= COST_INF) return state.setStatus(MovementStatus::Unreachable);
        if (!playing_) {
            if (!ctx.player.onGround()) return state;
            if (std::hypot(pos.x - route_->start.x, pos.z - route_->start.z) > 0.045 ||
                std::hypot(velocity.x, velocity.z) > 0.018) {
                centerOn(route_->start);
                return state;
            }
            const float yaw = route_->inputs.front().yaw;
            pointAt(yaw);
            if (std::abs(std::remainder(view.yaw - yaw, 360.f)) > 2) return state;
            auto actual = movement_costs::planCornerJump(ctx.calc, src_, dest_, pos);
            if (!actual) return state.setStatus(MovementStatus::Unreachable);
            if (std::abs(std::remainder(view.yaw - actual->inputs.front().yaw, 360.f)) > 2) {
                route_ = std::move(actual);
                return state;
            }
            route_ = std::move(actual);
            playing_ = true;
        }
        if (tick_ < route_->inputs.size()) {
            const auto& input = route_->inputs[tick_];
            pointAt(route_->inputs[std::min(tick_ + 1, route_->inputs.size() - 1)].yaw);
            state.setInput(Input::MoveForward, input.forward);
            state.setInput(Input::Sprint, input.forward);
            state.setInput(Input::Jump, input.jump);
            ++tick_;
            return state;
        }
        if (ctx.player.onGround()) {
            if (ctx.playerFeet() == dest_) return state.setStatus(MovementStatus::Success);
            centerOn(Vec3d::center(dest_));
        }
        return state;
    }

private:
    mutable std::optional<movement_costs::CornerJump> route_;
    bool playing_ = false;
    std::size_t tick_ = 0;
};

class MovementParkourShulker : public Movement {
public:
    MovementParkourShulker(BlockPos src, BlockPos dest) : Movement(MovementType::ParkourShulker, src, dest, {}, std::nullopt) {}

    bool safeToCancel(ExecContext&) const override { return phase_ == Phase::Align; }
    void reset() override {
        Movement::reset();
        phase_ = Phase::Align;
        phaseTicks_ = 0;
    }

protected:
    std::unordered_set<BlockPos> calculateValidPositions() const override {
        std::unordered_set<BlockPos> positions;
        for (int y = src_.y; y <= dest_.y + 1; ++y) {
            positions.insert({src_.x, y, src_.z});
            positions.insert({dest_.x, y, dest_.z});
        }
        return positions;
    }

    MovementState& updateState(ExecContext& ctx, MovementState& state) override {
        Movement::updateState(ctx, state);
        if (state.status != MovementStatus::Running) return state;
        auto& player = ctx.player;
        const Vec3d p = player.position(), v = player.velocity();
        const BlockPos lower{dest_.x, src_.y, dest_.z};
        const BlockPos upper = lower.above();
        const auto d = dest_ - src_;
        const float heading = yawTo(d.x, d.z);
        auto advance = [&](Phase phase) { phase_ = phase; phaseTicks_ = 0; };
        auto abort = [&]() -> MovementState& {
            if (player.ownedContainerReady()) player.closeOwnedContainer();
            return state.setStatus(MovementStatus::Unreachable);
        };
        if (++phaseTicks_ > (phase_ == Phase::Align ? 85 : 24) ||
            p.y < src_.y - 0.1 || calculateCost(ctx.calc) >= COST_INF) return abort();
        state.setTarget({Rotation{heading, 0}, false});
        switch (phase_) {
            case Phase::Align: {
                const double x = src_.x + 0.5 - d.x * 0.255;
                const double z = src_.z + 0.5 - d.z * 0.255;
                if (!player.onGround()) return state;
                if (std::hypot(p.x - x, p.z - z) > 0.10 || std::hypot(v.x, v.z) > 0.02) {
                    const double dx = x - p.x - v.x * 1.9, dz = z - p.z - v.z * 1.9;
                    const float yaw = yawTo(dx, dz);
                    state.setTarget({Rotation{yaw, 0}, false});
                    if (std::hypot(dx, dz) > 0.03 && std::abs(std::remainder(player.rotation().yaw - yaw, 360.f)) < 12)
                        state.setInput(Input::MoveForward, true).setInput(Input::Sneak, true);
                    return state;
                }
                if (player.containerOpen()) return abort();
                if (std::abs(std::remainder(player.rotation().yaw - heading, 360.f)) <= 3 &&
                    player.openContainerAt(lower)) advance(Phase::LowerOpen);
                break;
            }
            case Phase::LowerOpen:
                if (phaseTicks_ >= 12 && player.ownedContainerReady()) {
                    player.closeOwnedContainer();
                    advance(Phase::LowerClosing);
                }
                break;
            case Phase::LowerClosing:
                if (player.containerOpen()) break;
                state.setInput(Input::Jump, true);
                advance(Phase::DoubleJump);
                break;
            case Phase::DoubleJump:
                if (d.y == 2 && p.y > dest_.y + 0.02) {
                    advance(Phase::FinalJump);
                    state.setInput(Input::MoveForward, ctx.playerFeet() != dest_);
                    break;
                }
                if (p.y > src_.y + 2.10 && v.y > 0 && player.openContainerAt(upper)) {
                    advance(Phase::UpperOpen);
                    break;
                }
                state.setInput(Input::Jump, true).setInput(Input::MoveForward, true);
                break;
            case Phase::UpperOpen:
                if (phaseTicks_ >= 7 && player.ownedContainerReady() && p.y <= src_.y + 2.06) {
                    if (p.y < src_.y + 1.84) return abort();
                    player.closeOwnedContainer();
                    advance(Phase::UpperClosing);
                }
                break;
            case Phase::UpperClosing:
                if (player.containerOpen()) break;
                advance(Phase::FinalJump);
                [[fallthrough]];
            case Phase::FinalJump:
                if (ctx.playerFeet() == dest_ && player.onGround() && std::abs(p.y - dest_.y) < 0.035)
                    return state.setStatus(MovementStatus::Success);
                if (p.y < src_.y + 1.80) return abort();
                state.setInput(Input::Jump, p.y < dest_.y - 0.04);
                state.setInput(Input::MoveForward, ctx.playerFeet() != dest_);
                break;
        }
        return state;
    }

private:
    static float yawTo(double x, double z) {
        return static_cast<float>(std::atan2(-x, z) * 180 / std::numbers::pi);
    }
    enum class Phase { Align, LowerOpen, LowerClosing, DoubleJump, UpperOpen, UpperClosing, FinalJump };
    Phase phase_ = Phase::Align;
    int phaseTicks_ = 0;
};

// ===========================================================================
// EdgeWalk (Bedrock addition): sneak along the edge of the floor beside blocks that leave a
// little room in their cell, such as honey blocks.
// ===========================================================================

class MovementEdgeWalk : public Movement {
public:
    MovementEdgeWalk(BlockPos src, BlockPos dest) : Movement(MovementType::EdgeWalk, src, dest, {}, std::nullopt) {
        const BlockPos d = dest - src;
        stepX_ = (d.x > 0) - (d.x < 0);
        stepZ_ = (d.z > 0) - (d.z < 0);
    }

    bool safeToCancel(ExecContext& ctx) const override { return ctx.player.onGround(); }
    void reset() override {
        Movement::reset();
        side_ = 0;
    }

protected:
    std::unordered_set<BlockPos> calculateValidPositions() const override {
        std::unordered_set<BlockPos> set;
        const int n = std::abs(dest_.x - src_.x) + std::abs(dest_.z - src_.z);
        for (int k = 0; k <= n; ++k)
            for (int s = -1; s <= 1; ++s) set.insert({src_.x + stepX_ * k - stepZ_ * s, src_.y, src_.z + stepZ_ * k + stepX_ * s});
        return set;
    }

    MovementState& updateState(ExecContext& ctx, MovementState& state) override {
        Movement::updateState(ctx, state);
        if (state.status != MovementStatus::Running) return state;
        if (side_ == 0) {
            auto walk = movement_costs::edgeWalk(ctx.calc, src_, dest_);
            if (!walk) return state.setStatus(MovementStatus::Unreachable);
            side_ = walk->side;
        }
        const Vec3d pos = ctx.player.position();
        if (pos.y < src_.y - 0.3) return state.setStatus(MovementStatus::Unreachable);  // fell off the edge
        // distances along the walk (from the middle of the destination) and towards the side
        const double along = (pos.x - dest_.x - 0.5) * stepX_ + (pos.z - dest_.z - 0.5) * stepZ_;
        const double out = ((pos.x - src_.x - 0.5) * -stepZ_ + (pos.z - src_.z - 0.5) * stepX_) * side_;
        const float yaw = static_cast<float>(std::atan2(-stepX_, stepZ_) * 180 / std::numbers::pi);
        state.setTarget({Rotation{yaw, ctx.playerRotations().pitch}, false});
        state.setInput(Input::Sneak, true);  // sneaking keeps the feet on the edge
        if (along > -0.25) {
            // past the blocks: back to the middle of the destination
            if (std::abs(along) < 0.25 && std::abs(out) < 0.25 && ctx.player.onGround())
                return state.setStatus(MovementStatus::Success);
            mh::moveTowards(ctx, state, dest_);
            return state;
        }
        const bool sideways = std::abs(std::remainder(ctx.playerRotations().yaw - yaw, 360.f)) < 10;
        if (!sideways) return state;
        // first to the edge, then along it, still pushing over the edge
        state.setInput(side_ > 0 ? Input::MoveRight : Input::MoveLeft, true);
        state.setInput(Input::MoveForward, out > 0.62);
        return state;
    }

private:
    int stepX_, stepZ_;
    int side_ = 0;
};

// ===========================================================================
// LadderHop (Bedrock addition): from the thin top of a ladder, jump into another ladder.
// ===========================================================================

class MovementLadderHop : public Movement {
public:
    MovementLadderHop(BlockPos src, BlockPos dest) : Movement(MovementType::LadderHop, src, dest, {}, std::nullopt) {}

    bool safeToCancel(ExecContext& ctx) const override { return !jumped_ && ctx.player.onGround(); }
    void reset() override {
        Movement::reset();
        jumped_ = false;
        groundTicks_ = 0;
        takeoff_.reset();
    }

protected:
    std::unordered_set<BlockPos> calculateValidPositions() const override {
        // the jump may swing a little past the cells between; the feet never get more than a
        // block above the higher end (cells above would be those of the movements after it)
        std::unordered_set<BlockPos> set;
        for (int x = std::min(src_.x, dest_.x) - 1; x <= std::max(src_.x, dest_.x) + 1; ++x)
            for (int z = std::min(src_.z, dest_.z) - 1; z <= std::max(src_.z, dest_.z) + 1; ++z)
                for (int y = src_.y - 1; y <= std::max(src_.y, dest_.y) + 1; ++y) set.insert({x, y, z});
        return set;
    }

    MovementState& updateState(ExecContext& ctx, MovementState& state) override {
        Movement::updateState(ctx, state);
        if (state.status != MovementStatus::Running) return state;
        if (holdingLadder(ctx, dest_)) return state.setStatus(MovementStatus::Success);
        // landed on the top of the ladder instead (the jump peaks above it): as good as climbed
        if (ctx.playerFeet() == dest_.above() && ctx.player.onGround() && !helper::isClimbable(ctx.props(dest_.above())))
            return state.setStatus(MovementStatus::Success);
        const Vec3d pos = ctx.player.position();
        if (pos.y < src_.y - 1.2) return state.setStatus(MovementStatus::Unreachable);  // fell
        // Straight at the ladder, even around a corner: the block between stops one axis and the
        // body slides along its side until it clears it. Aiming at the open corner first wastes
        // the jump (in game the turn came when the feet were already below the ladder).
        // Climbing one block, the feet reach the ladder's cell at the top of the jump: pass well
        // beside its plate (the 3 pixels on the wall side of the cell stop a body that comes
        // along the wall). At the same height the jump peaks above the ladder: come down on its
        // top, against the wall (as good as climbing it), or into its upper part.
        const BlockPos d = dest_ - src_;
        Vec3d target = Vec3d::center(dest_);
        const auto destWall = helper::ladderWall(ctx.props(dest_));
        if (destWall) {
            const double towardsWall = dest_.y > src_.y ? -0.2 : 0.1;
            target.x += destWall->x * towardsWall;
            target.z += destWall->z * towardsWall;
        }
        // Steer from where the momentum takes us in 4 ticks: that brakes before the target. Not
        // towards the ladder's wall, which stops us anyway: braking there only loses the height
        // the feet need when they reach the ladder.
        const Vec3d vel = ctx.player.velocity();
        const bool airborne = jumped_ && !ctx.player.onGround();
        double aheadX = airborne ? vel.x * 4 : 0, aheadZ = airborne ? vel.z * 4 : 0;
        if (destWall) {
            const double intoWall = aheadX * destWall->x + aheadZ * destWall->z;
            if (intoWall > 0) {
                aheadX -= intoWall * destWall->x;
                aheadZ -= intoWall * destWall->z;
            }
        }
        const double dx = target.x - pos.x - aheadX, dz = target.z - pos.z - aheadZ;
        const float yaw = static_cast<float>(std::atan2(-dx, dz) * 180 / std::numbers::pi);
        state.setTarget({Rotation{yaw, ctx.playerRotations().pitch}, false});
        // A sprint jump up into a ladder or further than a block and a half: the fall below the
        // ladder comes within about 10 ticks. Not for the short ones: from standing still the
        // Bedrock client starts sprinting a tick late, without the boost, while the server sprints
        // at once, carries us past the ladder and corrects us onto wherever it landed.
        const bool sprint = dest_.y > src_.y || std::hypot(d.x, d.z) > 1.5;
        if (Settings::get().chatDebug)
            debugLog(std::format("ladder hop {} -> {}: pos {:.2f} {:.2f} {:.2f} vel {:.2f} {:.2f} {:.2f} ground {} feet {} jumped {}",
                          src_, dest_, pos.x, pos.y, pos.z, ctx.player.velocity().x, ctx.player.velocity().y,
                          ctx.player.velocity().z, ctx.player.onGround(), ctx.playerFeet(), jumped_));
        if (!jumped_ && helper::isClimbable(ctx.props(ctx.playerFeet()))) {
            // slipped into the ladder below: climb back onto its top, against its wall
            if (auto wall = helper::ladderWall(ctx.props(ctx.playerFeet()))) {
                const float wallYaw = static_cast<float>(std::atan2(-wall->x, wall->z) * 180 / std::numbers::pi);
                state.setTarget({Rotation{wallYaw, ctx.playerRotations().pitch}, false});
            }
            state.setInput(Input::MoveForward, true).setInput(Input::Jump, true);
            return state;
        }
        if (!jumped_) {
            if (!ctx.player.onGround()) {
                // still settling on the top of the ladder: keep over it, against its wall
                if (auto wall = helper::ladderWall(ctx.props(src_.below()))) {
                    const float wallYaw = static_cast<float>(std::atan2(-wall->x, wall->z) * 180 / std::numbers::pi);
                    state.setTarget({Rotation{wallYaw, ctx.playerRotations().pitch}, false});
                    state.setInput(Input::MoveForward, true);
                }
                return state;
            }
            // on the ground somewhere else (a correction of the server): plan again from there
            if (ctx.playerFeet().y != src_.y) return state.setStatus(MovementStatus::Unreachable);
            // from the end of the plate, once the server agrees on where we are
            if (!takeoff_.ready(ctx, state, src_, dest_, yaw)) return state;
            state.setInput(Input::Jump, true).setInput(Input::MoveForward, true).setInput(Input::Sprint, sprint);
            jumped_ = true;
            groundTicks_ = 0;
            return state;
        }
        // Landed, but not where we were going (on the top of the block beside it, where the server
        // put us...): plan again from there rather than push on towards the target, off the edge.
        groundTicks_ = ctx.player.onGround() ? groundTicks_ + 1 : 0;
        if (groundTicks_ > 2) return state.setStatus(MovementStatus::Unreachable);
        // in the air: push towards where we should be (the yaw already allows for the momentum)
        state.setInput(Input::MoveForward, std::hypot(dx, dz) > 0.08);
        state.setInput(Input::Sprint, sprint);
        return state;
    }

private:
    bool jumped_ = false;
    int groundTicks_ = 0;
    LadderTakeoff takeoff_;
};

// ===========================================================================
// MomentumJump (Bedrock addition): a 4 block gap, with a hop on the run-up for momentum.
// ===========================================================================

namespace {
// Flat ground physics along the jump: sprinting, 0.13 of speed and 0.546 of friction on the
// ground, 0.026 and 0.91 in the air; a jump goes up at 0.42 and adds 0.2 forward. Jumping now
// with `motion` (after friction): where the feet are when the landing tick starts (its vertical
// move comes first, there: it has to be over the block), where they are after it, and the
// motion kept for the next tick (a jump then adds its boost to the air speed: the momentum).
struct FlatHop {
    double landingStart, landed, motion;
};
FlatHop flatHop(double motion) {
    motion += 0.2 + 0.13;
    double distance = motion, y = 0.42, vy = (0.42 - 0.08) * 0.98;
    motion *= 0.546;
    for (int tick = 0; tick < 40; ++tick) {
        motion += 0.026;
        if (y + vy <= 0) {
            const double start = distance;
            distance += motion;
            return {start, distance, motion * 0.91};
        }
        distance += motion;
        motion *= 0.91;
        y += vy;
        vy = (vy - 0.08) * 0.98;
    }
    return {distance, distance, motion};
}
}  // namespace

class MovementMomentumJump : public Movement {
public:
    MovementMomentumJump(BlockPos src, BlockPos dest) : Movement(MovementType::MomentumJump, src, dest, {}, std::nullopt) {
        const BlockPos d = dest - src;
        stepX_ = (d.x > 0) - (d.x < 0);
        stepZ_ = (d.z > 0) - (d.z < 0);
    }

    bool safeToCancel(ExecContext& ctx) const override {
        return (phase_ == Phase::BackUp || phase_ == Phase::Settle) && ctx.player.onGround();
    }
    void reset() override {
        Movement::reset();
        runUp_ = 0;
        attempts_ = 0;
        phase_ = Phase::BackUp;
        last_.reset();
    }

protected:
    std::unordered_set<BlockPos> calculateValidPositions() const override {
        std::unordered_set<BlockPos> set;
        for (int k = -movement_costs::kMaxMomentumRunUp - 1; k <= movement_costs::kMomentumGap + 2; ++k)
            for (int s = -1; s <= 1; ++s)
                for (int dy = 0; dy <= 1; ++dy)
                    set.insert({src_.x + stepX_ * k - stepZ_ * s, src_.y + dy, src_.z + stepZ_ * k + stepX_ * s});
        return set;
    }

    MovementState& updateState(ExecContext& ctx, MovementState& state) override {
        Movement::updateState(ctx, state);
        if (state.status != MovementStatus::Running) return state;
        if (runUp_ == 0) {
            auto jump = movement_costs::momentumJump(ctx.calc, src_, dest_);
            if (!jump) return state.setStatus(MovementStatus::Unreachable);
            runUp_ = jump->runUp;
        }
        const Vec3d pos = ctx.player.position();
        const bool ground = ctx.player.onGround();
        if (ctx.playerFeet() == dest_ && ground) return state.setStatus(MovementStatus::Success);
        if (pos.y < src_.y - 0.5) return state.setStatus(MovementStatus::Unreachable);  // fell
        // Along the jump, from the middle of src. The motion comes from the last tick's move
        // (what the game reports as the velocity differs between versions).
        const double along = (pos.x - src_.x - 0.5) * stepX_ + (pos.z - src_.z - 0.5) * stepZ_;
        const double motion = last_ ? (along - last_->first) * (last_->second ? 0.546 : 0.91) : 0;
        last_ = std::pair{along, ground};
        // face the landing block: running at it keeps us on the line of the jump
        const Vec3d aim = Vec3d::center(dest_);
        const float yaw = static_cast<float>(std::atan2(-(aim.x - pos.x), aim.z - pos.z) * 180 / std::numbers::pi);
        state.setTarget({Rotation{yaw, ctx.playerRotations().pitch}, false});
        constexpr double limit = 0.5 + 0.28;  // the feet are still over the takeoff block
        if (Settings::get().chatDebug)
            debugLog(std::format("momentum jump {} -> {}: phase {} along {:.3f} motion {:.3f} ground {}", src_, dest_,
                                 static_cast<int>(phase_), along, motion, ground));
        switch (phase_) {
            case Phase::BackUp: {
                // to the start of the run-up, sneaking (it may be one block wide)
                state.setInput(Input::Sneak, true);
                if (along > -runUp_ + 0.1) {
                    state.setInput(Input::MoveBack, true);
                    return state;
                }
                phase_ = Phase::Settle;
                settled_ = 0;
                return state;
            }
            case Phase::Settle:
                // Still, not sneaking, for a moment: the server's corrections of the back-up
                // arrive (it is a step back onto the run-up if one does), and the sprint does not
                // start in the same tick as the sneaking stops.
                if (along > -runUp_ + 0.1 + 0.15) {
                    phase_ = Phase::BackUp;
                    return state;
                }
                settled_ = ground && std::abs(motion) < 0.003 ? settled_ + 1 : 0;
                if (settled_ < kSettleTicks || std::abs(std::remainder(ctx.playerRotations().yaw - yaw, 360.f)) > 10)
                    return state;
                phase_ = Phase::Run;
                [[fallthrough]];
            case Phase::Run: {
                state.setInput(Input::MoveForward, true).setInput(Input::Sprint, true);
                if (!ground) return state;
                // Hop when it lands on the run-up and the jump from there makes the gap, on the
                // tick that leaves the most margin (a tick moves us 0.29).
                auto margin = [&](double at, double speed) -> std::optional<double> {
                    const FlatHop hop = flatHop(speed);
                    if (at + hop.landed > limit) return std::nullopt;
                    const FlatHop jump = flatHop(hop.motion);
                    const double over = at + hop.landed + jump.landingStart;
                    const double nearEdge = kGapEnd - 0.3 + 0.05, farEdge = kGapEnd + 1 + 0.3 - 0.05;
                    if (over < nearEdge || over > farEdge) return std::nullopt;
                    return std::min({over - nearEdge, farEdge - over, limit - at - hop.landed});
                };
                const auto now = margin(along, motion), next = margin(along + motion + 0.13, (motion + 0.13) * 0.546);
                if (now && (!next || *now >= *next)) {
                    state.setInput(Input::Jump, true);
                    phase_ = Phase::Hop;
                    return state;
                }
                if (!now && along + flatHop(motion).landed > limit) {
                    // Past the moment for the hop (pushed, lagging...): a jump from here falls
                    // short. Stop well before the edge and run again.
                    phase_ = Phase::Brake;
                    state.setInput(Input::MoveForward, false).setInput(Input::Sprint, false);
                    state.setInput(Input::Sneak, true).setInput(Input::MoveBack, true);
                }
                return state;
            }
            case Phase::Hop:
                // hold the jump: we jump again the very tick we land
                state.setInput(Input::MoveForward, true).setInput(Input::Sprint, true).setInput(Input::Jump, true);
                if (ground) phase_ = Phase::Jump;
                return state;
            case Phase::Jump:
                state.setInput(Input::MoveForward, true).setInput(Input::Sprint, true);
                return state;
            case Phase::Brake:
                state.setInput(Input::Sneak, true).setInput(Input::MoveBack, true);
                if (ground && std::abs(motion) < 0.003) {
                    if (++attempts_ >= 3) return state.setStatus(MovementStatus::Unreachable);
                    phase_ = Phase::BackUp;
                }
                return state;
        }
        return state;
    }

private:
    enum class Phase { BackUp, Settle, Run, Hop, Jump, Brake };
    static constexpr double kGapEnd = 0.5 + movement_costs::kMomentumGap;  // the near side of the landing block
    static constexpr int kSettleTicks = 12;  // the server corrects a gap over 0.3 that lasts 500 ms
    int stepX_ = 0, stepZ_ = 0, runUp_ = 0, attempts_ = 0, settled_ = 0;
    Phase phase_ = Phase::BackUp;
    std::optional<std::pair<double, bool>> last_;  // along, on the ground
};

// ===========================================================================
// LadderShimmy (Bedrock addition): hanging in a ladder, sideways into the next one.
// ===========================================================================

class MovementLadderShimmy : public Movement {
public:
    MovementLadderShimmy(BlockPos src, BlockPos dest) : Movement(MovementType::LadderShimmy, src, dest, {}, std::nullopt) {}

protected:
    std::unordered_set<BlockPos> calculateValidPositions() const override {
        return {src_, dest_, src_.above(), dest_.above(), src_.below(), dest_.below()};
    }

    MovementState& updateState(ExecContext& ctx, MovementState& state) override {
        Movement::updateState(ctx, state);
        if (state.status != MovementStatus::Running) return state;
        if (dest_.y > src_.y) return climbAcross(ctx, state);
        if (ctx.playerFeet() == dest_) return state.setStatus(MovementStatus::Success);
        const Vec3d pos = ctx.player.position();
        if (pos.y < src_.y - 0.6) return state.setStatus(MovementStatus::Unreachable);
        if (!helper::isClimbable(ctx.props(src_))) {
            mh::moveTowards(ctx, state, dest_);  // from a block: walk into the ladder
            return state;
        }
        state.setInput(Input::Sneak, true);  // sneaking holds us on the ladder
        const BlockPos d = dest_ - src_;
        // face the wall the ladder hangs on and step sideways
        const auto wall = helper::ladderWall(ctx.props(src_));
        const BlockPos facing = wall ? *wall : d;
        const float yaw = static_cast<float>(std::atan2(-facing.x, facing.z) * 180 / std::numbers::pi);
        state.setTarget({Rotation{yaw, ctx.playerRotations().pitch}, false});
        if (std::abs(std::remainder(ctx.playerRotations().yaw - yaw, 360.f)) > 20) return state;
        const int right = -facing.z * d.x + facing.x * d.z;  // the step, on our right (+1) or left (-1)
        if (right > 0) state.setInput(Input::MoveRight, true);
        else if (right < 0) state.setInput(Input::MoveLeft, true);
        else state.setInput(facing == d ? Input::MoveForward : Input::MoveBack, true);
        return state;
    }

    // One up and one along the wall (a staircase of ladders): climb this one pushing into the
    // wall, at its edge on the next one's side, and step across once the feet are at the next
    // one's height (they leave this ladder there; a moment later and they fall back).
    MovementState& climbAcross(ExecContext& ctx, MovementState& state) {
        const Vec3d pos = ctx.player.position();
        // really in it: the feet are rounded up a little, and a moment later they fall back
        if (holdingLadder(ctx, dest_) && std::floor(pos.y) >= dest_.y) return state.setStatus(MovementStatus::Success);
        if (pos.y < src_.y - 0.6) return state.setStatus(MovementStatus::Unreachable);
        const auto wall = helper::ladderWall(ctx.props(src_));
        if (!wall) return state.setStatus(MovementStatus::Unreachable);
        const BlockPos d{dest_.x - src_.x, 0, dest_.z - src_.z};
        const float yaw = static_cast<float>(std::atan2(-wall->x, wall->z) * 180 / std::numbers::pi);
        state.setTarget({Rotation{yaw, ctx.playerRotations().pitch}, false});
        if (std::abs(std::remainder(ctx.playerRotations().yaw - yaw, 360.f)) > 20) return state;
        // how far the feet still are from the next ladder's column (0 at the line between them)
        const double toEdge = 0.5 - ((pos.x - src_.x - 0.5) * d.x + (pos.z - src_.z - 0.5) * d.z);
        const int right = -wall->z * d.x + wall->x * d.z;  // the step, on our right (+1) or left
        const Input across = right > 0 ? Input::MoveRight : Input::MoveLeft;
        const Input back = right > 0 ? Input::MoveLeft : Input::MoveRight;
        const bool atTop = pos.y >= dest_.y - 0.05;
        if (atTop) {
            state.setInput(Input::MoveForward, true).setInput(Input::Jump, true).setInput(across, true);
            return state;
        }
        // Hold the feet 0.06 to 0.18 from the edge until then. A ladder keeps the air's friction:
        // let go of the key and we coast about ten times our speed, so steer by where that ends.
        const Vec3d vel = ctx.player.velocity();
        const double coast = toEdge - (vel.x * d.x + vel.z * d.z) * 10;
        if (coast > 0.18) state.setInput(across, true);
        else if (coast < 0.06) state.setInput(back, true);
        if (toEdge > 0.25) {
            // to the edge first, held by sneaking: the climb is faster than the steps in a ladder
            state.setInput(Input::Sneak, true);
            return state;
        }
        state.setInput(Input::MoveForward, true).setInput(Input::Jump, true);  // climbing
        return state;
    }
};

std::unique_ptr<Movement> Movement::create(const MovementSpec& spec) {
    std::unique_ptr<Movement> m;
    switch (spec.type) {
        case MovementType::Traverse: m = std::make_unique<MovementTraverse>(spec.src, spec.dest); break;
        case MovementType::Ascend: m = std::make_unique<MovementAscend>(spec.src, spec.dest); break;
        case MovementType::Descend: m = std::make_unique<MovementDescend>(spec.src, spec.dest); break;
        case MovementType::Fall: m = std::make_unique<MovementFall>(spec.src, spec.dest); break;
        case MovementType::Diagonal: m = std::make_unique<MovementDiagonal>(spec.src, spec.dest); break;
        case MovementType::Pillar: m = std::make_unique<MovementPillar>(spec.src, spec.dest); break;
        case MovementType::Downward: m = std::make_unique<MovementDownward>(spec.src, spec.dest); break;
        case MovementType::Parkour: m = std::make_unique<MovementParkour>(spec.src, spec.dest); break;
        case MovementType::ParkourCorner: m = std::make_unique<MovementParkourCorner>(spec.src, spec.dest); break;
        case MovementType::ParkourShulker: m = std::make_unique<MovementParkourShulker>(spec.src, spec.dest); break;
        case MovementType::EdgeWalk: m = std::make_unique<MovementEdgeWalk>(spec.src, spec.dest); break;
        case MovementType::LadderHop: m = std::make_unique<MovementLadderHop>(spec.src, spec.dest); break;
        case MovementType::LadderShimmy: m = std::make_unique<MovementLadderShimmy>(spec.src, spec.dest); break;
        case MovementType::MomentumJump: m = std::make_unique<MovementMomentumJump>(spec.src, spec.dest); break;
    }
    m->overrideCost(spec.cost);
    m->calculatedWhileLoaded = spec.calculatedWhileLoaded;
    return m;
}

}  // namespace baritone
