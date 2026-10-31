#pragma once
#include <array>
#include <optional>
#include "baritone/api/PlayerContext.h"

namespace test {
// A player with a real inventory model for core behavior/process regressions.
class TestPlayer : public baritone::PlayerContext {
public:
    std::array<baritone::HotbarStack, 36> inventory{};
    std::array<baritone::HotbarStack, 4> equipment{};
    int selected = 0;
    int disconnectRequests = 0;
    bool requestDisconnect() override { ++disconnectRequests; return true; }
    std::optional<std::pair<int, int>> lastSwap;
    bool valid() const override { return true; }
    baritone::Vec3d position() const override { return {0.5, 1, 0.5}; }
    baritone::Vec3d eyePosition() const override { return {0.5, 2.62, 0.5}; }
    baritone::Vec3d velocity() const override { return {}; }
    baritone::Rotation rotation() const override { return {}; }
    bool onGround() const override { return true; }
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
    std::vector<baritone::EntityInfo> entities() const override { return {}; }
    bool startOrContinueBreaking(const baritone::BlockPos&, baritone::Direction) override { return false; }
    void stopBreaking() override {}
    void useItemOn(const baritone::BlockPos&, baritone::Direction, const baritone::Vec3d&) override {}
    void swingArm() override {}
    void refreshWorld(const baritone::BlockPos&, int) override {}
};
}
