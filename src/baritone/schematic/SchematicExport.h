#pragma once
#include <filesystem>
#include "baritone/selection/SelectionManager.h"
#include "baritone/world/WorldSnapshot.h"
namespace baritone {
// Export supported block states only. Fails on unknown chunks, never exports
// cached approximations or overwrites an existing file.
void exportMcStructure(const WorldSnapshot& world, Selection selection, const std::filesystem::path& path);
}
