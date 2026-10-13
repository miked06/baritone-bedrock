#pragma once
#include <memory>
#include <unordered_map>

#include "baritone/cache/CachedChunk.h"
#include "baritone/world/BlockProps.h"
#include "baritone/world/WorldSnapshot.h"

namespace baritone {

class CachedWorld;

// Fast block access for one path calculation (Baritone's BlockStateInterface). Not
// thread-safe: each calculation owns one. Keeps the chunks it touched alive, so a chunk
// unloaded by the game during the calculation is still readable. Chunks that are not
// loaded are read from the long-distance cache (air / water / lava / stone approximations).
class BlockStateInterface {
public:
    explicit BlockStateInterface(std::shared_ptr<const WorldSnapshot> world);

    StateId get(int x, int y, int z) {
        if (y < minY_ || y >= maxY_) return kAir;
        int cx = x >> 4, cz = z >> 4;
        if (!curValid_ || curCx_ != cx || curCz_ != cz) {
            cur_ = lookup(cx, cz);
            curCx_ = cx;
            curCz_ = cz;
            curValid_ = true;
        }
        if (cur_.live) return cur_.live->get(x & 15, y, z & 15);
        if (cur_.cached) return cachedIds_[static_cast<int>(cur_.cached->type(x & 15, y, z & 15))];
        return kAir;
    }

    const BlockProps& props(int x, int y, int z) { return registry_.props(get(x, y, z)); }
    const BlockProps& props(StateId id) const { return registry_.props(id); }

    // Loaded or cached (BlockStateInterface.isLoaded): the pathfinder may go there.
    bool isLoaded(int x, int z) {
        Entry e = lookup(x >> 4, z >> 4);
        return e.live || e.cached;
    }
    // Really loaded (worldContainsLoadedChunk): the blocks are exact.
    bool isLoadedLive(int x, int z) { return lookup(x >> 4, z >> 4).loaded; }

    int minY() const { return minY_; }
    int maxY() const { return maxY_; }
    const WorldSnapshot& world() const { return *world_; }

private:
    struct Entry {
        const ChunkData* live = nullptr;
        const CachedChunk* cached = nullptr;
        bool loaded = false;  // really loaded, even when read from the cache (pathThroughCachedOnly)
    };
    Entry lookup(int cx, int cz);

    std::shared_ptr<const WorldSnapshot> world_;
    std::shared_ptr<CachedWorld> cache_;
    StateId cachedIds_[4] = {kAir, kAir, kAir, kAir};
    const BlockRegistry& registry_;
    int minY_, maxY_;
    Entry cur_;
    int curCx_ = 0, curCz_ = 0;
    bool curValid_ = false;
    struct Held {
        std::shared_ptr<const ChunkData> live;
        std::shared_ptr<const CachedChunk> cached;
        bool loaded = false;
    };
    bool useTheRealWorld_ = true;  // Java: !pathThroughCachedOnly
    std::unordered_map<int64_t, Held> held_;
};

}  // namespace baritone
