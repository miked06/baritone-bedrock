#include "baritone/schematic/SchematicExport.h"
#include "baritone/schematic/Schematic.h"
#include <charconv>
#include <fstream>
#include <map>
#include <stdexcept>

namespace baritone {
namespace {
struct Writer {
    std::ofstream out;
    explicit Writer(const std::filesystem::path& path) : out(path, std::ios::binary | std::ios::noreplace) {
        if (!out) throw std::runtime_error("Cannot create export temporary file");
        out.exceptions(std::ios::badbit | std::ios::failbit);
    }
    void byte(uint8_t value) { out.put(static_cast<char>(value)); }
    void integer(int32_t value) { for (int i = 0; i < 4; ++i) byte(static_cast<uint32_t>(value) >> (8 * i)); }
    void numeric(int64_t value, int bytes) { for (int i = 0; i < bytes; ++i) byte(static_cast<uint64_t>(value) >> (8 * i)); }
    void string(const std::string& value) {
        if (value.size() > UINT16_MAX) throw std::invalid_argument("NBT string too long");
        byte(value.size() & 255); byte((value.size() >> 8) & 255); out.write(value.data(), value.size());
    }
    void tag(uint8_t type, const std::string& name) { byte(type); string(name); }
    void integer(const std::string& name, int32_t value) { tag(3, name); integer(value); }
    void list(const std::string& name, uint8_t kind, int32_t size) { tag(9, name); byte(kind); integer(size); }
};
}
void exportMcStructure(const WorldSnapshot& world, Selection selection, const std::filesystem::path& path) {
    const auto lo = selection.min(), hi = selection.max();
    auto side = [](int a, int b) { auto n = int64_t(b) - a + 1; if (n > INT32_MAX) throw std::invalid_argument("Export selection too large"); return int(n); };
    const BlockPos size{side(lo.x, hi.x), side(lo.y, hi.y), side(lo.z, hi.z)};
    const auto volume = schematicVolume(size);
    if (volume > 8 * 1024 * 1024) throw std::invalid_argument("Export limit is eight million block positions");
    if (lo.y < world.minY() || hi.y >= world.maxY()) throw std::invalid_argument("Export selection extends outside build height");
    if (path.extension() != ".mcstructure") throw std::invalid_argument("Export filename must end in .mcstructure");
    if (std::filesystem::exists(path)) throw std::invalid_argument("Export file already exists; choose another name");
    std::map<StateId, int32_t> palette;
    std::vector<StateId> states;
    std::vector<int32_t> indices; indices.reserve(volume);
    for (int x = 0; x < size.x; ++x) for (int y = 0; y < size.y; ++y) for (int z = 0; z < size.z; ++z) {
        const auto p = lo.offset(x, y, z);
        auto chunk = world.chunk(p.x >> 4, p.z >> 4);
        if (!chunk) throw std::runtime_error("Load every selected chunk before exporting");
        const auto id = chunk->get(p.x & 15, p.y, p.z & 15);
        auto [entry, inserted] = palette.try_emplace(id, static_cast<int32_t>(states.size()));
        if (inserted) states.push_back(id);
        indices.push_back(entry->second);
    }
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    auto temporary = path; temporary += ".tmp";
    bool created = false;
    try {
        Writer w(temporary); created = true;
        w.tag(10, ""); w.integer("format_version", 1);
        w.list("size", 3, 3); for (int n : {size.x, size.y, size.z}) w.integer(n);
        w.list("structure_world_origin", 3, 3); for (int n : {lo.x, lo.y, lo.z}) w.integer(n);
        w.tag(10, "structure");
        w.list("block_indices", 9, 2);
        w.byte(3); w.integer(static_cast<int32_t>(volume)); for (auto i : indices) w.integer(i);
        w.byte(3); w.integer(static_cast<int32_t>(volume)); for (std::size_t i = 0; i < volume; ++i) w.integer(-1);
        w.list("entities", 10, 0);
        w.tag(10, "palette"); w.tag(10, "default");
        w.list("block_palette", 10, static_cast<int32_t>(states.size()));
        for (auto id : states) {
            const auto& props = BlockRegistry::get().props(id);
            w.tag(8, "name"); w.string(props.name);
            if (props.blockVersion) w.integer("version", props.blockVersion);
            w.tag(10, "states");
            for (const auto& [key, value] : props.states) {
                int64_t number = 0;
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
                uint8_t type = 0;
                for (const auto& [name, nativeType] : props.stateTypes) if (name == key) { type = nativeType; break; }
                if (type == 8) { w.tag(8, key); w.string(value); }
                else if (type >= 1 && type <= 4 && parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size()) {
                    w.tag(type, key); w.numeric(number, type == 1 ? 1 : type == 2 ? 2 : type == 3 ? 4 : 8);
                } else if (value == "true" || value == "false" || (key.ends_with("_bit") && (value == "0" || value == "1"))) {
                    w.tag(1, key); w.byte(value == "true" || value == "1");
                } else if (parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && number >= INT32_MIN && number <= INT32_MAX) w.integer(key, static_cast<int32_t>(number));
                else { w.tag(8, key); w.string(value); }
            }
            w.byte(0); w.byte(0);
        }
        w.tag(10, "block_position_data"); w.byte(0);
        for (int i = 0; i < 4; ++i) w.byte(0); // default, palette, structure, root
        w.out.close();
        if (std::filesystem::exists(path)) throw std::runtime_error("Export destination appeared while writing");
        std::filesystem::rename(temporary, path);
    } catch (...) {
        if (created) { std::error_code ignored; std::filesystem::remove(temporary, ignored); }
        throw;
    }
}
}
