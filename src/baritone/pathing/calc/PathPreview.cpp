#include "baritone/pathing/calc/PathPreview.h"

#include <algorithm>
#include <unordered_set>

#include "baritone/pathing/movement/Movement.h"

namespace baritone {
PathPreview describePath(PathPtr path, CalculationContext& context) {
    PathPreview preview;
    preview.path = std::move(path);
    std::unordered_set<BlockPos> seenBreak, seenPlace, seenHazard;
    auto append = [](std::vector<BlockPos>& positions, std::unordered_set<BlockPos>& seen, const auto& cells) {
        for (const auto& cell : cells)
            if (seen.insert(cell).second) positions.push_back(cell);
    };
    for (const auto& move : preview.path->movements()) {
        auto movement = Movement::create(move);
        append(preview.toBreak, seenBreak, movement->toBreak(context));
        append(preview.toPlace, seenPlace, movement->toPlace(context));
        preview.cachedMoves += !move.calculatedWhileLoaded;
        preview.largestDrop = std::max(preview.largestDrop, move.src.y - move.dest.y);
    }

    constexpr BlockPos nearby[]{{0, 0, 0}, {0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const auto& pos : preview.path->positions()) {
        for (const auto& offset : nearby) {
            const auto cell = pos + offset;
            const auto& block = context.props(cell.x, cell.y, cell.z);
            const auto type = block.type;
            if ((block.fluid == Fluid::Lava || type == BlockType::Fire || type == BlockType::Cactus ||
                 type == BlockType::Magma || type == BlockType::PowderSnow || type == BlockType::SweetBerryBush) &&
                seenHazard.insert(cell).second) preview.hazards.push_back(cell);
        }
    }
    return preview;
}
}
