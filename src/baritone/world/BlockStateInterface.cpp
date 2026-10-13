#include "baritone/world/BlockStateInterface.h"

#include "baritone/Settings.h"
#include "baritone/cache/CachedWorld.h"

namespace baritone {

BlockStateInterface::BlockStateInterface(std::shared_ptr<const WorldSnapshot> world)
    : world_(std::move(world)),
      cache_(world_->cache()),
      registry_(BlockRegistry::get()),
      minY_(world_->minY()),
      maxY_(world_->maxY()) {
    if (cache_ && (cache_->minY() != minY_ || cache_->minY() + cache_->height() != maxY_)) cache_.reset();
    if (cache_) {
        for (int t = 0; t < 4; ++t) cachedIds_[t] = cache_->stateOf(static_cast<PathingBlockType>(t));
    }
    // Java pathThroughCachedOnly: the loaded chunks are read from the cache too
    useTheRealWorld_ = !(cache_ && Settings::get().pathThroughCachedOnly);
}

BlockStateInterface::Entry BlockStateInterface::lookup(int cx, int cz) {
    int64_t key = WorldSnapshot::key(cx, cz);
    auto it = held_.find(key);
    if (it == held_.end()) {
        // Missing chunks are remembered too, so they are not looked up again.
        Held h;
        h.live = world_->chunk(cx, cz);
        h.loaded = h.live != nullptr;
        if (!useTheRealWorld_) h.live.reset();
        if (!h.live && cache_) h.cached = cache_->chunk(cx, cz);
        it = held_.emplace(key, std::move(h)).first;
    }
    return {it->second.live.get(), it->second.cached.get(), it->second.loaded};
}

}  // namespace baritone
