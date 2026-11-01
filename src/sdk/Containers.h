#pragma once
#include <array>
#include <memory>
#include <string>
#include <vector>
#include "sdk/Game.h"
namespace sdk {
struct ContainerSlotRef {
    std::array<uint8_t, 12> name{};
    int slot = 0;
    int modelSlot = -1;
    bool operator==(const ContainerSlotRef&) const = default;
};
struct ContainerView { std::string name; uint8_t networkName; int size; };
struct TransferReceipt { bool valid = false; uint32_t sourceId = 0, targetId = 0; int sourceCount = 0, targetCount = 0; };
struct TransferSnapshot { std::shared_ptr<const void> source, target; };
TransferSnapshot snapshotContainerTransfer(LocalPlayer& player, const ContainerSlotRef& source, const ContainerSlotRef& target);
bool applyInventorySwap(LocalPlayer& player,int a,int b,TransferReceipt receipt,const TransferSnapshot& snapshot);
TransferReceipt containerTransferReceipt(const void* response, const ContainerSlotRef& source, const ContainerSlotRef& target);
std::vector<ContainerView> openContainerViews(LocalPlayer& player);
ContainerSlotRef inventorySlotRef(int slot);
ContainerSlotRef containerSlotRef(LocalPlayer& player, const std::string& collection, int slot);
const ItemStack* containerItem(LocalPlayer& player, const ContainerSlotRef& slot);
int sendContainerTransfer(LocalPlayer& player, const ContainerSlotRef& source, const ContainerSlotRef& target, int count);
bool applyContainerTransfer(LocalPlayer& player, const ContainerSlotRef& source, const ContainerSlotRef& target, int count, int sourceBefore, int targetBefore, TransferReceipt receipt, const TransferSnapshot& snapshot);
}
