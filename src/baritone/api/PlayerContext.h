#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "baritone/utils/BlockPos.h"
#include "baritone/utils/Rotation.h"
#include "baritone/world/BlockProps.h"

namespace baritone {

// One hotbar slot as the bot sees it.
struct HotbarStack {
    std::string name;  // "minecraft:diamond_pickaxe", empty if no item
    int count = 0;
    bool isBlock = false;  // places a block
    Tool tool = Tool::None;
    int toolTier = 0;      // 0 none, 1 wood/gold, 2 stone, 3 iron, 4 diamond, 5 netherite
    float toolSpeed = 1;   // vanilla destroy speed multiplier of the tool
    int damage = 0;        // durability used
    int maxDamage = 0;     // 0 = no durability
    int efficiency = 0;    // enchantment levels
    bool silkTouch = false;
    int frostWalker = 0;   // boots
    bool empty() const { return count == 0 || name.empty(); }
};

// An entity of the world, copied (safe to keep after the tick).
struct EntityInfo {
    uint64_t id = 0;       // runtime id
    std::string type;      // "minecraft:zombie", "minecraft:player", "minecraft:item"
    std::string name;      // player name (players only)
    std::string itemName;  // dropped items: "minecraft:diamond"
    int itemCount = 0;
    Vec3d position;        // feet
    bool isPlayer = false;
    bool isItem = false;
};

// Everything the bot reads from or does to the local player (Baritone's IPlayerContext +
// PlayerController). Implemented on the game side; only used on the game thread.
class PlayerContext {
public:
    virtual ~PlayerContext() = default;

    virtual bool valid() const = 0;
    virtual Vec3d position() const = 0;     // feet: bottom center of the bounding box
    virtual Vec3d eyePosition() const = 0;  // camera height while standing
    virtual Vec3d velocity() const = 0;
    virtual Rotation rotation() const = 0;  // current rotation of the player
    virtual bool onGround() const = 0;
    virtual bool isSneaking() const = 0;
    virtual bool isInWater() const = 0;
    virtual double blockReachDistance() const = 0;

    // Hotbar (slots 0-8) and main inventory (9-35)
    virtual int selectedSlot() const = 0;
    virtual void selectSlot(int slot) = 0;
    virtual HotbarStack hotbar(int slot) const = 0;  // any slot: 0-8 hotbar, 9-35 main inventory
    // worn armor: 0 head, 1 chest, 2 legs, 3 feet (Java getItemBySlot)
    virtual HotbarStack armor(int /*slot*/) const { return {}; }
    // Swaps two inventory slots through the server (Baritone's windowClick SWAP). False if
    // the request could not be sent; the hotbar changes a few ticks later.
    virtual bool swapSlots(int /*slotA*/, int /*slotB*/) { return false; }
    // A move was sent and the server has not answered yet: the slots are about to change.
    virtual bool inventoryMovePending() const { return false; }
    // Elytra: gliding right now. Using the held item in the air (a firework) is not supported
    // on Bedrock yet: the game's own right click path has to be found (calling GameMode::useItem
    // only fires the rocket on the client, the server rubber-bands the player).
    virtual bool isGliding() const { return false; }
    virtual bool isFlying() const { return false; }  // creative flight
    virtual bool isCreative() const { return false; }  // blocks break instantly
    virtual int foodLevel() const { return 20; }       // hunger 0-20
    // Amplifier (0 = level I) of an active effect ("haste", "mining_fatigue"), -1 if none.
    virtual int effectAmplifier(std::string_view) const { return -1; }
    virtual bool useHeldItem() { return false; }
    // Uses the held item on the first block the game targets, checked every frame, for about
    // the next tick (call it every tick to keep it on). Water bucket clutch: at full speed a
    // fall crosses the reach in one tick.
    virtual void useHeldItemWhenTargeting() {}
    virtual bool slotChangePending() const { return false; }
    // Number of items named `name` in the whole inventory.
    virtual int countItem(std::string_view name) const = 0;

    // Loaded entities except the local player.
    virtual std::vector<EntityInfo> entities() const = 0;

    // Interaction. `face` is the face of `pos` being clicked.
    // true when the block broke after several ticks of hitting (Java hasBrokenBlock after damage)
    virtual bool startOrContinueBreaking(const BlockPos& pos, Direction face) = 0;
    virtual void stopBreaking() = 0;
    virtual void useItemOn(const BlockPos& pos, Direction face, const Vec3d& hit) = 0;  // place / open doors
    virtual void swingArm() = 0;

    // Re-read the live world around a position into the snapshot before deciding.
    virtual void refreshWorld(const BlockPos& center, int radius) = 0;
};

}  // namespace baritone
