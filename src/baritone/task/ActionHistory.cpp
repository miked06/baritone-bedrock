#include "baritone/task/ActionHistory.h"
#include <set>
namespace baritone {
void ActionHistory::bind(WorldIdentity identity) { if (!(identity == identity_)) { clear(); identity_ = std::move(identity); } }
void ActionHistory::intent(BlockPos pos, StateId before, uint64_t tick) {
    if (!pending_.contains(pos) && pending_.size() >= 64) return;
    pending_.try_emplace(pos, Pending{before, before, tick});
}
void ActionHistory::observe(uint64_t tick, const Reader& read) {
    for (auto it = pending_.begin(); it != pending_.end();) {
        auto& p = it->second;
        if (tick - p.tick > 400) { it = pending_.erase(it); continue; }
        const auto current = read(it->first);
        if (!current) { ++it; continue; }
        if (*current != p.after) { p.after = *current; p.changedAt = tick; }
        if (p.after != p.before && tick >= p.changedAt + 3) {
            entries_.push_back({nextId_++, tick, it->first, p.before, p.after});
            if (entries_.size() > 4096) entries_.pop_front();
            it = pending_.erase(it);
        } else ++it;
    }
}
std::vector<ActionHistory::Entry> ActionHistory::recent(unsigned count) const {
    if (!count || count > 64) throw std::invalid_argument("Undo count must be 1..64 recent observed changes");
    std::vector<Entry> out;
    for (auto it = entries_.rbegin(); it != entries_.rend() && out.size() < count; ++it) if (!it->undone) out.push_back(*it);
    return out;
}
void ActionHistory::markUndone(uint64_t id) { for (auto& entry : entries_) if (entry.id == id) entry.undone = true; }
BlockPos ActionHistory::placementTarget(BlockPos clicked, Direction face, const BlockProps& current) {
    return current.replaceable || current.type == BlockType::Air ? clicked : clicked.offset(face);
}
bool ActionHistory::reversible(StateId state) {
    const auto& p = BlockRegistry::get().props(state); const auto& n = p.name;
    if (p.type == BlockType::Air) return true;
    if (p.hasFluid() || p.falling || p.unbreakable || !p.fullCube) return false;
    static const std::set<std::string> plain{"minecraft:stone", "minecraft:cobblestone", "minecraft:dirt", "minecraft:grass_block", "minecraft:grass", "minecraft:deepslate", "minecraft:cobbled_deepslate", "minecraft:tuff", "minecraft:granite", "minecraft:andesite", "minecraft:diorite", "minecraft:netherrack", "minecraft:end_stone", "minecraft:bricks", "minecraft:stone_bricks", "minecraft:glass"};
    return plain.contains(n) || n.ends_with("_planks") || n.ends_with("_log") || n.ends_with("_wood") || n.ends_with("_wool") || n.ends_with("_concrete");
}
}
