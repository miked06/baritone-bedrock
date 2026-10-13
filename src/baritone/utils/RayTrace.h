#pragma once
#include <optional>

#include "baritone/utils/BlockPos.h"
#include "baritone/utils/Rotation.h"
#include "baritone/world/BlockStateInterface.h"

namespace baritone {

struct BlockHit {
    BlockPos pos;
    Direction face;  // face of `pos` that was hit
    Vec3d hit;       // exact point
};

struct LocalBox {
    double minX, minY, minZ, maxX, maxY, maxZ;
};

// Outline box used for ray tracing, relative to the block origin. nullopt = not targetable
// (air, liquids).
std::optional<LocalBox> outlineBox(const BlockProps& p);

// First block hit by a ray (Baritone's RayTraceUtils.rayTraceTowards / world.clip with
// OUTLINE shapes and no fluids).
std::optional<BlockHit> rayTrace(BlockStateInterface& bsi, const Vec3d& start, const Rotation& rotation, double reach);

// Center of the block's collision/outline shape (VecUtils.calculateBlockCenter).
Vec3d blockCenter(BlockStateInterface& bsi, const BlockPos& pos);

}  // namespace baritone
