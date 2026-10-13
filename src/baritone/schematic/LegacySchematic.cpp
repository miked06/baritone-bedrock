// MCEdit / old WorldEdit .schematic (Minecraft before 1.13): numeric block ids and 4-bit
// metadata. Converted to today's Bedrock names, with the orientation the builder can
// reproduce (stairs, logs and pillars, slabs, furnaces and chests).
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

#include "baritone/schematic/NbtReader.h"
#include "baritone/schematic/Schematic.h"

namespace baritone {

namespace {

constexpr std::array<const char*, 16> kColors = {"white", "orange", "magenta",   "light_blue", "yellow", "lime",
                                                 "pink",  "gray",   "light_gray", "cyan",       "purple", "blue",
                                                 "brown", "green",  "red",       "black"};
constexpr std::array<const char*, 6> kWoods = {"oak", "spruce", "birch", "jungle", "acacia", "dark_oak"};

std::string mc(const std::string& n) { return "minecraft:" + n; }

DesiredBlock block(const std::string& n) { return DesiredBlock::block(mc(n)); }

DesiredBlock withState(DesiredBlock b, const char* key, std::string value) {
    b.states.emplace_back(key, std::move(value));
    return b;
}

// data & 3: 0 east, 1 west, 2 south, 3 north (= Bedrock weirdo_direction), data & 4: upside down
DesiredBlock stairs(const std::string& n, int data) {
    DesiredBlock b = block(n);
    b.states.emplace_back("weirdo_direction", std::to_string(data & 3));
    b.states.emplace_back("upside_down_bit", (data & 4) ? "1" : "0");
    return b;
}

DesiredBlock slab(const std::string& n, int data) {
    return withState(block(n), "minecraft:vertical_half", (data & 8) ? "top" : "bottom");
}

// furnace / chest metadata: 2 north, 3 south, 4 west, 5 east
DesiredBlock facing(const std::string& n, int data) {
    static constexpr const char* dirs[] = {"north", "north", "north", "south", "west", "east"};
    return withState(block(n), "minecraft:cardinal_direction", dirs[std::min(data & 7, 5)]);
}

DesiredBlock pillar(const std::string& n, int axisBits) {  // 0 y, 1 x, 2 z
    static constexpr const char* axes[] = {"y", "x", "z", "y"};
    return withState(block(n), "pillar_axis", axes[axisBits & 3]);
}

DesiredBlock log(const char* wood, int data) {
    if ((data >> 2) == 3) return block(std::string(wood) + "_wood");  // bark on all sides
    return pillar(std::string(wood) + "_log", data >> 2);
}

}  // namespace

DesiredBlock legacyBlock(int id, int data) {
    data &= 15;
    const std::string color = kColors[data];
    switch (id) {
        case 0: return DesiredBlock::air();
        case 1: {
            static constexpr const char* v[] = {"stone", "granite", "polished_granite", "diorite", "polished_diorite",
                                                "andesite", "polished_andesite"};
            return block(v[data < 7 ? data : 0]);
        }
        case 2: return block("grass_block");
        case 3: return block(data == 1 ? "coarse_dirt" : data == 2 ? "podzol" : "dirt");
        case 4: return block("cobblestone");
        case 5: return block(std::string(kWoods[data % 6]) + "_planks");
        case 6: return block(std::string(kWoods[(data & 7) % 6]) + "_sapling");
        case 7: return block("bedrock");
        case 8: case 9: return block("water");
        case 10: case 11: return block("lava");
        case 12: return block(data == 1 ? "red_sand" : "sand");
        case 13: return block("gravel");
        case 14: return block("gold_ore");
        case 15: return block("iron_ore");
        case 16: return block("coal_ore");
        case 17: return log(kWoods[data & 3], data);
        case 18: return block(std::string(kWoods[data & 3]) + "_leaves");
        case 19: return block(data == 1 ? "wet_sponge" : "sponge");
        case 20: return block("glass");
        case 21: return block("lapis_ore");
        case 22: return block("lapis_block");
        case 23: return block("dispenser");
        case 24: return block(data == 1 ? "chiseled_sandstone" : data == 2 ? "cut_sandstone" : "sandstone");
        case 25: return block("noteblock");
        case 26: return block("bed");
        case 27: return block("golden_rail");
        case 28: return block("detector_rail");
        case 29: return block("sticky_piston");
        case 30: return block("web");
        case 31: return block(data == 2 ? "fern" : data == 0 ? "deadbush" : "short_grass");
        case 32: return block("deadbush");
        case 33: return block("piston");
        case 35: return block(color + "_wool");
        case 37: return block("dandelion");
        case 38: {
            static constexpr const char* v[] = {"poppy",       "blue_orchid", "allium",     "azure_bluet", "red_tulip",
                                                "orange_tulip", "white_tulip", "pink_tulip", "oxeye_daisy"};
            return block(v[data < 9 ? data : 0]);
        }
        case 39: return block("brown_mushroom");
        case 40: return block("red_mushroom");
        case 41: return block("gold_block");
        case 42: return block("iron_block");
        case 43: case 44: {
            static constexpr const char* v[] = {"smooth_stone", "sandstone",   "petrified_oak", "cobblestone",
                                                "brick",        "stone_brick", "nether_brick",  "quartz"};
            std::string base = v[data & 7];
            return id == 43 ? block(base + "_double_slab") : slab(base + "_slab", data);
        }
        case 45: return block("brick_block");
        case 46: return block("tnt");
        case 47: return block("bookshelf");
        case 48: return block("mossy_cobblestone");
        case 49: return block("obsidian");
        case 50: return block("torch");
        case 51: return block("fire");
        case 52: return block("mob_spawner");
        case 53: return stairs("oak_stairs", data);
        case 54: return facing("chest", data);
        case 55: return block("redstone_wire");
        case 56: return block("diamond_ore");
        case 57: return block("diamond_block");
        case 58: return block("crafting_table");
        case 59: return block("wheat");
        case 60: return block("farmland");
        case 61: return facing("furnace", data);
        case 62: return facing("lit_furnace", data);
        case 63: return block("standing_sign");
        case 64: return block("wooden_door");
        case 65: return block("ladder");
        case 66: return block("rail");
        case 67: return stairs("stone_stairs", data);
        case 68: return block("wall_sign");
        case 69: return block("lever");
        case 70: return block("stone_pressure_plate");
        case 71: return block("iron_door");
        case 72: return block("wooden_pressure_plate");
        case 73: return block("redstone_ore");
        case 74: return block("lit_redstone_ore");
        case 75: return block("unlit_redstone_torch");
        case 76: return block("redstone_torch");
        case 77: return block("stone_button");
        case 78: return block("snow_layer");
        case 79: return block("ice");
        case 80: return block("snow");
        case 81: return block("cactus");
        case 82: return block("clay");
        case 83: return block("reeds");
        case 84: return block("jukebox");
        case 85: return block("oak_fence");
        case 86: return block("carved_pumpkin");
        case 87: return block("netherrack");
        case 88: return block("soul_sand");
        case 89: return block("glowstone");
        case 90: return block("portal");
        case 91: return block("lit_pumpkin");
        case 92: return block("cake");
        case 93: return block("unpowered_repeater");
        case 94: return block("powered_repeater");
        case 95: return block(color + "_stained_glass");
        case 96: return block("trapdoor");
        case 97: {
            static constexpr const char* v[] = {"infested_stone", "infested_cobblestone", "infested_stone_bricks",
                                                "infested_mossy_stone_bricks", "infested_cracked_stone_bricks",
                                                "infested_chiseled_stone_bricks"};
            return block(v[data < 6 ? data : 0]);
        }
        case 98: {
            static constexpr const char* v[] = {"stone_bricks", "mossy_stone_bricks", "cracked_stone_bricks",
                                                "chiseled_stone_bricks"};
            return block(v[data & 3]);
        }
        case 99: return block("brown_mushroom_block");
        case 100: return block("red_mushroom_block");
        case 101: return block("iron_bars");
        case 102: return block("glass_pane");
        case 103: return block("melon_block");
        case 104: return block("pumpkin_stem");
        case 105: return block("melon_stem");
        case 106: return block("vine");
        case 107: return block("fence_gate");
        case 108: return stairs("brick_stairs", data);
        case 109: return stairs("stone_brick_stairs", data);
        case 110: return block("mycelium");
        case 111: return block("waterlily");
        case 112: return block("nether_brick");
        case 113: return block("nether_brick_fence");
        case 114: return stairs("nether_brick_stairs", data);
        case 115: return block("nether_wart");
        case 116: return block("enchanting_table");
        case 117: return block("brewing_stand");
        case 118: return block("cauldron");
        case 119: return block("end_portal");
        case 120: return block("end_portal_frame");
        case 121: return block("end_stone");
        case 122: return block("dragon_egg");
        case 123: return block("redstone_lamp");
        case 124: return block("lit_redstone_lamp");
        case 125: return block(std::string(kWoods[(data & 7) % 6]) + "_double_slab");
        case 126: return slab(std::string(kWoods[(data & 7) % 6]) + "_slab", data);
        case 127: return block("cocoa");
        case 128: return stairs("sandstone_stairs", data);
        case 129: return block("emerald_ore");
        case 130: return facing("ender_chest", data);
        case 131: return block("tripwire_hook");
        case 132: return block("trip_wire");
        case 133: return block("emerald_block");
        case 134: return stairs("spruce_stairs", data);
        case 135: return stairs("birch_stairs", data);
        case 136: return stairs("jungle_stairs", data);
        case 137: return block("command_block");
        case 138: return block("beacon");
        case 139: return block(data == 1 ? "mossy_cobblestone_wall" : "cobblestone_wall");
        case 140: return block("flower_pot");
        case 141: return block("carrots");
        case 142: return block("potatoes");
        case 143: return block("wooden_button");
        case 144: return block("skeleton_skull");
        case 145: return block(data >= 8 ? "damaged_anvil" : data >= 4 ? "chipped_anvil" : "anvil");
        case 146: return facing("trapped_chest", data);
        case 147: return block("light_weighted_pressure_plate");
        case 148: return block("heavy_weighted_pressure_plate");
        case 149: return block("unpowered_comparator");
        case 150: return block("powered_comparator");
        case 151: return block("daylight_detector");
        case 152: return block("redstone_block");
        case 153: return block("quartz_ore");
        case 154: return block("hopper");
        case 155:
            if (data == 1) return block("chiseled_quartz_block");
            if (data >= 2) return pillar("quartz_pillar", data == 3 ? 1 : data == 4 ? 2 : 0);
            return block("quartz_block");
        case 156: return stairs("quartz_stairs", data);
        case 157: return block("activator_rail");
        case 158: return block("dropper");
        case 159: return block(color + "_terracotta");
        case 160: return block(color + "_stained_glass_pane");
        case 161: return block(data % 2 ? "dark_oak_leaves" : "acacia_leaves");
        case 162: return log((data & 1) ? "dark_oak" : "acacia", data);
        case 163: return stairs("acacia_stairs", data);
        case 164: return stairs("dark_oak_stairs", data);
        case 165: return block("slime");
        case 166: return block("barrier");
        case 167: return block("iron_trapdoor");
        case 168: return block(data == 1 ? "prismarine_bricks" : data == 2 ? "dark_prismarine" : "prismarine");
        case 169: return block("sea_lantern");
        case 170: return pillar("hay_block", data >> 2);
        case 171: return block(color + "_carpet");
        case 172: return block("hardened_clay");
        case 173: return block("coal_block");
        case 174: return block("packed_ice");
        case 175: {
            static constexpr const char* v[] = {"sunflower", "lilac", "tall_grass", "large_fern", "rose_bush", "peony"};
            return block(v[(data & 7) % 6]);
        }
        case 176: return block("standing_banner");
        case 177: return block("wall_banner");
        case 178: return block("daylight_detector_inverted");
        case 179: return block(data == 1 ? "chiseled_red_sandstone" : data == 2 ? "cut_red_sandstone" : "red_sandstone");
        case 180: return stairs("red_sandstone_stairs", data);
        case 181: return block("red_sandstone_double_slab");
        case 182: return slab("red_sandstone_slab", data);
        case 183: return block("spruce_fence_gate");
        case 184: return block("birch_fence_gate");
        case 185: return block("jungle_fence_gate");
        case 186: return block("dark_oak_fence_gate");
        case 187: return block("acacia_fence_gate");
        case 188: return block("spruce_fence");
        case 189: return block("birch_fence");
        case 190: return block("jungle_fence");
        case 191: return block("dark_oak_fence");
        case 192: return block("acacia_fence");
        case 193: return block("spruce_door");
        case 194: return block("birch_door");
        case 195: return block("jungle_door");
        case 196: return block("acacia_door");
        case 197: return block("dark_oak_door");
        case 198: return block("end_rod");
        case 199: return block("chorus_plant");
        case 200: return block("chorus_flower");
        case 201: return block("purpur_block");
        case 202: return pillar("purpur_pillar", data >> 2);
        case 203: return stairs("purpur_stairs", data);
        case 204: return block("purpur_double_slab");
        case 205: return slab("purpur_slab", data);
        case 206: return block("end_bricks");
        case 207: return block("beetroot");
        case 208: return block("grass_path");
        case 209: return block("end_gateway");
        case 210: return block("repeating_command_block");
        case 211: return block("chain_command_block");
        case 212: return block("frosted_ice");
        case 213: return block("magma");
        case 214: return block("nether_wart_block");
        case 215: return block("red_nether_brick");
        case 216: return pillar("bone_block", data >> 2);
        case 217: return DesiredBlock::any();  // structure_void
        case 218: return block("observer");
        case 251: return block(color + "_concrete");
        case 252: return block(color + "_concrete_powder");
        case 255: return block("structure_block");
        default:
            if (id >= 219 && id <= 234) return block(std::string(kColors[id - 219]) + "_shulker_box");
            if (id >= 235 && id <= 250) {
                std::string c = kColors[id - 235];
                return block((c == "light_gray" ? std::string("silver") : c) + "_glazed_terracotta");
            }
            return DesiredBlock::any();  // unknown: leave what is there
    }
}

SchematicPtr parseLegacySchematic(const nbt::Tag& root) {
    auto num = [&](const char* key) {
        const nbt::Tag* t = root.get(key);
        if (!t || !t->integer()) throw std::runtime_error(std::string("champ manquant : ") + key);
        return static_cast<int>(*t->integer()) & 0xFFFF;
    };
    int w = num("Width"), h = num("Height"), l = num("Length");
    const nbt::Tag* blocks = root.get("Blocks");
    const nbt::Tag* data = root.get("Data");
    const nbt::Tag* add = root.get("AddBlocks");
    if (!blocks || !blocks->intArray() || !data || !data->intArray()) throw std::runtime_error("Blocks ou Data manquant");
    std::size_t total = static_cast<std::size_t>(w) * h * l;
    if (w == 0 || h == 0 || l == 0 || blocks->intArray()->size() < total || data->intArray()->size() < total) {
        throw std::runtime_error("taille incoherente");
    }
    const auto& ids = *blocks->intArray();
    const auto& meta = *data->intArray();
    const std::vector<int64_t>* high = add && add->intArray() ? add->intArray() : nullptr;

    // one palette entry per (id, data) seen
    std::vector<DesiredBlock> palette;
    std::vector<int> paletteOf(4096 * 16, -1);
    std::vector<int> indices(total, -1);
    for (std::size_t i = 0; i < total; ++i) {
        int id = static_cast<int>(ids[i]) & 0xFF;
        if (high && i / 2 < high->size()) {  // AddBlocks: 4 extra bits per block, two blocks per byte
            int nibble = static_cast<int>((*high)[i / 2]) & 0xFF;
            id |= ((i % 2 == 0) ? (nibble >> 4) : (nibble & 0xF)) << 8;
        }
        int d = static_cast<int>(meta[i]) & 0xF;
        int key = (id & 0xFFF) * 16 + d;
        if (paletteOf[key] < 0) {
            paletteOf[key] = static_cast<int>(palette.size());
            palette.push_back(legacyBlock(id, d));
        }
        // index (y * length + z) * width + x
        int x = static_cast<int>(i % w), z = static_cast<int>((i / w) % l), y = static_cast<int>(i / (static_cast<std::size_t>(w) * l));
        indices[(static_cast<std::size_t>(x) * h + y) * l + z] = paletteOf[key];
    }
    return std::make_shared<StaticSchematic>(BlockPos{w, h, l}, std::move(palette), std::move(indices));
}

}  // namespace baritone
