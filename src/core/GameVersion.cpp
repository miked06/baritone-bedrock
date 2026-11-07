#include "core/GameVersion.h"

#include <Windows.h>
#include <appmodel.h>
#include <vector>

#include "core/Log.h"
#include "sdk/Offsets.h"
#include "memory/Memory.h"
#include <cstring>

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
    if (version.Major != 1 || version.Minor != 26 || version.Build != 6301 || version.Revision != 0) {
        logger::error("Unsupported game version. This build requires {}", supported);
        return false;
    }
    return true;
}
bool validateNativeProfile() {
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    struct Entry {const char* name;uintptr_t table;std::size_t index;uintptr_t function;};
    const Entry entries[]={
        {"crafting",offsets::CraftingController_vtable,16,offsets::CraftingController_autoPlace},
        {"inventory discard",offsets::CraftingController_vtable,23,offsets::CraftingController_discard},
        {"anvil preview",offsets::AnvilController_vtable,8,offsets::AnvilController_preview},
        {"anvil output",offsets::AnvilController_vtable,15,offsets::AnvilController_take},
        {"grindstone preview",offsets::GrindstoneController_vtable,8,offsets::GrindstoneController_preview},
        {"grindstone output",offsets::GrindstoneController_vtable,15,offsets::GrindstoneController_take}
    };
    for(const auto& entry:entries) {
        const auto* slot=reinterpret_cast<const uintptr_t*>(base+entry.table+entry.index*sizeof(uintptr_t));
        if(!mem::isReadable(slot,sizeof(*slot)) || *slot!=base+entry.function || !mem::isReadable(reinterpret_cast<void*>(base+entry.function),16)) {
            logger::error("Native profile mismatch: {}",entry.name);return false;
        }
    }
    constexpr uint8_t prefix[]={0x55,0x41,0x56,0x56,0x57,0x53,0x48,0x81,0xEC,0x90,0,0,0};
    constexpr uint8_t scenePrefix[]={0x55,0x48,0x83,0xEC,0x70,0x48,0x8D,0x6C,0x24,0x70};
    const auto* key=reinterpret_cast<const void*>(base+offsets::ClientInstance_inventoryKey);
    const auto* scene=reinterpret_cast<const void*>(base+offsets::SceneStack_pop);
    const auto* empty=reinterpret_cast<const uintptr_t*>(base+offsets::ItemStack_empty);
    if(!mem::isReadable(key,sizeof(prefix)) || std::memcmp(key,prefix,sizeof(prefix)) || !mem::isReadable(scene,sizeof(scenePrefix)) || std::memcmp(scene,scenePrefix,sizeof(scenePrefix)) || !mem::isReadable(empty,0x98) || *empty!=base+offsets::ItemStack_vtable || empty[1] || empty[2] || *(reinterpret_cast<const uint8_t*>(empty)+0x22)) {
        logger::error("Native profile mismatch: inventory handler, scene stack or canonical empty stack");return false;
    }
    logger::info("Native profile verified: 6 controller entries, inventory handler, scene stack and canonical empty stack");return true;
}
}
