#include "core/Paths.h"

#include <Windows.h>
#include <ShlObj.h>
#include <format>

namespace paths {

const std::filesystem::path& dataDir() {
    static const std::filesystem::path dir = [] {
        std::filesystem::path base;
        PWSTR local = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
            base = local;
        } else {
            base = std::filesystem::temp_directory_path();
        }
        CoTaskMemFree(local);
        auto path = base / "BaritoneBedrock";
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        return path;
    }();
    return dir;
}

std::string worldKey(std::string_view levelId) {
    // Local IDs are directory names. Check for the matching save rather than
    // relying on a server-supplied name or treating it as a filesystem path.
    const bool valid = !levelId.empty() && levelId.size() <= 64 &&
        levelId.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-=+") == std::string_view::npos;
    if (valid) {
        PWSTR roaming = nullptr;
        std::filesystem::path users;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)))
            users = std::filesystem::path(roaming) / "Minecraft Bedrock" / "Users";
        CoTaskMemFree(roaming);
        std::error_code ec;
        std::u8string account;
        bool ambiguous = false;
        if (!users.empty()) {
            for (std::filesystem::directory_iterator it(users, ec), end; !ec && it != end; it.increment(ec)) {
                const auto candidate = it->path() / "games" / "com.mojang" / "minecraftWorlds" / levelId;
                std::error_code check;
                if (!std::filesystem::exists(candidate / "level.dat", check)) {
                    if (check) { ambiguous = true; break; }
                    continue;
                }
                if (!account.empty()) { ambiguous = true; break; }
                account = it->path().filename().u8string();
            }
        }
        if (!ec && !ambiguous && !account.empty()) {
            // Hex preserves distinctions between case-sensitive game IDs on
            // case-insensitive Windows filesystems.
            std::string key = "local-";
            for (auto c : account) key += std::format("{:02x}", static_cast<unsigned>(c));
            key += '-';
            for (unsigned char c : levelId) key += std::format("{:02x}", static_cast<unsigned>(c));
            return key;
        }
    }
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) throw std::runtime_error("Could not create a world session ID");
    std::string key = "session-";
    const auto* bytes = reinterpret_cast<const unsigned char*>(&guid);
    for (std::size_t i = 0; i < sizeof guid; ++i) key += std::format("{:02x}", static_cast<unsigned>(bytes[i]));
    return key;
}

}  // namespace paths
