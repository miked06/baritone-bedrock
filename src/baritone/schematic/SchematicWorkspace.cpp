#include "baritone/schematic/SchematicWorkspace.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include "baritone/schematic/Orientation.h"

namespace baritone {
void SchematicWorkspace::requireLoaded() const {
    if (!loaded()) throw std::invalid_argument("No schematic loaded. Use #schematic load <file> first");
}
void SchematicWorkspace::load(std::string name, SchematicPtr source, BlockPos origin, std::string sourcePath) {
    if (!source) throw std::invalid_argument("Missing schematic");
    schematicVolume(source->size());
    for (auto [pos, size] : {std::pair{origin.x, source->size().x}, {origin.y, source->size().y}, {origin.z, source->size().z}})
        if (static_cast<int64_t>(pos) + size - 1 > INT32_MAX) throw std::invalid_argument("Placement coordinates overflow");
    name_ = std::move(name);
    sourcePath_ = std::move(sourcePath);
    source_ = std::move(source);
    origin_ = origin;
    turns_ = mirror_ = 0;
    layers_.reset();
    preview_ = true;
    rebuild();
}
void SchematicWorkspace::clear() {
    source_.reset(); placed_.reset(); name_.clear(); sourcePath_.clear(); layers_.reset(); ++revision_;
}
void SchematicWorkspace::rebuild() {
    placed_ = source_;
    if (mirror_) placed_ = std::make_shared<MirroredSchematic>(placed_, mirror_);
    if (turns_) placed_ = std::make_shared<RotatedSchematic>(placed_, turns_);
    if (layers_) {
        auto [lo, hi] = *layers_;
        placed_ = std::make_shared<MaskSchematic>(placed_, [lo, hi](int, int y, int) { return y >= lo && y <= hi; });
    }
    ++revision_;
}
std::pair<BlockPos, BlockPos> SchematicWorkspace::bounds() const {
    requireLoaded();
    auto size = placed_->size();
    return {origin_, origin_.offset(size.x - 1, size.y - 1, size.z - 1)};
}
void SchematicWorkspace::move(BlockPos origin) {
    requireLoaded();
    auto size = placed_->size();
    for (auto [pos, side] : {std::pair{origin.x, size.x}, {origin.y, size.y}, {origin.z, size.z}})
        if (static_cast<int64_t>(pos) + side - 1 > INT32_MAX) throw std::invalid_argument("Placement coordinates overflow");
    origin_ = origin; ++revision_;
}
void SchematicWorkspace::rotate(int clockwiseQuarterTurns) {
    requireLoaded();
    const int next = ((clockwiseQuarterTurns % 4) + 4) % 4;
    BlockPos size = source_->size();
    if (next % 2) std::swap(size.x, size.z);
    for (auto [pos, side] : {std::pair{origin_.x, size.x}, {origin_.z, size.z}})
        if (static_cast<int64_t>(pos) + side - 1 > INT32_MAX) throw std::invalid_argument("Placement coordinates overflow");
    turns_ = next; rebuild();
}
void SchematicWorkspace::mirror(int mode) {
    requireLoaded();
    if (mode < 0 || mode > 2) throw std::invalid_argument("Mirror: none, x or z");
    mirror_ = mode; rebuild();
}
void SchematicWorkspace::layer(std::optional<std::pair<int, int>> range) {
    requireLoaded();
    if (range && (range->first < 0 || range->second < range->first || range->second >= source_->size().y))
        throw std::invalid_argument("Layer range must be within the schematic's height (zero-based)");
    layers_ = range; rebuild();
}
SchematicWorkspace::Materials SchematicWorkspace::materials() const {
    requireLoaded();
    // All file loaders produce a palette-backed source. Horizontal transforms
    // preserve its histogram; avoid copying a DesiredBlock/string per cell.
    if (auto source = std::dynamic_pointer_cast<const StaticSchematic>(source_)) {
        const auto range = layers_.value_or(std::pair{0, source_->size().y - 1});
        return source->materialCounts(range.first, range.second);
    }
    Materials result;
    auto size = placed_->size();
    for (int x = 0; x < size.x; ++x) for (int y = 0; y < size.y; ++y) for (int z = 0; z < size.z; ++z) {
        const auto d = placed_->desired(x, y, z, BlockProps{});
        if (d.kind == DesiredBlock::Kind::Block) ++result.blocks[d.name];
        else if (d.kind == DesiredBlock::Kind::Air) ++result.air;
        else ++result.ignored;
    }
    return result;
}
std::vector<SchematicWorkspace::PreviewCell> SchematicWorkspace::preview(
    const WorldSnapshot& world, BlockPos center, int radius, std::size_t maxCells) const {
    std::vector<PreviewCell> result;
    if (!loaded() || !preview_ || !maxCells || radius < 0) return result;
    // A caller cannot accidentally turn this into a full-volume per-frame scan.
    radius = std::min(radius, 32);
    auto size = placed_->size();
    auto lower = [radius](int c, int o) { return static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(c) - radius - o, 0, INT32_MAX)); };
    auto upper = [radius](int c, int o, int side) { return static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(c) + radius - o, -1, side - 1)); };
    BlockPos lo{lower(center.x, origin_.x), lower(center.y, origin_.y), lower(center.z, origin_.z)};
    BlockPos hi{upper(center.x, origin_.x, size.x), upper(center.y, origin_.y, size.y), upper(center.z, origin_.z, size.z)};
    const auto& registry = BlockRegistry::get();
    for (int y = lo.y; y <= hi.y; ++y) for (int x = lo.x; x <= hi.x; ++x) for (int z = lo.z; z <= hi.z; ++z) {
        BlockPos p = origin_.offset(x, y, z);
        auto chunk = world.chunk(p.x >> 4, p.z >> 4);
        const bool loaded = chunk && p.y >= world.minY() && p.y < world.maxY();
        const auto& current = registry.props(loaded ? chunk->get(p.x & 15, p.y, p.z & 15) : kAir);
        auto desired = placed_->desired(x, y, z, current);
        if (desired.kind == DesiredBlock::Kind::Any) continue;
        if (!loaded) result.push_back({p, Difference::Unloaded});
        else if (desired.kind == DesiredBlock::Kind::Air) {
            if (current.type != BlockType::Air) result.push_back({p, Difference::Remove});
        } else if (!blockMatches(desired, current, false)) {
            result.push_back({p, current.type == BlockType::Air || current.replaceable ? Difference::Missing : Difference::Wrong});
        }
        if (result.size() >= maxCells) return result;
    }
    return result;
}
} // namespace baritone
