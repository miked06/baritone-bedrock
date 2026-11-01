#include "baritone/schematic/SchematicSurvey.h"
#include <algorithm>
#include <chrono>
#include "baritone/schematic/Orientation.h"
#include "baritone/cache/WorldScanner.h"

namespace baritone {
void SchematicSurvey::start(SchematicPtr schematic, BlockPos origin, uint64_t revision) {
    if (!schematic) throw std::invalid_argument("No schematic loaded");
    auto size = schematic->size();
    const auto volume = schematicVolume(size);
    for (auto [p, n] : {std::pair{origin.x, size.x}, {origin.y, size.y}, {origin.z, size.z}})
        if (int64_t(p) + n - 1 > INT32_MAX) throw std::invalid_argument("Placement coordinates overflow");
    clear(); source_ = std::move(schematic); origin_ = origin; revision_ = revision; volume_ = volume;
}
void SchematicSurvey::need(const DesiredBlock& desired, BlockPos local) {
    auto state = [&](std::string_view key) -> std::string_view {
        for (const auto& [k, v] : desired.states) if (k == key) return v;
        return {};
    };
    if (desired.name.ends_with("_door") || desired.name == "minecraft:wooden_door") {
        if (state("upper_block_bit") == "1" || state("upper_block_bit") == "true") --local.y;
        if (!assemblies_.insert(local).second) return;
    }
    if (desired.name.ends_with("_bed") || desired.name == "minecraft:bed") {
        if (state("head_piece_bit") == "1" || state("head_piece_bit") == "true") {
            const auto direction = state("direction");
            if (direction == "0") --local.z;
            else if (direction == "1") ++local.x;
            else if (direction == "2") ++local.z;
            else if (direction == "3") --local.x;
            else result_.warnings.push_back("Unknown bed direction: material count may include both halves");
        }
        if (!assemblies_.insert(local).second) return;
    }
    std::string item = desired.name;
    uint64_t count = 1;
    if (item == "minecraft:standing_sign" || item == "minecraft:wall_sign") item = "minecraft:oak_sign";
    if (item == "minecraft:wall_torch") item = "minecraft:torch";
    if (item == "minecraft:lit_furnace") item = "minecraft:furnace";
    if (item == "minecraft:lit_redstone_lamp") item = "minecraft:redstone_lamp";
    if (auto pos = item.find("double_"); pos != std::string::npos && item.find("slab") != std::string::npos) {
        item.erase(pos, 7); count = 2;
    }
    // Legacy Bedrock slab palettes encode item variants in states. Do not silently
    // claim an exact shopping list when the item mapping has not been resolved.
    if (item.find("stone_block_slab") != std::string::npos) {
        const std::string warning = "Legacy stone slab variants require checking their item type";
        if (std::ranges::find(result_.warnings, warning) == result_.warnings.end()) result_.warnings.push_back(warning);
    }
    result_.items[item] += count;
}
bool SchematicSurvey::step(const WorldSnapshot& world, std::size_t budget) {
    if (!active()) return ready();
    auto size = source_->size();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
    const auto end = std::min(volume_, cursor_ + std::min(budget, std::size_t{4096}));
    const auto begin = cursor_;
    while (cursor_ < end) {
        if (cursor_ > begin && (cursor_ & 63) == 0 && std::chrono::steady_clock::now() >= deadline) break;
        auto i = cursor_++;
        int z = int(i % size.z), y = int((i / size.z) % size.y), x = int(i / (std::size_t(size.y) * size.z));
        BlockPos p = origin_.offset(x, y, z);
        auto chunk = world.chunk(p.x >> 4, p.z >> 4);
        bool loaded = chunk && p.y >= world.minY() && p.y < world.maxY();
        const auto& current = BlockRegistry::get().props(loaded ? chunk->get(p.x & 15, p.y, p.z & 15) : kAir);
        const auto desired = source_->desired(x, y, z, current);
        if (desired.kind == DesiredBlock::Kind::Any) { ++result_.ignored; continue; }
        SchematicWorkspace::Difference difference;
        if (!loaded) { ++result_.unknown; difference = SchematicWorkspace::Difference::Unloaded; }
        else if (desired.kind == DesiredBlock::Kind::Air) {
            if (current.type == BlockType::Air) { ++result_.matching; continue; }
            ++result_.extra; difference = SchematicWorkspace::Difference::Remove;
        } else if (blockMatches(desired, current, false)) { ++result_.matching; continue; }
        else {
            if (current.type == BlockType::Air || current.replaceable) { ++result_.missing; difference = SchematicWorkspace::Difference::Missing; }
            else { ++result_.wrong; difference = SchematicWorkspace::Difference::Wrong; }
            need(desired, {x, y, z});
        }
        if (result_.problems.size() < 512) result_.problems.push_back({p, difference});
    }
    return ready();
}
std::vector<MaterialRequirement> planMaterials(const std::map<std::string, uint64_t>& requirements,
    const PlayerContext& player, const std::vector<std::string>& substitutions) {
    std::map<std::string, uint64_t> available;
    for (int slot = 0; slot < 36; ++slot) {
        auto stack = player.hotbar(slot);
        if (!stack.empty() && stack.count > 0) available[stack.name] += stack.count;
    }
    std::vector<MaterialRequirement> result;
    for (const auto& [item, required] : requirements) {
        MaterialRequirement row{item, required};
        const auto n = std::min(required, available[item]);
        available[item] -= n; row.available = n; row.missing = required - n;
        if (n) row.allocated[item] = n;
        result.push_back(std::move(row));
    }
    for (auto& row : result) for (const auto& rule : substitutions) {
        auto separator = rule.find('>');
        if (separator == std::string::npos || BlockFilter::normalize(rule.substr(0, separator)) != row.item) continue;
        auto alternative = BlockFilter::normalize(rule.substr(separator + 1));
        auto n = std::min(row.missing, available[alternative]);
        available[alternative] -= n; row.available += n; row.missing -= n;
        if (n) row.allocated[alternative] += n;
    }
    return result;
}
}
