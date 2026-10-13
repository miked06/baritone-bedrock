#include "platform/WorldTracker.h"

#include <algorithm>
#include <chrono>
#include <mutex>

#include "baritone/Settings.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "platform/BlockClassifier.h"
#include "sdk/Game.h"

namespace platform {

using baritone::BlockPos;
using baritone::ChunkData;
using baritone::StateId;
using baritone::WorldSnapshot;
using Clock = std::chrono::steady_clock;

namespace {

std::mutex g_snapshotMutex;

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

int dimensionId(const std::string& name) {
    if (name == "Nether") return 1;
    if (name == "TheEnd") return 2;
    return 0;
}

}  // namespace

WorldTracker& WorldTracker::get() {
    static WorldTracker instance;
    return instance;
}

std::shared_ptr<const WorldSnapshot> WorldTracker::snapshot() const {
    std::lock_guard lock(g_snapshotMutex);
    return snapshot_;
}

void WorldTracker::reset() {
    flushCache();
    world_.clear();
    std::lock_guard lock(g_snapshotMutex);
    snapshot_.reset();
    scannedAt_.clear();
    dimension_ = -1;
    region_ = nullptr;
    // Block* pointers stay valid for the whole game session, so blockIds_ is kept.
}

void WorldTracker::setWorld(const std::string& folder) {
    if (folder == world_) return;
    flushCache();
    world_ = folder;
}

StateId WorldTracker::idOfName(const std::string& name) {
    auto it = namedIds_.find(name);
    if (it != namedIds_.end()) return it->second;
    StateId id = baritone::BlockRegistry::get().add(classifyBlock(name, nullptr));
    namedIds_.emplace(name, id);
    return id;
}

void WorldTracker::ensureCache() {
    bool wanted = baritone::Settings::get().chunkCaching && !world_.empty() && snapshot_;
    if (!wanted) {
        if (cache_) flushCache();
        return;
    }
    if (cache_) return;
    const char* solid = dimension_ == 1 ? "minecraft:netherrack" : dimension_ == 2 ? "minecraft:end_stone" : "minecraft:stone";
    std::array<StateId, 4> ids{baritone::kAir, idOfName("minecraft:water"), idOfName("minecraft:lava"), idOfName(solid)};
    auto dir = paths::dataDir() / "cache" / world_ / ("dim" + std::to_string(dimension_));
    cache_ = std::make_shared<baritone::CachedWorld>(dir, minY_, maxY_ - minY_, ids);
    snapshot_->setCache(cache_);
    logger::info("Long-distance cache: {}", dir.string());
}

void WorldTracker::packIntoCache(const ChunkData& chunk) {
    if (cache_) cache_->put(baritone::CachedChunk::pack(chunk, minY_, maxY_ - minY_, nowMs()));
}

std::size_t WorldTracker::repack() {
    if (!cache_ || !snapshot_) return 0;
    std::size_t n = 0;
    for (auto [cx, cz] : snapshot_->loadedChunks()) {
        if (auto c = snapshot_->chunk(cx, cz)) {
            packIntoCache(*c);
            ++n;
        }
    }
    return n;
}

void WorldTracker::flushCache() {
    if (!cache_) return;
    auto start = Clock::now();
    if (snapshot_) {
        for (auto [cx, cz] : snapshot_->loadedChunks())
            if (auto c = snapshot_->chunk(cx, cz)) packIntoCache(*c);
        snapshot_->setCache(nullptr);
    }
    cache_->saveNow();
    logger::info("Long-distance cache saved ({} chunks in memory, {:.0f} ms)", cache_->cachedChunks(),
                 std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    cache_.reset();
}

void WorldTracker::setDimension(int dimension, int minY, int maxY) {
    flushCache();
    std::lock_guard lock(g_snapshotMutex);
    dimension_ = dimension;
    minY_ = minY;
    maxY_ = maxY;
    snapshot_ = std::make_shared<WorldSnapshot>(dimension, minY, maxY);
    scannedAt_.clear();
    logger::info("World tracker: dimension {} (y {}..{})", dimension, minY, maxY);
}

StateId WorldTracker::idOf(const sdk::Block* block) {
    if (!block) return baritone::kAir;
    auto it = blockIds_.find(block);
    if (it != blockIds_.end()) return it->second;
    const sdk::nbt::CompoundTag& tag = block->serializationId();
    std::string name = tag.string("name").value_or(block->legacy()->name());
    StateId id = baritone::BlockRegistry::get().add(classifyBlock(name, tag.compound("states")));
    blockIds_.emplace(block, id);
    return id;
}

void WorldTracker::scanChunk(int cx, int cz, bool refresh) {
    auto chunk = std::make_shared<ChunkData>(cx, cz, minY_, maxY_ - minY_);
    sdk::BlockPos pos;
    const sdk::Block* lastBlock = nullptr;
    StateId lastId = baritone::kAir;
    for (int lx = 0; lx < 16; ++lx) {
        pos.x = (cx << 4) | lx;
        for (int lz = 0; lz < 16; ++lz) {
            pos.z = (cz << 4) | lz;
            for (int y = minY_; y < maxY_; ++y) {
                pos.y = y;
                const sdk::Block* block = region_->getBlock(pos);
                if (block != lastBlock) {  // long runs of the same block (air, stone)
                    lastBlock = block;
                    lastId = idOf(block);
                }
                if (lastId != baritone::kAir) chunk->set(lx, y, lz, lastId);
            }
        }
    }
    // a refresh catches block changes: Java repackOnAnyBlockChange repacks the chunk then
    // (chunks leaving the radius are always packed)
    if (!refresh || baritone::Settings::get().repackOnAnyBlockChange) packIntoCache(*chunk);
    snapshot_->put(std::move(chunk));
    scannedAt_[WorldSnapshot::key(cx, cz)] = tick_;
}

void WorldTracker::tick(sdk::BlockSource& region, sdk::LocalPlayer& player) {
    ++tick_;
    region_ = &region;
    sdk::Dimension* dim = player.dimension();
    int dimension = dim ? dimensionId(dim->name()) : 0;
    if (!snapshot_ || dimension != dimension_) setDimension(dimension, region.minHeight(), region.maxHeight());
    ensureCache();

    sdk::BlockPos feet = sdk::BlockPos::floor(player.feet());
    int pcx = feet.x >> 4, pcz = feet.z >> 4;
    auto start = Clock::now();
    auto elapsedMs = [&] { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); };

    // Candidates: missing chunks first, then stale ones near the player, closest first.
    struct Candidate {
        int cx, cz, dist;
        bool missing;
    };
    std::vector<Candidate> candidates;
    for (int dx = -loadRadius; dx <= loadRadius; ++dx) {
        for (int dz = -loadRadius; dz <= loadRadius; ++dz) {
            int cx = pcx + dx, cz = pcz + dz;
            auto it = scannedAt_.find(WorldSnapshot::key(cx, cz));
            bool missing = it == scannedAt_.end();
            int dist = std::max(std::abs(dx), std::abs(dz));
            bool stale = !missing && dist <= refreshRadius && tick_ - it->second >= static_cast<uint64_t>(refreshTicks);
            if (missing || stale) candidates.push_back({cx, cz, dx * dx + dz * dz, missing});
        }
    }
    std::ranges::sort(candidates, [](const Candidate& a, const Candidate& b) { return a.dist < b.dist; });

    for (const Candidate& c : candidates) {
        if (elapsedMs() > budgetMs) break;
        sdk::BlockPos center{(c.cx << 4) + 8, std::clamp(feet.y, minY_, maxY_ - 1), (c.cz << 4) + 8};
        if (!region.hasChunksAt(center, 0)) continue;  // not loaded by the game (yet)
        auto chunkStart = Clock::now();
        scanChunk(c.cx, c.cz, !c.missing);
        lastChunkScanMs_ = std::chrono::duration<double, std::milli>(Clock::now() - chunkStart).count();
    }

    // write the cache regularly, keep only the regions around the player in memory
    if (cache_ && tick_ % 1200 == 0) {
        if (baritone::Settings::get().pruneRegionsFromRAM) cache_->prune(pcx, pcz, 1);
        cache_->saveAsync();
    }

    // forget chunks far outside the radius
    if (tick_ % 100 == 0) {
        for (auto [cx, cz] : snapshot_->loadedChunks()) {
            if (std::max(std::abs(cx - pcx), std::abs(cz - pcz)) > loadRadius + 2) {
                if (auto c = snapshot_->chunk(cx, cz)) packIntoCache(*c);  // last known state
                snapshot_->remove(cx, cz);
                scannedAt_.erase(WorldSnapshot::key(cx, cz));
            }
        }
    }
}

void WorldTracker::refreshAround(const BlockPos& c, int radius) {
    if (!region_ || !snapshot_) return;
    sdk::BlockPos pos;
    for (int x = c.x - radius; x <= c.x + radius; ++x) {
        for (int z = c.z - radius; z <= c.z + radius; ++z) {
            for (int y = std::max(minY_, c.y - radius); y <= std::min(maxY_ - 1, c.y + radius); ++y) {
                pos = {x, y, z};
                snapshot_->setBlock(x, y, z, idOf(region_->getBlock(pos)));
            }
        }
    }
}

}  // namespace platform
