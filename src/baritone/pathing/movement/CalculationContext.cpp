#include "baritone/pathing/movement/CalculationContext.h"

#include <limits>

#include "baritone/Settings.h"

namespace baritone {

namespace {

std::unordered_set<std::string> toSet(const BlockList& list) {
    return {list.begin(), list.end()};
}

// Vanilla break speed with bare hands: 1/(hardness*30) per tick if harvestable,
// 1/(hardness*100) otherwise.
double handBreakSpeed(const BlockProps& block) {
    if (block.hardness < 0 || block.unbreakable) return 0;
    if (block.hardness == 0) return std::numeric_limits<double>::infinity();
    return 1.0 / (block.hardness * (block.requiresTool ? 100.0 : 30.0));
}

}  // namespace

CalculationContext::CalculationContext(std::shared_ptr<const WorldSnapshot> world, PlayerAbilities abilities, bool)
    : bsi(std::move(world)), breakSpeed_(std::move(abilities.breakSpeed)) {
    const Settings& s = Settings::get();
    hasWaterBucket = s.allowWaterBucketFall && abilities.hasWaterBucket && bsi.world().dimension() != 1;  // water evaporates in the nether
    hasThrowaway = s.allowPlace && abilities.hasThrowaway;
    canSprint = s.allowSprint && abilities.foodLevel > 6;
    frostWalker = abilities.frostWalker;
    placeBlockCost = s.blockPlacementPenalty;
    allowBreak = s.allowBreak;
    allowBreakAnyway = toSet(s.allowBreakAnyway);
    allowParkour = s.allowParkour;
    allowParkourPlace = s.allowParkourPlace;
    allowJumpAtBuildLimit = s.allowJumpAtBuildLimit || s.allowJumpAt256;
    allowWalkOnMagmaBlocks = s.allowWalkOnMagmaBlocks;
    allowParkourAscend = s.allowParkourAscend;
    assumeWalkOnWater = s.assumeWalkOnWater;
    assumeWalkOnLava = s.assumeWalkOnLava;
    allowFallIntoLava = s.allowFallIntoLava;
    allowDiagonalDescend = s.allowDiagonalDescend;
    allowDiagonalAscend = s.allowDiagonalAscend;
    allowDownward = s.allowDownward;
    allowVines = s.allowVines;
    allowWalkOnBottomSlab = s.allowWalkOnBottomSlab;
    avoidUpdatingFallingBlocks = s.avoidUpdatingFallingBlocks;
    strictLiquidCheck = s.strictLiquidCheck;
    minFallHeight = 3;  // Math.min(maxFallHeightNoWater, 3) in Baritone
    maxFallHeightNoWater = s.maxFallHeightNoWater;
    maxFallHeightBucket = s.maxFallHeightBucket;
    waterWalkSpeed = costs::WALK_ONE_IN_WATER_COST;  // TODO: depth strider
    breakBlockAdditionalCost = s.blockBreakAdditionalPenalty;
    jumpPenalty = s.jumpPenalty;
    walkOnWaterOnePenalty = s.walkOnWaterOnePenalty;
    avoidBreakingMultiplier = s.avoidBreakingMultiplier;
    backtrackCostFavoringCoefficient = s.backtrackCostFavoringCoefficient;
    blocksToAvoid = toSet(s.blocksToAvoid);
    blocksToDisallowBreaking = toSet(s.blocksToDisallowBreaking);
    blocksToAvoidBreaking = toSet(s.blocksToAvoidBreaking);
}

double CalculationContext::costOfPlacingAt(int x, int y, int z, const BlockProps& current) const {
    if (y < bsi.minY() || y >= bsi.maxY()) return costs::COST_INF;
    if (placeCostOverride) return placeCostOverride(x, y, z, current);
    if (!hasThrowaway) return costs::COST_INF;
    // source blocks / flowing fluid (Java FluidState.isSource: level 0)
    if (!Settings::get().allowPlaceInFluidsSource && current.hasFluid() && current.fluidLevel == 0) return costs::COST_INF;
    if (!Settings::get().allowPlaceInFluidsFlow && current.hasFluid() && current.fluidLevel != 0) return costs::COST_INF;
    if (y < bsi.minY() || y >= bsi.maxY()) return costs::COST_INF;
    if (placeOverride && placeOverride(x, y, z, current) >= costs::COST_INF) return costs::COST_INF;
    return placeBlockCost;
}

double CalculationContext::breakCostMultiplierAt(int x, int y, int z, const BlockProps& current) const {
    if (ignoreBreakRestrictions) return 1;
    if (breakMultiplierOverride) return breakMultiplierOverride(x, y, z, current);
    if (!allowBreak && !allowBreakAnyway.contains(current.name)) return costs::COST_INF;
    if (breakOverride && breakOverride(x, y, z, current) >= costs::COST_INF) return costs::COST_INF;
    return 1;
}

double CalculationContext::breakTime(const BlockProps& block) const {
    double speed = breakSpeed_ ? breakSpeed_(block) : handBreakSpeed(block);
    // Java ToolSet.avoidanceMultiplier: blocksToAvoidBreaking break avoidBreakingMultiplier times as fast
    if (blocksToAvoidBreaking.contains(block.name)) speed *= avoidBreakingMultiplier;
    if (speed <= 0) return costs::COST_INF;
    return 1.0 / speed;  // 0 for instantly broken blocks, like Baritone
}

}  // namespace baritone
