#pragma once
#include <string>

#include "baritone/world/BlockProps.h"
#include "sdk/Nbt.h"

namespace platform {

// Turns a Bedrock block state (identifier + states) into what the pathfinder needs:
// collision shape class, special type, fluid, hardness and best tool. Pure function of
// its inputs, so it is computed once per block state.
baritone::BlockProps classifyBlock(const std::string& name, const sdk::nbt::CompoundTag* states);

}  // namespace platform
