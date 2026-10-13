#include "baritone/pathing/movement/MovementHelper.h"

#include "baritone/Settings.h"

namespace baritone::helper {

namespace {

enum class Ternary { Yes, No, Maybe };

using T = BlockType;

// Java FallingBlock.isFree: a falling block would drop into this.
bool isFree(const BlockProps& p) {
    return p.type == T::Air || p.type == T::Fire || p.hasFluid() || p.replaceable;
}

Ternary canWalkThroughBlockState(const CalculationContext& ctx, const BlockProps& p) {
    switch (p.type) {
        case T::Air: return Ternary::Yes;
        case T::Fire:
        case T::Cobweb:
        case T::EndPortal:
        case T::BubbleColumn:
        case T::Slab:
        case T::Trapdoor:
        case T::Honey:
        case T::SweetBerryBush:
        case T::PowderSnow:
        case T::Cauldron:
        case T::IronDoor: return Ternary::No;
        default: break;
    }
    if (ctx.isAvoided(p)) return Ternary::No;
    if (p.type == T::Door || p.type == T::FenceGate) return Ternary::Yes;  // assumes every door can be opened
    if (p.type == T::Carpet || p.type == T::SnowLayer) return Ternary::Maybe;
    if (p.hasFluid()) return p.fluidAmount() != 8 ? Ternary::No : Ternary::Maybe;
    return p.pathfindable ? Ternary::Yes : Ternary::No;
}

Ternary fullyPassableBlockState(const BlockProps& p) {
    switch (p.type) {
        case T::Air: return Ternary::Yes;
        case T::Fire:
        case T::Cobweb:
        case T::Vine:
        case T::Ladder:
        case T::Door:
        case T::IronDoor:
        case T::FenceGate:
        case T::SnowLayer:
        case T::Trapdoor:
        case T::EndPortal: return Ternary::No;
        default: break;
    }
    if (p.hasFluid()) return Ternary::No;
    return p.pathfindable ? Ternary::Yes : Ternary::No;
}

Ternary canWalkOnBlockState(const CalculationContext& ctx, const BlockProps& p) {
    if ((p.fullCube || p.solidTop) && (p.type != T::Magma || ctx.allowWalkOnMagmaBlocks) && p.type != T::BubbleColumn &&
        p.type != T::Honey) {
        return Ternary::Yes;
    }
    switch (p.type) {
        case T::Ladder: return Ternary::Yes;
        case T::Vine: return ctx.allowVines ? Ternary::Yes : Ternary::No;
        case T::Farmland:
        case T::DirtPath:
        case T::Chest:
        case T::Glass:
        case T::Stairs: return Ternary::Yes;
        default: break;
    }
    if (isWater(p)) return Ternary::Maybe;
    if (isLava(p) && ctx.assumeWalkOnLava) return Ternary::Maybe;
    if (p.type == T::Slab) {
        if (!ctx.allowWalkOnBottomSlab) return p.bottomSlab ? Ternary::No : Ternary::Yes;
        return Ternary::Yes;
    }
    return Ternary::No;
}

bool avoidAdjacentBreaking(CalculationContext& ctx, int x, int y, int z, bool directlyAbove) {
    // Breaking next to this block could make it move (liquid flowing in, sand falling).
    const BlockProps& p = ctx.props(x, y, z);
    if (!directlyAbove && p.falling && ctx.avoidUpdatingFallingBlocks && isFree(ctx.props(x, y - 1, z))) {
        return true;
    }
    if (p.type == T::Water || p.type == T::Lava) {
        if (directlyAbove || ctx.strictLiquidCheck) return true;
        if (p.fluidLevel == 0) return true;  // sources flow horizontally
        return !ctx.props(x, y - 1, z).hasFluid();  // flowing liquid prefers to go down
    }
    return p.hasFluid();  // waterlogged
}

}  // namespace

bool possiblyFlowing(const BlockProps& p) {
    return p.hasFluid() && p.fluidAmount() != 8;
}

bool isFlowing(CalculationContext& ctx, int x, int y, int z, const BlockProps& p) {
    if (!p.hasFluid()) return false;
    if (p.fluidAmount() != 8) return true;
    return possiblyFlowing(ctx.props(x + 1, y, z)) || possiblyFlowing(ctx.props(x - 1, y, z)) ||
           possiblyFlowing(ctx.props(x, y, z + 1)) || possiblyFlowing(ctx.props(x, y, z - 1));
}

bool canWalkThrough(CalculationContext& ctx, int x, int y, int z, const BlockProps& p) {
    Ternary t = canWalkThroughBlockState(ctx, p);
    if (t != Ternary::Maybe) return t == Ternary::Yes;

    if (p.type == T::Carpet) return canWalkOn(ctx, x, y - 1, z);
    if (p.type == T::SnowLayer) {
        if (!ctx.isLoadedLive(x, z)) return true;  // cached chunks have no layer count
        if (p.snowLayers >= 3) return false;   // 3+ layers block a 2 high ceiling
        return canWalkOn(ctx, x, y - 1, z);
    }
    if (p.hasFluid()) {
        if (isFlowing(ctx, x, y, z, p)) return false;
        if (ctx.assumeWalkOnWater) return false;
        const BlockProps& up = ctx.props(x, y + 1, z);
        if (up.hasFluid() || up.type == T::LilyPad) return false;
        return isWater(p);
    }
    return p.pathfindable;
}

bool fullyPassable(CalculationContext&, int, int, int, const BlockProps& p) {
    return fullyPassableBlockState(p) == Ternary::Yes;
}

bool canWalkOn(CalculationContext& ctx, int x, int y, int z, const BlockProps& p) {
    Ternary t = canWalkOnBlockState(ctx, p);
    if (t != Ternary::Maybe) return t == Ternary::Yes;

    if (isWater(p)) {
        const BlockProps& up = ctx.props(x, y + 1, z);
        if (up.type == T::LilyPad || up.type == T::Carpet) return true;
        bool upIsFlowingWater = isWater(up) && up.fluidLevel != 0;  // Java Fluids.FLOWING_WATER
        if (isFlowing(ctx, x, y, z, p) || upIsFlowingWater) {
            // the only flowing water we can walk on is under still water, with jesus off
            return isWater(up) && !ctx.assumeWalkOnWater;
        }
        // with jesus on, only water without water above; with jesus off, only water under water
        return isWater(up) != ctx.assumeWalkOnWater;
    }
    if (isLava(p) && !isFlowing(ctx, x, y, z, p) && ctx.assumeWalkOnLava) return true;
    return false;
}

bool isReplaceable(CalculationContext& ctx, int x, int /*y*/, int z, const BlockProps& p) {
    if (p.type == T::Air) return true;
    if (p.type == T::SnowLayer) {
        if (!ctx.isLoadedLive(x, z)) return true;
        return p.snowLayers == 1;
    }
    if (p.type == T::TallPlant) return true;
    return p.replaceable;
}

bool avoidWalkingInto(const BlockProps& p) {
    switch (p.type) {
        case T::Magma: return !Settings::get().allowWalkOnMagmaBlocks;
        case T::Cactus:
        case T::SweetBerryBush:
        case T::Fire:
        case T::EndPortal:
        case T::Cobweb:
        case T::BubbleColumn: return true;
        default: return p.hasFluid();
    }
}

bool mustBeSolidToWalkOn(CalculationContext& ctx, int x, int y, int z, const BlockProps& p) {
    if (isClimbable(p)) return false;
    if (p.hasFluid()) {
        // waterlogged blocks that are still solid ground (Java: top/double slab, top or inner
        // stairs, closed top trapdoor, scaffolding, leaves)
        if (p.topSlab || p.doubleSlab || p.topHalfStairs || p.innerStairs) return true;
        if (p.type == T::Trapdoor && !p.open && p.topTrapdoor) return true;
        if (p.type == T::Scaffolding || p.type == T::Leaves) return true;
        if (ctx.assumeWalkOnWater) return false;
        if (ctx.props(x, y + 1, z).hasFluid()) return false;
    }
    return true;
}

bool canPlaceAgainst(const BlockProps& p) {
    // can we look at the center of a side face and expect a placement to work?
    return isBlockNormalCube(p) || p.type == T::Glass;
}

bool canUseFrostWalker(const CalculationContext& ctx, const BlockProps& p) {
    return ctx.frostWalker != 0 && isWater(p) && p.fluidLevel == 0;
}

bool avoidBreaking(CalculationContext& ctx, int x, int y, int z, const BlockProps& p) {
    if (ctx.isDisallowedBreaking(p) || p.type == T::Ice || p.type == T::Infested || p.unbreakable) return true;
    return avoidAdjacentBreaking(ctx, x, y + 1, z, true) || avoidAdjacentBreaking(ctx, x + 1, y, z, false) ||
           avoidAdjacentBreaking(ctx, x - 1, y, z, false) || avoidAdjacentBreaking(ctx, x, y, z + 1, false) ||
           avoidAdjacentBreaking(ctx, x, y, z - 1, false);
}

double getMiningDurationTicks(CalculationContext& ctx, int x, int y, int z, const BlockProps& p, bool includeFalling) {
    if (canWalkThrough(ctx, x, y, z, p)) return 0;  // we won't mine it, so don't check fallings above
    if (p.hasFluid()) return costs::COST_INF;
    double mult = ctx.breakCostMultiplierAt(x, y, z, p);
    if (mult >= costs::COST_INF) return costs::COST_INF;
    if (avoidBreaking(ctx, x, y, z, p)) return costs::COST_INF;
    double ticks = ctx.breakTime(p);
    if (ticks >= costs::COST_INF) return costs::COST_INF;
    double result = (ticks + ctx.breakBlockAdditionalCost) * mult;
    if (includeFalling) {
        const BlockProps& above = ctx.props(x, y + 1, z);
        if (above.falling) result += getMiningDurationTicks(ctx, x, y + 1, z, above, true);
    }
    return result;
}

}  // namespace baritone::helper
