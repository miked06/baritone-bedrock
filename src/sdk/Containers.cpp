#include "sdk/Containers.h"
#include <Windows.h>
#include <cstring>
#include <memory>
#include <unordered_map>
#include "memory/Signatures.h"
#include "core/Log.h"
#include "baritone/api/TransferReconciliation.h"

namespace sdk {
namespace {
// Verified in 1.26.6005.0, not inferred from the newer generated headers:
// manager map +C8 (list head at +D0); model name +10, FullContainerName +30; model vt4 size,
// vt8 getItemStack, vt13 setItem. See docs/NATIVE_MODULE_NOTES.md.
using Models = std::unordered_map<std::string, std::shared_ptr<void>>;
static_assert(sizeof(Models) == 64);
const Models* models(LocalPlayer& player) {
    auto* manager = player.containerManager();
    if (!mem::isReadable(manager, 0x110)) return nullptr;
    const auto* result = reinterpret_cast<const Models*>(static_cast<const uint8_t*>(manager) + 0xC8);
    return result->size() <= 128 ? result : nullptr;
}
void* modelFor(LocalPlayer& player, const ContainerSlotRef& ref) {
    auto* map = models(player); if (!map) return nullptr;
    for (const auto& [name, model] : *map) {
        auto* pointer = model.get();
        if (!mem::isReadable(pointer, 0x110)) continue;
        if (std::memcmp(static_cast<const uint8_t*>(pointer) + 0x30, ref.name.data(), 1) != 0) continue;
        // Only the active optional dynamic id is meaningful; padding is ignored.
        const auto* full = static_cast<const uint8_t*>(pointer) + 0x30;
        if (bool(full[8]) != bool(ref.name[8])) continue;
        if (full[8] && std::memcmp(full + 4, ref.name.data() + 4, 4) != 0) continue;
        int size = mem::callVirtual<int>(pointer, 4);
        const int index=ref.modelSlot>=0 ? ref.modelSlot : ref.slot;
        if (size >= 0 && size <= 256 && index >= 0 && index < size) return pointer;
    }
    return nullptr;
}
void writeItem(LocalPlayer& player, const ContainerSlotRef& ref, const ItemStack& item) {
    if (ref.name[0] == 28 || ref.name[0] == 29) {
        auto* inventory = player.supplies()->inventory();
        if (inventory) inventory->setItem(ref.slot, item);
    } else if (auto* model = modelFor(player, ref)) mem::callVirtual<void, int, const ItemStack&>(model, 13, ref.modelSlot>=0 ? ref.modelSlot : ref.slot, item);
}
struct Copy {
    alignas(16) uint8_t bytes[offsets::ItemStack_size];
    explicit Copy(const ItemStack& source) {
        sigs::ItemStackBase_copy.as<void*(__fastcall*)(void*, const void*)>()(bytes, &source);
        std::memcpy(bytes, &source, 8);
        std::memcpy(bytes + offsets::ItemStack_netIdVariant, reinterpret_cast<const uint8_t*>(&source) + offsets::ItemStack_netIdVariant, 0x18);
    }
    ~Copy() { mem::callVirtual<void*, uint32_t>(bytes, 0, 0u); }
    void count(int n) { bytes[0x22] = static_cast<uint8_t>(n); }
    void netId(uint32_t id) { std::memset(bytes + offsets::ItemStack_netIdVariant, 0, 0x18); std::memcpy(bytes + offsets::ItemStack_netIdVariant, &id, sizeof(id)); }
    const ItemStack& item() const { return *reinterpret_cast<const ItemStack*>(bytes); }
};
const ItemStack* emptyStack() {
    // Unique initialized ItemStack with null item/data and count zero in the
    // 1.26.6005.0 image. Native code uses it as its empty-stack fallback.
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto* empty = reinterpret_cast<const ItemStack*>(base + 0x1293AE90);
    if (!mem::isReadable(empty, offsets::ItemStack_size) ||
        *reinterpret_cast<const uintptr_t*>(empty) != base + 0xEFF9E80 ||
        mem::member<uintptr_t>(empty, 8) || mem::member<uintptr_t>(empty, 0x10) || empty->count()) return nullptr;
    return empty;
}
}
std::vector<ContainerView> openContainerViews(LocalPlayer& player) {
    std::vector<ContainerView> result;
    if (const auto* map = models(player)) for (const auto& [name, model] : *map) {
        auto* pointer = model.get(); if (!mem::isReadable(pointer, 0x110) || name.size() > 128) continue;
        int size = mem::callVirtual<int>(pointer, 4);
        if (size >= 0 && size <= 256) result.push_back({name, mem::member<uint8_t>(pointer, 0x30), size});
    }
    return result;
}
ContainerSlotRef inventorySlotRef(int slot) {
    ContainerSlotRef result; result.name[0] = slot < 9 ? 28 : 29; result.slot = slot; return result;
}
ContainerSlotRef containerSlotRef(LocalPlayer& player, const std::string& collection, int slot) {
    auto* map = models(player);
    if (!map) throw std::invalid_argument("No readable container screen is open");
    auto entry = map->find(collection);
    if (entry == map->end() || !mem::isReadable(entry->second.get(), 0x110)) throw std::invalid_argument("Container collection not available");
    int size = mem::callVirtual<int>(entry->second.get(), 4);
    if (slot < 0 || slot >= size || size > 256) throw std::invalid_argument("Container slot outside supported bounds");
    ContainerSlotRef result; result.slot = slot; result.modelSlot=slot;
    std::memcpy(result.name.data(), static_cast<uint8_t*>(entry->second.get()) + 0x30, 12);
    // Network indices differ from the one-slot UI model indices. Anvil inputs
    // use 1/2; grindstone uses shared-UI slots 16/17; furnace uses 0/1/2.
    if (result.name[0]==24 || result.name[0]==0) result.slot+=1;
    else if (result.name[0]==26 || result.name[0]==1) result.slot+=2;
    else if (result.name[0]==50) result.slot+=16;
    else if (result.name[0]==51) result.slot+=17;
    return result;
}
const ItemStack* containerItem(LocalPlayer& player, const ContainerSlotRef& ref) {
    if (ref.name[0] == 28 || ref.name[0] == 29) {
        if (ref.slot < 0 || ref.slot >= 36) return nullptr;
        auto* inventory = player.supplies()->inventory(); return inventory ? inventory->getItem(ref.slot) : nullptr;
    }
    auto* model = modelFor(player, ref);
    return model ? &mem::callVirtual<const ItemStack&, int>(model, 8, ref.modelSlot>=0 ? ref.modelSlot : ref.slot) : nullptr;
}
int sendContainerTransfer(LocalPlayer& player, const ContainerSlotRef& source, const ContainerSlotRef& target, int count) {
    // Workstation previews require the native create/consume/XP operation.
    if (source.name[0] == 2 || target.name[0] == 2 || source.name[0] == 52 || target.name[0] == 52) return 0;
    auto* from = containerItem(player, source); auto* to = containerItem(player, target);
    if (!from || !to || from->empty() || count < 1 || count > from->count() || source == target ||
        (!to->empty() && !from->mergeCompatible(*to)) || count + (to->empty() ? 0 : to->count()) > from->maxStackSize()) return 0;
    if (!to->empty() && count == from->count() && !emptyStack()) return 0;
    void* manager = mem::member<void*>(&player, offsets::Player_itemStackNetManager);
    if (!mem::isReadable(manager, 0x70) || !mem::member<bool>(manager, offsets::ItemStackNetManager_enabled) || mem::member<void*>(manager, offsets::ItemStackNetManager_currentRequest)) return 0;
    auto* action = static_cast<uint8_t*>(gameAlloc(offsets::ItemStackRequestTransfer_size)); if (!action) return 0;
    std::memset(action, 0, offsets::ItemStackRequestTransfer_size);
    *reinterpret_cast<uintptr_t*>(action) = sigs::ItemStackRequestTransfer_vtable.address;
    action[8] = 1; action[0x10] = action[0x11] = 1; action[0x12] = static_cast<uint8_t>(count);
    auto fill = [](uint8_t* info, const ContainerSlotRef& ref, const ItemStack* stack) {
        std::memcpy(info, ref.name.data(), 12); info[0xC] = static_cast<uint8_t>(ref.slot);
        std::memcpy(info + 0x10, reinterpret_cast<const uint8_t*>(stack) + offsets::ItemStack_netIdVariant, 0x18);
    };
    fill(action + offsets::ItemStackRequestTransfer_src, source, from); fill(action + offsets::ItemStackRequestTransfer_dst, target, to);
    sigs::ItemStackNet_beginRequest.as<void(__fastcall*)(void*, bool)>()(manager, false);
    auto* request = mem::member<void*>(manager, offsets::ItemStackNetManager_currentRequest);
    int id = request ? mem::member<int32_t>(request, 8) : 0;
    void* owned = action;
    sigs::ItemStackNet_addRequestAction.as<void(__fastcall*)(void*, void**)>()(manager, &owned);
    sigs::ItemStackNet_endRequest.as<void(__fastcall*)(void*)>()(manager);
    return id;
}
TransferReceipt containerTransferReceipt(const void* response, const ContainerSlotRef& source, const ContainerSlotRef& target) {
    TransferReceipt receipt;
    if (!mem::isReadable(response, 0x30)) return receipt;
    const auto* first = mem::member<const uint8_t*>(response, 0x18);
    const auto* last = mem::member<const uint8_t*>(response, 0x20);
    if (!first || last < first || (last - first) % 40 || last - first > 128 * 40 || !mem::isReadable(first, last - first)) return receipt;
    bool a = false, b = false;
    for (auto* container = first; container < last; container += 40) {
        const auto* begin = mem::member<const uint8_t*>(container, 16);
        const auto* end = mem::member<const uint8_t*>(container, 24);
        if (!begin || end < begin || (end - begin) % 88 || end - begin > 256 * 88 || !mem::isReadable(begin, end - begin)) continue;
        auto matches = [&](const ContainerSlotRef& ref, const uint8_t* slot) {
            bool inventory = (ref.name[0] == 12 || ref.name[0] == 28 || ref.name[0] == 29) && (container[0] == 12 || container[0] == 28 || container[0] == 29);
            return (inventory || container[0] == ref.name[0]) && slot[0] == ref.slot &&
                bool(container[8]) == bool(ref.name[8]) && (!container[8] || std::memcmp(container + 4, ref.name.data() + 4, 4) == 0);
        };
        for (auto* slot = begin; slot < end; slot += 88) {
            logger::debug("Transfer receipt container {} dynamic {}:{} slot {} count {} netId {} (wanted {}:{} -> {}:{})",
                container[0], container[8], mem::member<uint32_t>(container, 4), slot[0], slot[2], mem::member<uint32_t>(slot, 4),
                source.name[0], source.slot, target.name[0], target.slot);
            if (matches(source, slot)) { a = true; receipt.sourceCount = slot[2]; std::memcpy(&receipt.sourceId, slot + 4, 4); }
            if (matches(target, slot)) { b = true; receipt.targetCount = slot[2]; std::memcpy(&receipt.targetId, slot + 4, 4); }
        }
    }
    receipt.valid = a && b; return receipt;
}
TransferSnapshot snapshotContainerTransfer(LocalPlayer& player, const ContainerSlotRef& source, const ContainerSlotRef& target) {
    auto* from = containerItem(player, source); auto* to = containerItem(player, target);
    if (!from || !to) return {};
    return {std::make_shared<Copy>(*from), std::make_shared<Copy>(*to)};
}
bool applyInventorySwap(LocalPlayer& player,int a,int b,TransferReceipt receipt,const TransferSnapshot& snapshot) {
    if (!snapshot.source || !snapshot.target || !receipt.valid) return false;
    const auto& from=static_cast<const Copy*>(snapshot.source.get())->item();
    const auto& to=static_cast<const Copy*>(snapshot.target.get())->item();
    if (receipt.sourceCount!=(to.empty() ? 0 : to.count()) || receipt.targetCount!=(from.empty() ? 0 : from.count())) return false;
    Copy swappedA(to),swappedB(from);
    swappedA.netId(receipt.sourceId); swappedB.netId(receipt.targetId);
    writeItem(player,inventorySlotRef(a),swappedA.item()); writeItem(player,inventorySlotRef(b),swappedB.item());
    return true;
}
bool applyContainerTransfer(LocalPlayer& player, const ContainerSlotRef& source, const ContainerSlotRef& target, int count, int sourceBefore, int targetBefore, TransferReceipt receipt, const TransferSnapshot& snapshot) {
    logger::debug("Transfer reconcile valid {} receipt {} -> {}, expected {} -> {}", receipt.valid, receipt.sourceCount, receipt.targetCount, sourceBefore - count, targetBefore + count);
    if (!snapshot.source || !snapshot.target || !receipt.valid || receipt.sourceCount != sourceBefore - count ||
        !baritone::transferTargetReceiptValid(target.name[0],targetBefore+count,receipt.targetCount)) return false;
    auto* from = containerItem(player, source); auto* to = containerItem(player, target);
    if (!from || !to) return false;
    logger::debug("Transfer local counts {} -> {}, empty {} -> {} (before {} -> {})", from->count(), to->count(), from->empty(), to->empty(), sourceBefore, targetBefore);
    const auto& original = static_cast<const Copy*>(snapshot.source.get())->item();
    const auto& originalTarget = static_cast<const Copy*>(snapshot.target.get())->item();
    if (sourceBefore != original.count() || targetBefore != (originalTarget.empty() ? 0 : originalTarget.count()) ||
        (targetBefore && !original.mergeCompatible(originalTarget))) return false;
    const auto* empty = originalTarget.empty() ? &originalTarget : emptyStack();
    if (sourceBefore == count && !empty) return false;
    auto compatible = [&](const ItemStack& stack, int before, int after) {
        const bool same = original.plainStackKey().empty() ? stack.item() == original.item() : stack.plainStackKey() == original.plainStackKey();
        return baritone::transferSideCompatible(stack.empty(), same, stack.count(), before, after);
    };
    // External container models receive native server updates independently of the
    // player's inventory. Either side can already be updated when this tick runs.
    const auto inventory=[](const ContainerSlotRef& slot) { return slot.name[0]==28 || slot.name[0]==29; };
    if ((inventory(source) && !compatible(*from, sourceBefore, receipt.sourceCount)) || (inventory(target) && !compatible(*to, targetBefore, receipt.targetCount))) return false;
    Copy a(sourceBefore == count ? *empty : original), b(originalTarget.empty() ? original : originalTarget);
    a.count(receipt.sourceCount); b.count(receipt.targetCount); a.netId(receipt.sourceId); b.netId(receipt.targetId);
    // External models receive the game's own authoritative updates. Rewriting
    // furnace input/fuel here could undo a consumption that happened after the
    // receipt; only the player's omitted local prediction needs reconciliation.
    if (inventory(source)) writeItem(player, source, a.item());
    if (inventory(target)) writeItem(player, target, b.item());
    return true;
}
}
