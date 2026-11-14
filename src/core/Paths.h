#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace paths {

// %LOCALAPPDATA%\BaritoneBedrock (created on first use): logs, settings, cache, waypoints.
const std::filesystem::path& dataDir();
// Returns a local save key or a unique key for the current visit.
std::string worldKey(std::string_view levelId);

}  // namespace paths
