#pragma once
#include <array>
#include <cstdint>

#include "baritone/pathing/movement/CalculationContext.h"
#include "baritone/utils/BlockPos.h"

namespace baritone {

struct MoveResult {
    int x = 0, y = 0, z = 0;
    double cost = costs::COST_INF;
    void reset() {
        x = y = z = 0;
        cost = costs::COST_INF;
    }
};

enum class MovementType : uint8_t { Traverse, Ascend, Descend, Fall, Diagonal, Pillar, Downward, Parkour };

// Every move the A* tries from a node (Baritone's Moves enum).
struct Move {
    const char* name;
    MovementType type;
    int dx, dy, dz;
    bool dynamicXZ;  // the destination x/z is computed (parkour)
    bool dynamicY;   // the destination y is computed (descend/fall, diagonal, parkour)
    Direction direction;  // for parkour
    void (*apply)(const Move& m, CalculationContext& ctx, int x, int y, int z, MoveResult& res);
};

const std::array<Move, 26>& allMoves();

// Cost functions of each movement, also used to re-check a movement during execution.
namespace movement_costs {
double traverse(CalculationContext& ctx, int x, int y, int z, int destX, int destZ);
double ascend(CalculationContext& ctx, int x, int y, int z, int destX, int destZ);
// Descend one block; if the block below the destination is missing, becomes a fall.
void descend(CalculationContext& ctx, int x, int y, int z, int destX, int destZ, MoveResult& res);
// Returns true when the fall needs a water bucket.
bool dynamicFallCost(CalculationContext& ctx, int x, int y, int z, int destX, int destZ, double frontBreak,
                     const BlockProps& below, MoveResult& res);
void diagonal(CalculationContext& ctx, int x, int y, int z, int destX, int destZ, MoveResult& res);
double pillar(CalculationContext& ctx, int x, int y, int z);
double downward(CalculationContext& ctx, int x, int y, int z);
void parkour(CalculationContext& ctx, int x, int y, int z, Direction dir, MoveResult& res);
// Bedrock addition (not in Baritone): a sprint jump along a diagonal (sx, sz = +-1) over a gap,
// 2 blocks diagonally, 3 with sprinting. Lands at the same height.
void parkourDiagonal(CalculationContext& ctx, int x, int y, int z, int sx, int sz, MoveResult& res);
}  // namespace movement_costs

}  // namespace baritone
