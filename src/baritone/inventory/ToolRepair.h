#pragma once
#include "baritone/api/PlayerContext.h"
#include <algorithm>
#include <stdexcept>

namespace baritone {
inline bool sameRepairItem(const HotbarStack& a, const HotbarStack& b, bool damage = true) {
    return !a.empty() && !b.empty() && a.name == b.name && a.maxDamage == b.maxDamage &&
        (!damage || a.damage == b.damage) && a.enchantments == b.enchantments && a.customName == b.customName;
}
inline bool repairMaterial(const HotbarStack& tool, const HotbarStack& material) {
    if (material.empty()) return false;
    if (tool.name == material.name) return material.count == 1 && material.maxDamage == tool.maxDamage;
    const auto name = std::string_view(tool.name);
    if (name.starts_with("minecraft:wooden_") || name == "minecraft:shield")
        return material.name.ends_with("_planks") && material.name.starts_with("minecraft:");
    if (name.starts_with("minecraft:stone_")) return material.name == "minecraft:cobblestone";
    if (name.starts_with("minecraft:iron_") || name.starts_with("minecraft:chainmail_")) return material.name == "minecraft:iron_ingot";
    if (name.starts_with("minecraft:golden_")) return material.name == "minecraft:gold_ingot";
    if (name.starts_with("minecraft:diamond_")) return material.name == "minecraft:diamond";
    if (name.starts_with("minecraft:netherite_")) return material.name == "minecraft:netherite_ingot";
    if (name.starts_with("minecraft:leather_")) return material.name == "minecraft:leather";
    if (name == "minecraft:elytra") return material.name == "minecraft:phantom_membrane";
    if (name == "minecraft:turtle_helmet") return material.name == "minecraft:turtle_scute";
    return false;
}
inline void validateRepairInputs(std::string_view method, const HotbarStack& tool, const HotbarStack& material, int count, int maxLevels) {
    if ((method != "anvil" && method != "grindstone") || tool.empty() || tool.count != 1 || tool.maxDamage <= 0 ||
        tool.damage <= 0 || tool.damage >= tool.maxDamage || count < 1 || count > 64 || count > material.count || maxLevels < 0 || maxLevels > 39)
        throw std::invalid_argument("Repair needs one damaged durable item, material quantity 1..64 and level-cost ceiling 0..39");
    if (method == "anvil" && !repairMaterial(tool, material))
        throw std::invalid_argument("Choose the same item or a supported base repair material, not an enchantment book");
    if (method == "grindstone" && (material.name != tool.name || material.count != 1 || count != 1 || material.maxDamage != tool.maxDamage ||
        !tool.enchantments.empty() || !material.enchantments.empty() || !tool.customName.empty() || !material.customName.empty()))
        throw std::invalid_argument("Automatic grindstone repair only accepts two matching unnamed, unenchanted items; enchanted/named items are retained");
}
inline void validateRepairOutput(const RepairPreview& preview, std::string_view method, const HotbarStack& original, int supplied, int maxLevels) {
    if (!preview.supported || preview.method != method || preview.output.count != 1 || !sameRepairItem(preview.output, original, false) ||
        preview.output.damage < 0 || preview.output.damage >= original.damage)
        throw std::invalid_argument("The native preview is not the same item with improved durability and unchanged name/enchantments");
    if (preview.levels < 0 || preview.levels > maxLevels || preview.materialCost < 1 || preview.materialCost > supplied)
        throw std::invalid_argument("The native repair cost exceeds the requested material or XP-level ceiling");
}
inline void validateMendingItem(const HotbarStack& tool, int percent, int timeoutSeconds) {
    if (percent < 1 || percent > 100 || timeoutSeconds < 30 || timeoutSeconds > 1800)
        throw std::invalid_argument("Mending target must be 1..100 percent, timeout 30..1800 seconds");
    if (tool.empty() || tool.count != 1 || tool.maxDamage <= 0 || tool.damage <= 0 || tool.damage >= tool.maxDamage ||
        !std::ranges::any_of(tool.enchantments, [](const auto& enchant) { return enchant.first == 26 && enchant.second > 0; }))
        throw std::invalid_argument("Choose one damaged durable item with Mending; XP must come from the real world source");
    if (int64_t(tool.maxDamage - tool.damage) * 100 >= int64_t(tool.maxDamage) * percent)
        throw std::invalid_argument("The requested durability has already been reached");
}
} // namespace baritone
