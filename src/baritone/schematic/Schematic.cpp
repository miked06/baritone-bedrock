#include "baritone/schematic/Schematic.h"

#include <cmath>
#include <cstdlib>

#include <stdexcept>
#include <string>

#include "baritone/schematic/NbtReader.h"

namespace baritone {

DesiredBlock StaticSchematic::desired(int x, int y, int z, const std::string&) const {
    std::size_t i = (static_cast<std::size_t>(x) * size_.y + y) * size_.z + z;
    if (i >= indices_.size()) return DesiredBlock::any();
    int idx = indices_[i];
    if (idx < 0 || idx >= static_cast<int>(palette_.size())) return DesiredBlock::any();
    return palette_[idx];
}

SchematicPtr loadMcStructure(const std::string& path) {
    nbt::Tag root = nbt::parse(nbt::readFile(path), /*littleEndian=*/true);
    const nbt::Tag* sizeTag = root.get("size");
    const nbt::Tag* structure = root.get("structure");
    if (!sizeTag || !sizeTag->list() || sizeTag->list()->size() != 3 || !structure) {
        throw std::runtime_error("Fichier .mcstructure invalide (size/structure manquant)");
    }
    const auto& sl = *sizeTag->list();
    BlockPos size{static_cast<int>(sl[0].integer().value_or(0)), static_cast<int>(sl[1].integer().value_or(0)),
                  static_cast<int>(sl[2].integer().value_or(0))};
    if (size.x <= 0 || size.y <= 0 || size.z <= 0 || static_cast<int64_t>(size.x) * size.y * size.z > 64 * 1024 * 1024) {
        throw std::runtime_error("Taille de structure invalide");
    }

    // palette.default.block_palette: [{name, states, version}]
    const nbt::Tag* palette = structure->get("palette");
    const nbt::Tag* def = palette ? palette->get("default") : nullptr;
    const nbt::Tag* blockPalette = def ? def->get("block_palette") : nullptr;
    if (!blockPalette || !blockPalette->list()) throw std::runtime_error("Palette manquante");
    std::vector<DesiredBlock> pal;
    for (const nbt::Tag& entry : *blockPalette->list()) {
        const nbt::Tag* name = entry.get("name");
        std::string n = name && name->string() ? *name->string() : "minecraft:air";
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
    if (!indicesTag || !indicesTag->list() || indicesTag->list()->empty()) throw std::runtime_error("block_indices manquant");
    const nbt::Tag& layer0 = (*indicesTag->list())[0];
    std::vector<int> indices;
    if (auto* arr = layer0.intArray()) {
        for (int64_t v : *arr) indices.push_back(static_cast<int>(v));
    } else if (auto* list = layer0.list()) {
        for (const nbt::Tag& v : *list) indices.push_back(static_cast<int>(v.integer().value_or(-1)));
    }
    if (indices.size() != static_cast<std::size_t>(size.x) * size.y * size.z) {
        throw std::runtime_error("Nombre d'indices incoherent");
    }
    return std::make_shared<StaticSchematic>(size, std::move(pal), std::move(indices));
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

DesiredBlock RotatedSchematic::desired(int x, int y, int z, const std::string& current) const {
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

DesiredBlock MirroredSchematic::desired(int x, int y, int z, const std::string& current) const {
    BlockPos s = size();
    int mx = mirror_ == 2 ? s.x - x - 1 : x;
    int mz = mirror_ == 1 ? s.z - z - 1 : z;
    return mirrorBlock(inner_->desired(mx, y, mz, current), mirror_);
}

}  // namespace baritone

