#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "baritone/world/BlockProps.h"

namespace baritone {

class CachedWorld;

// 16x16x16 block states. Index = (y << 8) | (z << 4) | x, all local.
struct Section {
    std::array<StateId, 4096> ids{};
};

// Copy of one chunk column, written on the game thread and read by the path calculation
// thread. Single block writes are plain aligned 16-bit stores (atomic on x64); sections
// are created at most once and live as long as the chunk.
class ChunkData {
public:
    ChunkData(int cx, int cz, int minY, int height);
    ~ChunkData();
    ChunkData(const ChunkData&) = delete;
    ChunkData& operator=(const ChunkData&) = delete;

    int cx() const { return cx_; }
    int cz() const { return cz_; }

    StateId get(int lx, int y, int lz) const {
        int s = (y - minY_) >> 4;
        if (s < 0 || s >= numSections_) return kAir;
        const Section* sec = sections_[s].load(std::memory_order_acquire);
        if (!sec) return kAir;
        return sec->ids[((y & 15) << 8) | (lz << 4) | lx];
    }

    void set(int lx, int y, int lz, StateId id);

    int minY() const { return minY_; }
    int numSections() const { return numSections_; }
    // nullptr when the section is all air
    const Section* section(int index) const { return sections_[index].load(std::memory_order_acquire); }

private:
    int cx_, cz_, minY_, numSections_;
    std::unique_ptr<std::atomic<Section*>[]> sections_;
};

// All chunks the bot currently knows about in the player's dimension.
class WorldSnapshot {
public:
    WorldSnapshot(int dimension, int minY, int maxY) : dimension_(dimension), minY_(minY), maxY_(maxY) {}

    int dimension() const { return dimension_; }
    int minY() const { return minY_; }
    int maxY() const { return maxY_; }  // exclusive

    std::shared_ptr<const ChunkData> chunk(int cx, int cz) const;
    std::shared_ptr<ChunkData> mutableChunk(int cx, int cz) const;
    bool isLoaded(int cx, int cz) const;
    void put(std::shared_ptr<ChunkData> chunk);
    void remove(int cx, int cz);
    std::size_t chunkCount() const;
    std::vector<std::pair<int, int>> loadedChunks() const;

    // Updates a single block if its chunk is known (game thread).
    void setBlock(int x, int y, int z, StateId id);

    // Chunks that have been loaded at least once in this dimension (#explore), also in
    // previous sessions when the long-distance cache is on.
    bool wasExplored(int cx, int cz) const;

    // Long-distance cache of this dimension (nullptr when chunkCaching is off).
    std::shared_ptr<CachedWorld> cache() const;
    void setCache(std::shared_ptr<CachedWorld> cache);
    std::size_t exploredCount() const;

    static int64_t key(int cx, int cz) { return (static_cast<int64_t>(cx) << 32) | static_cast<uint32_t>(cz); }

private:
    int dimension_, minY_, maxY_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<int64_t, std::shared_ptr<ChunkData>> chunks_;
    std::unordered_set<int64_t> explored_;
    std::shared_ptr<CachedWorld> cache_;
};

}  // namespace baritone
