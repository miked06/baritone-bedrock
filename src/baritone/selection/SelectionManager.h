#pragma once
#include <algorithm>
#include <optional>
#include <vector>

#include "baritone/utils/BlockPos.h"

namespace baritone {

// A box between two corners, both included (Baritone's Selection).
struct Selection {
    BlockPos pos1, pos2;

    BlockPos min() const { return {std::min(pos1.x, pos2.x), std::min(pos1.y, pos2.y), std::min(pos1.z, pos2.z)}; }
    BlockPos max() const { return {std::max(pos1.x, pos2.x), std::max(pos1.y, pos2.y), std::max(pos1.z, pos2.z)}; }
    BlockPos size() const {
        BlockPos lo = min(), hi = max();
        return {hi.x - lo.x + 1, hi.y - lo.y + 1, hi.z - lo.z + 1};
    }
    bool contains(const BlockPos& p) const {
        BlockPos lo = min(), hi = max();
        return p.x >= lo.x && p.y >= lo.y && p.z >= lo.z && p.x <= hi.x && p.y <= hi.y && p.z <= hi.z;
    }
    // Moves the face on side `d` by n blocks (expand: outwards, negative n: inwards).
    void expand(Direction d, int n) {
        BlockPos lo = min(), hi = max();
        DirOffset o = offsetOf(d);
        if (o.x > 0 || o.y > 0 || o.z > 0) hi = hi.offset(d, n);
        else lo = lo.offset(d, n);
        pos1 = lo;
        pos2 = hi;
    }
    void shift(Direction d, int n) {
        pos1 = pos1.offset(d, n);
        pos2 = pos2.offset(d, n);
    }
};

// Every #sel selection, oldest first (Baritone's SelectionManager).
class SelectionManager {
public:
    void add(Selection s) { selections_.push_back(s); }
    std::optional<Selection> removeLast() {
        if (selections_.empty()) return std::nullopt;
        Selection s = selections_.back();
        selections_.pop_back();
        return s;
    }
    void clear() { selections_.clear(); }
    std::vector<Selection>& all() { return selections_; }
    const std::vector<Selection>& all() const { return selections_; }
    bool contains(const BlockPos& p) const {
        return std::ranges::any_of(selections_, [&](const Selection& s) { return s.contains(p); });
    }

private:
    std::vector<Selection> selections_;
};

}  // namespace baritone
