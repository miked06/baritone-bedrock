#pragma once
#include <bitset>
#include <unordered_map>
#include "baritone/utils/BlockPos.h"
namespace baritone {
// Exact world positions, compacted in 16-cube sections. A large completed build
// must not allocate one hash-table node per block just to remember its progress.
class CompletedPositions {
public:
    void insert(BlockPos p) { sections_[section(p)].set(index(p)); }
    bool contains(BlockPos p) const {
        auto it = sections_.find(section(p));
        return it != sections_.end() && it->second.test(index(p));
    }
    void erase(BlockPos p) {
        auto it = sections_.find(section(p));
        if (it == sections_.end()) return;
        it->second.reset(index(p));
        if (it->second.none()) sections_.erase(it);
    }
    void clear() { sections_.clear(); }
    std::size_t sectionCount() const { return sections_.size(); }
private:
    static BlockPos section(BlockPos p) { return {p.x >> 4, p.y >> 4, p.z >> 4}; }
    static std::size_t index(BlockPos p) { return ((p.y & 15) << 8) | ((p.z & 15) << 4) | (p.x & 15); }
    std::unordered_map<BlockPos, std::bitset<4096>> sections_;
};
}
