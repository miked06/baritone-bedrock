#pragma once
#include <deque>
#include <map>
#include <functional>
#include <optional>
#include "baritone/task/TaskQueue.h"
#include "baritone/world/BlockProps.h"
#include "baritone/utils/BlockPos.h"
namespace baritone {
class ActionHistory {
public:
    struct Entry { uint64_t id, tick; BlockPos position; StateId before, after; bool undone = false; };
    using Reader = std::function<std::optional<StateId>(BlockPos)>;
    void bind(WorldIdentity identity);
    void intent(BlockPos pos, StateId before, uint64_t tick);
    void observe(uint64_t tick, const Reader& reader);
    void clear() { pending_.clear(); entries_.clear(); }
    const auto& entries() const { return entries_; }
    const auto& pending() const { return pending_; }
    std::vector<Entry> recent(unsigned count) const;
    void markUndone(uint64_t id);
    static bool reversible(StateId state);
    static BlockPos placementTarget(BlockPos clicked, Direction face, const BlockProps& current);
private:
    struct Pending { StateId before, after; uint64_t tick, changedAt = 0; };
    std::map<BlockPos, Pending> pending_;
    std::deque<Entry> entries_;
    WorldIdentity identity_;
    uint64_t nextId_ = 1;
};
}
