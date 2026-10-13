#include "baritone/world/WorldSnapshot.h"

#include <atomic>

#include "baritone/cache/CachedWorld.h"

namespace baritone {

ChunkData::ChunkData(int cx, int cz, int minY, int height)
    : cx_(cx), cz_(cz), minY_(minY), numSections_((height + 15) >> 4),
      sections_(std::make_unique<std::atomic<Section*>[]>(numSections_)) {
    for (int i = 0; i < numSections_; ++i) sections_[i].store(nullptr, std::memory_order_relaxed);
}

ChunkData::~ChunkData() {
    for (int i = 0; i < numSections_; ++i) delete sections_[i].load(std::memory_order_relaxed);
}

void ChunkData::set(int lx, int y, int lz, StateId id) {
    int s = (y - minY_) >> 4;
    if (s < 0 || s >= numSections_) return;
    Section* sec = sections_[s].load(std::memory_order_acquire);
    if (!sec) {
        if (id == kAir) return;
        sec = new Section();
        sections_[s].store(sec, std::memory_order_release);
    }
    std::atomic_ref<StateId>(sec->ids[((y & 15) << 8) | (lz << 4) | lx]).store(id, std::memory_order_relaxed);
}

std::shared_ptr<const ChunkData> WorldSnapshot::chunk(int cx, int cz) const {
    return mutableChunk(cx, cz);
}

std::shared_ptr<ChunkData> WorldSnapshot::mutableChunk(int cx, int cz) const {
    std::shared_lock lock(mutex_);
    auto it = chunks_.find(key(cx, cz));
    return it == chunks_.end() ? nullptr : it->second;
}

bool WorldSnapshot::isLoaded(int cx, int cz) const {
    std::shared_lock lock(mutex_);
    return chunks_.contains(key(cx, cz));
}

void WorldSnapshot::put(std::shared_ptr<ChunkData> chunk) {
    std::unique_lock lock(mutex_);
    int64_t k = key(chunk->cx(), chunk->cz());
    explored_.insert(k);
    chunks_[k] = std::move(chunk);
}

bool WorldSnapshot::wasExplored(int cx, int cz) const {
    std::shared_ptr<CachedWorld> cache;
    {
        std::shared_lock lock(mutex_);
        if (explored_.contains(key(cx, cz))) return true;
        cache = cache_;
    }
    return cache && cache->isCached(cx, cz);
}

std::shared_ptr<CachedWorld> WorldSnapshot::cache() const {
    std::shared_lock lock(mutex_);
    return cache_;
}

void WorldSnapshot::setCache(std::shared_ptr<CachedWorld> cache) {
    std::unique_lock lock(mutex_);
    cache_ = std::move(cache);
}

std::size_t WorldSnapshot::exploredCount() const {
    std::shared_lock lock(mutex_);
    return explored_.size();
}

void WorldSnapshot::remove(int cx, int cz) {
    std::unique_lock lock(mutex_);
    chunks_.erase(key(cx, cz));
}

std::size_t WorldSnapshot::chunkCount() const {
    std::shared_lock lock(mutex_);
    return chunks_.size();
}

std::vector<std::pair<int, int>> WorldSnapshot::loadedChunks() const {
    std::shared_lock lock(mutex_);
    std::vector<std::pair<int, int>> out;
    out.reserve(chunks_.size());
    for (const auto& [k, c] : chunks_) out.emplace_back(c->cx(), c->cz());
    return out;
}

void WorldSnapshot::setBlock(int x, int y, int z, StateId id) {
    if (auto c = mutableChunk(x >> 4, z >> 4)) c->set(x & 15, y, z & 15, id);
}

}  // namespace baritone
