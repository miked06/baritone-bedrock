#include "core/GameVersion.h"

#include <Windows.h>
#include <appmodel.h>
#include <vector>

#include "core/Log.h"

namespace gameversion {
bool check() {
    UINT32 length = 0;
    LONG result = GetCurrentPackageId(&length, nullptr);
    if (result != ERROR_INSUFFICIENT_BUFFER) {
        logger::error("Cannot identify Minecraft package (error {}). Supported: {}", result, supported);
        return false;
    }
    std::vector<BYTE> buffer(length);
    result = GetCurrentPackageId(&length, buffer.data());
    if (result != ERROR_SUCCESS || length < sizeof(PACKAGE_ID)) {
        logger::error("Cannot read Minecraft package (error {})", result);
        return false;
    }
    const auto& version = reinterpret_cast<const PACKAGE_ID*>(buffer.data())->version;
    logger::info("Minecraft package: {}.{}.{}.{}", version.Major, version.Minor, version.Build, version.Revision);
    if (version.Major != 1 || version.Minor != 26 || version.Build != 6005 || version.Revision != 0) {
        logger::error("Unsupported game version. This build requires {}", supported);
        return false;
    }
    return true;
}
}
