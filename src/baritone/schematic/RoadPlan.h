#pragma once
#include "baritone/schematic/Schematic.h"
namespace baritone {
struct RoadPlacement { SchematicPtr schematic; BlockPos origin; std::size_t paving=0, lights=0; };
// A bounded X-then-Z road. Coordinates describe the paving blocks, not feet.
// It creates a preview; the existing construction planner handles foundations.
RoadPlacement planRoad(BlockPos from,BlockPos to,int width,std::string material,int lightInterval=0);
}
