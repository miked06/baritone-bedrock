#include "baritone/cache/WorldScanner.h"

#include "baritone/Settings.h"
#include "baritone/cache/CachedWorld.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace baritone {

namespace {

// What a block drops when mined without silk touch, when it differs from the block.
const std::unordered_map<std::string, std::vector<std::string>>& dropTable() {
    static const std::unordered_map<std::string, std::vector<std::string>> table{
        {"minecraft:stone", {"minecraft:cobblestone"}},
        {"minecraft:deepslate", {"minecraft:cobbled_deepslate"}},
        {"minecraft:grass_block", {"minecraft:dirt"}},
        {"minecraft:grass", {"minecraft:dirt"}},
        {"minecraft:mycelium", {"minecraft:dirt"}},
        {"minecraft:podzol", {"minecraft:dirt"}},
        {"minecraft:coal_ore", {"minecraft:coal"}},
        {"minecraft:deepslate_coal_ore", {"minecraft:coal"}},
        {"minecraft:iron_ore", {"minecraft:raw_iron"}},
        {"minecraft:deepslate_iron_ore", {"minecraft:raw_iron"}},
        {"minecraft:copper_ore", {"minecraft:raw_copper"}},
        {"minecraft:deepslate_copper_ore", {"minecraft:raw_copper"}},
        {"minecraft:gold_ore", {"minecraft:raw_gold"}},
        {"minecraft:deepslate_gold_ore", {"minecraft:raw_gold"}},
        {"minecraft:diamond_ore", {"minecraft:diamond"}},
        {"minecraft:deepslate_diamond_ore", {"minecraft:diamond"}},
        {"minecraft:emerald_ore", {"minecraft:emerald"}},
        {"minecraft:deepslate_emerald_ore", {"minecraft:emerald"}},
        {"minecraft:lapis_ore", {"minecraft:lapis_lazuli"}},
        {"minecraft:deepslate_lapis_ore", {"minecraft:lapis_lazuli"}},
        {"minecraft:redstone_ore", {"minecraft:redstone"}},
        {"minecraft:lit_redstone_ore", {"minecraft:redstone"}},
        {"minecraft:deepslate_redstone_ore", {"minecraft:redstone"}},
        {"minecraft:lit_deepslate_redstone_ore", {"minecraft:redstone"}},
        {"minecraft:quartz_ore", {"minecraft:quartz"}},
        {"minecraft:nether_gold_ore", {"minecraft:gold_nugget"}},
        {"minecraft:glowstone", {"minecraft:glowstone_dust"}},
        {"minecraft:gravel", {"minecraft:gravel", "minecraft:flint"}},
        {"minecraft:clay", {"minecraft:clay_ball"}},
        {"minecraft:snow", {"minecraft:snowball"}},
        {"minecraft:melon_block", {"minecraft:melon_slice"}},
        {"minecraft:sea_lantern", {"minecraft:prismarine_crystals"}},
        {"minecraft:amethyst_cluster", {"minecraft:amethyst_shard"}},
    };
    return table;
}

}  // namespace

std::string BlockFilter::normalize(std::string name) {
    std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (name.find(':') == std::string::npos) name = "minecraft:" + name;
    return name;
}

// The ore blocks of one family: diamond_ore also means deepslate_diamond_ore (most diamonds
// are in deepslate since 1.18), and an item name means its ores ("diamond", "iron", "lapis").
static std::vector<std::string> oreFamily(const std::string& n) {
    static const std::unordered_map<std::string, std::string> aliases{
        {"minecraft:iron", "minecraft:raw_iron"},     {"minecraft:iron_ingot", "minecraft:raw_iron"},
        {"minecraft:gold", "minecraft:raw_gold"},     {"minecraft:gold_ingot", "minecraft:raw_gold"},
        {"minecraft:copper", "minecraft:raw_copper"}, {"minecraft:copper_ingot", "minecraft:raw_copper"},
        {"minecraft:lapis", "minecraft:lapis_lazuli"},
    };
    std::string item = n;
    if (auto it = aliases.find(n); it != aliases.end()) item = it->second;
    if (auto it = dropTable().find(n); it != dropTable().end() && n.ends_with("_ore")) item = it->second.front();
    std::vector<std::string> out;
    for (const auto& [block, drops] : dropTable()) {
        if (block.ends_with("_ore") && drops.front() == item) out.push_back(block);
    }
    if (n == "minecraft:netherite" || n == "minecraft:netherite_scrap") out.push_back("minecraft:ancient_debris");
    if (item == "minecraft:raw_gold" && !n.ends_with("_ore")) out.push_back("minecraft:nether_gold_ore");  // "gold" in the nether
    return out;
}

BlockFilter::BlockFilter(const std::vector<std::string>& names) {
    auto add = [this](const std::string& block) {
        names_.insert(block);
        drops_.insert(block);  // with silk touch, or blocks that drop themselves
        if (auto it = dropTable().find(block); it != dropTable().end()) {
            for (const std::string& d : it->second) drops_.insert(d);
        }
    };
    // Java names typed out of habit
    static const std::unordered_map<std::string, std::string> javaNames{
        {"minecraft:nether_quartz_ore", "minecraft:quartz_ore"}, {"minecraft:magma_block", "minecraft:magma"},
        {"minecraft:cobweb", "minecraft:web"},                     {"minecraft:spawner", "minecraft:mob_spawner"},
        {"minecraft:nether_portal", "minecraft:portal"},           {"minecraft:sugar_cane", "minecraft:reeds"},
    };
    for (const std::string& raw : names) {
        std::string n = normalize(raw);
        if (auto it = javaNames.find(n); it != javaNames.end()) n = it->second;
        std::vector<std::string> family = oreFamily(n);
        for (const std::string& ore : family) add(ore);
        if (family.empty() || dropTable().contains(n)) add(n);  // an item name alone is not a block
    }
}

std::string BlockFilter::toString() const {
    std::string out;
    for (const std::string& n : names_) out += (out.empty() ? "" : ", ") + n;
    return out;
}

std::vector<BlockPos> scanLoadedChunks(const WorldSnapshot& world, const BlockFilter& filter, const BlockPos& center,
                                       std::size_t max, int maxChunkRadius) {
    const BlockRegistry& registry = BlockRegistry::get();
    std::size_t states = registry.size();
    std::vector<char> match(states, 0);
    bool any = false;
    for (std::size_t id = 0; id < states; ++id) {
        if (filter.matches(registry.props(static_cast<StateId>(id)))) {
            match[id] = 1;
            any = true;
        }
    }
    std::vector<BlockPos> found;
    if (!any) return found;

    int ccx = center.x >> 4, ccz = center.z >> 4;
    std::vector<std::pair<int, int>> chunks;
    for (auto [cx, cz] : world.loadedChunks()) {
        if (std::max(std::abs(cx - ccx), std::abs(cz - ccz)) <= maxChunkRadius) chunks.emplace_back(cx, cz);
    }
    std::ranges::sort(chunks, [&](auto a, auto b) {
        int da = (a.first - ccx) * (a.first - ccx) + (a.second - ccz) * (a.second - ccz);
        int db = (b.first - ccx) * (b.first - ccx) + (b.second - ccz) * (b.second - ccz);
        return da < db;
    });
    for (auto [cx, cz] : chunks) {
        auto chunk = world.chunk(cx, cz);
        if (!chunk) continue;
        for (int s = 0; s < chunk->numSections(); ++s) {
            const Section* sec = chunk->section(s);
            if (!sec) continue;
            int baseY = chunk->minY() + s * 16;
            for (int i = 0; i < 4096; ++i) {
                StateId id = sec->ids[i];
                if (id < states && match[id]) {
                    found.push_back({(cx << 4) | (i & 15), baseY + (i >> 8), (cz << 4) | ((i >> 4) & 15)});
                }
            }
        }
        // closest chunks first: once enough are found, farther chunks can't beat them much
        if (found.size() >= max * 4) break;
    }
    auto dist = [&](const BlockPos& p) { return p.distSq(center); };
    if (found.size() > max) {
        std::ranges::nth_element(found, found.begin() + static_cast<std::ptrdiff_t>(max),
                                 [&](const BlockPos& a, const BlockPos& b) { return dist(a) < dist(b); });
        found.resize(max);
    }
    std::ranges::sort(found, [&](const BlockPos& a, const BlockPos& b) { return dist(a) < dist(b); });
    return found;
}

std::vector<BlockPos> searchWorld(const WorldSnapshot& world, const BlockFilter& filter, const BlockPos& center,
                                  std::size_t max, int maxChunkRadius) {
    std::vector<BlockPos> found = scanLoadedChunks(world, filter, center, max, maxChunkRadius);
    auto cache = world.cache();
    bool tracked = std::ranges::any_of(filter.names(), [](const std::string& n) { return isTrackedBlock(n); });
    if (cache && tracked) {
        // maxRegionDistance 1: the player's region and the 8 around it, like Baritone
        auto cached = cache->locationsOf([&](const std::string& n) { return filter.matchesName(n); }, center,
                                         static_cast<std::size_t>(std::max(0, Settings::get().maxCachedWorldScanCount)), 1);
        for (const BlockPos& p : cached) {
            if (!world.isLoaded(p.x >> 4, p.z >> 4)) found.push_back(p);  // loaded chunks were scanned exactly
        }
        std::ranges::sort(found, [&](const BlockPos& a, const BlockPos& b) { return a.distSq(center) < b.distSq(center); });
        if (found.size() > max) found.resize(max);
    }
    return found;
}

}  // namespace baritone
