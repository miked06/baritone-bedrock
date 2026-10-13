#include "baritone/cache/CachedChunk.h"

#include <array>
#include <unordered_set>

#include "baritone/pathing/movement/MovementHelper.h"
#include "baritone/world/WorldSnapshot.h"

namespace baritone {

PathingBlockType pathingTypeOf(const BlockProps& p) {
    if (p.fluid == Fluid::Water) {
        // only sources are plausibly usable; flowing water is cached as avoid
        return helper::possiblyFlowing(p) ? PathingBlockType::Avoid : PathingBlockType::Water;
    }
    if (helper::avoidWalkingInto(p) || p.bottomSlab) return PathingBlockType::Avoid;
    // AirBlock, TallGrassBlock, DoublePlantBlock (Baritone also lists FlowerBlock)
    if (p.type == BlockType::Air || p.type == BlockType::TallPlant) return PathingBlockType::Air;
    if (p.type == BlockType::Other && !p.collides && p.replaceable) return PathingBlockType::Air;
    return PathingBlockType::Solid;
}

bool isTrackedBlock(std::string_view name) {
    static const std::unordered_set<std::string_view> tracked = {
        // Baritone's list (Bedrock names)
        "minecraft:diamond_block", "minecraft:coal_block", "minecraft:iron_block", "minecraft:gold_block",
        "minecraft:emerald_ore", "minecraft:deepslate_emerald_ore", "minecraft:emerald_block",
        "minecraft:ender_chest", "minecraft:furnace", "minecraft:lit_furnace", "minecraft:chest",
        "minecraft:trapped_chest", "minecraft:end_portal", "minecraft:end_portal_frame", "minecraft:mob_spawner",
        "minecraft:barrier", "minecraft:observer", "minecraft:portal", "minecraft:hopper", "minecraft:beacon",
        "minecraft:brewing_stand", "minecraft:enchanting_table", "minecraft:anvil", "minecraft:chipped_anvil",
        "minecraft:damaged_anvil", "minecraft:bed", "minecraft:dragon_egg", "minecraft:jukebox",
        "minecraft:end_gateway", "minecraft:web", "minecraft:nether_wart", "minecraft:ladder", "minecraft:vine",
        // useful for #goto
        "minecraft:crafting_table", "minecraft:blast_furnace", "minecraft:smoker", "minecraft:barrel",
        // rare ores, so #mine can go back to them (Baritone leaves ores out of the cache)
        "minecraft:diamond_ore", "minecraft:deepslate_diamond_ore", "minecraft:ancient_debris",
        "minecraft:gold_ore", "minecraft:deepslate_gold_ore", "minecraft:lapis_ore", "minecraft:deepslate_lapis_ore",
    };
    if (tracked.contains(name)) return true;
    return name.ends_with("_shulker_box") || name.ends_with("_skull") || name.ends_with("_head");
}

CachedChunk::CachedChunk(int cx, int cz, int minY, int height)
    : cx_(cx), cz_(cz), minY_(minY), height_(height), bits_(static_cast<std::size_t>(height) * 256 / 4, 0) {}

std::shared_ptr<CachedChunk> CachedChunk::pack(const ChunkData& chunk, int minY, int height, int64_t timestamp) {
    auto out = std::make_shared<CachedChunk>(chunk.cx(), chunk.cz(), minY, height);
    out->timestamp_ = timestamp;
    const BlockRegistry& registry = BlockRegistry::get();
    // per state: pathing type + tracked, computed once per pack
    std::vector<int8_t> typeOf(registry.size(), -1);
    std::vector<int8_t> trackedOf(registry.size(), -1);
    auto info = [&](StateId id) -> std::pair<PathingBlockType, bool> {
        if (id >= typeOf.size()) {
            typeOf.resize(id + 1, -1);
            trackedOf.resize(id + 1, -1);
        }
        if (typeOf[id] < 0) {
            const BlockProps& p = registry.props(id);
            typeOf[id] = static_cast<int8_t>(pathingTypeOf(p));
            trackedOf[id] = isTrackedBlock(p.name) ? 1 : 0;
        }
        return {static_cast<PathingBlockType>(typeOf[id]), trackedOf[id] == 1};
    };

    int baseX = chunk.cx() << 4, baseZ = chunk.cz() << 4;
    for (int s = 0; s < chunk.numSections(); ++s) {
        const Section* sec = chunk.section(s);
        if (!sec) continue;  // all air: bits stay 0
        int sectionY = chunk.minY() + (s << 4);
        for (int i = 0; i < 4096; ++i) {
            StateId id = sec->ids[i];
            if (id == kAir) continue;
            int y = sectionY + (i >> 8);
            if (y < minY || y >= minY + height) continue;
            int lz = (i >> 4) & 15, lx = i & 15;
            auto [type, tracked] = info(id);
            if (type != PathingBlockType::Air) out->setType(lx, y, lz, type);
            if (tracked) out->special_[registry.props(id).name].push_back({baseX + lx, y, baseZ + lz});
        }
    }
    return out;
}

}  // namespace baritone
