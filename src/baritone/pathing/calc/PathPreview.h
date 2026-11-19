#pragma once

#include "baritone/pathing/calc/Path.h"

namespace baritone {
struct PathPreview {
    PathPtr path;
    std::vector<BlockPos> toBreak, toPlace, hazards;
    std::size_t cachedMoves = 0;
    int largestDrop = 0;
    bool complete = false;
    double milliseconds = 0;
    std::string error;
};

PathPreview describePath(PathPtr path, CalculationContext& context);
}
