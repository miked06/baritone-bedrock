#include "baritone/pathing/movement/Moves.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "baritone/pathing/movement/MovementHelper.h"
#include "baritone/pathing/movement/CornerJump.h"

namespace baritone {

using namespace costs;
using namespace helper;
using T = BlockType;

namespace movement_costs {

namespace {

bool hasPlaceOption(CalculationContext& ctx, int destX, int y, int destZ, int skipX, int skipZ) {
    for (Direction d : kPlaceAgainstDirs) {
        DirOffset o = offsetOf(d);
        int ax = destX + o.x, ay = y + o.y, az = destZ + o.z;
        if (ax == skipX && az == skipZ) continue;  // would be a backplace
        if (canPlaceAgainst(ctx, ax, ay, az)) return true;
    }
    return false;
}

bool hasAgainst(CalculationContext& ctx, int x, int y, int z) {
    return isBlockNormalCube(ctx.props(x + 1, y, z)) || isBlockNormalCube(ctx.props(x - 1, y, z)) ||
           isBlockNormalCube(ctx.props(x, y, z + 1)) || isBlockNormalCube(ctx.props(x, y, z - 1));
}

bool checkOvershootSafety(CalculationContext& ctx, int x, int y, int z) {
    // we walk into these two blocks after landing anyway
    return !avoidWalkingInto(ctx.props(x, y, z)) && !avoidWalkingInto(ctx.props(x, y + 1, z));
}

// Head height of a jump (2 blocks above the feet): a ladder there is only a thin plate the
// head passes beside; the feet never reach it.
bool headRoom(CalculationContext& ctx, int x, int y, int z) {
    const BlockProps& p = ctx.props(x, y, z);
    return fullyPassable(ctx, x, y, z, p) || p.type == T::Ladder;
}

// The collision box of a block inside its cell, horizontally (cell coordinates 0..1).
struct Footprint {
    double x0, x1, z0, z1;
};

Footprint footprint(const BlockProps& p, bool endpointPane) {
    if (p.type == T::Ladder) {
        // a 3 pixel plate against the block it hangs on (facing away from it)
        constexpr double t = 0.1875;
        const auto facing = p.state("facing_direction");
        if (facing == "2") return {0, 1, 1 - t, 1};  // faces north: on the south side
        if (facing == "3") return {0, 1, 0, t};
        if (facing == "4") return {1 - t, 1, 0, 1};
        if (facing == "5") return {0, t, 0, 1};
    }
    const double hx = endpointPane ? p.supportHalfX() : p.collisionHalfX();
    const double hz = endpointPane ? p.supportHalfZ() : p.collisionHalfZ();
    return {0.5 - hx, 0.5 + hx, 0.5 - hz, 0.5 + hz};
}

double costFromJumpDistance(int dist) {
    switch (dist) {
        case 2: return WALK_ONE_BLOCK_COST * 2;
        case 3: return WALK_ONE_BLOCK_COST * 3;
        case 4: return SPRINT_ONE_BLOCK_COST * 4;
        default: return COST_INF;
    }
}

}  // namespace

std::optional<ParkourJump> parkourJump(CalculationContext& ctx, const BlockPos& src, const BlockPos& dest) {
    if (!ctx.allowParkour || dest.y >= ctx.bsi.maxY() || dest.y <= ctx.bsi.minY()) return std::nullopt;
    if (!ctx.allowJumpAtBuildLimit && src.y >= ctx.bsi.maxY() - 1) return std::nullopt;
    const BlockPos delta = dest - src;
    const int distanceSq = delta.x * delta.x + delta.z * delta.z;
    if (distanceSq == 0 || distanceSq > 13 || std::abs(delta.y) > 1) return std::nullopt;
    const BlockPos from = src.below(), onto = dest.below();
    const BlockProps& landing = ctx.props(onto.x, onto.y, onto.z);
    if (!landing.collides) return std::nullopt;
    const BlockProps& support = ctx.props(from.x, from.y, from.z);
    const bool specialSupport = support.parkourSupport != ParkourSupport::None ||
                                landing.parkourSupport != ParkourSupport::None;
    if (!specialSupport) {
        // Leave ordinary connected terrain to traverses and diagonals, and a step down onto
        // the next block to descends. Bedrock addition: jumps that land a block lower (Baritone
        // has none), over a gap where walking off the edge falls further.
        const int x = src.x + (delta.x > 0) - (delta.x < 0);
        const int z = src.z + (delta.z > 0) - (delta.z < 0);
        if (canWalkOn(ctx, x, src.y - 1, z)) return std::nullopt;
        if (delta.y < 0 && canWalkOn(ctx, x, src.y - 2, z)) return std::nullopt;
    }
    if (!canParkourOn(ctx, onto) || !canParkourOn(ctx, from)) return std::nullopt;
    if (!ctx.isLoadedLive(src.x, src.z) || !ctx.isLoadedLive(dest.x, dest.z)) return std::nullopt;

    const double startY = from.y + support.height, landY = onto.y + landing.height;
    const double rise = landY - startY;
    if ((rise > 0.01 && !ctx.allowParkourAscend) || rise > 1.18) return std::nullopt;
    const double length = std::sqrt(static_cast<double>(distanceSq));
    const double dx = delta.x / length, dz = delta.z / length;
    const auto edge = [](double vx, double vz, const BlockProps& block) {
        double x = vx == 0 ? 10 : block.supportHalfX() / std::abs(vx);
        double z = vz == 0 ? 10 : block.supportHalfZ() / std::abs(vz);
        return std::min(x, z);
    };
    const double startEdge = edge(dx, dz, support), landingEdge = edge(dx, dz, landing);
    const bool narrow = startEdge < 0.43 || landingEdge < 0.43;
    const bool sprint = ctx.canSprint && (length > 2.45 || rise > 0.6);
    if (!sprint && length > 3.0) return std::nullopt;
    const double speed = sprint ? 0.32 : 0.245;
    constexpr double apex = 1.25, apexTick = 5.5;
    const double flight = apexTick * (1 + std::sqrt(1 - rise / apex));
    constexpr double radius = 0.299, playerHeight = 1.8, epsilon = 0.002;
    auto clear = [&](double distance, double by, bool airborne) {
        const double px = src.x + 0.5 + dx * distance;
        const double pz = src.z + 0.5 + dz * distance;
        for (int x = static_cast<int>(std::floor(px - radius)); x <= std::floor(px + radius); ++x) {
            for (int z = static_cast<int>(std::floor(pz - radius)); z <= std::floor(pz + radius); ++z) {
                if (!ctx.isLoadedLive(x, z)) return false;
                const BlockProps& below = ctx.props(x, static_cast<int>(std::floor(by - epsilon)), z);
                if (airborne && avoidWalkingInto(below) && below.type != T::Water) return false;
                const int firstY = static_cast<int>(std::floor(by - epsilon)) - 1;
                const int lastY = static_cast<int>(std::floor(by + playerHeight - epsilon));
                for (int y = firstY; y <= lastY; ++y) {
                    const BlockProps& p = ctx.props(x, y, z);
                    if (avoidWalkingInto(p) || ctx.isAvoided(p)) return false;
                    if (!p.collides) continue;
                    const double top = y + p.height;
                    const double bottom = y + (p.isHorizontalRod() ? 0.375 : p.topSlab ? 0.5 : 0);
                    if (by + epsilon >= top || by + playerHeight <= bottom + epsilon) continue;
                    // For panes, check the post at either endpoint and treat connecting arms elsewhere
                    // as obstacles. The landing post is always centered. Rods, candles, honey and
                    // ladders use their real (smaller) boxes.
                    const bool endpointPane = p.parkourSupport == ParkourSupport::Pane &&
                                              (BlockPos{x, y, z} == from || BlockPos{x, y, z} == onto);
                    const Footprint f = footprint(p, endpointPane);
                    if (px > x + f.x0 - radius && px < x + f.x1 + radius && pz > z + f.z0 - radius &&
                        pz < z + f.z1 + radius) return false;
                }
            }
        }
        return true;
    };
    const double takeoffs[] = {std::min(0.25, startEdge), startEdge - 0.03, startEdge + 0.12};
    for (double takeoff : takeoffs) {
        // Land inside the platform, allowing a small fore/aft margin on wide supports.
        const double nearCenter = length - (narrow ? 0.05 : 0.20);
        if (takeoff < 0 || nearCenter - takeoff > flight * speed) continue;
        if (!clear(0, startY, false) || !clear(takeoff * 0.5, startY, false) || !clear(takeoff, startY, false)) continue;
        bool open = true;
        constexpr int samples = 16;
        for (int i = 1; i <= samples; ++i) {
            const double t = static_cast<double>(i) / samples;
            const double arc = flight * t / apexTick - 1;
            if (!clear(std::lerp(takeoff, nearCenter, t), startY + apex * (1 - arc * arc), true)) {
                open = false;
                break;
            }
        }
        if (open && clear(length, landY, false)) {
            // A long jump onto (or from) a candle or an end rod leaves almost no margin for the
            // steering: prefer walking closer and making a short one when there is a choice.
            const bool tiny = landing.hasSmallFootprint() || support.hasSmallFootprint();
            const double precision = tiny ? 8 * std::max(0.0, length - 2.05) + (delta.x != 0 && delta.z != 0 ? 4 : 0) : 0;
            return ParkourJump{takeoff, flight + takeoff / 0.20 + ctx.jumpPenalty + precision, sprint};
        }
    }
    return std::nullopt;
}

namespace {
// A floor with the usual friction and a full top: the momentum jump's model of the run-up
// depends on it (ice, slime and honey change the speed; soul sand and magma the jump).
bool plainFloor(CalculationContext& ctx, int x, int y, int z) {
    const BlockProps& p = ctx.props(x, y, z);
    if (!p.fullCube || p.parkourSupport != ParkourSupport::None || p.hasFluid() || isClimbable(p)) return false;
    if (p.type == T::Ice || p.type == T::Honey || p.type == T::SoulSand || p.type == T::Magma) return false;
    if (p.name.find("slime") != std::string::npos) return false;
    return canWalkOn(ctx, x, y, z, p) && !avoidWalkingInto(p) && !ctx.isAvoided(p);
}

// Room for the body through a whole jump: the head reaches 3.05 above the feet.
bool jumpRoom(CalculationContext& ctx, int x, int y, int z) {
    for (int dy = 0; dy <= 3; ++dy) {
        const BlockProps& p = ctx.props(x, y + dy, z);
        if (!fullyPassable(ctx, x, y + dy, z, p) || ctx.isAvoided(p)) return false;
    }
    return true;
}
}  // namespace

std::optional<MomentumJump> momentumJump(CalculationContext& ctx, BlockPos src, BlockPos dest) {
    if (!ctx.allowParkour || !ctx.canSprint) return std::nullopt;
    if (!ctx.allowJumpAtBuildLimit && src.y >= ctx.bsi.maxY() - 4) return std::nullopt;
    const BlockPos d = dest - src;
    constexpr int length = kMomentumGap + 1;
    if (d.y != 0 || !((std::abs(d.x) == length && d.z == 0) || (d.x == 0 && std::abs(d.z) == length))) return std::nullopt;
    const int sx = (d.x > 0) - (d.x < 0), sz = (d.z > 0) - (d.z < 0);
    if (!ctx.isLoadedLive(src.x, src.z) || !ctx.isLoadedLive(dest.x, dest.z)) return std::nullopt;
    if (!plainFloor(ctx, src.x, src.y - 1, src.z) || !plainFloor(ctx, dest.x, dest.y - 1, dest.z)) return std::nullopt;
    if (!jumpRoom(ctx, src.x, src.y, src.z)) return std::nullopt;
    // the gap: open up to the top of the jump, and nothing to land on (a shorter jump would do)
    for (int i = 1; i <= kMomentumGap; ++i) {
        const int x = src.x + sx * i, z = src.z + sz * i;
        if (!ctx.isLoadedLive(x, z) || !jumpRoom(ctx, x, src.y, z)) return std::nullopt;
        if (canWalkOn(ctx, x, src.y - 1, z)) return std::nullopt;
    }
    // coming down into the landing block, and the half block we slide on after it
    for (int dy = 0; dy <= 2; ++dy)
        if (!fullyPassable(ctx, dest.x, dest.y + dy, dest.z)) return std::nullopt;
    if (!checkOvershootSafety(ctx, dest.x + sx, dest.y, dest.z + sz)) return std::nullopt;
    // the run-up: the hop on it needs the same room as the jump
    int runUp = 0;
    while (runUp < kMaxMomentumRunUp) {
        const int x = src.x - sx * (runUp + 1), z = src.z - sz * (runUp + 1);
        if (!ctx.isLoadedLive(x, z) || !plainFloor(ctx, x, src.y - 1, z) || !jumpRoom(ctx, x, src.y, z)) break;
        ++runUp;
    }
    if (runUp < kMinMomentumRunUp) return std::nullopt;
    // back up sneaking (the run-up may be one block wide), then sprint the run-up and the jump;
    // a little extra for a jump with a few tenths of a block of margin
    const double cost = runUp * SNEAK_ONE_BLOCK_COST + (runUp + length) * SPRINT_ONE_BLOCK_COST + 2 * ctx.jumpPenalty + 10;
    return MomentumJump{runUp, cost};
}

double shulkerClimb(CalculationContext& ctx, BlockPos src, BlockPos dest) {
    const auto delta = dest - src;
    if (!ctx.allowParkour || !ctx.allowParkourAscend || (delta.y != 2 && delta.y != 3) ||
        std::abs(delta.x) + std::abs(delta.z) != 1 || !ctx.isLoadedLive(dest.x, dest.z) ||
        std::ranges::find(ctx.failedShulkerClimbs, std::pair{src, dest}) != ctx.failedShulkerClimbs.end())
        return COST_INF;
    const BlockPos lower{dest.x, src.y, dest.z};
    // The opening lid is the step we stand on: it must open towards us (Bedrock facing ids).
    const uint8_t towardsUs = src.x > dest.x ? 5 : src.x < dest.x ? 4 : src.z > dest.z ? 3 : 2;
    const auto isBox = [&](BlockPos p) {
        if (ctx.props(p.x, p.y, p.z).parkourSupport != ParkourSupport::ShulkerBox || !ctx.permitsModify(p)) return false;
        const auto facing = ctx.blockFacing(p);
        return !facing || *facing == towardsUs;
    };
    if (!isBox(lower) || (delta.y == 3 && !isBox(lower.above())) ||
        !ctx.props(dest.x, dest.y - 1, dest.z).fullCube ||
        !ctx.props(src.x, src.y - 1, src.z).fullCube) return COST_INF;
    // The lids extend into the launch column. It needs room for three consecutive jumps.
    for (int y = src.y; y <= dest.y + 2; ++y)
        if (!fullyPassable(ctx, src.x, y, src.z) || ctx.isAvoided(ctx.props(src.x, y, src.z))) return COST_INF;
    for (int y = dest.y; y <= dest.y + 2; ++y)
        if (!fullyPassable(ctx, dest.x, y, dest.z) || ctx.isAvoided(ctx.props(dest.x, y, dest.z))) return COST_INF;
    return (delta.y == 3 ? 64 : 42) + delta.y * ctx.jumpPenalty;
}

double traverse(CalculationContext& ctx, int x, int y, int z, int destX, int destZ) {
    const BlockProps& pb0 = ctx.props(destX, y + 1, destZ);
    const BlockProps& pb1 = ctx.props(destX, y, destZ);
    const BlockProps& destOn = ctx.props(destX, y - 1, destZ);
    const BlockProps& srcDown = ctx.props(x, y - 1, z);
    bool standingOnABlock = mustBeSolidToWalkOn(ctx, x, y - 1, z, srcDown);
    bool frostWalker = standingOnABlock && !ctx.assumeWalkOnWater && canUseFrostWalker(ctx, destOn);

    if (frostWalker || canWalkOn(ctx, destX, y - 1, destZ, destOn)) {  // a walk, not a bridge
        double WC = WALK_ONE_BLOCK_COST;
        bool water = false;
        bool sneaking = false;
        if (isWater(pb0) || isWater(pb1)) {
            WC = ctx.waterWalkSpeed;
            water = true;
        } else {
            if (destOn.type == T::SoulSand) WC += (WALK_ONE_OVER_SOUL_SAND_COST - WALK_ONE_BLOCK_COST) / 2;
            else if (frostWalker) {
            } else if (destOn.type == T::Water) WC += ctx.walkOnWaterOnePenalty;
            if (srcDown.type == T::SoulSand) {
                WC += (WALK_ONE_OVER_SOUL_SAND_COST - WALK_ONE_BLOCK_COST) / 2;
            } else if (ctx.allowWalkOnMagmaBlocks && srcDown.type == T::Magma) {
                sneaking = true;
                WC += (SNEAK_ONE_BLOCK_COST - WALK_ONE_BLOCK_COST) / 2;
            }
        }
        double hardness1 = getMiningDurationTicks(ctx, destX, y, destZ, pb1, false);
        if (hardness1 >= COST_INF) return COST_INF;
        double hardness2 = getMiningDurationTicks(ctx, destX, y + 1, destZ, pb0, true);
        if (hardness1 == 0 && hardness2 == 0) {
            if (!water && !sneaking && ctx.canSprint) WC *= SPRINT_MULTIPLIER;  // nothing in the way: sprint
            return WC;
        }
        if (isClimbable(srcDown)) {
            hardness1 *= 5;
            hardness2 *= 5;
        }
        return WC + hardness1 + hardness2;
    }

    // a bridge: place a block under the destination
    if (isClimbable(srcDown)) return COST_INF;
    if (!isReplaceable(ctx, destX, y - 1, destZ, destOn)) return COST_INF;
    bool throughWater = isWater(pb0) || isWater(pb1);
    if (isWater(destOn) && throughWater) return COST_INF;  // traverse in water with assumeWalkOnWater
    double placeCost = ctx.costOfPlacingAt(destX, y - 1, destZ, destOn);
    if (placeCost >= COST_INF) return COST_INF;
    double hardness1 = getMiningDurationTicks(ctx, destX, y, destZ, pb1, false);
    if (hardness1 >= COST_INF) return COST_INF;
    double hardness2 = getMiningDurationTicks(ctx, destX, y + 1, destZ, pb0, true);
    double WC = throughWater ? ctx.waterWalkSpeed : WALK_ONE_BLOCK_COST;
    if (hasPlaceOption(ctx, destX, y - 1, destZ, x, z)) return WC + placeCost + hardness1 + hardness2;

    // no side to place against: sneak to the edge and place against the block we stand on
    if (srcDown.type == T::SoulSand || (srcDown.type == T::Slab && !srcDown.doubleSlab)) return COST_INF;
    if (!standingOnABlock) return COST_INF;  // swimming
    const BlockProps& blockSrc = ctx.props(x, y, z);
    if ((blockSrc.type == T::LilyPad || blockSrc.type == T::Carpet) && srcDown.hasFluid()) return COST_INF;
    WC = WC * (SNEAK_ONE_BLOCK_COST / WALK_ONE_BLOCK_COST);
    return WC + placeCost + hardness1 + hardness2;
}

double ascend(CalculationContext& ctx, int x, int y, int z, int destX, int destZ) {
    const BlockProps& toPlace = ctx.props(destX, y, destZ);
    // Bedrock addition: landing a jump on the 3 pixel top of a ladder misses easily, and the
    // ladder below does not always catch the fall. Climb it instead (into a column that goes on
    // is fine: that is how a ladder is entered).
    if (toPlace.type == T::Ladder && !isClimbable(ctx.props(destX, y + 1, destZ))) return COST_INF;
    double additionalPlacementCost = 0;
    if (!canWalkOn(ctx, destX, y, destZ, toPlace)) {
        additionalPlacementCost = ctx.costOfPlacingAt(destX, y, destZ, toPlace);
        if (additionalPlacementCost >= COST_INF) return COST_INF;
        if (!isReplaceable(ctx, destX, y, destZ, toPlace)) return COST_INF;
        if (!hasPlaceOption(ctx, destX, y, destZ, x, z)) return COST_INF;
    }
    const BlockProps& srcUp2 = ctx.props(x, y + 2, z);
    if (ctx.props(x, y + 3, z).falling && (canWalkThrough(ctx, x, y + 1, z) || !srcUp2.falling)) {
        return COST_INF;  // it would fall on us and could suffocate us
    }
    const BlockProps& srcDown = ctx.props(x, y - 1, z);
    // Bedrock addition: on the thin top of a ladder (the climb ends pressed against its wall)
    // the jump works as from a block; from inside a ladder it does not.
    const bool ladderTop = srcDown.type == T::Ladder && ladderWall(srcDown) && !isClimbable(ctx.props(x, y, z));
    if (isClimbable(srcDown) && !(ladderTop && additionalPlacementCost == 0)) return COST_INF;
    bool jumpingFromBottomSlab = isBottomSlab(srcDown);
    bool jumpingToBottomSlab = isBottomSlab(toPlace);
    if (jumpingFromBottomSlab && !jumpingToBottomSlab) return COST_INF;  // only slab -> slab from a bottom slab

    double walk;
    if (jumpingToBottomSlab) {
        if (jumpingFromBottomSlab) {
            walk = std::max(JUMP_ONE_BLOCK_COST, WALK_ONE_BLOCK_COST) + ctx.jumpPenalty;
        } else {
            walk = WALK_ONE_BLOCK_COST;  // just walk onto the slab
        }
    } else {
        if (toPlace.type == T::SoulSand) walk = WALK_ONE_OVER_SOUL_SAND_COST;
        else if (toPlace.type == T::Magma) walk = SNEAK_ONE_BLOCK_COST;
        else walk = std::max(JUMP_ONE_BLOCK_COST, WALK_ONE_BLOCK_COST);
        walk += ctx.jumpPenalty;
    }

    double total = walk + additionalPlacementCost;
    total += getMiningDurationTicks(ctx, x, y + 2, z, srcUp2, false);
    if (total >= COST_INF) return COST_INF;
    total += getMiningDurationTicks(ctx, destX, y + 1, destZ, false);
    if (total >= COST_INF) return COST_INF;
    total += getMiningDurationTicks(ctx, destX, y + 2, destZ, true);
    return total;
}

void descend(CalculationContext& ctx, int x, int y, int z, int destX, int destZ, MoveResult& res) {
    double total = 0;
    const BlockProps& destDown = ctx.props(destX, y - 1, destZ);
    total += getMiningDurationTicks(ctx, destX, y - 1, destZ, destDown, false);
    if (total >= COST_INF) return;
    total += getMiningDurationTicks(ctx, destX, y, destZ, false);
    if (total >= COST_INF) return;
    total += getMiningDurationTicks(ctx, destX, y + 1, destZ, true);
    if (total >= COST_INF) return;

    const BlockProps& fromDown = ctx.props(x, y - 1, z);
    if (isClimbable(fromDown)) return;

    const BlockProps& below = ctx.props(destX, y - 2, destZ);
    if (!canWalkOn(ctx, destX, y - 2, destZ, below)) {
        dynamicFallCost(ctx, x, y, z, destX, destZ, total, below, res);
        return;
    }
    if (isClimbable(destDown)) return;
    if (canUseFrostWalker(ctx, destDown)) return;  // the water freezes when we walk into it

    // walk half the block plus 0.3 to the edge, then the other 0.2 while falling
    double walk = WALK_OFF_BLOCK_COST;
    if (fromDown.type == T::SoulSand) walk *= WALK_ONE_OVER_SOUL_SAND_COST / WALK_ONE_BLOCK_COST;
    total += walk + std::max(fallCost(1), CENTER_AFTER_FALL_COST);
    res.x = destX;
    res.y = y - 1;
    res.z = destZ;
    res.cost = total;
}

bool dynamicFallCost(CalculationContext& ctx, int x, int y, int z, int destX, int destZ, double frontBreak,
                     const BlockProps& below, MoveResult& res) {
    (void)x;
    (void)z;
    if (frontBreak != 0 && ctx.props(destX, y + 2, destZ).falling) {
        // breaking blocks in front: don't let anything fall into this column
        return false;
    }
    if (!canWalkThrough(ctx, destX, y - 2, destZ, below)) return false;
    double costSoFar = 0;
    int effectiveStartHeight = y;
    for (int fallHeight = 3;; fallHeight++) {
        int newY = y - fallHeight;
        if (newY < ctx.bsi.minY()) return false;  // the void
        bool reachedMinimum = fallHeight >= ctx.minFallHeight;
        const BlockProps& onto = ctx.props(destX, newY, destZ);
        int unprotectedFallHeight = fallHeight - (y - effectiveStartHeight);
        if (unprotectedFallHeight >= 4097) return false;
        double tentativeCost = WALK_OFF_BLOCK_COST + fallCost(unprotectedFallHeight) + frontBreak + costSoFar;
        if (reachedMinimum && isWater(onto)) {
            if (!canWalkThrough(ctx, destX, newY, destZ, onto)) return false;
            if (ctx.assumeWalkOnWater) return false;
            if (isFlowing(ctx, destX, newY, destZ, onto)) return false;
            if (!canWalkOn(ctx, destX, newY - 1, destZ)) return false;  // we could punch through the water
            res.x = destX;
            res.y = newY;
            res.z = destZ;
            res.cost = tentativeCost;
            return false;
        }
        if (reachedMinimum && ctx.allowFallIntoLava && isLava(onto)) {
            res.x = destX;
            res.y = newY;
            res.z = destZ;
            res.cost = tentativeCost;
            return false;
        }
        if (unprotectedFallHeight <= 11 && isClimbable(onto)) {
            // grabbing a ladder/vine resets the fall speed
            costSoFar += fallCost(unprotectedFallHeight - 1);
            costSoFar += LADDER_DOWN_ONE_COST;
            effectiveStartHeight = newY;
            continue;
        }
        if (canWalkThrough(ctx, destX, newY, destZ, onto)) continue;
        if (!canWalkOn(ctx, destX, newY, destZ, onto)) return false;
        if (isBottomSlab(onto)) return false;  // glitchy, more fall damage than expected
        if (reachedMinimum && unprotectedFallHeight <= ctx.maxFallHeightNoWater + 1) {
            res.x = destX;
            res.y = newY + 1;
            res.z = destZ;
            res.cost = tentativeCost;
            return false;
        }
        if (reachedMinimum && ctx.hasWaterBucket && unprotectedFallHeight <= ctx.maxFallHeightBucket + 1) {
            res.x = destX;
            res.y = newY + 1;
            res.z = destZ;
            res.cost = tentativeCost + ctx.placeBucketCost();
            return true;
        }
        return false;
    }
}

void diagonal(CalculationContext& ctx, int x, int y, int z, int destX, int destZ, MoveResult& res) {
    if (!canWalkThrough(ctx, destX, y + 1, destZ)) return;
    const BlockProps& destInto = ctx.props(destX, y, destZ);
    const BlockProps* destWalkOn;
    const BlockProps* fromDown;
    bool ascend = false, descend = false, frostWalker = false;
    if (!canWalkThrough(ctx, destX, y, destZ, destInto)) {
        ascend = true;
        if (!ctx.allowDiagonalAscend || !canWalkThrough(ctx, x, y + 2, z) || !canWalkOn(ctx, destX, y, destZ, destInto) ||
            !canWalkThrough(ctx, destX, y + 2, destZ)) {
            return;
        }
        destWalkOn = &destInto;
        fromDown = &ctx.props(x, y - 1, z);
    } else {
        destWalkOn = &ctx.props(destX, y - 1, destZ);
        fromDown = &ctx.props(x, y - 1, z);
        bool standingOnABlock = mustBeSolidToWalkOn(ctx, x, y - 1, z, *fromDown);
        frostWalker = standingOnABlock && canUseFrostWalker(ctx, *destWalkOn);
        if (!frostWalker && !canWalkOn(ctx, destX, y - 1, destZ, *destWalkOn)) {
            descend = true;
            if (!ctx.allowDiagonalDescend || !canWalkOn(ctx, destX, y - 2, destZ) ||
                !canWalkThrough(ctx, destX, y - 1, destZ, *destWalkOn)) {
                return;
            }
        }
        frostWalker = frostWalker && !ctx.assumeWalkOnWater;
    }
    double multiplier = WALK_ONE_BLOCK_COST;
    bool sneaking = false;
    if (destWalkOn->type == T::SoulSand) {
        multiplier += (WALK_ONE_OVER_SOUL_SAND_COST - WALK_ONE_BLOCK_COST) / 2;
    } else if (ctx.allowWalkOnMagmaBlocks && destWalkOn->type == T::Magma) {
        multiplier += (SNEAK_ONE_BLOCK_COST - WALK_ONE_BLOCK_COST) / 2;
        sneaking = true;
    } else if (frostWalker) {
    } else if (destWalkOn->type == T::Water) {
        multiplier += ctx.walkOnWaterOnePenalty * SQRT_2;
    }
    if (isClimbable(*fromDown)) return;
    if (fromDown->type == T::SoulSand) {
        multiplier += (WALK_ONE_OVER_SOUL_SAND_COST - WALK_ONE_BLOCK_COST) / 2;
    } else if (ctx.allowWalkOnMagmaBlocks && fromDown->type == T::Magma) {
        multiplier += (SNEAK_ONE_BLOCK_COST - WALK_ONE_BLOCK_COST) / 2;
        sneaking = true;
    }

    // (Java checks cuttingOver1 for magma twice; same here)
    const BlockProps& cuttingOver1 = ctx.props(x, y - 1, destZ);
    if ((!ctx.allowWalkOnMagmaBlocks && cuttingOver1.type == T::Magma) || isLava(cuttingOver1)) return;
    const BlockProps& cuttingOver2 = ctx.props(destX, y - 1, z);
    if ((!ctx.allowWalkOnMagmaBlocks && cuttingOver1.type == T::Magma) || isLava(cuttingOver2)) return;

    bool water = false;
    const BlockProps& startIn = ctx.props(x, y, z);
    if (isWater(startIn) || isWater(destInto)) {
        if (ascend) return;
        multiplier = ctx.waterWalkSpeed;  // floating: what is below doesn't matter
        water = true;
    }
    const BlockProps& pb0 = ctx.props(x, y, destZ);
    const BlockProps& pb2 = ctx.props(destX, y, z);
    if (ascend) {
        bool ATop = canWalkThrough(ctx, x, y + 2, destZ);
        bool AMid = canWalkThrough(ctx, x, y + 1, destZ);
        bool ALow = canWalkThrough(ctx, x, y, destZ, pb0);
        bool BTop = canWalkThrough(ctx, destX, y + 2, z);
        bool BMid = canWalkThrough(ctx, destX, y + 1, z);
        bool BLow = canWalkThrough(ctx, destX, y, z, pb2);
        if ((!(ATop && AMid && ALow) && !(BTop && BMid && BLow)) || avoidWalkingInto(pb0) || avoidWalkingInto(pb2) ||
            (ATop && AMid && canWalkOn(ctx, x, y, destZ, pb0)) || (BTop && BMid && canWalkOn(ctx, destX, y, z, pb2)) ||
            (!ATop && AMid && ALow) || (!BTop && BMid && BLow)) {
            return;
        }
        res.cost = multiplier * SQRT_2 + JUMP_ONE_BLOCK_COST;
        res.x = destX;
        res.z = destZ;
        res.y = y + 1;
        return;
    }
    double optionA = getMiningDurationTicks(ctx, x, y, destZ, pb0, false);
    double optionB = getMiningDurationTicks(ctx, destX, y, z, pb2, false);
    if (optionA != 0 && optionB != 0) return;
    const BlockProps& pb1 = ctx.props(x, y + 1, destZ);
    optionA += getMiningDurationTicks(ctx, x, y + 1, destZ, pb1, true);
    if (optionA != 0 && optionB != 0) return;
    const BlockProps& pb3 = ctx.props(destX, y + 1, z);
    if (optionA == 0 && ((avoidWalkingInto(pb2) && pb2.type != T::Water) || avoidWalkingInto(pb3))) return;
    optionB += getMiningDurationTicks(ctx, destX, y + 1, z, pb3, true);
    if (optionA != 0 && optionB != 0) return;
    if (optionB == 0 && ((avoidWalkingInto(pb0) && pb0.type != T::Water) || avoidWalkingInto(pb1))) return;
    if (optionA != 0 || optionB != 0) {
        multiplier *= SQRT_2 - 0.001;  // edging around a corner
        if (isClimbable(startIn)) return;  // would climb instead of moving sideways
    } else if (ctx.canSprint && !water && !sneaking) {
        multiplier *= SPRINT_MULTIPLIER;
    }
    res.cost = multiplier * SQRT_2;
    if (descend) {
        res.cost += std::max(fallCost(1), CENTER_AFTER_FALL_COST);
        res.y = y - 1;
    } else {
        res.y = y;
    }
    res.x = destX;
    res.z = destZ;
}

double pillar(CalculationContext& ctx, int x, int y, int z) {
    const BlockProps& fromState = ctx.props(x, y, z);
    bool ladder = isClimbable(fromState);
    if (!ladder && ctx.allowParkour && ctx.props(x, y + 1, z).type == T::Ladder) {
        // Bedrock addition: a ladder starting at head height. A jump brings the feet into it.
        const BlockProps& floor = ctx.props(x, y - 1, z);
        if (canWalkOn(ctx, x, y - 1, z, floor) && !isClimbable(floor) && !floor.hasFluid() && !fromState.hasFluid() &&
            fullyPassable(ctx, x, y, z, fromState) && !isBottomSlab(floor))
            return JUMP_ONE_BLOCK_COST + LADDER_UP_ONE_COST + ctx.jumpPenalty;
    }
    const BlockProps& fromDown = ctx.props(x, y - 1, z);
    if (!ladder) {
        if (isClimbable(fromDown)) return COST_INF;  // can't pillar from a ladder onto something else
        if (isBottomSlab(fromDown)) return COST_INF;
    }
    const BlockProps& toBreak = ctx.props(x, y + 2, z);
    if (toBreak.type == T::FenceGate) return COST_INF;
    const BlockProps* srcUp = nullptr;
    if (isWater(fromState)) {
        srcUp = &ctx.props(x, y + 1, z);
        // The final submerged step has air above the destination. Requiring
        // water at head height traps the search one block below the surface.
        if (isWater(*srcUp) && (isWater(toBreak) || canWalkThrough(ctx, x, y + 2, z, toBreak)))
            return LADDER_UP_ONE_COST;
    }
    double placeCost = 0;
    if (!ladder) {
        placeCost = ctx.costOfPlacingAt(x, y, z, fromState);
        if (placeCost >= COST_INF) return COST_INF;
        if (fromDown.type == T::Air) placeCost += 0.1;  // slightly penalize pillaring on air
    }
    if ((isLiquid(fromState) && !canPlaceAgainst(fromDown)) || (isLiquid(fromDown) && ctx.assumeWalkOnWater)) {
        return COST_INF;
    }
    if ((fromState.type == T::LilyPad || fromState.type == T::Carpet) && fromDown.hasFluid()) return COST_INF;
    double hardness = getMiningDurationTicks(ctx, x, y + 2, z, toBreak, true);
    if (hardness >= COST_INF) return COST_INF;
    if (hardness != 0) {
        if (isClimbable(toBreak)) {
            hardness = 0;  // we climb it instead of breaking it
        } else {
            const BlockProps& check = ctx.props(x, y + 3, z);
            if (check.falling) {
                if (!srcUp) srcUp = &ctx.props(x, y + 1, z);
                if (!toBreak.falling || !srcUp->falling) return COST_INF;
            }
        }
    }
    if (ladder) return LADDER_UP_ONE_COST + hardness * 5;
    return JUMP_ONE_BLOCK_COST + placeCost + ctx.jumpPenalty + hardness;
}

double downward(CalculationContext& ctx, int x, int y, int z) {
    if (!ctx.allowDownward) return COST_INF;
    const BlockProps& down = ctx.props(x, y - 1, z);
    // Bedrock addition: from the thin top of a ladder hanging in the air, down into it (stepping
    // off the plate's outer edge): the ladder holds us, to climb it or the next one from inside
    if (down.type == T::Ladder && ladderWall(down) && !canWalkOn(ctx, x, y - 2, z)) return LADDER_DOWN_ONE_COST;
    if (!canWalkOn(ctx, x, y - 2, z)) return COST_INF;
    if (isClimbable(down)) return LADDER_DOWN_ONE_COST;
    // it may be falling now, but it will be air by the time we get here
    return fallCost(1) + getMiningDurationTicks(ctx, x, y - 1, z, down, false);
}

// Bedrock addition: Baritone only jumps along the four axes, so two blocks that only meet
// diagonally across a gap (a common parkour layout) had no path at all. Same rules as
// parkour(): head room for the jump, no ladder/stairs/slab/fluid to jump from, never over lava,
// and the cells the player's body sweeps through are clear (on a diagonal that includes the
// two "corner" cells of every step).
void parkourDiagonal(CalculationContext& ctx, int x, int y, int z, int sx, int sz, MoveResult& res) {
    if (!ctx.allowParkour) return;
    if (!ctx.allowJumpAtBuildLimit && y >= ctx.bsi.maxY() - 1) return;
    if (!fullyPassable(ctx, x, y + 2, z)) return;
    const BlockProps& standingOn = ctx.props(x, y - 1, z);
    if (isClimbable(standingOn) || standingOn.type == T::Stairs || isBottomSlab(standingOn)) return;
    if (standingOn.parkourSupport != ParkourSupport::None) return;  // parkourJump's job
    if (standingOn.type == T::SoulSand || (ctx.allowWalkOnMagmaBlocks && standingOn.type == T::Magma)) return;
    if (ctx.assumeWalkOnWater && standingOn.hasFluid()) return;
    if (ctx.props(x, y, z).hasFluid()) return;  // can't jump out of water

    // the first diagonal cell is the gap: with a floor there, a diagonal walk does it
    if (canWalkOn(ctx, x + sx, y - 1, z + sz)) return;
    const int maxJump = ctx.canSprint ? 3 : 2;
    auto clearColumn = [&](int cx, int cz, int top) {
        for (int dy = 0; dy <= top; ++dy)
            if (!fullyPassable(ctx, cx, y + dy, cz)) return false;
        return true;
    };
    auto overLava = [&](int cx, int cz) { const BlockProps& p = ctx.props(cx, y - 1, cz); return avoidWalkingInto(p) && p.type != T::Water; };
    for (int i = 1; i <= maxJump; ++i) {
        int cx = x + sx * i, cz = z + sz * i;
        // the two corners between the previous diagonal cell and this one
        int ax = x + sx * i, az = z + sz * (i - 1);
        int bx = x + sx * (i - 1), bz = z + sz * i;
        if (!clearColumn(ax, az, 2) || !clearColumn(bx, bz, 2)) return;
        if (overLava(ax, az) || overLava(bx, bz)) return;
        if (!clearColumn(cx, cz, i < maxJump ? 2 : 1)) return;
        const BlockProps& landingOn = ctx.props(cx, y - 1, cz);
        if (i >= 2 && landingOn.type != T::Farmland && landingOn.parkourSupport == ParkourSupport::None &&
            canWalkOn(ctx, cx, y - 1, cz, landingOn) && fullyPassable(ctx, cx, y + 2, cz)) {
            if (!checkOvershootSafety(ctx, cx + sx, y, cz + sz)) return;
            res.x = cx;
            res.y = y;
            res.z = cz;
            res.cost = SPRINT_ONE_BLOCK_COST * i * std::numbers::sqrt2 + ctx.jumpPenalty;
            return;
        }
        if (overLava(cx, cz)) return;
        if (i >= 2 && !fullyPassable(ctx, cx, y + 3, cz)) return;  // the top of the jump
    }
}

void parkour(CalculationContext& ctx, int x, int y, int z, Direction dir, MoveResult& res) {
    if (!ctx.allowParkour) return;
    if (!ctx.allowJumpAtBuildLimit && y >= ctx.bsi.maxY() - 1) return;
    DirOffset o = offsetOf(dir);
    int xDiff = o.x, zDiff = o.z;
    if (!fullyPassable(ctx, x + xDiff, y, z + zDiff)) return;
    const BlockProps& adj = ctx.props(x + xDiff, y - 1, z + zDiff);
    if (canWalkOn(ctx, x + xDiff, y - 1, z + zDiff, adj)) return;  // a traverse would do
    if (avoidWalkingInto(adj) && adj.type != T::Water) return;  // magma sucks (and never over lava)
    if (!fullyPassable(ctx, x + xDiff, y + 1, z + zDiff)) return;
    if (!headRoom(ctx, x + xDiff, y + 2, z + zDiff)) return;
    if (!headRoom(ctx, x, y + 2, z)) return;
    const BlockProps& standingOn = ctx.props(x, y - 1, z);
    if (isClimbable(standingOn) || standingOn.type == T::Stairs || isBottomSlab(standingOn)) return;
    // Candles, end rods, panes: their real height and width make a different jump;
    // parkourJump simulates those.
    if (standingOn.parkourSupport != ParkourSupport::None) return;
    if (ctx.assumeWalkOnWater && standingOn.hasFluid()) return;
    if (ctx.props(x, y, z).hasFluid()) return;  // can't jump out of water

    // TODO(bedrock): measure the real sprint-jump distance on Bedrock
    int maxJump;
    if (ctx.allowWalkOnMagmaBlocks && standingOn.type == T::Magma) maxJump = 2;
    else if (standingOn.type == T::SoulSand) maxJump = 2;  // 1 block gap
    else maxJump = ctx.canSprint ? 4 : 3;

    int verifiedMaxJump = 1;
    bool headBump = false;  // something 3 blocks up stops the jump just below its top
    for (int i = 2; i <= maxJump; i++) {
        int destX = x + xDiff * i;
        int destZ = z + zDiff * i;
        if (!fullyPassable(ctx, destX, y + 1, destZ)) break;
        if (!headRoom(ctx, destX, y + 2, destZ)) break;

        const BlockProps& destInto = ctx.props(destX, y, destZ);
        if (!fullyPassable(ctx, destX, y, destZ, destInto)) {
            if (i <= 3 && !headBump && ctx.allowParkourAscend && ctx.canSprint && canWalkOn(ctx, destX, y, destZ, destInto) &&
                checkOvershootSafety(ctx, destX + xDiff, y + 1, destZ + zDiff)) {
                res.x = destX;
                res.y = y + 1;
                res.z = destZ;
                res.cost = i * SPRINT_ONE_BLOCK_COST + ctx.jumpPenalty;
                return;
            }
            break;
        }

        const BlockProps& landingOn = ctx.props(destX, y - 1, destZ);
        // landing on a candle, rod or pane is parkourJump's job; jumping over one is fine
        const bool specialLanding = landingOn.parkourSupport != ParkourSupport::None && landingOn.collides;
        if (!specialLanding && ((landingOn.type != T::Farmland && canWalkOn(ctx, destX, y - 1, destZ, landingOn)) ||
            (std::min(16, ctx.frostWalker + 2) >= i && canUseFrostWalker(ctx, landingOn)))) {
            if (checkOvershootSafety(ctx, destX + xDiff, y, destZ + zDiff)) {
                res.x = destX;
                res.y = y;
                res.z = destZ;
                res.cost = costFromJumpDistance(i) + ctx.jumpPenalty;
                return;
            }
            break;
        }
        if (!fullyPassable(ctx, destX, y + 3, destZ)) {
            // Bedrock addition: Baritone gives up here. A block 3 up only lowers the top of the
            // jump from 1.25 to 1.2 (the head is 1.8 tall): about one tick less of flight, still
            // enough for a flat 3 block gap, but no longer for climbing a block. Parkour maps
            // put candles and ceilings there on purpose.
            const BlockProps& ceiling = ctx.props(destX, y + 3, destZ);
            if (ceiling.hasFluid() || avoidWalkingInto(ceiling) || ctx.isAvoided(ceiling)) break;
            headBump = true;
        }
        verifiedMaxJump = i;
    }

    if (!ctx.allowParkourPlace) return;
    for (int i = verifiedMaxJump; i > 1; i--) {
        int destX = x + i * xDiff;
        int destZ = z + i * zDiff;
        const BlockProps& toReplace = ctx.props(destX, y - 1, destZ);
        double placeCost = ctx.costOfPlacingAt(destX, y - 1, destZ, toReplace);
        if (placeCost >= COST_INF) continue;
        if (!isReplaceable(ctx, destX, y - 1, destZ, toReplace)) continue;
        if (!checkOvershootSafety(ctx, destX + xDiff, y, destZ + zDiff)) continue;
        // can't turn around fast enough to place against the block we jumped from
        if (hasPlaceOption(ctx, destX, y - 1, destZ, destX - xDiff, destZ - zDiff)) {
            res.x = destX;
            res.y = y;
            res.z = destZ;
            res.cost = costFromJumpDistance(i) + placeCost + ctx.jumpPenalty;
            return;
        }
    }
}

void ladderJump(CalculationContext& ctx, int x, int y, int z, Direction dir, MoveResult& res) {
    if (!ctx.allowParkour) return;
    if (!ctx.allowJumpAtBuildLimit && y >= ctx.bsi.maxY() - 2) return;
    const DirOffset o = offsetOf(dir);
    const BlockProps& standingOn = ctx.props(x, y - 1, z);
    if (!canWalkOn(ctx, x, y - 1, z, standingOn) || isClimbable(standingOn) || standingOn.type == T::Stairs ||
        isBottomSlab(standingOn) || standingOn.parkourSupport != ParkourSupport::None || standingOn.hasFluid() ||
        standingOn.type == T::SoulSand || standingOn.type == T::Honey) return;
    const BlockProps& at = ctx.props(x, y, z);
    if (at.hasFluid() || isClimbable(at) || !headRoom(ctx, x, y + 2, z)) return;
    for (int i = 1; i <= 2; ++i) {
        const int lx = x + o.x * i, lz = z + o.z * i;
        if (!ctx.isLoadedLive(lx, lz)) return;
        const BlockProps& ladder = ctx.props(lx, y + 1, lz);
        if (ladder.type == T::Ladder) {
            // the legs cross the cell below it before the top of the jump; the head goes on up
            if (!fullyPassable(ctx, lx, y, lz) || !headRoom(ctx, lx, y + 2, lz)) return;
            // the block behind the ladder stops us in its cell, and pressing into it climbs
            if (!ctx.props(lx + o.x, y + 1, lz + o.z).fullCube) return;
            res.x = lx;
            res.y = y + 1;
            res.z = lz;
            res.cost = WALK_ONE_BLOCK_COST * i + JUMP_ONE_BLOCK_COST + ctx.jumpPenalty;
            return;
        }
        if (!fullyPassable(ctx, lx, y, lz) || !fullyPassable(ctx, lx, y + 1, lz) || !headRoom(ctx, lx, y + 2, lz)) return;
        const BlockProps& under = ctx.props(lx, y - 1, lz);
        if (avoidWalkingInto(under) && under.type != T::Water) return;  // never over lava
    }
}

std::optional<EdgeWalk> edgeWalk(CalculationContext& ctx, BlockPos src, BlockPos dest) {
    const BlockPos d = dest - src;
    const int n = std::abs(d.x) + std::abs(d.z);
    if (d.y != 0 || (d.x != 0 && d.z != 0) || n < 2 || n > 5) return std::nullopt;
    if (!ctx.isLoadedLive(src.x, src.z) || !ctx.isLoadedLive(dest.x, dest.z)) return std::nullopt;
    const BlockPos step{(d.x > 0) - (d.x < 0), 0, (d.z > 0) - (d.z < 0)};
    const auto standable = [&](BlockPos p) {
        const BlockProps& floor = ctx.props(p.x, p.y - 1, p.z);
        return floor.fullCube && canWalkOn(ctx, p.x, p.y - 1, p.z, floor) && !avoidWalkingInto(floor) &&
               fullyPassable(ctx, p.x, p.y, p.z) && fullyPassable(ctx, p.x, p.y + 1, p.z);
    };
    if (!standable(src) || !standable(dest)) return std::nullopt;
    for (int side : {1, -1}) {
        const BlockPos perp{-step.z * side, 0, step.x * side};
        bool ok = true, blocked = false;
        for (int k = 0; k <= n && ok; ++k) {
            const BlockPos c{src.x + step.x * k, src.y, src.z + step.z * k};
            const BlockPos beside = c + perp;
            if (!ctx.isLoadedLive(beside.x, beside.z)) ok = false;
            // the body hangs over the column beside
            for (int dy = 0; dy <= 1 && ok; ++dy) {
                const BlockProps& b = ctx.props(beside.x, c.y + dy, beside.z);
                if (!fullyPassable(ctx, beside.x, c.y + dy, beside.z, b) || ctx.isAvoided(b)) ok = false;
            }
            if (isLava(ctx.props(beside.x, c.y - 1, beside.z))) ok = false;
            if (k == 0 || k == n || !ok) continue;
            // a floor with a full top up to its edge, and only blocks that keep clear of it
            const BlockProps& floor = ctx.props(c.x, c.y - 1, c.z);
            if (!floor.fullCube || avoidWalkingInto(floor)) ok = false;
            for (int dy = 0; dy <= 1 && ok; ++dy) {
                const BlockProps& p = ctx.props(c.x, c.y + dy, c.z);
                if (fullyPassable(ctx, c.x, c.y + dy, c.z, p)) continue;
                blocked = true;
                const double half = perp.x != 0 ? p.collisionHalfX() : p.collisionHalfZ();
                if (!p.collides || p.hasFluid() || avoidWalkingInto(p) || ctx.isAvoided(p) || p.type == T::Ladder ||
                    p.parkourSupport == ParkourSupport::Pane || half > 0.44) ok = false;
            }
        }
        // with nothing in the way, walking along the middle is simpler
        if (ok && blocked) return EdgeWalk{side, (n + 2) * SNEAK_ONE_BLOCK_COST};
    }
    return std::nullopt;
}

double ladderHop(CalculationContext& ctx, BlockPos src, BlockPos dest) {
    if (!ctx.allowParkour) return COST_INF;
    const BlockProps& top = ctx.props(src.x, src.y - 1, src.z);
    if (top.type != T::Ladder || !ladderWall(top)) return COST_INF;  // standing on the thin top of a ladder
    const BlockPos d = dest - src;
    if (d.y < 0 || d.y > 1 || (d.x == 0 && d.z == 0) || std::abs(d.x) > 2 || std::abs(d.z) > 2) return COST_INF;
    const BlockProps& target = ctx.props(dest.x, dest.y, dest.z);
    if (target.type != T::Ladder || !ladderWall(target)) return COST_INF;
    if (!ctx.isLoadedLive(src.x, src.z) || !ctx.isLoadedLive(dest.x, dest.z)) return COST_INF;
    if (d.y == 1) {
        // the feet reach one block up from about 1 to 1.6 blocks of flight
        const double gx = std::max(0.0, std::abs(d.x) - 0.5), gz = std::max(0.0, std::abs(d.z) - 0.5);
        if (gx * gx + gz * gz > 1.6 * 1.6) return COST_INF;
    }
    const BlockProps& at = ctx.props(src.x, src.y, src.z);
    if (!fullyPassable(ctx, src.x, src.y, src.z, at) || !headRoom(ctx, src.x, src.y + 1, src.z) ||
        !headRoom(ctx, src.x, src.y + 2, src.z) || !headRoom(ctx, dest.x, dest.y + 1, dest.z)) return COST_INF;
    // through the cells between, along x then z or z then x (around a corner)
    const auto open = [&](int x, int z) {
        for (int y = src.y; y <= std::max(src.y + 1, dest.y + 1); ++y) {
            const BlockProps& p = ctx.props(x, y, z);
            if (!(fullyPassable(ctx, x, y, z, p) || p.type == T::Ladder) || avoidWalkingInto(p) || ctx.isAvoided(p)) return false;
        }
        return !isLava(ctx.props(x, src.y - 1, z));
    };
    const auto route = [&](bool xFirst) {
        int x = src.x, z = src.z;
        for (int i = 0; i < std::abs(d.x) + std::abs(d.z) - 1; ++i) {
            const bool moveX = xFirst ? x != dest.x : z == dest.z;
            if (moveX) x += d.x > 0 ? 1 : -1;
            else z += d.z > 0 ? 1 : -1;
            if (!open(x, z)) return false;
        }
        return true;
    };
    if (!route(true) && !route(false)) return COST_INF;
    // The takeoff is at the end of the plate on the destination's side, the feet past the
    // ladder's cell (above it, Bedrock's client takes the jump for a climb): the cell there must
    // be open. A ladder beside it on the same wall (a staircase of ladders) has its plate in the
    // way: that one is climbed from inside instead (LadderShimmy one up).
    const BlockPos wall = *ladderWall(top);
    int sx = d.x - (d.x * wall.x + d.z * wall.z) * wall.x, sz = d.z - (d.x * wall.x + d.z * wall.z) * wall.z;
    if (sx == 0 && sz == 0) {  // straight out from the wall: the takeoff goes right
        sx = -wall.z;
        sz = wall.x;
    }
    sx = (sx > 0) - (sx < 0);
    sz = (sz > 0) - (sz < 0);
    for (int y = src.y; y <= src.y + 1; ++y)
        if (!fullyPassable(ctx, src.x + sx, y, src.z + sz)) return COST_INF;
    return JUMP_ONE_BLOCK_COST + SPRINT_ONE_BLOCK_COST * std::hypot(d.x, d.z) + 8 + ctx.jumpPenalty;
}

double ladderShimmy(CalculationContext& ctx, BlockPos src, BlockPos dest) {
    const BlockPos d = dest - src;
    if (d.y < 0 || d.y > 1 || std::abs(d.x) + std::abs(d.z) != 1) return COST_INF;
    const BlockProps& from = ctx.props(src.x, src.y, src.z);
    const BlockProps& to = ctx.props(dest.x, dest.y, dest.z);
    if (to.type != T::Ladder || !ladderWall(to)) return COST_INF;
    if (d.y == 1) {
        // A staircase of ladders on one wall, one along and one up: climb out of the top of this
        // one and step sideways into the next as the feet pass its bottom. In front of the plates
        // nothing is in the way; from the thin top the next one's plate would be.
        const auto wall = ladderWall(from), next = ladderWall(to);
        if (from.type != T::Ladder || !wall || *wall != *next || d.x * wall->x + d.z * wall->z != 0) return COST_INF;
        if (!fullyPassable(ctx, src.x, src.y + 1, src.z) || !headRoom(ctx, src.x, src.y + 2, src.z) ||
            !headRoom(ctx, dest.x, dest.y + 1, dest.z) || !headRoom(ctx, dest.x, dest.y + 2, dest.z)) return COST_INF;
        return LADDER_UP_ONE_COST + WALK_ONE_BLOCK_COST + 4;
    }
    if (!headRoom(ctx, src.x, src.y + 1, src.z) || !headRoom(ctx, dest.x, dest.y + 1, dest.z)) return COST_INF;
    if (from.type != T::Ladder) {
        // walking from a block into a ladder beside it, at the same height: it catches us
        const BlockProps& floor = ctx.props(src.x, src.y - 1, src.z);
        if (!fullyPassable(ctx, src.x, src.y, src.z, from) || !canWalkOn(ctx, src.x, src.y - 1, src.z, floor) ||
            isClimbable(floor) || *ladderWall(to) == BlockPos{-d.x, 0, -d.z}) return COST_INF;
        return WALK_ONE_BLOCK_COST;
    }
    // the plates must not stand in the way: never through the block a ladder hangs on
    const auto wall = ladderWall(from);
    if (wall && *wall == d) return COST_INF;
    return SNEAK_ONE_BLOCK_COST;
}

}  // namespace movement_costs

namespace {

using namespace movement_costs;

void applyDownward(const Move&, CalculationContext& ctx, int x, int y, int z, MoveResult& res) {
    res.x = x;
    res.y = y - 1;
    res.z = z;
    res.cost = downward(ctx, x, y, z);
}

void applyPillar(const Move&, CalculationContext& ctx, int x, int y, int z, MoveResult& res) {
    res.x = x;
    res.y = y + 1;
    res.z = z;
    res.cost = pillar(ctx, x, y, z);
}

void applyTraverse(const Move& m, CalculationContext& ctx, int x, int y, int z, MoveResult& res) {
    res.x = x + m.dx;
    res.y = y;
    res.z = z + m.dz;
    res.cost = traverse(ctx, x, y, z, x + m.dx, z + m.dz);
}

void applyAscend(const Move& m, CalculationContext& ctx, int x, int y, int z, MoveResult& res) {
    res.x = x + m.dx;
    res.y = y + 1;
    res.z = z + m.dz;
    res.cost = ascend(ctx, x, y, z, x + m.dx, z + m.dz);
}

void applyDescend(const Move& m, CalculationContext& ctx, int x, int y, int z, MoveResult& res) {
    descend(ctx, x, y, z, x + m.dx, z + m.dz, res);
}

void applyDiagonal(const Move& m, CalculationContext& ctx, int x, int y, int z, MoveResult& res) {
    diagonal(ctx, x, y, z, x + m.dx, z + m.dz, res);
}

void applyParkour(const Move& m, CalculationContext& ctx, int x, int y, int z, MoveResult& res) {
    parkour(ctx, x, y, z, m.direction, res);
}

void applyParkourDiagonal(const Move& m, CalculationContext& ctx, int x, int y, int z, MoveResult& res) {
    parkourDiagonal(ctx, x, y, z, m.dx > 0 ? 1 : -1, m.dz > 0 ? 1 : -1, res);
}

void applyParkourJump(const Move& m, CalculationContext& ctx, int x, int y, int z, MoveResult& res) {
    const BlockPos src{x, y, z};
    const BlockPos dest = src.offset(m.dx, m.dy, m.dz);
    if (auto jump = parkourJump(ctx, src, dest)) res = {dest.x, dest.y, dest.z, jump->cost};
}

void applyCornerJump(const Move& m, CalculationContext& ctx, int x, int y, int z, MoveResult& res) {
    const BlockPos src{x, y, z};
    const BlockPos dest = src.offset(m.dx, m.dy, m.dz);
    if (auto plan = planCornerJump(ctx, src, dest))
        res = {dest.x, dest.y, dest.z, 16.0 + plan->inputs.size() + ctx.jumpPenalty};
}

void applyLadderJump(const Move& m, CalculationContext& ctx, int x, int y, int z, MoveResult& res) {
    ladderJump(ctx, x, y, z, m.direction, res);
}

void applyEdgeWalk(const Move& m, CalculationContext& ctx, int x, int y, int z, MoveResult& res) {
    const BlockPos src{x, y, z}, dest = src.offset(m.dx, 0, m.dz);
    if (auto walk = edgeWalk(ctx, src, dest)) res = {dest.x, dest.y, dest.z, walk->cost};
}

void applyLadderHop(const Move& m, CalculationContext& ctx, int x, int y, int z, MoveResult& res) {
    const BlockPos src{x, y, z}, dest = src.offset(m.dx, m.dy, m.dz);
    res = {dest.x, dest.y, dest.z, ladderHop(ctx, src, dest)};
}

void applyLadderShimmy(const Move& m, CalculationContext& ctx, int x, int y, int z, MoveResult& res) {
    const BlockPos src{x, y, z}, dest = src.offset(m.dx, m.dy, m.dz);
    res = {dest.x, dest.y, dest.z, ladderShimmy(ctx, src, dest)};
}

void applyMomentumJump(const Move& m, CalculationContext& ctx, int x, int y, int z, MoveResult& res) {
    const BlockPos src{x, y, z}, dest = src.offset(m.dx, 0, m.dz);
    if (auto jump = momentumJump(ctx, src, dest)) res = {dest.x, dest.y, dest.z, jump->cost};
}

void applyShulkerClimb(const Move& m, CalculationContext& ctx, int x, int y, int z, MoveResult& res) {
    const BlockPos src{x, y, z};
    const BlockPos dest = src.offset(m.dx, m.dy, m.dz);
    res = {dest.x, dest.y, dest.z, shulkerClimb(ctx, src, dest)};
}

constexpr Direction N = Direction::North, S = Direction::South, E = Direction::East, W = Direction::West;

}  // namespace

std::span<const Move> allMoves() {
    using MT = MovementType;
    static const std::array<Move, 26> moves{{
        {"DOWNWARD", MT::Downward, 0, -1, 0, false, false, Direction::Down, applyDownward},
        {"PILLAR", MT::Pillar, 0, 1, 0, false, false, Direction::Up, applyPillar},
        {"TRAVERSE_NORTH", MT::Traverse, 0, 0, -1, false, false, N, applyTraverse},
        {"TRAVERSE_SOUTH", MT::Traverse, 0, 0, 1, false, false, S, applyTraverse},
        {"TRAVERSE_EAST", MT::Traverse, 1, 0, 0, false, false, E, applyTraverse},
        {"TRAVERSE_WEST", MT::Traverse, -1, 0, 0, false, false, W, applyTraverse},
        {"ASCEND_NORTH", MT::Ascend, 0, 1, -1, false, false, N, applyAscend},
        {"ASCEND_SOUTH", MT::Ascend, 0, 1, 1, false, false, S, applyAscend},
        {"ASCEND_EAST", MT::Ascend, 1, 1, 0, false, false, E, applyAscend},
        {"ASCEND_WEST", MT::Ascend, -1, 1, 0, false, false, W, applyAscend},
        {"DESCEND_EAST", MT::Descend, 1, -1, 0, false, true, E, applyDescend},
        {"DESCEND_WEST", MT::Descend, -1, -1, 0, false, true, W, applyDescend},
        {"DESCEND_NORTH", MT::Descend, 0, -1, -1, false, true, N, applyDescend},
        {"DESCEND_SOUTH", MT::Descend, 0, -1, 1, false, true, S, applyDescend},
        {"DIAGONAL_NORTHEAST", MT::Diagonal, 1, 0, -1, false, true, N, applyDiagonal},
        {"DIAGONAL_NORTHWEST", MT::Diagonal, -1, 0, -1, false, true, N, applyDiagonal},
        {"DIAGONAL_SOUTHEAST", MT::Diagonal, 1, 0, 1, false, true, S, applyDiagonal},
        {"DIAGONAL_SOUTHWEST", MT::Diagonal, -1, 0, 1, false, true, S, applyDiagonal},
        {"PARKOUR_NORTH", MT::Parkour, 0, 0, -4, true, true, N, applyParkour},
        {"PARKOUR_SOUTH", MT::Parkour, 0, 0, 4, true, true, S, applyParkour},
        {"PARKOUR_EAST", MT::Parkour, 4, 0, 0, true, true, E, applyParkour},
        {"PARKOUR_WEST", MT::Parkour, -4, 0, 0, true, true, W, applyParkour},
        {"PARKOUR_NORTHEAST", MT::Parkour, 3, 0, -3, true, true, N, applyParkourDiagonal},
        {"PARKOUR_NORTHWEST", MT::Parkour, -3, 0, -3, true, true, N, applyParkourDiagonal},
        {"PARKOUR_SOUTHEAST", MT::Parkour, 3, 0, 3, true, true, S, applyParkourDiagonal},
        {"PARKOUR_SOUTHWEST", MT::Parkour, -3, 0, 3, true, true, S, applyParkourDiagonal},
    }};
    static const std::vector<Move> withParkour = [] {
        std::vector<Move> result(moves.begin(), moves.end());
        for (auto dir : kHorizontals) {
            const auto step = offsetOf(dir);
            result.push_back({"PARKOUR_CORNER", MT::ParkourCorner, 2 * step.x, 0, 2 * step.z, false, false, dir, applyCornerJump});
            result.push_back({"PARKOUR_CORNER", MT::ParkourCorner, 3 * step.x, 0, 3 * step.z, false, false, dir, applyCornerJump});
            result.push_back({"PARKOUR_SHULKER", MT::ParkourShulker, step.x, 2, step.z, false, false, dir, applyShulkerClimb});
            result.push_back({"PARKOUR_SHULKER", MT::ParkourShulker, step.x, 3, step.z, false, false, dir, applyShulkerClimb});
            result.push_back({"PARKOUR_LADDER", MT::Parkour, 2 * step.x, 1, 2 * step.z, true, true, dir, applyLadderJump});
            for (int n = 2; n <= 5; ++n)
                result.push_back({"EDGE_WALK", MT::EdgeWalk, n * step.x, 0, n * step.z, false, false, dir, applyEdgeWalk});
            result.push_back({"LADDER_SHIMMY", MT::LadderShimmy, step.x, 0, step.z, false, false, dir, applyLadderShimmy});
            result.push_back({"LADDER_SHIMMY", MT::LadderShimmy, step.x, 1, step.z, false, false, dir, applyLadderShimmy});
            result.push_back({"PARKOUR_MOMENTUM", MT::MomentumJump, (kMomentumGap + 1) * step.x, 0, (kMomentumGap + 1) * step.z,
                              false, false, dir, applyMomentumJump});
        }
        for (int dx = -2; dx <= 2; ++dx)
            for (int dz = -2; dz <= 2; ++dz)
                for (int dy = 0; dy <= 1; ++dy)
                    if (dx != 0 || dz != 0)
                        result.push_back({"LADDER_HOP", MT::LadderHop, dx, dy, dz, false, false, N, applyLadderHop});
        for (int dx = -3; dx <= 3; ++dx) {
            for (int dz = -3; dz <= 3; ++dz) {
                const int distance = dx * dx + dz * dz;
                if (distance == 0 || distance > 13) continue;
                if (distance == 10)
                    result.push_back({"PARKOUR_CORNER", MT::ParkourCorner, dx, -1, dz, false, false, N, applyCornerJump});
                if (distance == 8)
                    result.push_back({"PARKOUR_CORNER", MT::ParkourCorner, dx, -2, dz, false, false, N, applyCornerJump});
                for (int dy = -1; dy <= 1; ++dy) {
                    result.push_back({"PARKOUR_JUMP", MT::Parkour, dx, dy, dz, false, false, N, applyParkourJump});
                }
            }
        }
        return result;
    }();
    return withParkour;
}

}  // namespace baritone
