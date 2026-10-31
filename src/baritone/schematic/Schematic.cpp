#include "baritone/schematic/Schematic.h"

#include <cmath>
#include <cstdlib>

#include <stdexcept>
#include <string>

#include "baritone/schematic/NbtReader.h"

namespace baritone {

std::size_t schematicVolume(BlockPos size) {
    constexpr uint64_t limit = 64ull * 1024 * 1024;
    uint64_t volume = 1;
    for (int side : {size.x, size.y, size.z}) {
        if (side <= 0 || static_cast<uint64_t>(side) > limit / volume)
            throw std::invalid_argument("Invalid schematic dimensions (maximum 64 million blocks)");
        volume *= side;
    }
    return static_cast<std::size_t>(volume);
}

StaticSchematic::StaticSchematic(BlockPos size, std::vector<DesiredBlock> palette, std::vector<int> indices,
                                 std::vector<std::string> warnings)
    : size_(size), palette_(std::move(palette)), indices_(std::move(indices)), warnings_(std::move(warnings)) {
    if (indices_.size() != schematicVolume(size_)) throw std::invalid_argument("Inconsistent schematic index count");
    for (int index : indices_)
        if (index < -1 || (index >= 0 && static_cast<std::size_t>(index) >= palette_.size()))
            throw std::invalid_argument("Schematic refers to an undefined palette index");
}

DesiredBlock StaticSchematic::desired(int x, int y, int z, const BlockProps&) const {
    if (!inBounds(x, y, z)) return DesiredBlock::any();
    std::size_t i = (static_cast<std::size_t>(x) * size_.y + y) * size_.z + z;
    if (i >= indices_.size()) return DesiredBlock::any();
    int idx = indices_[i];
    if (idx < 0 || idx >= static_cast<int>(palette_.size())) return DesiredBlock::any();
    return palette_[idx];
}

SchematicMaterials StaticSchematic::materialCounts(int firstY, int lastY) const {
    SchematicMaterials result;
    if (lastY == -1) lastY = size_.y - 1;
    if (firstY < 0 || lastY < firstY || lastY >= size_.y) throw std::invalid_argument("Invalid schematic layer range");
    std::vector<uint64_t> counts(palette_.size());
    result.ignored = static_cast<uint64_t>(size_.x) * size_.z * (size_.y - (lastY - firstY + 1));
    for (int x = 0; x < size_.x; ++x) {
        const auto start = (static_cast<std::size_t>(x) * size_.y + firstY) * size_.z;
        const auto end = (static_cast<std::size_t>(x) * size_.y + lastY + 1) * size_.z;
        for (auto i = start; i < end; ++i) {
            const int index = indices_[i];
            if (index == -1) ++result.ignored;
            else ++counts[index];
        }
    }
    for (std::size_t i = 0; i < counts.size(); ++i) {
        if (!counts[i]) continue;
        const auto& block = palette_[i];
        if (block.kind == DesiredBlock::Kind::Block) result.blocks[block.name] += counts[i];
        else if (block.kind == DesiredBlock::Kind::Air) result.air += counts[i];
        else result.ignored += counts[i];
    }
    return result;
}

SchematicPtr loadMcStructure(const std::string& path) {
    nbt::Tag root = nbt::parse(nbt::readFile(path), /*littleEndian=*/true);
    const nbt::Tag* sizeTag = root.get("size");
    const nbt::Tag* structure = root.get("structure");
    if (!sizeTag || !sizeTag->list() || sizeTag->list()->size() != 3 || !structure) {
        throw std::runtime_error("Invalid .mcstructure file (missing size/structure)");
    }
    const auto& sl = *sizeTag->list();
    auto side = [](const nbt::Tag& tag) {
        auto value = tag.integer().value_or(0);
        if (value <= 0 || value > INT32_MAX) throw std::runtime_error("Invalid schematic size");
        return static_cast<int>(value);
    };
    BlockPos size{side(sl[0]), side(sl[1]), side(sl[2])};
    schematicVolume(size);

    // palette.default.block_palette: [{name, states, version}]
    const nbt::Tag* palette = structure->get("palette");
    const nbt::Tag* def = palette ? palette->get("default") : nullptr;
    const nbt::Tag* blockPalette = def ? def->get("block_palette") : nullptr;
    if (!blockPalette || !blockPalette->list()) throw std::runtime_error("Missing palette");
    std::vector<DesiredBlock> pal;
    for (const nbt::Tag& entry : *blockPalette->list()) {
        const nbt::Tag* name = entry.get("name");
        if (!name || !name->string() || name->string()->empty()) throw std::runtime_error("Missing block palette name");
        std::string n = *name->string();
        if (n == "minecraft:air") {
            pal.push_back(DesiredBlock::air());
        } else if (n == "minecraft:structure_void") {
            pal.push_back(DesiredBlock::any());
        } else {
            DesiredBlock b = DesiredBlock::block(n);
            if (const nbt::Tag* states = entry.get("states"); states && states->compound()) {
                for (const auto& [k, v] : *states->compound()) {
                    if (auto s = v.string()) b.states.emplace_back(k, *s);
                    else if (auto i = v.integer()) b.states.emplace_back(k, std::to_string(*i));
                }
            }
            pal.push_back(std::move(b));
        }
    }

    // block_indices: two layers (blocks, then waterlogging); we use the first one
    const nbt::Tag* indicesTag = structure->get("block_indices");
    if (!indicesTag || !indicesTag->list() || indicesTag->list()->empty()) throw std::runtime_error("Missing block_indices");
    const nbt::Tag& layer0 = (*indicesTag->list())[0];
    std::vector<int> indices;
    if (auto* arr = layer0.intArray()) {
        for (int64_t v : *arr) {
            if (v < -1 || v >= static_cast<int64_t>(pal.size())) throw std::runtime_error("Invalid block palette index");
            indices.push_back(static_cast<int>(v));
        }
    } else if (auto* list = layer0.list()) {
        for (const nbt::Tag& v : *list) {
            const auto value = v.integer();
            if (!value || *value < -1 || *value >= static_cast<int64_t>(pal.size())) throw std::runtime_error("Invalid block palette index");
            indices.push_back(static_cast<int>(*value));
        }
    }
    if (indices.size() != static_cast<std::size_t>(size.x) * size.y * size.z) {
        throw std::runtime_error("Inconsistent index count");
    }
    std::vector<std::string> warnings;
    if (const auto* entities = structure->get("entities"); entities && entities->list() && !entities->list()->empty())
        warnings.emplace_back("Entities are not built by the survival builder");
    if (const auto* data = def->get("block_position_data"); data && data->compound() && !data->compound()->empty())
        warnings.emplace_back("Block-entity data (containers, signs, etc.) is not restored");
    if (indicesTag->list()->size() > 1) {
        const auto& water = (*indicesTag->list())[1];
        bool filled = false;
        if (auto arr = water.intArray()) for (auto i : *arr) filled |= i != -1;
        if (auto list = water.list()) for (const auto& i : *list) filled |= i.integer().value_or(-1) != -1;
        if (filled) warnings.emplace_back("Secondary water/fluid layers are not placed");
    }
    return std::make_shared<StaticSchematic>(size, std::move(pal), std::move(indices), std::move(warnings));
}

MaskSchematic::Mask sphereMask(BlockPos size, bool filled) {
    double cx = size.x / 2.0, cy = size.y / 2.0, cz = size.z / 2.0;
    double rx = cx * cx, ry = cy * cy, rz = cz * cz;
    auto outside = [=](double dx, double dy, double dz) { return dx * dx / rx + dy * dy / ry + dz * dz / rz > 1; };
    return [=](int x, int y, int z) {
        double dx = std::abs((x + 0.5) - cx), dy = std::abs((y + 0.5) - cy), dz = std::abs((z + 0.5) - cz);
        if (outside(dx, dy, dz)) return false;
        return filled || outside(dx + 1, dy, dz) || outside(dx, dy + 1, dz) || outside(dx, dy, dz + 1);
    };
}

MaskSchematic::Mask cylinderMask(BlockPos size, bool filled, char axis) {
    // Java: A = y for an X cylinder else x, B = y for a Z cylinder else z
    auto getA = [axis](int x, int y) { return axis == 'x' ? y : x; };
    auto getB = [axis](int y, int z) { return axis == 'z' ? y : z; };
    double ca = getA(size.x, size.y) / 2.0, cb = getB(size.y, size.z) / 2.0;
    double ra = (ca - 1) * (ca - 1), rb = (cb - 1) * (cb - 1);
    auto outside = [=](double da, double db) { return da * da / ra + db * db / rb > 1; };
    return [=](int x, int y, int z) {
        double da = std::abs((getA(x, y) + 0.5) - ca), db = std::abs((getB(y, z) + 0.5) - cb);
        if (outside(da, db)) return false;
        return filled || outside(da + 1, db) || outside(da, db + 1);
    };
}

namespace {

// horizontal directions clockwise (seen from above): north, east, south, west
int clockwiseIndex(std::string_view name) {
    if (name == "north") return 0;
    if (name == "east") return 1;
    if (name == "south") return 2;
    if (name == "west") return 3;
    return -1;
}
constexpr std::string_view kClockwise[] = {"north", "east", "south", "west"};

// Bedrock numeric direction states -> clockwise index and back
//   weirdo_direction (stairs): 0 east, 1 west, 2 south, 3 north
//   direction (doors, beds...): 0 south, 1 west, 2 north, 3 east
//   facing_direction: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east
constexpr int kWeirdoToCw[] = {1, 3, 2, 0};
constexpr int kDirectionToCw[] = {2, 3, 0, 1};
constexpr int kFacingToCw[] = {-1, -1, 0, 2, 3, 1};

int fromCw(const int* table, int n, int cw) {
    for (int i = 0; i < n; ++i)
        if (table[i] == cw) return i;
    return -1;
}

// applies `turn` (clockwise index -> clockwise index) to every horizontal direction state
template <typename F>
DesiredBlock transformDirections(DesiredBlock b, F turn, bool swapAxes) {
    for (auto& [key, value] : b.states) {
        if (key == "pillar_axis" && swapAxes) {
            if (value == "x") value = "z";
            else if (value == "z") value = "x";
        } else if (key == "minecraft:cardinal_direction" || key == "minecraft:facing_direction") {
            int cw = clockwiseIndex(value);
            if (cw >= 0) value = std::string(kClockwise[turn(cw)]);
        } else if (key == "weirdo_direction" || key == "direction" || key == "facing_direction") {
            const int* table = key == "weirdo_direction" ? kWeirdoToCw : key == "direction" ? kDirectionToCw : kFacingToCw;
            int n = key == "facing_direction" ? 6 : 4;
            int v = std::atoi(value.c_str());
            if (v < 0 || v >= n || table[v] < 0) continue;
            int out = fromCw(table, n, turn(table[v]));
            if (out >= 0) value = std::to_string(out);
        }
    }
    return b;
}

}  // namespace

DesiredBlock rotateBlock(DesiredBlock block, int quarterTurns) {
    int t = ((quarterTurns % 4) + 4) % 4;
    if (t == 0 || block.kind != DesiredBlock::Kind::Block) return block;
    return transformDirections(std::move(block), [t](int cw) { return (cw + t) % 4; }, t % 2 == 1);
}

DesiredBlock mirrorBlock(DesiredBlock block, int mirror) {
    if (mirror == 0 || block.kind != DesiredBlock::Kind::Block) return block;
    // LEFT_RIGHT flips z (north <-> south), FRONT_BACK flips x (east <-> west)
    return transformDirections(std::move(block), [mirror](int cw) {
        if (mirror == 1 && (cw == 0 || cw == 2)) return 2 - cw;
        if (mirror == 2 && (cw == 1 || cw == 3)) return 4 - cw;
        return cw;
    }, false);
}

DesiredBlock RotatedSchematic::desired(int x, int y, int z, const BlockProps& current) const {
    // Java: the position turned back by the inverse rotation, the state turned forward
    BlockPos s = size();
    int inv = (4 - turns_) % 4;
    int ix = x, iz = z;
    switch (inv) {
        case 1: ix = s.z - z - 1, iz = x; break;          // CLOCKWISE_90
        case 2: ix = s.x - x - 1, iz = s.z - z - 1; break;  // CLOCKWISE_180
        case 3: ix = z, iz = s.x - x - 1; break;          // COUNTERCLOCKWISE_90
        default: break;
    }
    return rotateBlock(inner_->desired(ix, y, iz, current), turns_);
}

DesiredBlock MirroredSchematic::desired(int x, int y, int z, const BlockProps& current) const {
    BlockPos s = size();
    int mx = mirror_ == 2 ? s.x - x - 1 : x;
    int mz = mirror_ == 1 ? s.z - z - 1 : z;
    return mirrorBlock(inner_->desired(mx, y, mz, current), mirror_);
}

}  // namespace baritone

