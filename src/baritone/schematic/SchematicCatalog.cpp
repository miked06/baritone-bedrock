#include "baritone/schematic/SchematicCatalog.h"
#include <fstream>
#include <iomanip>
#include <set>
namespace baritone {
void PlacementPreset::apply(SchematicWorkspace& work) const {
    work.move(origin); work.rotate(rotation); work.mirror(mirror); work.layer(layers); work.setPreview(visible);
}
void SchematicCatalog::store(const std::string& key, const SchematicWorkspace& work) {
    if (!work.loaded() || work.sourcePath().empty()) throw std::invalid_argument("Load a schematic file before storing its placement");
    if (key.empty() || key.size() > 128 || key.find_first_of("\r\n") != std::string::npos) throw std::invalid_argument("Placement names must be 1..128 characters");
    if (!entries_.contains(key) && entries_.size() >= 64) throw std::invalid_argument("At most 64 placement presets");
    std::size_t resident = 0;
    for (const auto& [name, e] : entries_) if (name != key && e.resident) ++resident;
    if (resident >= 8) throw std::invalid_argument("At most eight resident placements; remove one before storing another");
    PlacementPreset p{work.sourcePath(), work.origin(), work.rotation(), work.mirrorMode(), work.previewEnabled(), work.layers()};
    entries_.insert_or_assign(key, Entry{std::move(p), work}); ++revision_;
}
const SchematicCatalog::Entry& SchematicCatalog::entry(const std::string& key) const {
    auto found = entries_.find(key); if (found == entries_.end()) throw std::invalid_argument("Unknown placement preset"); return found->second;
}
bool SchematicCatalog::select(const std::string& key, SchematicWorkspace& work) const {
    const auto& e = entry(key); if (!e.resident) return false;
    work = *e.resident; work.setPreview(e.preset.visible); return true;
}
void SchematicCatalog::remove(const std::string& key) {
    if (!entries_.erase(key)) throw std::invalid_argument("Unknown placement preset"); ++revision_;
}
void SchematicCatalog::save(const std::filesystem::path& file) const {
    std::filesystem::create_directories(file.parent_path()); auto tmp = file; tmp += ".tmp";
    std::ofstream out(tmp); out << "BARITONE_PLACEMENTS 1\n";
    for (const auto& [name, e] : entries_) {
        const auto& p = e.preset;
        out << std::quoted(name) << ' ' << std::quoted(p.file) << ' ' << p.origin.x << ' ' << p.origin.y << ' ' << p.origin.z << ' '
            << p.rotation << ' ' << p.mirror << ' ' << p.visible << ' ' << (p.layers ? p.layers->first : -1) << ' ' << (p.layers ? p.layers->second : -1) << '\n';
    }
    out.close(); if (!out) throw std::runtime_error("Could not save placement presets"); std::filesystem::rename(tmp, file);
}
void SchematicCatalog::load(const std::filesystem::path& file) {
    clear(); if (!std::filesystem::exists(file)) return;
    if (std::filesystem::file_size(file) > 512 * 1024) throw std::invalid_argument("Placement file is too large");
    std::ifstream in(file); std::string magic; int version;
    if (!(in >> magic >> version) || magic != "BARITONE_PLACEMENTS" || version != 1) throw std::invalid_argument("Invalid placement file");
    std::map<std::string, Entry> parsed;
    while (in >> std::ws && in.peek() != EOF) {
        std::string name; PlacementPreset p; int lo, hi;
        if (!(in >> std::quoted(name) >> std::quoted(p.file) >> p.origin.x >> p.origin.y >> p.origin.z >> p.rotation >> p.mirror >> p.visible >> lo >> hi) ||
            name.empty() || name.size() > 128 || p.file.empty() || p.file.size() > 4096 || p.rotation < 0 || p.rotation > 3 || p.mirror < 0 || p.mirror > 2 ||
            !((lo == -1 && hi == -1) || (lo >= 0 && hi >= lo)) || parsed.size() >= 64 || parsed.contains(name)) throw std::invalid_argument("Invalid placement entry");
        if (lo >= 0) p.layers = std::pair{lo, hi};
        parsed.emplace(std::move(name), Entry{std::move(p), std::nullopt});
    }
    entries_ = std::move(parsed); ++revision_;
}
std::vector<SchematicWorkspace::PreviewCell> SchematicCatalog::preview(const WorldSnapshot& world, BlockPos center, const SchematicWorkspace& active, std::size_t limit) const {
    limit = std::min<std::size_t>(limit, 512);
    auto result = active.preview(world, center, entries_.empty() ? 16 : 8, limit);
    std::set<BlockPos> seen; for (const auto& c : result) seen.insert(c.pos);
    for (const auto& [name, e] : entries_) {
        if (result.size() >= limit) break;
        if (!e.resident) continue;
        for (const auto& cell : e.resident->preview(world, center, 8, limit - result.size()))
            if (seen.insert(cell.pos).second) result.push_back(cell);
    }
    return result;
}
}
