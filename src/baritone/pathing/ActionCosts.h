#pragma once
#include <array>
#include <cmath>

// Costs are in ticks (1/20 s), like Baritone's ActionCosts. Bedrock shares Java's walking
// physics constants (gravity 0.08, drag 0.98, walk 4.317 m/s, sprint 5.612 m/s); values to
// re-measure in game are marked TODO(bedrock).
namespace baritone::costs {

inline constexpr double SQRT_2 = 1.4142135623730951;

inline constexpr double WALK_ONE_BLOCK_COST = 20 / 4.317;      // 4.633
inline constexpr double WALK_ONE_IN_WATER_COST = 20 / 2.2;     // 9.091
inline constexpr double WALK_ONE_OVER_SOUL_SAND_COST = WALK_ONE_BLOCK_COST * 2;
inline constexpr double LADDER_UP_ONE_COST = 20 / 2.35;        // 8.511 TODO(bedrock)
inline constexpr double LADDER_DOWN_ONE_COST = 20 / 3.0;       // 6.667 TODO(bedrock)
inline constexpr double SNEAK_ONE_BLOCK_COST = 20 / 1.3;       // 15.385
inline constexpr double SPRINT_ONE_BLOCK_COST = 20 / 5.612;    // 3.564
inline constexpr double SPRINT_MULTIPLIER = SPRINT_ONE_BLOCK_COST / WALK_ONE_BLOCK_COST;
inline constexpr double WALK_OFF_BLOCK_COST = WALK_ONE_BLOCK_COST * 0.8;
inline constexpr double CENTER_AFTER_FALL_COST = WALK_ONE_BLOCK_COST - WALK_OFF_BLOCK_COST;
inline constexpr double COST_INF = 1000000;

// Ticks to fall `distance` blocks from rest.
double distanceToTicks(double distance);

// FALL_N_BLOCKS_COST[n] = ticks to fall n blocks.
const std::array<double, 4097>& fallNBlocksCost();
inline double fallCost(int blocks) {
    return fallNBlocksCost()[blocks];
}

extern const double FALL_1_25_BLOCKS_COST;
extern const double FALL_0_25_BLOCKS_COST;
extern const double JUMP_ONE_BLOCK_COST;

}  // namespace baritone::costs
