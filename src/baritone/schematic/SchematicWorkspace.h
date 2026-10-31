#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "baritone/schematic/Schematic.h"
#include "baritone/world/WorldSnapshot.h"

namespace baritone {

// A Bedrock-native replacement for a Java mod's open schematic placement.
// Loading and editing a placement never starts the builder or changes the world.
class SchematicWorkspace {
public:
    enum class Difference { Missing, Wrong, Remove, Unloaded };
    struct PreviewCell { BlockPos pos; Difference difference; };
    using Materials = SchematicMaterials;
    void load(std::string name, SchematicPtr source, BlockPos origin);
    void clear();
    bool loaded() const { return source_ != nullptr; }
    const std::string& name() const { return name_; }
    std::vector<std::string> warnings() const { return source_ ? source_->warnings() : std::vector<std::string>{}; }
    SchematicPtr schematic() const { return placed_; }
    BlockPos origin() const { return origin_; }
    std::pair<BlockPos, BlockPos> bounds() const;
    void move(BlockPos origin);
    void rotate(int clockwiseQuarterTurns);
    void mirror(int mode); // 0 none, 1 flip Z, 2 flip X, before rotation
    int rotation() const { return turns_; }
    int mirrorMode() const { return mirror_; }
    void layer(std::optional<std::pair<int, int>> range);
    auto layers() const { return layers_; }
    bool previewEnabled() const { return preview_; }
    void setPreview(bool enabled) { preview_ = enabled; ++revision_; }
    uint64_t revision() const { return revision_; }
    Materials materials() const;
    // Only exact live chunks are compared. Work is bounded spatially and by the
    // number of output boxes; cached approximations are never treated as matches.
    std::vector<PreviewCell> preview(const WorldSnapshot& world, BlockPos center,
                                    int radius = 16, std::size_t maxCells = 2048) const;
private:
    void rebuild();
    void requireLoaded() const;
    std::string name_;
    SchematicPtr source_, placed_;
    BlockPos origin_{};
    int turns_ = 0, mirror_ = 0;
    bool preview_ = true;
    std::optional<std::pair<int, int>> layers_;
    uint64_t revision_ = 0;
};

} // namespace baritone
