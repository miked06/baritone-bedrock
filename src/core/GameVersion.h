#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace gameversion {
struct NativeProfile {
    std::string_view package;
    uintptr_t craftingTable, craft, discard, recipeSelect, craftingSlotTap;
    uintptr_t anvilTable, anvilPreview, anvilTake;
    uintptr_t grindstoneTable, grindstonePreview, grindstoneTake;
    uintptr_t inventoryKey, scenePop, emptyStack, itemStackTable;
};

// Select an exact package before reading game objects or installing hooks.
bool selectNativeProfile();
const NativeProfile& profile();
std::string supportedPackages();
bool validateNativeProfile();
}
