#pragma once
#include "baritone/pathing/movement/CalculationContext.h"

// Static block rules of Baritone's MovementHelper, expressed on BlockProps.
namespace baritone::helper {

inline bool isWater(const BlockProps& p) { return p.fluid == Fluid::Water; }
inline bool isLava(const BlockProps& p) { return p.fluid == Fluid::Lava; }
inline bool isLiquid(const BlockProps& p) { return p.hasFluid(); }
inline bool isBottomSlab(const BlockProps& p) { return p.bottomSlab; }
inline bool isClimbable(const BlockProps& p) { return p.type == BlockType::Ladder || p.type == BlockType::Vine; }
inline bool isBlockNormalCube(const BlockProps& p) { return p.fullCube; }

bool possiblyFlowing(const BlockProps& p);
bool isFlowing(CalculationContext& ctx, int x, int y, int z, const BlockProps& p);

bool canWalkThrough(CalculationContext& ctx, int x, int y, int z, const BlockProps& p);
inline bool canWalkThrough(CalculationContext& ctx, int x, int y, int z) {
    return canWalkThrough(ctx, x, y, z, ctx.props(x, y, z));
}

bool fullyPassable(CalculationContext& ctx, int x, int y, int z, const BlockProps& p);
inline bool fullyPassable(CalculationContext& ctx, int x, int y, int z) {
    return fullyPassable(ctx, x, y, z, ctx.props(x, y, z));
}

bool canWalkOn(CalculationContext& ctx, int x, int y, int z, const BlockProps& p);
inline bool canWalkOn(CalculationContext& ctx, int x, int y, int z) {
    return canWalkOn(ctx, x, y, z, ctx.props(x, y, z));
}

bool isReplaceable(CalculationContext& ctx, int x, int y, int z, const BlockProps& p);
bool avoidWalkingInto(const BlockProps& p);
bool mustBeSolidToWalkOn(CalculationContext& ctx, int x, int y, int z, const BlockProps& p);
bool canPlaceAgainst(const BlockProps& p);
inline bool canPlaceAgainst(CalculationContext& ctx, int x, int y, int z) { return canPlaceAgainst(ctx.props(x, y, z)); }
bool canUseFrostWalker(const CalculationContext& ctx, const BlockProps& p);
bool avoidBreaking(CalculationContext& ctx, int x, int y, int z, const BlockProps& p);

double getMiningDurationTicks(CalculationContext& ctx, int x, int y, int z, const BlockProps& p, bool includeFalling);
inline double getMiningDurationTicks(CalculationContext& ctx, int x, int y, int z, bool includeFalling) {
    return getMiningDurationTicks(ctx, x, y, z, ctx.props(x, y, z), includeFalling);
}

}  // namespace baritone::helper
