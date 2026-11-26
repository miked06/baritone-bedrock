#include "baritone/world/TerrainDump.h"

#include <algorithm>
#include <format>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace baritone {

namespace {

// State names and values have no spaces in practice; keep the format line-based anyway.
std::string encode(const std::string& text) {
    if (text.empty()) return "%";
    std::string out;
    for (char c : text) out += c == ' ' ? std::string("%20") : std::string(1, c);
    return out;
}

std::string decode(const std::string& text) {
    if (text == "%") return {};
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text.compare(i, 3, "%20") == 0) {
            out += ' ';
            i += 2;
        } else {
            out += text[i];
        }
    }
    return out;
}

void writeProps(std::string& out, std::size_t index, const BlockProps& p) {
    out += std::format("S {} {} {} {:d} {:d} {:d} {:.6g} {:d} {} {} {} {:d} {:d} {:d} {:d} {:d} {:d} {} {:d} {} {} {:d} {:d} {:d} {:d} {:.6g} {} {:d} {}",
                       index, encode(p.name), static_cast<int>(p.type), p.collides, p.fullCube, p.solidTop, p.height,
                       p.pathfindable, static_cast<int>(p.parkourSupport), static_cast<int>(p.fluid), p.fluidLevel,
                       p.bottomSlab, p.topSlab, p.doubleSlab, p.topHalfStairs, p.innerStairs, p.topTrapdoor,
                       p.snowLayers, p.open, p.growth, p.matureGrowth, p.falling, p.replaceable, p.canPlaceAgainst,
                       p.unbreakable, p.hardness, static_cast<int>(p.bestTool), p.requiresTool, p.states.size());
    for (const auto& [key, value] : p.states) out += " " + encode(key) + " " + encode(value);
    out += '\n';
}

BlockProps readProps(std::istringstream& line) {
    BlockProps p;
    std::string name;
    int type, collides, fullCube, solidTop, pathfindable, parkour, fluid, fluidLevel, bottomSlab, topSlab, doubleSlab,
        topHalfStairs, innerStairs, topTrapdoor, snowLayers, open, growth, matureGrowth, falling, replaceable,
        canPlaceAgainst, unbreakable, bestTool, requiresTool;
    std::size_t count;
    line >> name >> type >> collides >> fullCube >> solidTop >> p.height >> pathfindable >> parkour >> fluid >>
        fluidLevel >> bottomSlab >> topSlab >> doubleSlab >> topHalfStairs >> innerStairs >> topTrapdoor >> snowLayers >>
        open >> growth >> matureGrowth >> falling >> replaceable >> canPlaceAgainst >> unbreakable >> p.hardness >>
        bestTool >> requiresTool >> count;
    if (!line || count > 64) throw std::runtime_error("malformed block state line");
    p.name = decode(name);
    p.type = static_cast<BlockType>(type);
    p.collides = collides, p.fullCube = fullCube, p.solidTop = solidTop, p.pathfindable = pathfindable;
    p.parkourSupport = static_cast<ParkourSupport>(parkour);
    p.fluid = static_cast<Fluid>(fluid);
    p.fluidLevel = static_cast<uint8_t>(fluidLevel);
    p.bottomSlab = bottomSlab, p.topSlab = topSlab, p.doubleSlab = doubleSlab, p.topHalfStairs = topHalfStairs;
    p.innerStairs = innerStairs, p.topTrapdoor = topTrapdoor, p.snowLayers = static_cast<uint8_t>(snowLayers);
    p.open = open, p.growth = static_cast<uint8_t>(growth), p.matureGrowth = static_cast<uint8_t>(matureGrowth);
    p.falling = falling, p.replaceable = replaceable, p.canPlaceAgainst = canPlaceAgainst, p.unbreakable = unbreakable;
    p.bestTool = static_cast<Tool>(bestTool);
    p.requiresTool = requiresTool;
    for (std::size_t i = 0; i < count; ++i) {
        std::string key, value;
        line >> key >> value;
        if (!line) throw std::runtime_error("malformed block state values");
        p.states.emplace_back(decode(key), decode(value));
    }
    return p;
}

}  // namespace

std::string writeTerrainDump(const WorldSnapshot& world, const BlockPos& min, const BlockPos& max, const Vec3d& player) {
    std::unordered_map<StateId, std::size_t> index{{kAir, 0}};
    std::vector<StateId> order{kAir};
    std::string cells, facings;
    std::size_t count = 0, facingCount = 0;
    const BlockRegistry& registry = BlockRegistry::get();
    for (int cx = min.x >> 4; cx <= max.x >> 4; ++cx) {
        for (int cz = min.z >> 4; cz <= max.z >> 4; ++cz) {
            auto chunk = world.chunk(cx, cz);
            if (!chunk) continue;
            for (int x = std::max(min.x, cx << 4); x <= std::min(max.x, (cx << 4) | 15); ++x)
                for (int z = std::max(min.z, cz << 4); z <= std::min(max.z, (cz << 4) | 15); ++z)
                    for (int y = std::max(min.y, world.minY()); y <= std::min(max.y, world.maxY() - 1); ++y) {
                        const StateId id = chunk->get(x & 15, y, z & 15);
                        if (id == kAir || id >= registry.size()) continue;
                        auto [it, inserted] = index.try_emplace(id, order.size());
                        if (inserted) order.push_back(id);
                        cells += std::format("{} {} {} {}\n", x, y, z, it->second);
                        ++count;
                        if (auto f = chunk->facing(x & 15, y, z & 15)) {
                            facings += std::format("{} {} {} {}\n", x, y, z, *f);
                            ++facingCount;
                        }
                    }
        }
    }
    std::string out = "BBTERRAIN 1\n";
    out += std::format("dimension {} {} {}\n", world.dimension(), world.minY(), world.maxY());
    out += std::format("box {} {} {} {} {} {}\n", min.x, min.y, min.z, max.x, max.y, max.z);
    out += std::format("player {:.4f} {:.4f} {:.4f}\n", player.x, player.y, player.z);
    out += std::format("states {}\n", order.size());
    for (std::size_t i = 0; i < order.size(); ++i) writeProps(out, i, registry.props(order[i]));
    out += std::format("blocks {}\n", count);
    out += cells;
    out += std::format("facings {}\n", facingCount);
    return out + facings;
}

TerrainDump readTerrainDump(std::istream& in) {
    TerrainDump dump;
    std::string line, word;
    auto next = [&](const char* expected) {
        if (!std::getline(in, line)) throw std::runtime_error(std::string("missing ") + expected);
        std::istringstream s(line);
        s >> word;
        if (word != expected) throw std::runtime_error(std::string("expected ") + expected);
        return s;
    };
    {
        auto s = next("BBTERRAIN");
        int version = 0;
        s >> version;
        if (version != 1) throw std::runtime_error("unknown terrain dump version");
    }
    next("dimension") >> dump.dimension >> dump.minY >> dump.maxY;
    {
        auto s = next("box");
        s >> dump.min.x >> dump.min.y >> dump.min.z >> dump.max.x >> dump.max.y >> dump.max.z;
    }
    next("player") >> dump.player.x >> dump.player.y >> dump.player.z;
    std::size_t states = 0, blocks = 0;
    next("states") >> states;
    if (states == 0 || states > kMaxStates) throw std::runtime_error("invalid state count");
    for (std::size_t i = 0; i < states; ++i) {
        auto s = next("S");
        std::size_t idx;
        s >> idx;
        if (idx != i) throw std::runtime_error("block states out of order");
        dump.states.push_back(readProps(s));
    }
    next("blocks") >> blocks;
    dump.blocks.reserve(blocks);
    for (std::size_t i = 0; i < blocks; ++i) {
        BlockPos p;
        uint32_t idx;
        if (!(in >> p.x >> p.y >> p.z >> idx) || idx >= dump.states.size()) throw std::runtime_error("malformed block");
        dump.blocks.emplace_back(p, idx);
    }
    // optional: block entity facings
    std::size_t facings = 0;
    while (std::getline(in, line)) {
        std::istringstream s(line);
        if (!(s >> word)) continue;
        if (word != "facings" || !(s >> facings)) throw std::runtime_error("expected facings");
        break;
    }
    for (std::size_t i = 0; i < facings; ++i) {
        BlockPos p;
        int f;
        if (!(in >> p.x >> p.y >> p.z >> f) || f < 0 || f > 5) throw std::runtime_error("malformed facing");
        dump.facings.emplace_back(p, static_cast<uint8_t>(f));
    }
    return dump;
}

std::shared_ptr<WorldSnapshot> buildTerrainWorld(const TerrainDump& dump) {
    std::vector<StateId> ids(dump.states.size(), kAir);
    for (std::size_t i = 1; i < dump.states.size(); ++i) ids[i] = BlockRegistry::get().add(dump.states[i]);
    auto world = std::make_shared<WorldSnapshot>(dump.dimension, dump.minY, dump.maxY);
    for (int cx = dump.min.x >> 4; cx <= dump.max.x >> 4; ++cx)
        for (int cz = dump.min.z >> 4; cz <= dump.max.z >> 4; ++cz)
            world->put(std::make_shared<ChunkData>(cx, cz, dump.minY, dump.maxY - dump.minY));
    for (const auto& [pos, idx] : dump.blocks) world->setBlock(pos.x, pos.y, pos.z, ids[idx]);
    for (const auto& [pos, f] : dump.facings)
        if (auto chunk = world->mutableChunk(pos.x >> 4, pos.z >> 4)) chunk->setFacing(pos.x & 15, pos.y, pos.z & 15, f);
    return world;
}

}  // namespace baritone
