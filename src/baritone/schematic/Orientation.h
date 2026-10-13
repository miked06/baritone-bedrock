#pragma once
#include <optional>
#include <string_view>

#include "baritone/schematic/Schematic.h"
#include "baritone/utils/BlockPos.h"
#include "baritone/world/BlockProps.h"

namespace baritone {

// Bedrock decides some block states from how a block is placed. The builder can reproduce
// those by choosing the face it clicks, where on the face, and where the player looks:
//   pillar_axis                  (logs, pillars)  axis of the clicked face
//   minecraft:vertical_half      (slabs)          upper half of a side face, or bottom face of the block above
//   upside_down_bit              (stairs)         same rule as slabs
//   weirdo_direction             (stairs)         direction the player faces
//   minecraft:cardinal_direction (furnaces, chests, pumpkins...) faces the player
bool isOrientationState(std::string_view key);

// Same block and, unless ignoreDirection, the same orientation states.
bool blockMatches(const DesiredBlock& desired, const BlockProps& current, bool ignoreDirection);

struct Placement {
    Direction against;      // side of the target position where the clicked block is
    bool topHalf;           // the hit is in the upper half of the block space
    Direction playerFacing; // horizontal direction the player looks
};
// Would placing `desired` this way give it the right orientation?
bool placementGivesOrientation(const DesiredBlock& desired, const Placement& placement);

// The horizontal direction the player must face to place `desired`, if it matters.
std::optional<Direction> requiredFacing(const DesiredBlock& desired);
// Minecraft yaw (0 = south, 90 = west) to a horizontal direction.
Direction horizontalFacing(float yaw);

}  // namespace baritone
