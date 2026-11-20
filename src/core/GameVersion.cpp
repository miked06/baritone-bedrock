#include "core/GameVersion.h"

#include <Windows.h>
#include <appmodel.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <vector>

#include "core/Log.h"
#include "memory/Memory.h"

namespace gameversion {
namespace {
constexpr NativeProfile profiles[]{
#define PROFILE(...) {__VA_ARGS__},
#include "core/NativeProfiles.inc"
#undef PROFILE
};
static_assert(std::size(profiles) == 1, "Only the current stable Windows package is supported");
const NativeProfile* selected = nullptr;
}

std::string supportedPackages() {
    std::string result;
    for (const auto& entry : profiles) {
        if (!result.empty()) result += ", ";
        result += entry.package;
    }
    return result;
}

bool selectNativeProfile() {
    UINT32 length = 0;
    LONG result = GetCurrentPackageId(&length, nullptr);
    if (result != ERROR_INSUFFICIENT_BUFFER) {
        logger::error("Cannot identify Minecraft package (error {}). Supported packages: {}", result, supportedPackages());
        return false;
    }
    std::vector<BYTE> buffer(length);
    result = GetCurrentPackageId(&length, buffer.data());
    if (result != ERROR_SUCCESS || length < sizeof(PACKAGE_ID)) {
        logger::error("Cannot read Minecraft package (error {})", result);
        return false;
    }
    const auto& id = *reinterpret_cast<const PACKAGE_ID*>(buffer.data());
    if (!id.name || std::wstring_view(id.name) != L"Microsoft.MinecraftUWP" ||
        id.processorArchitecture != PROCESSOR_ARCHITECTURE_AMD64) {
        logger::error("Unsupported package identity. Expected Microsoft.MinecraftUWP x64");
        return false;
    }
    const auto& version = id.version;
    const auto package = std::format("{}.{}.{}.{}", version.Major, version.Minor, version.Build, version.Revision);
    const auto* match = std::ranges::find(profiles, package, &NativeProfile::package);
    if (match == std::end(profiles)) {
        logger::error("Minecraft package {} has no native profile. Supported packages: {}", package, supportedPackages());
        return false;
    }
    selected = match;
    logger::info("Selected native profile for Minecraft package {}", package);
    return true;
}

const NativeProfile& profile() { return *selected; }

bool validateNativeProfile() {
    if (!selected) return false;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto& p = profile();
    struct Entry { const char* name; uintptr_t table; std::size_t index; uintptr_t function; };
    const Entry entries[]{
        {"crafting", p.craftingTable, 16, p.craft},
        {"inventory discard", p.craftingTable, 23, p.discard},
        {"anvil preview", p.anvilTable, 8, p.anvilPreview},
        {"anvil output", p.anvilTable, 15, p.anvilTake},
        {"grindstone preview", p.grindstoneTable, 8, p.grindstonePreview},
        {"grindstone output", p.grindstoneTable, 15, p.grindstoneTake}
    };
    for (const auto& entry : entries) {
        const auto* slot = reinterpret_cast<const uintptr_t*>(base + entry.table + entry.index * sizeof(uintptr_t));
        if (!mem::isReadable(slot, sizeof(*slot)) || *slot != base + entry.function ||
            !mem::isReadable(reinterpret_cast<void*>(base + entry.function), 16)) {
            logger::error("Native profile {} mismatch: {}", p.package, entry.name);
            return false;
        }
    }
    constexpr uint8_t prefix[]{0x55,0x41,0x56,0x56,0x57,0x53,0x48,0x81,0xEC,0x90,0,0,0};
    constexpr uint8_t scenePrefix[]{0x55,0x48,0x83,0xEC,0x70,0x48,0x8D,0x6C,0x24,0x70};
    const auto* key = reinterpret_cast<const void*>(base + p.inventoryKey);
    const auto* scene = reinterpret_cast<const void*>(base + p.scenePop);
    const auto* empty = reinterpret_cast<const uintptr_t*>(base + p.emptyStack);
    if (!mem::isReadable(key, sizeof(prefix)) || std::memcmp(key, prefix, sizeof(prefix)) ||
        !mem::isReadable(scene, sizeof(scenePrefix)) || std::memcmp(scene, scenePrefix, sizeof(scenePrefix)) ||
        !mem::isReadable(empty, 0x98) || *empty != base + p.itemStackTable || empty[1] || empty[2] ||
        *(reinterpret_cast<const uint8_t*>(empty) + 0x22)) {
        logger::error("Native profile {} mismatch: inventory handler, scene stack or canonical empty stack", p.package);
        return false;
    }
    logger::info("Native profile {} verified: 6 controller entries, inventory handler, scene stack and canonical empty stack", p.package);
    return true;
}
}
