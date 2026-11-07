#pragma once
#include <filesystem>
#include <map>
#include <string>
namespace baritone {
// Portable ZIP (stored entries), bounded to 16 files / 8 MiB. Entry names are
// fixed by the caller; world caches, credentials and save files are not collected.
void writeDiagnosticBundle(const std::filesystem::path& output, const std::map<std::string,std::string>& files);
std::string diagnosticFileTail(const std::filesystem::path& file, std::size_t limit);
}
