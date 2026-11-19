// Java Edition schematic formats (Baritone reads them through its schematic/format
// package): Sponge .schem and Litematica .litematic. Block states are converted to
// Bedrock block names, and the orientation properties the builder can reproduce
// (axis, stairs, slabs, facing of furnaces/chests/pumpkins) to Bedrock states.
#include <algorithm>
#include <bit>
#include <cctype>
#include <climits>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

#include "baritone/schematic/Inflate.h"
#include "baritone/schematic/NbtReader.h"
#include "baritone/schematic/Orientation.h"
#include "baritone/schematic/Schematic.h"

namespace baritone {

SchematicPtr parseLegacySchematic(const nbt::Tag& root);  // LegacySchematic.cpp

namespace {

[[noreturn]] void invalid(const std::string& what) { throw std::runtime_error(what); }

int64_t intOf(const nbt::Tag* t, const char* what) {
    if (!t || !t->integer()) invalid(std::string("missing field: ") + what);
    return *t->integer();
}

}  // namespace

static DesiredBlock javaToBedrockName(std::string name, const std::map<std::string, std::string>& properties);

namespace {
using JavaProperties = std::map<std::string, std::string>;

std::pair<std::string, JavaProperties> parseJavaState(std::string_view state) {
    std::map<std::string, std::string> props;
    std::size_t open = state.find('[');
    if (open != std::string_view::npos) {
        if (state.find(']', open) != state.size() - 1) invalid("Malformed Java block-state brackets");
        std::string_view list = state.substr(open + 1, state.size() - open - 2);
        while (!list.empty()) {
            std::size_t comma = list.find(',');
            std::string_view kv = list.substr(0, comma);
            std::size_t eq = kv.find('=');
            if (eq == kv.npos || eq == 0 || eq + 1 == kv.size() ||
                !props.emplace(kv.substr(0, eq), kv.substr(eq + 1)).second) invalid("Malformed or duplicate Java block-state property");
            if (comma == std::string_view::npos) break;
            list.remove_prefix(comma + 1);
        }
    }
    auto name = state.substr(0, open);
    if (name.empty()) invalid("Empty Java block name");
    return {std::string(name), std::move(props)};
}

// Java properties -> the Bedrock orientation states of Orientation.h
void convertOrientation(DesiredBlock& b, std::string_view sourceName, JavaProperties& p) {
    auto take = [&](const char* k) -> std::string {
        auto property = p.extract(k);
        return property.empty() ? std::string() : std::move(property.mapped());
    };
    const std::string& n = b.name;
    if (isTallPlant(n) || n.ends_with("_door")) {
        const auto half = take("half");
        if (half == "upper" || half == "lower") b.states.emplace_back("upper_block_bit", half == "upper" ? "1" : "0");
        else if (!half.empty()) p.emplace("half", half);
        if (n.ends_with("_door")) {
            const auto facing = take("facing");
            if (facing == "north" || facing == "south" || facing == "east" || facing == "west")
                b.states.emplace_back("minecraft:cardinal_direction", facing);
            else if (!facing.empty()) p.emplace("facing", facing);
        }
        return;
    }
    if (n == "minecraft:bed") {
        const auto part = take("part");
        if (part == "head" || part == "foot") b.states.emplace_back("head_piece_bit", part == "head" ? "1" : "0");
        else if (!part.empty()) p.emplace("part", part);
        const auto facing = take("facing");
        static const std::map<std::string, std::string> directions{{"south", "0"}, {"west", "1"}, {"north", "2"}, {"east", "3"}};
        if (auto value = directions.find(facing); value != directions.end()) b.states.emplace_back("direction", value->second);
        else if (!facing.empty()) p.emplace("facing", facing);
        return;
    }
    if (n == "minecraft:torch" || n == "minecraft:soul_torch" || n == "minecraft:redstone_torch") {
        if (sourceName.ends_with("wall_torch")) {
            // Java describes where the torch points, while Bedrock stores the
            // side of the target that touches the supporting block.
            const auto facing = take("facing");
            if (facing == "east") b.states.emplace_back("torch_facing_direction", "west");
            else if (facing == "west") b.states.emplace_back("torch_facing_direction", "east");
            else if (facing == "north") b.states.emplace_back("torch_facing_direction", "south");
            else if (facing == "south") b.states.emplace_back("torch_facing_direction", "north");
            else p.emplace("facing", facing.empty() ? "(unspecified)" : facing);
        } else {
            b.states.emplace_back("torch_facing_direction", "top");
        }
        return;
    }
    if (n.ends_with("_stairs")) {
        static const std::map<std::string, std::string> weirdo = {{"east", "0"}, {"west", "1"}, {"south", "2"}, {"north", "3"}};
        if (auto it = weirdo.find(take("facing")); it != weirdo.end()) b.states.emplace_back("weirdo_direction", it->second);
        if (std::string half = take("half"); !half.empty()) b.states.emplace_back("upside_down_bit", half == "top" ? "1" : "0");
        return;
    }
    if (n.ends_with("_slab")) {
        std::string type = take("type");
        if (type == "double") b.name = n.substr(0, n.size() - 5) + "_double_slab";
        else if (type == "top" || type == "bottom") b.states.emplace_back("minecraft:vertical_half", type);
        return;
    }
    if (n != "minecraft:portal" && p.contains("axis")) {
        b.states.emplace_back("pillar_axis", take("axis"));
        return;
    }
    static const char* cardinal[] = {"minecraft:furnace", "minecraft:lit_furnace", "minecraft:blast_furnace",
                                     "minecraft:lit_blast_furnace", "minecraft:smoker", "minecraft:lit_smoker",
                                     "minecraft:chest", "minecraft:trapped_chest", "minecraft:ender_chest",
                                     "minecraft:carved_pumpkin", "minecraft:lit_pumpkin"};
    if (std::ranges::find(cardinal, n) != std::end(cardinal)) {
        std::string facing = take("facing");
        if (facing == "north" || facing == "south" || facing == "east" || facing == "west") {
            b.states.emplace_back("minecraft:cardinal_direction", facing);
        }
    }
}

struct JavaImportNotes {
    std::set<std::string> omitted;
    bool truncated = false;

    void add(const std::string& name, const std::string& key, const std::string& value) {
        const auto entry = name.substr(0, 80) + " " + key.substr(0, 40) + "=" + value.substr(0, 40);
        if (omitted.contains(entry)) return;
        if (omitted.size() < 12) omitted.insert(entry);
        else truncated = true;
    }

    void append(std::vector<std::string>& warnings) const {
        for (const auto& entry : omitted) warnings.push_back("No direct Java placement mapping for " + entry);
        if (truncated) warnings.push_back("Additional Java properties without a direct placement mapping were omitted from this report");
    }
};

DesiredBlock convertJava(const std::string& name, JavaProperties properties, JavaImportNotes* notes = nullptr) {
    DesiredBlock block = javaToBedrockName(name, properties);
    if (block.kind != DesiredBlock::Kind::Block) return block;
    convertOrientation(block, name, properties);
    if (notes) {
        if (block.name == "minecraft:bed") notes->add(name, "color", "check the Bedrock bed manually");
        for (const auto& [key, value] : properties) {
            if ((key == "waterlogged" || key == "lit") && value == "false") continue;
            notes->add(name, key, value);
        }
    }
    return block;
}
}  // namespace

DesiredBlock javaToBedrock(std::string_view state) {
    auto [name, properties] = parseJavaState(state);
    return convertJava(name, std::move(properties));
}

DesiredBlock javaToBedrock(std::string name, const std::map<std::string, std::string>& properties) {
    return convertJava(name, properties);
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
    const std::size_t total = schematicVolume({w, h, l});

    const nbt::Tag* palette = s->get("Palette");
    const nbt::Tag* data = s->get("BlockData");
    if (const nbt::Tag* blocks = s->get("Blocks"); blocks && blocks->isCompound()) {  // v3
        palette = blocks->get("Palette");
        data = blocks->get("Data");
    }
    if (!palette || !palette->compound() || !data || !data->intArray()) invalid("Missing Palette or BlockData");

    std::vector<DesiredBlock> pal;
    std::unordered_set<int64_t> defined;
    JavaImportNotes notes;
    for (const auto& [state, idx] : *palette->compound()) {
        auto i = idx.integer();
        if (!i || *i < 0 || *i > 1'000'000) invalid("invalid palette index");
        if (!defined.insert(*i).second) invalid("duplicate palette index");
        if (pal.size() <= static_cast<std::size_t>(*i)) pal.resize(*i + 1, DesiredBlock::any());
        auto [name, properties] = parseJavaState(state);
        pal[*i] = convertJava(name, std::move(properties), &notes);
    }

    // varint encoded, index (y * length + z) * width + x
    const auto& bytes = *data->intArray();
    std::vector<int> indices(total, -1);
    std::size_t pos = 0;
    for (std::size_t i = 0; i < total; ++i) {
        uint32_t value = 0;
        int shift = 0;
        for (;;) {
            if (pos >= bytes.size()) invalid("BlockData too short");
            int b = static_cast<int>(bytes[pos++]) & 0xFF;
            if (shift == 28 && (b & 0xF0)) invalid("invalid varint");
            value |= static_cast<uint32_t>(b & 0x7F) << shift;
            if (!(b & 0x80)) break;
            shift += 7;
            if (shift > 28) invalid("invalid varint");
        }
        if (!defined.contains(value)) invalid("BlockData refers to an undefined palette index");
        int x = static_cast<int>(i % w), z = static_cast<int>((i / w) % l), y = static_cast<int>(i / (static_cast<std::size_t>(w) * l));
        indices[(static_cast<std::size_t>(x) * h + y) * l + z] = value;
    }
    if (pos != bytes.size()) invalid("BlockData has trailing block indices");
    std::vector<std::string> warnings;
    const nbt::Tag* blockEntities = s->get("BlockEntities");
    if (const auto* blocks = s->get("Blocks"); blocks && blocks->isCompound()) blockEntities = blocks->get("BlockEntities");
    if (blockEntities && blockEntities->list() && !blockEntities->list()->empty())
        warnings.emplace_back("Block-entity data (containers, signs, etc.) is not restored");
    if (const auto* entities = s->get("Entities"); entities && entities->list() && !entities->list()->empty())
        warnings.emplace_back("Entities are not built by the survival builder");
    warnings.emplace_back("Java conversion reproduces block names and supported orientation states; other properties can differ on Bedrock");
    notes.append(warnings);
    return std::make_shared<StaticSchematic>(BlockPos{w, h, l}, std::move(pal), std::move(indices), std::move(warnings));
}

// Litematica: one or more regions, block states packed in a long array.
SchematicPtr parseLitematic(const nbt::Tag& root) {
    const nbt::Tag* regions = root.get("Regions");
    if (!regions || !regions->compound() || regions->compound()->empty()) invalid("no regions");

    struct Region {
        BlockPos min, size;
        std::vector<DesiredBlock> palette;
        const std::vector<int64_t>* states;
    };
    std::vector<Region> list;
    std::vector<std::string> warnings;
    JavaImportNotes notes;
    BlockPos lo{INT32_MAX, INT32_MAX, INT32_MAX}, hi{INT32_MIN, INT32_MIN, INT32_MIN};
    for (const auto& [name, r] : *regions->compound()) {
        for (const char* key : {"TileEntities", "Entities"})
            if (const auto* values = r.get(key); values && values->list() && !values->list()->empty()) {
                const std::string warning = "Entities and block-entity data (containers, signs, etc.) are not restored";
                if (std::ranges::find(warnings, warning) == warnings.end()) warnings.push_back(warning);
            }
        const nbt::Tag* pos = r.get("Position");
        const nbt::Tag* size = r.get("Size");
        const nbt::Tag* pal = r.get("BlockStatePalette");
        const nbt::Tag* states = r.get("BlockStates");
        if (!pos || !size || !pal || !pal->list() || !states || !states->intArray()) invalid("region " + name + " is incomplete");
        auto coordinate = [&](const nbt::Tag* tag, const char* axis) {
            auto value = intOf(tag, axis);
            if (value <= INT32_MIN || value > INT32_MAX) invalid("region coordinate out of range");
            return static_cast<int>(value);
        };
        BlockPos p{coordinate(pos->get("x"), "x"), coordinate(pos->get("y"), "y"), coordinate(pos->get("z"), "z")};
        BlockPos s{coordinate(size->get("x"), "x"), coordinate(size->get("y"), "y"), coordinate(size->get("z"), "z")};
        schematicVolume({std::abs(s.x), std::abs(s.y), std::abs(s.z)});
        for (auto [at, side] : {std::pair{p.x, s.x}, {p.y, s.y}, {p.z, s.z}}) {
            const int64_t low = side < 0 ? static_cast<int64_t>(at) + side + 1 : at;
            if (low < INT32_MIN || low + std::abs(side) > INT32_MAX) invalid("region bounds overflow");
        }
        // a negative size extends towards negative coordinates from the position
        auto minimum = [](int at, int side) { return static_cast<int>(side < 0 ? static_cast<int64_t>(at) + side + 1 : at); };
        BlockPos min{minimum(p.x, s.x), minimum(p.y, s.y), minimum(p.z, s.z)};
        BlockPos abs{std::abs(s.x), std::abs(s.y), std::abs(s.z)};
        Region reg{min, abs, {}, states->intArray()};
        for (const nbt::Tag& entry : *pal->list()) {
            const nbt::Tag* n = entry.get("Name");
            std::map<std::string, std::string> props;
            if (const nbt::Tag* pr = entry.get("Properties"); pr && pr->compound()) {
                for (const auto& [k, v] : *pr->compound())
                    if (v.string()) props.emplace(k, *v.string());
            }
            if (!n || !n->string()) invalid("Missing block palette Name");
            reg.palette.push_back(convertJava(*n->string(), std::move(props), &notes));
        }
        if (reg.palette.empty()) invalid("Empty block palette");
        lo = {std::min(lo.x, min.x), std::min(lo.y, min.y), std::min(lo.z, min.z)};
        hi = {std::max(hi.x, min.x + abs.x), std::max(hi.y, min.y + abs.y), std::max(hi.z, min.z + abs.z)};
        list.push_back(std::move(reg));
    }
    if (list.empty()) invalid("empty regions");
    if (static_cast<int64_t>(hi.x) - lo.x > INT32_MAX || static_cast<int64_t>(hi.y) - lo.y > INT32_MAX || static_cast<int64_t>(hi.z) - lo.z > INT32_MAX)
        invalid("region extent overflow");
    BlockPos total{hi.x - lo.x, hi.y - lo.y, hi.z - lo.z};
    schematicVolume(total);

    std::vector<DesiredBlock> palette;  // merged
    std::vector<int> indices(static_cast<std::size_t>(total.x) * total.y * total.z, -1);
    for (const Region& r : list) {
        int base = static_cast<int>(palette.size());
        palette.insert(palette.end(), r.palette.begin(), r.palette.end());
        int bits = std::max(2, static_cast<int>(std::bit_width(static_cast<unsigned>(std::max<std::size_t>(r.palette.size(), 1) - 1))));
        uint64_t mask = (uint64_t{1} << bits) - 1;
        const auto& longs = *r.states;
        std::size_t volume = static_cast<std::size_t>(r.size.x) * r.size.y * r.size.z;
        if ((volume * bits + 63) / 64 > longs.size()) invalid("BlockStates too short");
        for (std::size_t i = 0; i < volume; ++i) {
            // LitematicaBitArray.getAt: entries may span two longs
            uint64_t start = i * bits;
            std::size_t a = start >> 6, b = ((i + 1) * bits - 1) >> 6;
            int off = static_cast<int>(start & 63);
            uint64_t v = static_cast<uint64_t>(longs[a]) >> off;
            if (a != b) v |= static_cast<uint64_t>(longs[b]) << (64 - off);
            int idx = static_cast<int>(v & mask);
            if (idx >= static_cast<int>(r.palette.size())) invalid("BlockStates refers to an undefined palette index");
            // index (y * sizeZ + z) * sizeX + x
            int x = static_cast<int>(i % r.size.x), z = static_cast<int>((i / r.size.x) % r.size.z),
                y = static_cast<int>(i / (static_cast<std::size_t>(r.size.x) * r.size.z));
            int wx = r.min.x - lo.x + x, wy = r.min.y - lo.y + y, wz = r.min.z - lo.z + z;
            indices[(static_cast<std::size_t>(wx) * total.y + wy) * total.z + wz] = base + idx;
        }
    }
    warnings.emplace_back("Java conversion reproduces block names and supported orientation states; other properties can differ on Bedrock");
    notes.append(warnings);
    return std::make_shared<StaticSchematic>(total, std::move(palette), std::move(indices), std::move(warnings));
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
    invalid("unknown extension (.mcstructure, .schem, .litematic, .schematic)");
}

}  // namespace baritone
