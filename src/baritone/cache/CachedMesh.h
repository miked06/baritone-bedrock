#pragma once
#include <array>
#include <vector>
#include "baritone/cache/CachedWorld.h"
#include "baritone/world/WorldSnapshot.h"

namespace baritone {
struct CachedFace {
    std::array<Vec3d, 4> vertices;
    PathingBlockType type;
};
// Surface geometry of remembered terrain outside the loaded world. Runs on a
// worker using immutable cached chunks; never reads the live game or its SDK.
std::vector<CachedFace> buildCachedMesh(CachedWorld& cache, const WorldSnapshot& loaded,
    const BlockPos& center, int chunkRadius = 12, std::size_t maxFaces = 8192);
}
