#pragma once
#include <deque>
#include <memory>
#include <unordered_map>
#include "baritone/world/WorldSnapshot.h"

namespace baritone {
// Bounded, deduplicated work queue. The platform packs these chunks on the game
// thread within its frame budget; queued snapshots survive removal from the world.
class ChunkPackerQueue {
public:
    void setLimit(std::size_t limit) {
        limit_ = limit;
        while (order_.size() > limit_) {
            chunks_.erase(order_.front());
            order_.pop_front();
        }
    }
    bool push(std::shared_ptr<const ChunkData> chunk) {
        if (!chunk || limit_ == 0) return false;
        auto key = WorldSnapshot::key(chunk->cx(), chunk->cz());
        if (auto it = chunks_.find(key); it != chunks_.end()) { it->second = std::move(chunk); return true; }
        if (order_.size() >= limit_) return false;
        chunks_.emplace(key, std::move(chunk));
        try { order_.push_back(key); }
        catch (...) { chunks_.erase(key); throw; }
        return true;
    }
    std::shared_ptr<const ChunkData> take() {
        if (order_.empty()) return {};
        auto key = order_.front();
        order_.pop_front();
        auto chunk = std::move(chunks_.at(key));
        chunks_.erase(key);
        return chunk;
    }
    std::size_t size() const { return order_.size(); }
    void clear() { chunks_.clear(); order_.clear(); }
private:
    std::size_t limit_ = 2000;
    std::deque<int64_t> order_;
    std::unordered_map<int64_t, std::shared_ptr<const ChunkData>> chunks_;
};
}
