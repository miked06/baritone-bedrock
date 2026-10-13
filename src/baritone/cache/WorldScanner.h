#pragma once
#include <string>
#include <unordered_set>
#include <vector>

#include "baritone/utils/BlockPos.h"
#include "baritone/world/BlockProps.h"
#include "baritone/world/WorldSnapshot.h"

namespace baritone {

// A set of block names, like Baritone's BlockOptionalMetaLookup ("diamond_ore" or
// "minecraft:diamond_ore").
class BlockFilter {
public:
    BlockFilter() = default;
    explicit BlockFilter(const std::vector<std::string>& names);

    bool empty() const { return names_.empty(); }
    bool matches(const BlockProps& p) const { return names_.contains(p.name); }
    bool matchesName(const std::string& name) const { return names_.contains(name); }
    // Items obtained by mining these blocks (diamond_ore -> diamond, stone -> cobblestone...).
    const std::unordered_set<std::string>& dropNames() const { return drops_; }
    const std::unordered_set<std::string>& names() const { return names_; }
    std::string toString() const;

    static std::string normalize(std::string name);

private:
    std::unordered_set<std::string> names_;
    std::unordered_set<std::string> drops_;
};

// Finds the closest blocks matching a filter in the loaded chunks (Baritone's
// WorldScanner.scanChunkRadius). Thread-safe: only reads the snapshot.
std::vector<BlockPos> scanLoadedChunks(const WorldSnapshot& world, const BlockFilter& filter, const BlockPos& center,
                                       std::size_t max, int maxChunkRadius);

// Loaded chunks + tracked blocks of the long-distance cache in the regions around
// (MineProcess.searchWorld). Closest first. Thread-safe; may read cache files.
std::vector<BlockPos> searchWorld(const WorldSnapshot& world, const BlockFilter& filter, const BlockPos& center,
                                  std::size_t max, int maxChunkRadius);

}  // namespace baritone
