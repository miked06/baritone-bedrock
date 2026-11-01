#pragma once
#include <filesystem>
#include "baritone/schematic/SchematicWorkspace.h"
namespace baritone {
struct PlacementPreset {
    std::string file;
    BlockPos origin;
    int rotation = 0, mirror = 0;
    bool visible = true;
    std::optional<std::pair<int, int>> layers;
    void apply(SchematicWorkspace& work) const;
};
// At most eight shared palette-backed placements stay resident. Presets survive
// restart and reload their file asynchronously when selected by the caller.
class SchematicCatalog {
public:
    struct Entry { PlacementPreset preset; std::optional<SchematicWorkspace> resident; };
    void store(const std::string& key, const SchematicWorkspace& work);
    const Entry& entry(const std::string& key) const;
    bool select(const std::string& key, SchematicWorkspace& work) const;
    void remove(const std::string& key);
    void clear() { entries_.clear(); ++revision_; }
    const auto& entries() const { return entries_; }
    uint64_t revision() const { return revision_; }
    void save(const std::filesystem::path& file) const;
    void load(const std::filesystem::path& file);
    std::vector<SchematicWorkspace::PreviewCell> preview(const WorldSnapshot& world, BlockPos center, const SchematicWorkspace& active, std::size_t limit = 512) const;
private:
    std::map<std::string, Entry> entries_;
    uint64_t revision_ = 0;
};
}
