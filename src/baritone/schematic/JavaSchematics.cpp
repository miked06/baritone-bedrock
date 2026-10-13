// Java Edition schematic formats (Baritone reads them through its schematic/format
// package): Sponge .schem and Litematica .litematic. Block states are converted to
// Bedrock block names, and the orientation properties the builder can reproduce
// (axis, stairs, slabs, facing of furnaces/chests/pumpkins) to Bedrock states.
#include <algorithm>
#include <bit>
#include <cctype>
#include <climits>
#include <map>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

#include "baritone/schematic/Inflate.h"
#include "baritone/schematic/NbtReader.h"
#include "baritone/schematic/Schematic.h"

namespace baritone {

SchematicPtr parseLegacySchematic(const nbt::Tag& root);  // LegacySchematic.cpp

namespace {

[[noreturn]] void invalid(const std::string& what) { throw std::runtime_error(what); }

int64_t intOf(const nbt::Tag* t, const char* what) {
    if (!t || !t->integer()) invalid(std::string("champ manquant : ") + what);
    return *t->integer();
}

}  // namespace

static DesiredBlock javaToBedrockName(std::string name, const std::map<std::string, std::string>& properties);

DesiredBlock javaToBedrock(std::string_view state) {
    std::map<std::string, std::string> props;
    std::size_t open = state.find('[');
    if (open != std::string_view::npos) {
        std::string_view list = state.substr(open + 1, state.find(']', open) - open - 1);
        while (!list.empty()) {
            std::size_t comma = list.find(',');
            std::string_view kv = list.substr(0, comma);
            std::size_t eq = kv.find('=');
            if (eq != std::string_view::npos) props.emplace(std::string(kv.substr(0, eq)), std::string(kv.substr(eq + 1)));
            if (comma == std::string_view::npos) break;
            list.remove_prefix(comma + 1);
        }
    }
    return javaToBedrock(std::string(state.substr(0, open)), props);
}

namespace {

// Java properties -> the Bedrock orientation states of Orientation.h
void convertOrientation(DesiredBlock& b, const std::map<std::string, std::string>& p) {
    auto get = [&](const char* k) -> std::string {
        auto it = p.find(k);
        return it == p.end() ? std::string() : it->second;
    };
    const std::string& n = b.name;
    if (n.ends_with("_stairs")) {
        static const std::map<std::string, std::string> weirdo = {{"east", "0"}, {"west", "1"}, {"south", "2"}, {"north", "3"}};
        if (auto it = weirdo.find(get("facing")); it != weirdo.end()) b.states.emplace_back("weirdo_direction", it->second);
        if (std::string half = get("half"); !half.empty()) b.states.emplace_back("upside_down_bit", half == "top" ? "1" : "0");
        return;
    }
    if (n.ends_with("_slab")) {
        std::string type = get("type");
        if (type == "double") b.name = n.substr(0, n.size() - 5) + "_double_slab";
        else if (type == "top" || type == "bottom") b.states.emplace_back("minecraft:vertical_half", type);
        return;
    }
    if (std::string axis = get("axis"); !axis.empty() && n != "minecraft:portal") {
        b.states.emplace_back("pillar_axis", axis);
        return;
    }
    static const char* cardinal[] = {"minecraft:furnace", "minecraft:lit_furnace", "minecraft:blast_furnace",
                                     "minecraft:lit_blast_furnace", "minecraft:smoker", "minecraft:lit_smoker",
                                     "minecraft:chest", "minecraft:trapped_chest", "minecraft:ender_chest",
                                     "minecraft:carved_pumpkin", "minecraft:lit_pumpkin"};
    if (std::ranges::find(cardinal, n) != std::end(cardinal)) {
        std::string facing = get("facing");
        if (facing == "north" || facing == "south" || facing == "east" || facing == "west") {
            b.states.emplace_back("minecraft:cardinal_direction", facing);
        }
    }
}

}  // namespace

DesiredBlock javaToBedrock(std::string name, const std::map<std::string, std::string>& properties) {
    DesiredBlock b = javaToBedrockName(std::move(name), properties);
    if (b.kind == DesiredBlock::Kind::Block) convertOrientation(b, properties);
    return b;
}

static DesiredBlock javaToBedrockName(std::string name, const std::map<std::string, std::string>& properties) {
    if (name.find(':') == std::string::npos) name = "minecraft:" + name;
    if (name == "minecraft:furnace" && properties.contains("lit") && properties.at("lit") == "true") name = "minecraft:lit_furnace";
    if (name == "minecraft:air" || name == "minecraft:cave_air" || name == "minecraft:void_air") return DesiredBlock::air();
    if (name == "minecraft:structure_void") return DesiredBlock::any();

    static const std::unordered_map<std::string_view, std::string_view> renamed = {
        {"minecraft:cobweb", "minecraft:web"},
        {"minecraft:spawner", "minecraft:mob_spawner"},
        {"minecraft:nether_portal", "minecraft:portal"},
        {"minecraft:terracotta", "minecraft:hardened_clay"},
        {"minecraft:dirt_path", "minecraft:grass_path"},
        {"minecraft:snow", "minecraft:snow_layer"},
        {"minecraft:snow_block", "minecraft:snow"},
        {"minecraft:slime_block", "minecraft:slime"},
        {"minecraft:note_block", "minecraft:noteblock"},
        {"minecraft:sugar_cane", "minecraft:reeds"},
        {"minecraft:lily_pad", "minecraft:waterlily"},
        {"minecraft:end_stone_bricks", "minecraft:end_bricks"},
        {"minecraft:magma_block", "minecraft:magma"},
        {"minecraft:nether_quartz_ore", "minecraft:quartz_ore"},
        {"minecraft:jack_o_lantern", "minecraft:lit_pumpkin"},
        {"minecraft:red_nether_bricks", "minecraft:red_nether_brick"},
        {"minecraft:nether_bricks", "minecraft:nether_brick"},
        {"minecraft:bricks", "minecraft:brick_block"},
        {"minecraft:melon", "minecraft:melon_block"},
        {"minecraft:powered_rail", "minecraft:golden_rail"},
        {"minecraft:dead_bush", "minecraft:deadbush"},
        {"minecraft:grass", "minecraft:short_grass"},
        {"minecraft:tall_seagrass", "minecraft:seagrass"},
        {"minecraft:wall_torch", "minecraft:torch"},
        {"minecraft:soul_wall_torch", "minecraft:soul_torch"},
        {"minecraft:redstone_wall_torch", "minecraft:redstone_torch"},
        {"minecraft:oak_pressure_plate", "minecraft:wooden_pressure_plate"},
        {"minecraft:oak_button", "minecraft:wooden_button"},
        {"minecraft:oak_door", "minecraft:wooden_door"},
        {"minecraft:oak_trapdoor", "minecraft:trapdoor"},
        {"minecraft:oak_fence_gate", "minecraft:fence_gate"},
        {"minecraft:oak_sign", "minecraft:standing_sign"},
        {"minecraft:oak_wall_sign", "minecraft:wall_sign"},
        {"minecraft:stone_slab", "minecraft:normal_stone_slab"},
        {"minecraft:piston_head", "minecraft:piston_arm_collision"},
        {"minecraft:moving_piston", "minecraft:moving_block"},
        {"minecraft:comparator", "minecraft:unpowered_comparator"},
        {"minecraft:repeater", "minecraft:unpowered_repeater"},
        {"minecraft:water_cauldron", "minecraft:cauldron"},
        {"minecraft:lava_cauldron", "minecraft:cauldron"},
        {"minecraft:powder_snow_cauldron", "minecraft:cauldron"},
        {"minecraft:shulker_box", "minecraft:undyed_shulker_box"},
        {"minecraft:attached_melon_stem", "minecraft:melon_stem"},
        {"minecraft:attached_pumpkin_stem", "minecraft:pumpkin_stem"},
        {"minecraft:light", "minecraft:light_block"},
        {"minecraft:rooted_dirt", "minecraft:dirt_with_roots"},
        {"minecraft:frogspawn", "minecraft:frog_spawn"},
    };
    if (auto it = renamed.find(name); it != renamed.end()) return DesiredBlock::block(std::string(it->second));
    // families
    if (name.ends_with("_bed")) return DesiredBlock::block("minecraft:bed");
    if (name.ends_with("_wall_banner")) return DesiredBlock::block("minecraft:wall_banner");
    if (name.ends_with("_banner")) return DesiredBlock::block("minecraft:standing_banner");
    if (name.ends_with("_wall_head") || name.ends_with("_wall_skull")) {
        std::string base = name.substr(0, name.rfind("_wall_")) + (name.ends_with("_head") ? "_head" : "_skull");
        return DesiredBlock::block(base);
    }
    return DesiredBlock::block(name);
}

namespace {

// Sponge schematic, versions 1-3 (WorldEdit / FAWE .schem).
SchematicPtr parseSponge(const nbt::Tag& root) {
    const nbt::Tag* s = root.get("Schematic");  // v3 wraps everything in "Schematic"
    if (!s || !s->isCompound()) s = &root;
    int w = static_cast<int>(intOf(s->get("Width"), "Width")) & 0xFFFF;
    int h = static_cast<int>(intOf(s->get("Height"), "Height")) & 0xFFFF;
    int l = static_cast<int>(intOf(s->get("Length"), "Length")) & 0xFFFF;
    if (w == 0 || h == 0 || l == 0) invalid("structure vide");

    const nbt::Tag* palette = s->get("Palette");
    const nbt::Tag* data = s->get("BlockData");
    if (const nbt::Tag* blocks = s->get("Blocks"); blocks && blocks->isCompound()) {  // v3
        palette = blocks->get("Palette");
        data = blocks->get("Data");
    }
    if (!palette || !palette->compound() || !data || !data->intArray()) invalid("Palette ou BlockData manquant");

    std::vector<DesiredBlock> pal;
    for (const auto& [state, idx] : *palette->compound()) {
        auto i = idx.integer();
        if (!i || *i < 0 || *i > 1'000'000) invalid("index de palette invalide");
        if (pal.size() <= static_cast<std::size_t>(*i)) pal.resize(*i + 1, DesiredBlock::any());
        pal[*i] = javaToBedrock(state);
    }

    // varint encoded, index (y * length + z) * width + x
    const auto& bytes = *data->intArray();
    std::size_t total = static_cast<std::size_t>(w) * h * l;
    std::vector<int> indices(total, -1);
    std::size_t pos = 0;
    for (std::size_t i = 0; i < total; ++i) {
        int value = 0, shift = 0;
        for (;;) {
            if (pos >= bytes.size()) invalid("BlockData trop court");
            int b = static_cast<int>(bytes[pos++]) & 0xFF;
            value |= (b & 0x7F) << shift;
            if (!(b & 0x80)) break;
            shift += 7;
            if (shift > 28) invalid("varint invalide");
        }
        int x = static_cast<int>(i % w), z = static_cast<int>((i / w) % l), y = static_cast<int>(i / (static_cast<std::size_t>(w) * l));
        indices[(static_cast<std::size_t>(x) * h + y) * l + z] = value;
    }
    return std::make_shared<StaticSchematic>(BlockPos{w, h, l}, std::move(pal), std::move(indices));
}

// Litematica: one or more regions, block states packed in a long array.
SchematicPtr parseLitematic(const nbt::Tag& root) {
    const nbt::Tag* regions = root.get("Regions");
    if (!regions || !regions->compound() || regions->compound()->empty()) invalid("aucune region");

    struct Region {
        BlockPos min, size;
        std::vector<DesiredBlock> palette;
        const std::vector<int64_t>* states;
    };
    std::vector<Region> list;
    BlockPos lo{INT32_MAX, INT32_MAX, INT32_MAX}, hi{INT32_MIN, INT32_MIN, INT32_MIN};
    for (const auto& [name, r] : *regions->compound()) {
        const nbt::Tag* pos = r.get("Position");
        const nbt::Tag* size = r.get("Size");
        const nbt::Tag* pal = r.get("BlockStatePalette");
        const nbt::Tag* states = r.get("BlockStates");
        if (!pos || !size || !pal || !pal->list() || !states || !states->intArray()) invalid("region " + name + " incomplete");
        BlockPos p{static_cast<int>(intOf(pos->get("x"), "x")), static_cast<int>(intOf(pos->get("y"), "y")),
                   static_cast<int>(intOf(pos->get("z"), "z"))};
        BlockPos s{static_cast<int>(intOf(size->get("x"), "x")), static_cast<int>(intOf(size->get("y"), "y")),
                   static_cast<int>(intOf(size->get("z"), "z"))};
        // a negative size extends towards negative coordinates from the position
        BlockPos min{s.x < 0 ? p.x + s.x + 1 : p.x, s.y < 0 ? p.y + s.y + 1 : p.y, s.z < 0 ? p.z + s.z + 1 : p.z};
        BlockPos abs{std::abs(s.x), std::abs(s.y), std::abs(s.z)};
        if (abs.x == 0 || abs.y == 0 || abs.z == 0) continue;
        Region reg{min, abs, {}, states->intArray()};
        for (const nbt::Tag& entry : *pal->list()) {
            const nbt::Tag* n = entry.get("Name");
            std::map<std::string, std::string> props;
            if (const nbt::Tag* pr = entry.get("Properties"); pr && pr->compound()) {
                for (const auto& [k, v] : *pr->compound())
                    if (v.string()) props.emplace(k, *v.string());
            }
            reg.palette.push_back(n && n->string() ? javaToBedrock(*n->string(), props) : DesiredBlock::any());
        }
        lo = {std::min(lo.x, min.x), std::min(lo.y, min.y), std::min(lo.z, min.z)};
        hi = {std::max(hi.x, min.x + abs.x), std::max(hi.y, min.y + abs.y), std::max(hi.z, min.z + abs.z)};
        list.push_back(std::move(reg));
    }
    if (list.empty()) invalid("regions vides");
    BlockPos total{hi.x - lo.x, hi.y - lo.y, hi.z - lo.z};
    if (static_cast<int64_t>(total.x) * total.y * total.z > 64 * 1024 * 1024) invalid("structure trop grande");

    std::vector<DesiredBlock> palette;  // merged
    std::vector<int> indices(static_cast<std::size_t>(total.x) * total.y * total.z, -1);
    for (const Region& r : list) {
        int base = static_cast<int>(palette.size());
        palette.insert(palette.end(), r.palette.begin(), r.palette.end());
        int bits = std::max(2, static_cast<int>(std::bit_width(static_cast<unsigned>(std::max<std::size_t>(r.palette.size(), 1) - 1))));
        uint64_t mask = (uint64_t{1} << bits) - 1;
        const auto& longs = *r.states;
        std::size_t volume = static_cast<std::size_t>(r.size.x) * r.size.y * r.size.z;
        if ((volume * bits + 63) / 64 > longs.size()) invalid("BlockStates trop court");
        for (std::size_t i = 0; i < volume; ++i) {
            // LitematicaBitArray.getAt: entries may span two longs
            uint64_t start = i * bits;
            std::size_t a = start >> 6, b = ((i + 1) * bits - 1) >> 6;
            int off = static_cast<int>(start & 63);
            uint64_t v = static_cast<uint64_t>(longs[a]) >> off;
            if (a != b) v |= static_cast<uint64_t>(longs[b]) << (64 - off);
            int idx = static_cast<int>(v & mask);
            if (idx >= static_cast<int>(r.palette.size())) continue;
            // index (y * sizeZ + z) * sizeX + x
            int x = static_cast<int>(i % r.size.x), z = static_cast<int>((i / r.size.x) % r.size.z),
                y = static_cast<int>(i / (static_cast<std::size_t>(r.size.x) * r.size.z));
            int wx = r.min.x - lo.x + x, wy = r.min.y - lo.y + y, wz = r.min.z - lo.z + z;
            indices[(static_cast<std::size_t>(wx) * total.y + wy) * total.z + wz] = base + idx;
        }
    }
    return std::make_shared<StaticSchematic>(total, std::move(palette), std::move(indices));
}

}  // namespace

SchematicPtr loadSchematic(const std::string& path) {
    std::string lower = path;
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.ends_with(".mcstructure")) return loadMcStructure(path);
    std::vector<uint8_t> data = decompressIfNeeded(nbt::readFile(path));
    nbt::Tag root = nbt::parse(data, /*littleEndian=*/false);
    if (lower.ends_with(".litematic")) return parseLitematic(root);
    if (lower.ends_with(".schem")) return parseSponge(root);
    if (lower.ends_with(".schematic")) {
        if (root.get("Palette") || root.get("Schematic")) return parseSponge(root);  // some tools use this extension
        return parseLegacySchematic(root);                                          // MCEdit, ids before 1.13
    }
    invalid("extension inconnue (.mcstructure, .schem, .litematic)");
}

}  // namespace baritone
