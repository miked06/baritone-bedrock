#pragma once
#include <optional>
#include <vector>

#include "baritone/api/PlayerContext.h"

namespace sdk {
class LocalPlayer;
class BlockSource;
class Level;
}  // namespace sdk

namespace platform {

// PlayerContext over the real game objects. Bound to the local player each tick.
class GamePlayerContext : public baritone::PlayerContext {
public:
    // Called once per tick.
    void bind(sdk::LocalPlayer* player, sdk::BlockSource* region) {
        if (player != player_) {  // a new (or no) player: nothing is being broken or moved
            breaking_.reset();
            moves_.clear();
        }
        player_ = player;
        region_ = region;
        ++tick_;
        if (player_ && !moves_.empty()) applyAnsweredMoves();
        if (player_ && !clickRefreshes_.empty()) refreshClickedBlocks();
    }

    // The game tells the server about a new selected slot during the next player tick
    // (MobEquipmentPacket). Using an item before that makes the server reject the
    // transaction and disconnect us, so clicks wait for this to be false.
    bool slotChangePending() const override { return tick_ - slotChangedAt_ < 3; }
    const std::optional<baritone::BlockPos>& breakingPos() const { return breaking_; }

    bool valid() const override { return player_ != nullptr; }
    baritone::Vec3d position() const override;
    baritone::Vec3d eyePosition() const override;
    baritone::Vec3d velocity() const override;
    baritone::Rotation rotation() const override;
    bool onGround() const override;
    bool isSneaking() const override;
    bool isInWater() const override;
    double blockReachDistance() const override;

    int selectedSlot() const override;
    void selectSlot(int slot) override;
    baritone::HotbarStack hotbar(int slot) const override;
    baritone::HotbarStack armor(int slot) const override;
    int foodLevel() const override;
    int effectAmplifier(std::string_view effect) const override;
    bool swapSlots(int slotA, int slotB) override;
    bool inventoryMovePending() const override { return !moves_.empty(); }
    bool isGliding() const override;
    bool isFlying() const override;
    bool isCreative() const override;
    bool useHeldItem() override;
    void useHeldItemWhenTargeting() override;
    // From the response hook: the server's answer to an inventory request.
    void onItemStackResponse(int requestId, int result);
    int countItem(std::string_view name) const override;
    std::vector<baritone::EntityInfo> entities() const override;
    void setLevel(sdk::Level* level) { level_ = level; }
    sdk::Level* level() const { return level_; }

    bool startOrContinueBreaking(const baritone::BlockPos& pos, baritone::Direction face) override;
    void stopBreaking() override;
    void useItemOn(const baritone::BlockPos& pos, baritone::Direction face, const baritone::Vec3d& hit) override;
    void swingArm() override {}

    void refreshWorld(const baritone::BlockPos& center, int radius) override;

private:
    sdk::LocalPlayer* player_ = nullptr;
    sdk::BlockSource* region_ = nullptr;
    int hitTicks_ = 0;  // ticks spent hitting breaking_
    sdk::Level* level_ = nullptr;
    std::optional<baritone::BlockPos> breaking_;
    uint64_t tick_ = 100;
    uint64_t slotChangedAt_ = 0;

    // Inventory moves sent to the server, applied to the client's slots once accepted
    struct Move {
        int requestId, slotA, slotB;
        uint64_t sentAt;
        int result = -1;  // -1: no answer yet
    };
    std::vector<Move> moves_;
    void applyAnsweredMoves();

    // Blocks right clicked through the mouse: the game changes them a few ticks later, so the
    // world snapshot reads them again then
    std::vector<std::pair<baritone::BlockPos, uint64_t>> clickRefreshes_;
    void refreshClickedBlocks();
};

}  // namespace platform
