#pragma once
#include <array>
#include <optional>
#include "baritone/api/PlayerContext.h"
#include "baritone/inventory/StorageTransfers.h"

namespace test {
// A player with a real inventory model for core behavior/process regressions.
class TestPlayer : public baritone::PlayerContext {
public:
    std::array<baritone::HotbarStack, 36> inventory{};
    std::array<baritone::HotbarStack, 4> equipment{};
    int selected = 0;
    int disconnectRequests = 0;
    baritone::Vec3d feet{0.5, 1, 0.5}, motion{};
    baritone::Rotation facing{};
    std::vector<baritone::EntityInfo> nearby;
    bool grounded = true;
    int useRequests = 0;
    int hunger = 20;
    float hitpoints = 20;
    bool heldUse = false;
    int interactions = 0, attacks = 0;
    bool containerOpened = false, ownedContainer = false, rejectTransfer = false;
    bool pendingInventory = false;
    bool inventoryMovePending() const override { return pendingInventory; }
    baritone::TransferState transfer = baritone::TransferState::Idle;
    std::vector<baritone::ContainerSlot> externalSlots;
    std::vector<baritone::RecipeOption> recipeOptions;
    baritone::TransferState crafting=baritone::TransferState::Idle;
    int craftRequests=0;
    baritone::RecipeOption lastCraft;
    baritone::RepairPreview repairResult;
    baritone::TransferState repairing = baritone::TransferState::Idle;
    bool rejectRepair = false, returnRepairInputs = false;
    int repairRequests = 0;
    baritone::RepairPreview repairPreview() const override { return repairResult; }
    bool takeRepairOutput(const baritone::RepairPreview& preview) override {
        ++repairRequests; repairing = rejectRepair ? baritone::TransferState::Rejected : baritone::TransferState::Accepted;
        if (rejectRepair) return true;
        for (auto& slot : externalSlots) {
            if (slot.networkName == 0 || slot.networkName == 50) slot.stack = {};
            if (slot.networkName == 1 || slot.networkName == 51) slot.stack.count -= preview.materialCost;
        }
        for (auto& slot : inventory) if (slot.empty()) { slot = preview.output; return true; }
        throw std::runtime_error("No free test repair output slot");
    }
    baritone::TransferState repairState() const override { return repairing; }
    std::string repairError() const override { return "Repair refused by test server"; }
    std::vector<baritone::RecipeOption> recipes(std::string_view item = {}) const override {
        std::vector<baritone::RecipeOption> result;
        for (const auto& r:recipeOptions) if (item.empty() || r.item==item) result.push_back(r);
        return result;
    }
    bool craftRecipe(const baritone::RecipeOption& recipe) override { lastCraft=recipe; ++craftRequests; crafting=baritone::TransferState::Pending; return true; }
    baritone::TransferState craftState() const override { return crafting; }
    std::string craftError() const override { return "Craft request refused by test server"; }
    bool interactEntity(uint64_t id) override {
        for (auto& entity : nearby) if (entity.id == id) { ++interactions; entity.inLove = true; return true; }
        return false;
    }
    bool attackEntity(uint64_t id) override {
        for (auto& entity : nearby) if (entity.id == id) { ++attacks; entity.health = 0; entity.dead = true; return true; }
        return false;
    }
    bool containerOpen() const override { return containerOpened; }
    bool ownedContainerReady() const override { return containerOpened && ownedContainer; }
    bool openContainerAt(baritone::BlockPos) override { containerOpened = ownedContainer = true; return true; }
    void closeOwnedContainer() override {
        if (!ownedContainer) return;
        if (returnRepairInputs) for (auto& source : externalSlots) {
            if (source.stack.empty() || (source.networkName != 0 && source.networkName != 1 && source.networkName != 50 && source.networkName != 51)) continue;
            auto dest = std::ranges::find_if(inventory, [&](const auto& stack) {
                return !stack.empty() && stack.name == source.stack.name && stack.damage == source.stack.damage &&
                    stack.enchantments == source.stack.enchantments && stack.customName == source.stack.customName &&
                    stack.count + source.stack.count <= stack.maxStackSize;
            });
            if (dest == inventory.end()) dest = std::ranges::find_if(inventory, [](const auto& stack) { return stack.empty(); });
            if (dest == inventory.end()) throw std::runtime_error("Test repair inputs cannot return");
            if (dest->empty()) *dest = source.stack; else dest->count += source.stack.count;
            source.stack = {};
        }
        containerOpened = ownedContainer = false;
    }
    std::vector<baritone::ContainerSlot> containerSlots() const override {
        auto slots = externalSlots;
        for (int i = 0; i < 36; ++i) slots.push_back({"player", i, true, inventory.at(i)});
        return slots;
    }
    bool transferContainerItem(const baritone::ContainerSlot& a, const baritone::ContainerSlot& b, int count) override {
        transfer = rejectTransfer ? baritone::TransferState::Rejected : baritone::TransferState::Accepted;
        if (rejectTransfer) return true;
        auto stack = [&](const baritone::ContainerSlot& slot) -> baritone::HotbarStack& {
            if (slot.playerInventory) return inventory.at(slot.slot);
            for (auto& item : externalSlots) if (item.collection == slot.collection && item.slot == slot.slot) return item.stack;
            throw std::out_of_range("Unknown test container slot");
        };
        auto& from = stack(a); auto& to = stack(b);
          if (count > from.count || count > baritone::mergeRoom(from, to)) return false;
          if (to.empty()) { to = from; to.count = count; }
          else to.count += count;
          from.count -= count; return true;
    }
    baritone::TransferState transferState() const override { return transfer; }
    std::string transferError() const override { return rejectTransfer ? "Transfer refused by test server" : ""; }
    int foodLevel() const override { return hunger; }
    float health() const override { return hitpoints; }
    bool holdUseItem(bool held) override { heldUse = held; return true; }
    bool useHeldItem() override { ++useRequests; return true; }
    bool requestDisconnect() override { ++disconnectRequests; return true; }
    std::optional<std::pair<int, int>> lastSwap;
    bool valid() const override { return true; }
    baritone::Vec3d position() const override { return feet; }
    baritone::Vec3d eyePosition() const override { return feet + baritone::Vec3d{0, 1.62, 0}; }
    baritone::Vec3d velocity() const override { return motion; }
    baritone::Rotation rotation() const override { return facing; }
    bool onGround() const override { return grounded; }
    bool isSneaking() const override { return false; }
    bool isInWater() const override { return false; }
    double blockReachDistance() const override { return 4.5; }
    int selectedSlot() const override { return selected; }
    void selectSlot(int slot) override { selected = slot; }
    baritone::HotbarStack hotbar(int slot) const override { return inventory.at(slot); }
    baritone::HotbarStack armor(int slot) const override { return equipment.at(slot); }
    bool swapSlots(int a, int b) override {
        lastSwap = {a, b};
        std::swap(inventory.at(a), inventory.at(b));
        return true;
    }
    int countItem(std::string_view name) const override {
        int count = 0;
        for (const auto& item : inventory) if (item.name == name) count += item.count;
        return count;
    }
    std::vector<baritone::EntityInfo> entities() const override { return nearby; }
    bool startOrContinueBreaking(const baritone::BlockPos&, baritone::Direction) override { return false; }
    void stopBreaking() override {}
    void useItemOn(const baritone::BlockPos&, baritone::Direction, const baritone::Vec3d&) override {}
    void swingArm() override {}
    void refreshWorld(const baritone::BlockPos&, int) override {}
};
}
