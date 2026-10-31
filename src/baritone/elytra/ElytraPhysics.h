#pragma once
#include <optional>
#include <vector>

#include "baritone/utils/BlockPos.h"
#include "baritone/utils/Rotation.h"
#include "baritone/world/BlockStateInterface.h"

namespace baritone::elytra {

// Elytra flight model (Java's LivingEntity.travel while fall flying, which Bedrock's
// gliding follows closely). Units: blocks and ticks.
struct FlightState {
    Vec3d pos;  // feet
    Vec3d vel;
};
struct DebugRay { Vec3d from, to; bool blocked = false; };
struct CollisionSample { Vec3d position; bool collided = false; };

inline constexpr double kGlideHeight = 0.6;  // bounding box while gliding
inline constexpr double kGlideWidth = 0.6;

// One tick of gliding with the given look rotation; `boosted` while a firework pushes.
FlightState step(const FlightState& s, const Rotation& look, bool boosted);

// Does the gliding box at `pos` intersect a solid block?
bool collides(BlockStateInterface& bsi, const Vec3d& pos);

// Simulates `ticks` ticks holding `look`; the positions, or nullopt on a collision.
std::optional<std::vector<Vec3d>> simulate(BlockStateInterface& bsi, FlightState s, const Rotation& look, int ticks,
                                           int boostTicks, std::vector<CollisionSample>* debug = nullptr);

// Is the straight segment a -> b free for the gliding box (with `margin` blocks around)?
bool clearLine(BlockStateInterface& bsi, const Vec3d& a, const Vec3d& b, double margin);

}  // namespace baritone::elytra
