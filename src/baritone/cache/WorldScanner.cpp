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

// Generic words for a group of blocks. Bedrock split its legacy "log", "log2", "wood",
// "leaves" and "planks" blocks into one block per species, so "#mine wood" would otherwise
// look for a block that no longer exists (and branch mine forever instead of cutting trees).
static const std::vector<std::string>* blockFamily(const std::string& n) {
    static const std::vector<std::string> species{"oak",      "spruce", "birch",  "jungle", "acacia",
                                                  "dark_oak", "mangrove", "cherry", "pale_oak"};
    static const std::vector<std::string> logs = [] {
        std::vector<std::string> out;
        for (const std::string& s : species) out.push_back("minecraft:" + s + "_log");
        for (const std::string& s : species) out.push_back("minecraft:" + s + "_wood");
        for (const char* s : {"crimson_stem", "warped_stem", "crimson_hyphae", "warped_hyphae"})
            out.push_back(std::string("minecraft:") + s);
        return out;
    }();
    static const std::vector<std::string> leaves = [] {
        std::vector<std::string> out;
        for (const std::string& s : species) out.push_back("minecraft:" + s + "_leaves");
        out.push_back("minecraft:azalea_leaves");
        out.push_back("minecraft:azalea_leaves_flowered");
        return out;
    }();
    static const std::vector<std::string> planks = [] {
        std::vector<std::string> out;
        for (const std::string& s : species) out.push_back("minecraft:" + s + "_planks");
        for (const char* s : {"crimson_planks", "warped_planks", "bamboo_planks"}) out.push_back(std::string("minecraft:") + s);
        return out;
    }();
    // one species: "oak" (or "#mine oak wood", merged by the constructor) means its logs
    static const std::unordered_map<std::string, std::vector<std::string>> speciesLogs = [] {
        std::unordered_map<std::string, std::vector<std::string>> out;
        for (const std::string& s : species) out["minecraft:" + s] = {"minecraft:" + s + "_log", "minecraft:" + s + "_wood"};
        out["minecraft:crimson"] = {"minecraft:crimson_stem", "minecraft:crimson_hyphae"};
        out["minecraft:warped"] = {"minecraft:warped_stem", "minecraft:warped_hyphae"};
        return out;
    }();
    if (auto it = speciesLogs.find(n); it != speciesLogs.end()) return &it->second;
    static const std::unordered_map<std::string, const std::vector<std::string>*> families{
        {"minecraft:wood", &logs},   {"minecraft:woods", &logs},  {"minecraft:log", &logs},
        {"minecraft:logs", &logs},   {"minecraft:log2", &logs},   {"minecraft:tree", &logs},
        {"minecraft:trees", &logs},  {"minecraft:stem", &logs},   {"minecraft:stems", &logs},
        {"minecraft:leaves", &leaves}, {"minecraft:leaves2", &leaves}, {"minecraft:leaf", &leaves},
        {"minecraft:planks", &planks}, {"minecraft:plank", &planks},
    };
    auto it = families.find(n);
    return it == families.end() ? nullptr : it->second;
}

BlockFilter::BlockFilter(const std::vector<std::string>& names) {
    auto add = [this](const std::string& block, const BlockSelector& selector) {
        names_.insert(block);
        selectors_.push_back({block, selector.states});
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
    // "#mine oak wood" arrives as two words: "oak" then "wood" means oak logs, not every log
    std::vector<std::string> merged;
    for (std::size_t i = 0; i < names.size(); ++i) {
        merged.push_back(names[i]);
        if (i + 1 >= names.size() || names[i].find_first_of("[:") != std::string::npos) continue;
        const std::string next = normalize(names[i + 1]);
        const bool woodWord = next == "minecraft:wood" || next == "minecraft:log" || next == "minecraft:logs" ||
                              next == "minecraft:stem" || next == "minecraft:stems";
        const auto* group = blockFamily(normalize(names[i]));
        if (woodWord && group && group->size() == 2) ++i;  // a species: its own logs only
    }
    for (const std::string& raw : merged) {
        const BlockSelector selector = BlockSelector::parse(raw);
        std::string n = selector.name;
        if (auto it = javaNames.find(n); it != javaNames.end()) n = it->second;
        if (const auto* group = blockFamily(n)) {
            for (const std::string& block : *group) add(block, selector);
            BlockSelector label = selector;
            label.name = n.substr(n.find(':') + 1);  // "wood", not 22 block names in every message
            display_.push_back(label.toString());
            continue;
        }
        const std::size_t first = selectors_.size();
        std::vector<std::string> family = oreFamily(n);
        for (const std::string& ore : family) add(ore, selector);
        if (family.empty() || dropTable().contains(n)) add(n, selector);  // an item name alone is not a block
        for (std::size_t i = first; i < selectors_.size(); ++i) display_.push_back(selectors_[i].toString());
    }
}

std::string BlockFilter::toString() const {
    std::string out;
    std::unordered_set<std::string> printed;
    if (!display_.empty()) {
        for (const std::string& text : display_)
            if (printed.insert(text).second) out += (out.empty() ? "" : ", ") + text;
        return out;
    }
    for (const BlockSelector& selector : selectors_) {
        const std::string text = selector.toString();
        if (printed.insert(text).second) out += (out.empty() ? "" : ", ") + text;
    }
    return out;
}

bool BlockFilter::matches(const BlockProps& p) const {
    return names_.contains(p.name) && std::ranges::any_of(selectors_, [&](const BlockSelector& selector) {
        return selector.matches(p);
    });
}

BlockFilter BlockFilter::restrictedTo(const std::unordered_set<std::string>& allowedNames) const {
    BlockFilter result;
    for (const auto& selector : selectors_) {
        if (!allowedNames.contains(selector.name)) continue;
        result.selectors_.push_back(selector);
        result.names_.insert(selector.name);
        result.drops_.insert(selector.name);
        if (auto it = dropTable().find(selector.name); it != dropTable().end())
            result.drops_.insert(it->second.begin(), it->second.end());
    }
    return result;
}

std::vector<BlockPos> scanLoadedChunks(const WorldSnapshot& world, const BlockFilter& filter, const BlockPos& center,
                                       std::size_t max, int maxChunkRadius) {
    if (max == 0 || filter.empty() || maxChunkRadius < 0) return {};
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
    if (max == 0 || filter.empty()) return {};
    std::vector<BlockPos> found;
    auto cache = world.cache();
    bool tracked = std::ranges::any_of(filter.names(), [](const std::string& n) { return isTrackedBlock(n); });
    if (cache && tracked) {
        // maxRegionDistance 1: the player's region and the 8 around it, like Baritone
        auto cached = cache->locationsOf([&](const std::string& n) { return filter.matchesName(n); }, center,
                                         static_cast<std::size_t>(std::max(0, Settings::get().maxCachedWorldScanCount)), 1);
        for (const BlockPos& p : cached) {
            // Java prune: a cached location in a loaded chunk must still match,
            // including properties; unloaded locations remain candidates to visit.
            auto chunk = world.chunk(p.x >> 4, p.z >> 4);
            if (!chunk || filter.matches(BlockRegistry::get().props(chunk->get(p.x & 15, p.y, p.z & 15))))
                found.push_back(p);
        }
    }
    const bool untracked = std::ranges::any_of(filter.names(), [](const std::string& n) { return !isTrackedBlock(n); });
    // Java MineProcess.searchWorld: always scan untracked blocks, otherwise extend
    // the cache only when requested and fewer than max locations were found.
    if (!cache || untracked || (Settings::get().extendCacheOnThreshold && found.size() < max)) {
        auto loaded = scanLoadedChunks(world, filter, center, max, maxChunkRadius);
        found.insert(found.end(), loaded.begin(), loaded.end());
    }
    std::ranges::sort(found, [](const BlockPos& a, const BlockPos& b) {
        return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z);
    });
    found.erase(std::unique(found.begin(), found.end()), found.end());
    std::ranges::sort(found, [&](const BlockPos& a, const BlockPos& b) { return a.distSq(center) < b.distSq(center); });
    if (found.size() > max) found.resize(max);
    return found;
}

}  // namespace baritone
