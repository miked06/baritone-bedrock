#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "baritone/cache/CachedWorld.h"
#include "baritone/utils/BlockPos.h"
#include "baritone/world/WorldSnapshot.h"

namespace sdk {
class Block;
class BlockSource;
class LocalPlayer;
}  // namespace sdk

namespace platform {

// Keeps the WorldSnapshot (read by path calculations on another thread) in sync with the
// game world. Game thread only, except snapshot() which any thread may call.
class WorldTracker {
public:
    static WorldTracker& get();

    // Loads/refreshes chunks around the player within a time budget.
    void tick(sdk::BlockSource& region, sdk::LocalPlayer& player);
    // Leaving the world / unloading: writes the long-distance cache and forgets everything.
    void reset();
    // Folder of the current world in the cache directory (chunkCaching). Set before tick().
    void setWorld(const std::string& folder);
    std::shared_ptr<baritone::CachedWorld> cache() const { return cache_; }
    // #repack: re-packs every loaded chunk into the cache; returns how many.
    std::size_t repack();
    // #saveall / #reloadall
    void saveCache() {
        if (cache_) cache_->saveNow();
    }
    void reloadCache() {
        flushCache();
        ensureCache();
    }

    // Block state id of a game block, registering it on first sight.
    baritone::StateId idOf(const sdk::Block* block);

    // Re-reads the live blocks of a cube into the snapshot (before acting on them).
    void refreshAround(const baritone::BlockPos& center, int radius);
    // #render: every loaded chunk is scanned again over the next ticks
    void markAllStale() { for (auto& [key, tick] : scannedAt_) tick = 0; }

    std::shared_ptr<const baritone::WorldSnapshot> snapshot() const;
    int dimension() const { return dimension_; }

    // Stats for #debug
    std::size_t knownStates() const { return blockIds_.size(); }
    double lastChunkScanMs() const { return lastChunkScanMs_; }

    int loadRadius = 8;       // chunks around the player kept in the snapshot
    int refreshRadius = 1;    // chunks re-scanned regularly (catch block changes)
    int refreshTicks = 40;    // how often, in ticks
    double budgetMs = 4.0;    // per tick

private:
    WorldTracker() = default;
    void scanChunk(int cx, int cz, bool refresh);
    void setDimension(int dimension, int minY, int maxY);
    // Long-distance cache of the current dimension: created when needed, written when leaving.
    void ensureCache();
    void flushCache();
    void packIntoCache(const baritone::ChunkData& chunk);
    baritone::StateId idOfName(const std::string& name);

    std::string world_;
    std::shared_ptr<baritone::CachedWorld> cache_;
    std::unordered_map<std::string, baritone::StateId> namedIds_;

    sdk::BlockSource* region_ = nullptr;
    std::shared_ptr<baritone::WorldSnapshot> snapshot_;
    std::unordered_map<const sdk::Block*, baritone::StateId> blockIds_;
    std::unordered_map<int64_t, uint64_t> scannedAt_;  // chunk key -> tick
    uint64_t tick_ = 0;
    int dimension_ = -1;
    int minY_ = 0, maxY_ = 0;
    double lastChunkScanMs_ = 0;
};

}  // namespace platform
