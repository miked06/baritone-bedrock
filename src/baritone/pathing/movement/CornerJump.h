#pragma once

#include <optional>
#include <vector>

#include "baritone/pathing/movement/CalculationContext.h"

namespace baritone::movement_costs {

struct CornerJumpInput {
    float yaw;
    bool forward;
    bool jump;
};

struct CornerJump {
    Vec3d start;
    std::vector<CornerJumpInput> inputs;
};

bool isCornerJump(CalculationContext& ctx, BlockPos src, BlockPos dest);
std::optional<CornerJump> planCornerJump(CalculationContext& ctx, BlockPos src, BlockPos dest,
                                        std::optional<Vec3d> start = std::nullopt);

}  // namespace baritone::movement_costs
