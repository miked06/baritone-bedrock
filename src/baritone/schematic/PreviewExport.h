#pragma once
#include <ostream>
#include "baritone/schematic/Schematic.h"
namespace baritone {
// Script-safe JSON for a transformed placement. No world/inventory access.
void writePreviewJson(std::ostream& output, const Schematic& schematic, BlockPos origin, std::string_view name);
}
