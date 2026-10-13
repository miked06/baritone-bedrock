#pragma once
#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "baritone/cache/CachedChunk.h"

namespace baritone {

// 32x32 chunks, one file on disk (Baritone's CachedRegion).
class CachedRegion {
public:
    static constexpr int kSize = 32;

    CachedRegion(int rx, int rz) : rx_(rx), rz_(rz) {}

    int rx() const { return rx_; }
    int rz() const { return rz_; }
    std::shared_ptr<const CachedChunk> chunk(int cx, int cz) const { return chunks_[index(cx, cz)]; }
    void put(std::shared_ptr<const CachedChunk> chunk);
    bool dirty() const { return dirty_; }
    void setDirty(bool d) { dirty_ = d; }
    const std::array<std::shared_ptr<const CachedChunk>, kSize * kSize>& chunks() const { return chunks_; }

    // Own little-endian format with run-length encoded bits. Throws std::runtime_error.
    static void write(const std::filesystem::path& file, const std::vector<std::shared_ptr<const CachedChunk>>& chunks,
                      int minY, int height);
    static std::shared_ptr<CachedRegion> read(const std::filesystem::path& file, int rx, int rz, int minY, int height);

private:
    static int index(int cx, int cz) { return ((cx & (kSize - 1)) << 5) | (cz & (kSize - 1)); }

    int rx_, rz_;
    bool dirty_ = false;
    std::array<std::shared_ptr<const CachedChunk>, kSize * kSize> chunks_{};
};

// Every chunk the bot has ever seen in one dimension of one world, in 2 bits per block,
// saved under %LOCALAPPDATA%\BaritoneBedrock\cache (Baritone's CachedWorld). Lets the
// pathfinder plan through chunks that are not loaded, and #mine / #goto find tracked
// blocks far away. Thread-safe: the game thread adds chunks, path calculations read.
class CachedWorld {
public:
    // stateIds: the block state used for each PathingBlockType when reading the cache
    // (air, water source, lava, stone/netherrack/end stone).
    CachedWorld(std::filesystem::path dir, int minY, int height, std::array<StateId, 4> stateIds);
    ~CachedWorld();
    CachedWorld(const CachedWorld&) = delete;
    CachedWorld& operator=(const CachedWorld&) = delete;

    StateId stateOf(PathingBlockType t) const { return stateIds_[static_cast<int>(t)]; }
    int minY() const { return minY_; }
    int height() const { return height_; }

    // nullptr if never seen. May read the region from disk (any thread).
    std::shared_ptr<const CachedChunk> chunk(int cx, int cz);
    bool isCached(int cx, int cz) { return chunk(cx, cz) != nullptr; }
    void put(std::shared_ptr<const CachedChunk> chunk);

    // Writes the modified regions on a worker thread (game thread).
    void saveAsync();
    // Writes everything now and waits (leaving the world, unload).
    void saveNow();
    // Unloads saved regions further than `distance` regions from the player (pruneRegionsFromRAM).
    void prune(int playerCx, int playerCz, int distance);

    // Tracked blocks matching `match` in the regions around `center`, closest first
    // (CachedWorld.getLocationsOf).
    std::vector<BlockPos> locationsOf(const std::function<bool(const std::string&)>& match, const BlockPos& center,
                                      std::size_t max, int maxRegionDistance);

    std::size_t loadedRegions() const;
    std::size_t cachedChunks() const;

private:
    static int64_t key(int rx, int rz) { return (static_cast<int64_t>(rx) << 32) | static_cast<uint32_t>(rz); }
    std::filesystem::path fileOf(int rx, int rz) const;
    std::shared_ptr<CachedRegion> region(int rx, int rz);  // loads from disk if needed
    struct Job {
        std::filesystem::path file;
        std::vector<std::shared_ptr<const CachedChunk>> chunks;
    };
    std::vector<Job> collectDirty();
    void writeJobs(const std::vector<Job>& jobs) const;

    std::filesystem::path dir_;
    int minY_, height_;
    std::array<StateId, 4> stateIds_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<int64_t, std::shared_ptr<CachedRegion>> regions_;
    std::unordered_set<int64_t> onDisk_;  // region files not loaded
    std::thread saver_;
};

}  // namespace baritone
