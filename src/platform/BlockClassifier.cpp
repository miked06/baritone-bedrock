#include "platform/BlockClassifier.h"

#include <array>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace platform {

using baritone::BlockProps;
using baritone::BlockType;
using baritone::Fluid;
using baritone::Tool;

namespace {

bool ends(std::string_view s, std::string_view suffix) { return s.ends_with(suffix); }
bool starts(std::string_view s, std::string_view prefix) { return s.starts_with(prefix); }
bool has(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }

template <std::size_t N>
bool oneOf(std::string_view s, const std::array<std::string_view, N>& list) {
    for (auto v : list)
        if (s == v) return true;
    return false;
}

// "light_block_0".."light_block_15"
bool light_prefix(std::string_view n) { return starts(n, "light_block"); }

// Blocks without collision a player walks through (Java isPathfindable(LAND) == true).
// Note: "minecraft:grass" is the old name of grass_block, a full cube.
bool isNoCollision(std::string_view n) {
    static const std::unordered_set<std::string_view> exact{
        "air", "short_grass", "tall_grass", "fern", "large_fern", "deadbush", "seagrass", "kelp",
        "torch", "soul_torch", "redstone_torch", "unlit_redstone_torch", "copper_torch", "rail", "golden_rail",
        "detector_rail", "activator_rail", "redstone_wire", "lever", "tripwire_hook", "trip_wire", "vine",
        "weeping_vines", "twisting_vines", "cave_vines", "cave_vines_body_with_berries",
        "cave_vines_head_with_berries", "wheat", "carrots", "potatoes", "beetroot", "melon_stem", "pumpkin_stem",
        "nether_wart", "red_mushroom", "brown_mushroom", "crimson_fungus", "warped_fungus", "crimson_roots",
        "warped_roots", "nether_sprouts", "reeds", "sugar_cane", "glow_lichen", "sculk_vein", "hanging_roots",
        "spore_blossom", "pink_petals", "wildflowers", "leaf_litter", "structure_void", "light_block", "frame",
        "glow_frame", "sweet_berry_bush", "fire", "soul_fire", "end_portal", "portal", "end_gateway",
        "bubble_column", "powder_snow", "torchflower", "torchflower_crop", "pitcher_crop", "small_dripleaf_block",
        "pale_hanging_moss", "firefly_bush", "short_dry_grass", "tall_dry_grass", "bush", "cactus_flower",
        "resin_clump", "unpowered_repeater", "powered_repeater", "unpowered_comparator", "powered_comparator",
        "dandelion", "poppy", "blue_orchid", "allium", "azure_bluet", "red_tulip", "orange_tulip", "white_tulip",
        "pink_tulip", "oxeye_daisy", "cornflower", "lily_of_the_valley", "wither_rose", "sunflower", "lilac",
        "rose_bush", "peony", "closed_eyeblossom", "open_eyeblossom", "red_flower", "yellow_flower", "double_plant",
        "tallgrass", "bamboo_sapling", "mangrove_propagule", "chorus_flower", "web", "lit_redstone_lamp_dummy",
    };
    if (exact.contains(n)) return true;
    if (light_prefix(n)) return true;
    return ends(n, "_sapling") || ends(n, "_button") || ends(n, "_pressure_plate") || ends(n, "_sign") ||
           ends(n, "_banner") || ends(n, "coral_fan") || ends(n, "_coral") || ends(n, "_torch") ||
           has(n, "coral_wall_fan") || ends(n, "_flower") || ends(n, "_vines");
}

struct Shape {
    float height;
    bool solidTop;  // the top covers the whole block (can stand on it)
};

// Blocks with a collision box that is not a full cube.
std::optional<Shape> partialShape(std::string_view n) {
    static const std::unordered_map<std::string_view, Shape> exact{
        {"soul_sand", {0.875f, true}},      {"mud", {0.875f, true}},
        {"farmland", {0.9375f, true}},      {"grass_path", {0.9375f, true}},
        {"dirt_path", {0.9375f, true}},     {"honey_block", {0.9375f, false}},
        {"chest", {0.875f, true}},          {"trapped_chest", {0.875f, true}},
        {"ender_chest", {0.875f, true}},    {"enchanting_table", {0.75f, true}},
        {"cactus", {0.9375f, false}},       {"cake", {0.5f, false}},
        {"lectern", {0.9375f, true}},       {"stonecutter_block", {0.5625f, true}},
        {"daylight_detector", {0.375f, true}}, {"daylight_detector_inverted", {0.375f, true}},
        {"bed", {0.5625f, true}},           {"cauldron", {1.f, false}},
        {"lava_cauldron", {1.f, false}},    {"hopper", {1.f, false}},
        {"anvil", {1.f, false}},            {"chipped_anvil", {1.f, false}},
        {"damaged_anvil", {1.f, false}},    {"brewing_stand", {0.875f, false}},
        {"lantern", {0.5625f, false}},      {"soul_lantern", {0.5625f, false}},
        {"campfire", {0.4375f, true}},      {"soul_campfire", {0.4375f, true}},
        {"flower_pot", {0.375f, false}},    {"skull", {0.5f, false}},
        {"grindstone", {1.f, false}},       {"bell", {1.f, false}},
        {"composter", {1.f, false}},        {"end_rod", {1.f, false}},
        {"lightning_rod", {1.f, false}},    {"chain", {1.f, false}},
        {"scaffolding", {1.f, false}},      {"bamboo", {1.f, false}},
        {"pointed_dripstone", {1.f, false}}, {"amethyst_cluster", {0.4375f, false}},
        {"conduit", {0.6875f, false}},      {"dragon_egg", {1.f, false}},
        {"turtle_egg", {0.4375f, false}},   {"sea_pickle", {0.375f, false}},
        {"azalea", {1.f, true}},            {"flowering_azalea", {1.f, true}},
        {"decorated_pot", {1.f, false}},    {"sniffer_egg", {1.f, false}},
        {"sculk_sensor", {0.5f, true}},     {"calibrated_sculk_sensor", {0.5f, true}},
        {"sculk_shrieker", {0.5f, true}},   {"cocoa", {0.75f, false}},
        {"big_dripleaf", {0.9375f, false}}, {"waterlily", {0.09375f, false}},
        {"end_portal_frame", {0.8125f, true}}, {"piston_arm_collision", {1.f, false}},
        {"sticky_piston_arm_collision", {1.f, false}}, {"iron_bars", {1.f, false}},
        {"glass_pane", {1.f, false}},       {"hard_glass_pane", {1.f, false}},
        {"heavy_core", {0.5f, false}},      {"vault", {1.f, true}},
        {"pale_moss_carpet", {0.0625f, true}},
    };
    if (auto it = exact.find(n); it != exact.end()) return it->second;
    if (ends(n, "_carpet") || n == "carpet") return Shape{0.0625f, true};
    if (ends(n, "_candle") || n == "candle" || has(n, "candle_cake")) return Shape{0.4375f, false};
    if (ends(n, "_bed")) return Shape{0.5625f, true};
    if (ends(n, "_pane") || ends(n, "_bars")) return Shape{1.f, false};
    if (has(n, "amethyst_bud")) return Shape{0.3125f, false};
    if (ends(n, "_head") || ends(n, "_skull")) return Shape{0.5f, false};
    if (ends(n, "_lantern")) return Shape{0.5625f, false};
    if (ends(n, "_chain")) return Shape{1.f, false};
    if (ends(n, "_shulker_box") || n == "undyed_shulker_box") return Shape{1.f, false};
    return std::nullopt;
}

// ---- hardness / tools ----------------------------------------------------------

struct Mining {
    float hardness;
    Tool tool;
    bool requiresTool;
};

Mining mining(std::string_view n) {
    static const std::unordered_map<std::string_view, Mining> exact{
        {"bedrock", {-1, Tool::None, false}}, {"barrier", {-1, Tool::None, false}},
        {"end_portal_frame", {-1, Tool::None, false}}, {"command_block", {-1, Tool::None, false}},
        {"repeating_command_block", {-1, Tool::None, false}}, {"chain_command_block", {-1, Tool::None, false}},
        {"structure_block", {-1, Tool::None, false}}, {"jigsaw", {-1, Tool::None, false}},
        {"border_block", {-1, Tool::None, false}}, {"allow", {-1, Tool::None, false}}, {"deny", {-1, Tool::None, false}},
        {"reinforced_deepslate", {55, Tool::Pickaxe, true}},
        {"obsidian", {50, Tool::Pickaxe, true}}, {"crying_obsidian", {50, Tool::Pickaxe, true}},
        {"netherite_block", {50, Tool::Pickaxe, true}}, {"respawn_anchor", {50, Tool::Pickaxe, true}},
        {"ancient_debris", {30, Tool::Pickaxe, true}}, {"ender_chest", {22.5f, Tool::Pickaxe, true}},
        {"stone", {1.5f, Tool::Pickaxe, true}}, {"granite", {1.5f, Tool::Pickaxe, true}},
        {"diorite", {1.5f, Tool::Pickaxe, true}}, {"andesite", {1.5f, Tool::Pickaxe, true}},
        {"tuff", {1.5f, Tool::Pickaxe, true}}, {"calcite", {0.75f, Tool::Pickaxe, true}},
        {"deepslate", {3, Tool::Pickaxe, true}}, {"cobbled_deepslate", {3.5f, Tool::Pickaxe, true}},
        {"cobblestone", {2, Tool::Pickaxe, true}}, {"mossy_cobblestone", {2, Tool::Pickaxe, true}},
        {"netherrack", {0.4f, Tool::Pickaxe, true}}, {"end_stone", {3, Tool::Pickaxe, true}},
        {"basalt", {1.25f, Tool::Pickaxe, true}}, {"blackstone", {1.5f, Tool::Pickaxe, true}},
        {"magma", {0.5f, Tool::Pickaxe, true}}, {"glowstone", {0.3f, Tool::None, false}},
        {"sea_lantern", {0.3f, Tool::None, false}}, {"ice", {0.5f, Tool::Pickaxe, false}},
        {"packed_ice", {0.5f, Tool::Pickaxe, false}}, {"blue_ice", {2.8f, Tool::Pickaxe, false}},
        {"snow", {0.2f, Tool::Shovel, true}}, {"snow_layer", {0.1f, Tool::Shovel, true}},
        {"clay", {0.6f, Tool::Shovel, false}}, {"dirt", {0.5f, Tool::Shovel, false}},
        {"coarse_dirt", {0.5f, Tool::Shovel, false}}, {"rooted_dirt", {0.5f, Tool::Shovel, false}},
        {"grass_block", {0.6f, Tool::Shovel, false}}, {"grass", {0.6f, Tool::Shovel, false}},
        {"mycelium", {0.6f, Tool::Shovel, false}}, {"podzol", {0.5f, Tool::Shovel, false}},
        {"mud", {0.5f, Tool::Shovel, false}}, {"packed_mud", {1, Tool::Pickaxe, false}},
        {"sand", {0.5f, Tool::Shovel, false}}, {"red_sand", {0.5f, Tool::Shovel, false}},
        {"suspicious_sand", {0.25f, Tool::Shovel, false}}, {"suspicious_gravel", {0.25f, Tool::Shovel, false}},
        {"gravel", {0.6f, Tool::Shovel, false}}, {"soul_sand", {0.5f, Tool::Shovel, false}},
        {"soul_soil", {0.5f, Tool::Shovel, false}}, {"farmland", {0.6f, Tool::Shovel, false}},
        {"grass_path", {0.65f, Tool::Shovel, false}}, {"dirt_path", {0.65f, Tool::Shovel, false}},
        {"glass", {0.3f, Tool::None, false}}, {"glass_pane", {0.3f, Tool::None, false}},
        {"bookshelf", {1.5f, Tool::Axe, false}}, {"crafting_table", {2.5f, Tool::Axe, false}},
        {"furnace", {3.5f, Tool::Pickaxe, true}}, {"lit_furnace", {3.5f, Tool::Pickaxe, true}},
        {"chest", {2.5f, Tool::Axe, false}}, {"trapped_chest", {2.5f, Tool::Axe, false}},
        {"ladder", {0.4f, Tool::Axe, false}}, {"cactus", {0.4f, Tool::None, false}},
        {"pumpkin", {1, Tool::Axe, false}}, {"melon_block", {1, Tool::Axe, false}},
        {"hay_block", {0.5f, Tool::Hoe, false}}, {"sponge", {0.6f, Tool::Hoe, false}},
        {"web", {4, Tool::Sword, true}}, {"bed", {0.2f, Tool::None, false}}, {"sculk", {0.2f, Tool::Hoe, false}},
        {"moss_block", {0.1f, Tool::Hoe, false}}, {"slime", {0, Tool::None, false}},
        {"honey_block", {0, Tool::None, false}}, {"bone_block", {2, Tool::Pickaxe, true}},
        {"mob_spawner", {5, Tool::Pickaxe, true}}, {"anvil", {5, Tool::Pickaxe, true}},
        {"cauldron", {2, Tool::Pickaxe, true}}, {"hopper", {3, Tool::Pickaxe, true}},
        {"iron_bars", {5, Tool::Pickaxe, true}}, {"lantern", {3.5f, Tool::Pickaxe, true}},
        {"observer", {3, Tool::Pickaxe, true}}, {"piston", {1.5f, Tool::Pickaxe, false}},
        {"sticky_piston", {1.5f, Tool::Pickaxe, false}}, {"dispenser", {3.5f, Tool::Pickaxe, true}},
        {"dropper", {3.5f, Tool::Pickaxe, true}}, {"noteblock", {0.8f, Tool::Axe, false}},
        {"jukebox", {2, Tool::Axe, false}}, {"enchanting_table", {5, Tool::Pickaxe, true}},
        {"beacon", {3, Tool::None, false}}, {"tnt", {0, Tool::None, false}},
        {"amethyst_block", {1.5f, Tool::Pickaxe, true}}, {"dripstone_block", {1.5f, Tool::Pickaxe, true}},
        {"iron_block", {5, Tool::Pickaxe, true}}, {"gold_block", {3, Tool::Pickaxe, true}},
        {"diamond_block", {5, Tool::Pickaxe, true}}, {"emerald_block", {5, Tool::Pickaxe, true}},
        {"coal_block", {5, Tool::Pickaxe, true}}, {"redstone_block", {5, Tool::Pickaxe, true}},
        {"lapis_block", {3, Tool::Pickaxe, true}}, {"quartz_block", {0.8f, Tool::Pickaxe, true}},
        {"sandstone", {0.8f, Tool::Pickaxe, true}}, {"red_sandstone", {0.8f, Tool::Pickaxe, true}},
        {"brick_block", {2, Tool::Pickaxe, true}}, {"nether_brick", {2, Tool::Pickaxe, true}},
        {"stonebrick", {1.5f, Tool::Pickaxe, true}}, {"stone_bricks", {1.5f, Tool::Pickaxe, true}},
        {"purpur_block", {1.5f, Tool::Pickaxe, true}}, {"prismarine", {1.5f, Tool::Pickaxe, true}},
        {"hardened_clay", {1.25f, Tool::Pickaxe, true}}, {"terracotta", {1.25f, Tool::Pickaxe, true}},
        {"shroomlight", {1, Tool::Hoe, false}}, {"nether_wart_block", {1, Tool::Hoe, false}},
        {"warped_wart_block", {1, Tool::Hoe, false}}, {"target", {0.5f, Tool::Hoe, false}},
        {"dried_kelp_block", {0.5f, Tool::Hoe, false}}, {"scaffolding", {0, Tool::None, false}},
    };
    if (auto it = exact.find(n); it != exact.end()) return it->second;

    if (starts(n, "deepslate_") && ends(n, "_ore")) return {4.5f, Tool::Pickaxe, true};
    if (ends(n, "_ore")) return {3, Tool::Pickaxe, true};
    if (starts(n, "raw_") && ends(n, "_block")) return {5, Tool::Pickaxe, true};
    if (has(n, "copper")) return {3, Tool::Pickaxe, true};
    if (ends(n, "_concrete_powder") || n == "concrete_powder") return {0.5f, Tool::Shovel, false};
    if (ends(n, "_concrete") || n == "concrete") return {1.8f, Tool::Pickaxe, true};
    if (has(n, "glazed_terracotta")) return {1.4f, Tool::Pickaxe, true};
    if (ends(n, "_terracotta")) return {1.25f, Tool::Pickaxe, true};
    if (ends(n, "_wool") || n == "wool") return {0.8f, Tool::Shears, false};
    if (ends(n, "_leaves") || has(n, "leaves")) return {0.2f, Tool::Hoe, false};
    if (has(n, "stained_glass") || ends(n, "_glass")) return {0.3f, Tool::None, false};
    if (ends(n, "_planks") || ends(n, "_log") || ends(n, "_wood") || ends(n, "_stem") || ends(n, "_hyphae") ||
        has(n, "bamboo_block") || ends(n, "_fence") || ends(n, "_fence_gate") || n == "fence_gate") {
        return {2, Tool::Axe, false};
    }
    if (n == "iron_door" || n == "iron_trapdoor") return {5, Tool::Pickaxe, true};
    if (ends(n, "_door") || ends(n, "_trapdoor") || n == "trapdoor" || n == "wooden_door") return {3, Tool::Axe, false};
    if (ends(n, "_sign") || ends(n, "_banner") || ends(n, "_hanging_sign")) return {1, Tool::Axe, false};
    if (ends(n, "_shulker_box")) return {2, Tool::Pickaxe, false};
    if (ends(n, "_wall")) return {2, Tool::Pickaxe, true};
    if (has(n, "brick")) return {2, Tool::Pickaxe, true};
    if (has(n, "deepslate")) return {3.5f, Tool::Pickaxe, true};
    if (has(n, "blackstone")) return {1.5f, Tool::Pickaxe, true};
    if (has(n, "quartz")) return {0.8f, Tool::Pickaxe, true};
    if (has(n, "sandstone")) return {0.8f, Tool::Pickaxe, true};
    if (has(n, "prismarine") || has(n, "purpur")) return {1.5f, Tool::Pickaxe, true};
    if (has(n, "stone")) return {1.5f, Tool::Pickaxe, true};
    if (has(n, "rail")) return {0.7f, Tool::Pickaxe, false};
    if (has(n, "coral")) return {0, Tool::None, false};
    return {1.5f, Tool::Pickaxe, true};  // unknown solid: assume stone-like
}

// Slabs, stairs etc. take the hardness of their base material.
std::string_view baseMaterial(std::string_view n) {
    for (std::string_view suffix : {"_double_slab", "_slab", "_stairs", "_wall"}) {
        if (ends(n, suffix)) return n.substr(0, n.size() - suffix.size());
    }
    return n;
}

std::optional<std::string> stringState(const sdk::nbt::CompoundTag* states, std::string_view key) {
    return states ? states->string(key) : std::nullopt;
}

int64_t intState(const sdk::nbt::CompoundTag* states, std::string_view key, int64_t fallback = 0) {
    if (!states) return fallback;
    return states->integer(key).value_or(fallback);
}

}  // namespace

namespace {
BlockProps classifyByName(const std::string& fullName, const sdk::nbt::CompoundTag* states);
}

BlockProps classifyBlock(const std::string& fullName, const sdk::nbt::CompoundTag* states) {
    BlockProps p = classifyByName(fullName, states);
    if (states) {
        for (const auto& [key, value] : states->tags) {
            switch (value.type()) {
                case sdk::nbt::TagType::String: p.states.emplace_back(key, *static_cast<const std::string*>(value.payload())); break;
                case sdk::nbt::TagType::Byte:
                case sdk::nbt::TagType::Short:
                case sdk::nbt::TagType::Int:
                case sdk::nbt::TagType::Int64: p.states.emplace_back(key, std::to_string(*states->integer(key))); break;
                default: break;
            }
        }
    }
    return p;
}

namespace {

BlockProps classifyByName(const std::string& fullName, const sdk::nbt::CompoundTag* states) {
    BlockProps p;
    p.name = fullName;
    std::string_view n = fullName;
    if (starts(n, "minecraft:")) n.remove_prefix(10);

    // defaults: a full solid cube
    p.collides = true;
    p.fullCube = true;
    p.height = 1.f;
    p.pathfindable = false;

    Mining m = mining(baseMaterial(n));
    p.hardness = m.hardness;
    p.bestTool = m.tool;
    p.requiresTool = m.requiresTool;
    p.unbreakable = m.hardness < 0;

    auto noCollision = [&] {
        p.collides = false;
        p.fullCube = false;
        p.height = 0.f;
        p.pathfindable = true;
    };
    auto partial = [&](float height, bool solidTop) {
        p.fullCube = false;
        p.height = height;
        p.solidTop = solidTop;
    };

    // ---- fluids ----
    if (n == "water" || n == "flowing_water" || n == "lava" || n == "flowing_lava") {
        noCollision();
        p.pathfindable = false;
        bool water = n == "water" || n == "flowing_water";
        p.type = water ? BlockType::Water : BlockType::Lava;
        p.fluid = water ? Fluid::Water : Fluid::Lava;
        p.fluidLevel = static_cast<uint8_t>(intState(states, "liquid_depth", 0));
        p.replaceable = true;
        p.unbreakable = true;
        p.hardness = 100;
        return p;
    }

    if (n == "air" || n == "cave_air" || n == "void_air") {
        noCollision();
        p.type = BlockType::Air;
        p.replaceable = true;
        p.hardness = 0;
        return p;
    }

    // crops: growth stage for #farm
    if (n == "wheat" || n == "carrots" || n == "potatoes" || n == "beetroot") {
        p.growth = static_cast<uint8_t>(intState(states, "growth", 0));
        p.matureGrowth = 7;
    } else if (n == "nether_wart") {
        p.growth = static_cast<uint8_t>(intState(states, "age", 0));
        p.matureGrowth = 3;
    } else if (n == "cocoa") {
        p.growth = static_cast<uint8_t>(intState(states, "age", 0));
        p.matureGrowth = 2;
    }

    if (isNoCollision(n)) {
        noCollision();
        p.hardness = (n == "web" || n == "sweet_berry_bush") ? p.hardness : 0.f;
        if (n == "fire" || n == "soul_fire") p.type = BlockType::Fire;
        else if (n == "web") {
            p.type = BlockType::Cobweb;
            p.pathfindable = false;
            p.hardness = 4;
        } else if (n == "sweet_berry_bush") p.type = BlockType::SweetBerryBush;
        else if (n == "end_portal" || n == "portal" || n == "end_gateway") p.type = BlockType::EndPortal;
        else if (n == "bubble_column") {
            p.type = BlockType::BubbleColumn;
            p.fluid = Fluid::Water;
        } else if (n == "powder_snow") {
            p.type = BlockType::PowderSnow;
            p.hardness = 0.25f;
        } else if (n == "vine") p.type = BlockType::Vine;
        else if (n == "tall_grass" || n == "large_fern" || n == "double_plant" || n == "short_grass" ||
                 n == "tallgrass" || n == "fern" || n == "deadbush" || n == "seagrass") {
            p.type = BlockType::TallPlant;
            p.replaceable = true;
        }
        if (n == "seagrass" || n == "kelp") {
            p.fluid = Fluid::Water;  // always waterlogged
        }
        if (n == "glow_lichen" || n == "sculk_vein" || n == "light_block" || starts(n, "light_block") ||
            n == "structure_void" || n == "short_dry_grass" || n == "tall_dry_grass" || n == "leaf_litter") {
            p.replaceable = true;
        }
        return p;
    }

    // ---- special collision blocks ----
    if (n == "ladder") {
        p.type = BlockType::Ladder;
        p.fullCube = false;
        p.pathfindable = true;  // thin box against a wall
        p.height = 1.f;
        return p;
    }
    if (n == "snow_layer") {
        p.type = BlockType::SnowLayer;
        p.fullCube = false;
        p.snowLayers = static_cast<uint8_t>(intState(states, "height", 0) + 1);
        p.height = (p.snowLayers - 1) / 8.f;
        p.collides = p.snowLayers > 1;
        p.replaceable = p.snowLayers == 1;
        p.pathfindable = p.snowLayers < 5;
        return p;
    }
    if (ends(n, "_slab") || ends(n, "_double_slab") || n == "stone_block_slab" || starts(n, "stone_block_slab")) {
        p.type = BlockType::Slab;
        if (ends(n, "_double_slab") || starts(n, "double_stone_block_slab")) {
            p.doubleSlab = true;  // full cube
            return p;
        }
        auto half = stringState(states, "minecraft:vertical_half");
        bool top = half ? *half == "top" : intState(states, "top_slot_bit", 0) != 0;
        p.fullCube = false;
        p.topSlab = top;
        p.bottomSlab = !top;
        p.height = top ? 1.f : 0.5f;
        p.solidTop = top;
        return p;
    }
    if (ends(n, "_stairs")) {
        p.type = BlockType::Stairs;
        p.fullCube = false;
        p.topHalfStairs = intState(states, "upside_down_bit", 0) != 0;
        return p;
    }
    if (n == "iron_door") {
        p.type = BlockType::IronDoor;
        p.fullCube = false;
        p.open = intState(states, "open_bit", 0) != 0;
        return p;
    }
    if (ends(n, "_door") || n == "wooden_door") {
        p.type = BlockType::Door;
        p.fullCube = false;
        p.open = intState(states, "open_bit", 0) != 0;
        return p;
    }
    if (ends(n, "_trapdoor") || n == "trapdoor") {
        p.type = BlockType::Trapdoor;
        p.fullCube = false;
        p.open = intState(states, "open_bit", 0) != 0;
        bool top = intState(states, "upside_down_bit", 0) != 0;
        p.topTrapdoor = top;
        p.height = (p.open || top) ? 1.f : 0.1875f;
        return p;
    }
    if (ends(n, "_fence_gate") || n == "fence_gate") {
        p.type = BlockType::FenceGate;
        p.fullCube = false;
        p.open = intState(states, "open_bit", 0) != 0;
        p.height = 1.5f;
        return p;
    }
    if (ends(n, "_fence") || n == "fence") {
        p.type = BlockType::Fence;
        partial(1.5f, false);
        return p;
    }
    if (ends(n, "_wall") || n == "cobblestone_wall") {
        p.type = BlockType::Wall;
        partial(1.5f, false);
        return p;
    }
    if (n == "waterlily") {
        p.type = BlockType::LilyPad;
        partial(0.09375f, false);
        p.hardness = 0;
        return p;
    }

    if (auto shape = partialShape(n)) {
        partial(shape->height, shape->solidTop);
        if (n == "soul_sand") p.type = BlockType::SoulSand;
        else if (n == "farmland") p.type = BlockType::Farmland;
        else if (n == "grass_path" || n == "dirt_path") p.type = BlockType::DirtPath;
        else if (n == "honey_block") p.type = BlockType::Honey;
        else if (n == "cactus") p.type = BlockType::Cactus;
        else if (n == "chest" || n == "trapped_chest" || n == "ender_chest") p.type = BlockType::Chest;
        else if (n == "scaffolding") {
            p.type = BlockType::Scaffolding;
            p.pathfindable = true;
        } else if (n == "cauldron" || n == "lava_cauldron") p.type = BlockType::Cauldron;
        else if (ends(n, "_carpet") || n == "carpet" || n == "pale_moss_carpet") p.type = BlockType::Carpet;
        return p;
    }

    // ---- full cubes with a special meaning ----
    if (n == "magma") p.type = BlockType::Magma;
    else if (ends(n, "_leaves") || n == "leaves" || n == "leaves2") p.type = BlockType::Leaves;
    else if (n == "ice" || n == "frosted_ice") p.type = BlockType::Ice;
    else if (starts(n, "infested_") || n == "monster_egg") p.type = BlockType::Infested;
    else if (n == "glass" || ends(n, "_glass") || has(n, "stained_glass") || n == "tinted_glass") p.type = BlockType::Glass;

    // falling blocks
    if (n == "sand" || n == "red_sand" || n == "gravel" || ends(n, "concrete_powder") || n == "anvil" ||
        n == "suspicious_sand" || n == "suspicious_gravel" || n == "dragon_egg") {
        p.falling = true;
    }
    p.canPlaceAgainst = true;
    return p;
}

}  // namespace

}  // namespace platform
