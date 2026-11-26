#pragma once
#include <istream>
#include <memory>
#include <string>
#include <vector>

#include "baritone/utils/BlockPos.h"
#include "baritone/world/BlockProps.h"
#include "baritone/world/WorldSnapshot.h"

namespace baritone {

// A box of the world as the pathfinder sees it (every block state with its classified
// properties), written by #terrain dump so path problems found in game can be replayed
// offline with the exact same blocks.
struct TerrainDump {
    BlockPos min, max;  // inclusive
    Vec3d player;
    int dimension = 0, minY = -64, maxY = 320;
    std::vector<BlockProps> states;                        // index 0 is air
    std::vector<std::pair<BlockPos, uint32_t>> blocks;     // non-air cells, index into states
    std::vector<std::pair<BlockPos, uint8_t>> facings;     // block entity facings (shulker boxes)
};

std::string writeTerrainDump(const WorldSnapshot& world, const BlockPos& min, const BlockPos& max, const Vec3d& player);
// Throws std::runtime_error on a malformed dump.
TerrainDump readTerrainDump(std::istream& in);
// Registers the dump's states and builds a snapshot whose chunks cover the box (the rest of
// those chunks is air).
std::shared_ptr<WorldSnapshot> buildTerrainWorld(const TerrainDump& dump);

}  // namespace baritone
