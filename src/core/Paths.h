#pragma once
#include <filesystem>

namespace paths {

// %LOCALAPPDATA%\BaritoneBedrock (created on first use): logs, settings, cache, waypoints.
const std::filesystem::path& dataDir();

}  // namespace paths
