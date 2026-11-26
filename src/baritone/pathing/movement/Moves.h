#pragma once
#include <array>
#include <cstdint>
#include <span>

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

enum class MovementType : uint8_t { Traverse, Ascend, Descend, Fall, Diagonal, Pillar, Downward, Parkour, ParkourCorner, ParkourShulker, EdgeWalk, LadderHop, LadderShimmy, MomentumJump };

inline bool isParkour(MovementType type) {
    return type == MovementType::Parkour || type == MovementType::ParkourCorner || type == MovementType::ParkourShulker ||
           type == MovementType::MomentumJump;
}

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

std::span<const Move> allMoves();

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
struct ParkourJump {
    double takeoff;
    double cost;
    bool sprint;
    // Sideways offset of the jump from the line between the two block centers (positive: to
    // the right of the jump direction), to pass beside something in the middle.
    double lane = 0;
};
std::optional<ParkourJump> parkourJump(CalculationContext& ctx, const BlockPos& src, const BlockPos& dest);
double shulkerClimb(CalculationContext& ctx, BlockPos src, BlockPos dest);
// Bedrock addition: jump into a ladder hanging one or two blocks ahead, one block up: the
// feet reach its cell at the top of the jump and the wall behind it stops us there.
void ladderJump(CalculationContext& ctx, int x, int y, int z, Direction dir, MoveResult& res);
// Bedrock addition: sneak along the edge of the floor beside blocks that leave a little room
// in their cell (a honey block is 1/16 smaller on every side). side: +1 to the right of the
// walking direction, -1 to the left.
struct EdgeWalk {
    int side;
    double cost;
};
std::optional<EdgeWalk> edgeWalk(CalculationContext& ctx, BlockPos src, BlockPos dest);
// Bedrock addition: from the thin top of a ladder (against its wall), jump into another
// ladder up to 2 blocks away, at the same height or one block up, also around the corner of
// the block they hang on.
double ladderHop(CalculationContext& ctx, BlockPos src, BlockPos dest);
// Bedrock addition: hanging in a ladder, move sideways into the next ladder (sneaking holds us).
double ladderShimmy(CalculationContext& ctx, BlockPos src, BlockPos dest);
// Bedrock addition: a straight 4 block gap (5 blocks from the middle of one block to the middle
// of the other). A sprint jump from the ground falls short of it by about a hundredth of a block;
// it takes momentum: a hop on the run-up, then the jump the tick we land, which adds the jump
// boost to the speed kept from the air. runUp: the free blocks behind the takeoff block (5 or 6)
// the executor backs up onto first.
inline constexpr int kMomentumGap = 4, kMinMomentumRunUp = 5, kMaxMomentumRunUp = 6;
struct MomentumJump {
    int runUp;
    double cost;
};
std::optional<MomentumJump> momentumJump(CalculationContext& ctx, BlockPos src, BlockPos dest);
}  // namespace movement_costs

}  // namespace baritone
