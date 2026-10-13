#include "baritone/pathing/movement/Moves.h"

#include <algorithm>
#include <numbers>

#include "baritone/pathing/movement/MovementHelper.h"

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

double costFromJumpDistance(int dist) {
    switch (dist) {
        case 2: return WALK_ONE_BLOCK_COST * 2;
        case 3: return WALK_ONE_BLOCK_COST * 3;
        case 4: return SPRINT_ONE_BLOCK_COST * 4;
        default: return COST_INF;
    }
}

}  // namespace

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
    if (isClimbable(srcDown)) return COST_INF;
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
    const BlockProps& fromDown = ctx.props(x, y - 1, z);
    if (!ladder) {
        if (isClimbable(fromDown)) return COST_INF;  // can't pillar from a ladder onto something else
        if (isBottomSlab(fromDown)) return COST_INF;
    }
    const BlockProps& toBreak = ctx.props(x, y + 2, z);
    if (toBreak.type == T::FenceGate) return COST_INF;
    const BlockProps* srcUp = nullptr;
    if (isWater(toBreak) && isWater(fromState)) {
        srcUp = &ctx.props(x, y + 1, z);
        if (isWater(*srcUp)) return LADDER_UP_ONE_COST;  // swim up a water column
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
    if (!canWalkOn(ctx, x, y - 2, z)) return COST_INF;
    const BlockProps& down = ctx.props(x, y - 1, z);
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
        if (i >= 2 && landingOn.type != T::Farmland && canWalkOn(ctx, cx, y - 1, cz, landingOn) && fullyPassable(ctx, cx, y + 2, cz)) {
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
    if (!fullyPassable(ctx, x + xDiff, y + 2, z + zDiff)) return;
    if (!fullyPassable(ctx, x, y + 2, z)) return;
    const BlockProps& standingOn = ctx.props(x, y - 1, z);
    if (isClimbable(standingOn) || standingOn.type == T::Stairs || isBottomSlab(standingOn)) return;
    if (ctx.assumeWalkOnWater && standingOn.hasFluid()) return;
    if (ctx.props(x, y, z).hasFluid()) return;  // can't jump out of water

    // TODO(bedrock): measure the real sprint-jump distance on Bedrock
    int maxJump;
    if (ctx.allowWalkOnMagmaBlocks && standingOn.type == T::Magma) maxJump = 2;
    else if (standingOn.type == T::SoulSand) maxJump = 2;  // 1 block gap
    else maxJump = ctx.canSprint ? 4 : 3;

    int verifiedMaxJump = 1;
    for (int i = 2; i <= maxJump; i++) {
        int destX = x + xDiff * i;
        int destZ = z + zDiff * i;
        if (!fullyPassable(ctx, destX, y + 1, destZ)) break;
        if (!fullyPassable(ctx, destX, y + 2, destZ)) break;

        const BlockProps& destInto = ctx.props(destX, y, destZ);
        if (!fullyPassable(ctx, destX, y, destZ, destInto)) {
            if (i <= 3 && ctx.allowParkourAscend && ctx.canSprint && canWalkOn(ctx, destX, y, destZ, destInto) &&
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
        if ((landingOn.type != T::Farmland && canWalkOn(ctx, destX, y - 1, destZ, landingOn)) ||
            (std::min(16, ctx.frostWalker + 2) >= i && canUseFrostWalker(ctx, landingOn))) {
            if (checkOvershootSafety(ctx, destX + xDiff, y, destZ + zDiff)) {
                res.x = destX;
                res.y = y;
                res.z = destZ;
                res.cost = costFromJumpDistance(i) + ctx.jumpPenalty;
                return;
            }
            break;
        }
        if (!fullyPassable(ctx, destX, y + 3, destZ)) break;
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

constexpr Direction N = Direction::North, S = Direction::South, E = Direction::East, W = Direction::West;

}  // namespace

const std::array<Move, 26>& allMoves() {
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
    return moves;
}

}  // namespace baritone
