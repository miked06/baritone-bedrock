#pragma once

#include <optional>

#include "baritone/pathing/VehiclePath.h"

namespace baritone {

bool railUsable(const WorldSnapshot& world, BlockPos rail);
bool railsConnected(const WorldSnapshot& world, BlockPos from, BlockPos to);
bool railHasStoppingPlatform(const WorldSnapshot& world, BlockPos rail);
std::optional<BlockPos> railBelow(const WorldSnapshot& world, Vec3d position);
VehiclePath findRailPath(const WorldSnapshot& world, BlockPos from, BlockPos to);

}  // namespace baritone
