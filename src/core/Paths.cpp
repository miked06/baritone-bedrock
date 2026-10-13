#include "core/Paths.h"

#include <Windows.h>
#include <ShlObj.h>

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

}  // namespace paths
