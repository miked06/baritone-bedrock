#pragma once
#include <functional>
#include <memory>
#include <unordered_set>
#include <vector>

#include "baritone/pathing/ActionCosts.h"
#include "baritone/world/BlockStateInterface.h"

namespace baritone {

// Mining speed of the player's best tool against a block state (Baritone's ToolSet).
// Returns the fraction of the block broken per tick (0 = cannot break).
using BreakSpeedFn = std::function<double(const BlockProps&)>;

// What the player has and the settings at the start of a calculation. Copied so that the
// calculation thread never reads anything the game thread may be changing.
struct PlayerAbilities {
    bool hasWaterBucket = false;
    bool hasThrowaway = false;  // blocks to place
    int foodLevel = 20;         // Java: no sprinting at 6 or less
    int frostWalker = 0;        // level on the worn boots
    BreakSpeedFn breakSpeed;    // empty = bare hands with the vanilla formula
};

class CalculationContext {
public:
    CalculationContext(std::shared_ptr<const WorldSnapshot> world, PlayerAbilities abilities, bool forUseOnAnotherThread);

    BlockStateInterface bsi;

    StateId get(int x, int y, int z) { return bsi.get(x, y, z); }
    const BlockProps& props(int x, int y, int z) { return bsi.props(x, y, z); }
    const BlockProps& props(StateId id) const { return bsi.props(id); }
    bool isLoaded(int x, int z) { return bsi.isLoaded(x, z); }  // loaded or in the long-distance cache
    bool isLoadedLive(int x, int z) { return bsi.isLoadedLive(x, z); }

    double costOfPlacingAt(int x, int y, int z, const BlockProps& current) const;
    double breakCostMultiplierAt(int x, int y, int z, const BlockProps& current) const;
    double placeBucketCost() const { return placeBlockCost; }  // shouldn't really be a factor
    // Ticks to break `block` with the best tool, COST_INF if impossible.
    double breakTime(const BlockProps& block) const;

    bool isAvoided(const BlockProps& p) const { return blocksToAvoid.contains(p.name); }
    bool isDisallowedBreaking(const BlockProps& p) const { return blocksToDisallowBreaking.contains(p.name); }

    // Snapshot of settings
    bool hasWaterBucket, hasThrowaway, canSprint;
    double placeBlockCost;
    bool allowBreak;
    std::unordered_set<std::string> allowBreakAnyway;
    bool allowParkour, allowParkourPlace, allowJumpAtBuildLimit, allowParkourAscend;
    bool allowWalkOnMagmaBlocks;
    bool assumeWalkOnWater, assumeWalkOnLava, allowFallIntoLava;
    bool allowDiagonalDescend, allowDiagonalAscend, allowDownward, allowVines, allowWalkOnBottomSlab;
    bool avoidUpdatingFallingBlocks, strictLiquidCheck;
    int minFallHeight, maxFallHeightNoWater, maxFallHeightBucket;
    double waterWalkSpeed, breakBlockAdditionalCost, jumpPenalty, walkOnWaterOnePenalty;
    double avoidBreakingMultiplier;
    std::unordered_set<std::string> blocksToAvoid, blocksToDisallowBreaking, blocksToAvoidBreaking;
    int frostWalker = 0;

    // Set by the process in control (e.g. the builder must not break what it built).
    // Return COST_INF to forbid; called from the calculation thread.
    std::function<double(int, int, int, const BlockProps&)> breakOverride;
    // Java GetToBlockCalculationContext: breakCostMultiplierAt is always 1
    bool ignoreBreakRestrictions = false;
    std::function<double(int, int, int, const BlockProps&)> placeOverride;
    // Java BuilderCalculationContext: the whole placing cost / break multiplier (replaces the
    // usual checks, like the Java overrides)
    std::function<double(int, int, int, const BlockProps&)> placeCostOverride;
    std::function<double(int, int, int, const BlockProps&)> breakMultiplierOverride;
    double backtrackCostFavoringCoefficient = 0.5;

private:
    BreakSpeedFn breakSpeed_;
};

}  // namespace baritone
