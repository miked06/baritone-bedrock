#include "baritone/world/BlockProps.h"

#include <stdexcept>

namespace baritone {

namespace {

BlockProps airProps() {
    BlockProps air;
    air.name = "minecraft:air";
    air.type = BlockType::Air;
    air.collides = false;
    air.height = 0.f;
    air.pathfindable = true;
    air.replaceable = true;
    return air;
}

}  // namespace

BlockRegistry::BlockRegistry() {
    states_[kAir] = airProps();
}

BlockRegistry& BlockRegistry::get() {
    // ~6 MB: heap allocated once instead of living in the DLL's static data
    static BlockRegistry* instance = new BlockRegistry();
    return *instance;
}

StateId BlockRegistry::add(BlockProps props) {
    std::size_t id = count_.load(std::memory_order_relaxed);
    if (id >= kMaxStates) throw std::runtime_error("BlockRegistry full");
    states_[id] = std::move(props);
    count_.store(id + 1, std::memory_order_release);
    return static_cast<StateId>(id);
}

void BlockRegistry::reset() {
    count_.store(1, std::memory_order_release);
}

}  // namespace baritone
