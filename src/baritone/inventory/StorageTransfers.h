#pragma once
#include <algorithm>
#include <optional>
#include <span>
#include "baritone/api/PlayerContext.h"

namespace baritone {
inline bool storageSlot(const ContainerSlot& slot) {
    return !slot.playerInventory && (slot.networkName == 7 || slot.networkName == 30 || slot.networkName == 58 ||
        (slot.networkName == 0 && (slot.collection.find("container") != std::string::npos || slot.collection.find("barrel") != std::string::npos || slot.collection.find("shulker") != std::string::npos)));
}
inline int mergeRoom(const HotbarStack& source, const HotbarStack& destination) {
    if (source.empty()) return 0;
    const int capacity = std::clamp(source.maxStackSize, 1, 64);
    if (destination.empty()) return capacity;
    if (source.name != destination.name || source.stackKey.empty() || source.stackKey != destination.stackKey) return 0;
    return std::max(0, std::min(capacity, destination.maxStackSize) - destination.count);
}
struct StorageTransfer { std::size_t source = 0, destination = 0; int count = 0; };
// Prefer merging before occupying another slot. Unknown/custom item data is
// preserved by choosing an empty destination instead of guessing stackability.
inline std::optional<StorageTransfer> planStorageTransfer(std::span<const ContainerSlot> slots, const std::string& item, int remaining, bool withdraw) {
    if (remaining <= 0) return {};
    for (bool empty : {false, true}) for (std::size_t from = 0; from < slots.size(); ++from) {
        const auto& source = slots[from];
        if (source.playerInventory == withdraw || source.stack.empty() || source.stack.name != item || (!source.playerInventory && !storageSlot(source))) continue;
        for (std::size_t to = 0; to < slots.size(); ++to) {
            const auto& target = slots[to];
            if (target.playerInventory != withdraw || target.stack.empty() != empty || (!target.playerInventory && !storageSlot(target))) continue;
            const int count = std::min({remaining, source.stack.count, mergeRoom(source.stack, target.stack)});
            if (count > 0) return StorageTransfer{from, to, count};
        }
    }
    return {};
}
}
