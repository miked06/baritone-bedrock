#pragma once
// Thin views over the game objects we use. Classes are opaque: every access goes through
// an offset or a vtable index from sdk/Offsets.h.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "memory/Memory.h"
#include "sdk/Components.h"
#include "sdk/Math.h"
#include "sdk/Nbt.h"
#include "sdk/Offsets.h"

// Our std::string must have the same layout as the game's (MSVC STL, no iterator debugging).
static_assert(sizeof(std::string) == 32, "Build in Release/RelWithDebInfo: the game's std::string is 32 bytes");

namespace sdk {

struct HashedString {
    uint64_t hash;
    std::string str;
    const HashedString* lastMatch;
};

enum class Face : uint8_t { Down = 0, Up = 1, North = 2, South = 3, West = 4, East = 5 };

class BlockLegacy {
public:
    // e.g. "minecraft:stone"
    const std::string& name() const { return mem::member<HashedString>(this, offsets::BlockLegacy_namespacedId).str; }
};

class Block {
public:
    BlockLegacy* legacy() const { return mem::member<BlockLegacy*>(this, offsets::Block_legacy); }
    // {"name": "minecraft:stone_slab", "states": {"minecraft:vertical_half": "top", ...}, "version": ...}
    const nbt::CompoundTag& serializationId() const {
        return mem::member<nbt::CompoundTag>(this, offsets::Block_serializationId);
    }
    const nbt::CompoundTag* states() const { return serializationId().compound("states"); }
};

class BlockSource {
public:
    const Block* getBlock(const BlockPos& pos) const {
        return mem::callVirtual<const Block*, const BlockPos&>(this, offsets::BlockSource_vt_getBlock, pos);
    }
    // layer 1 = the "extra" block (water in waterlogged blocks)
    const Block* getBlock(const BlockPos& pos, uint32_t layer) const {
        return mem::callVirtual<const Block*, const BlockPos&, uint32_t>(this, offsets::BlockSource_vt_getBlockLayer,
                                                                         pos, layer);
    }
    bool hasChunksAt(const BlockPos& pos, int radius) const {
        return mem::callVirtual<bool, const BlockPos&, int, bool>(this, offsets::BlockSource_vt_hasChunksAt, pos,
                                                                  radius, false);
    }
    int minHeight() const { return mem::member<int16_t>(this, offsets::BlockSource_minHeight); }
    int maxHeight() const { return mem::member<int16_t>(this, offsets::BlockSource_maxHeight); }
};

class Item {
public:
    const std::string& name() const { return mem::member<HashedString>(this, offsets::Item_namespacedId).str; }
};

class ItemStack {
public:
    Item* item() const {
        auto weak = mem::member<Item**>(this, 0x8);  // WeakPtr<Item>
        return weak ? *weak : nullptr;
    }
    uint8_t count() const { return mem::member<uint8_t>(this, 0x22); }
    bool empty() const { return item() == nullptr || count() == 0; }
    const Block* block() const { return mem::member<const Block*>(this, 0x18); }  // set for block items
    // Durability used (ItemStackBase::getDamageValue) and the item's maximum (0 = no durability).
    int damage() const;
    int maxDamage() const;
    // Enchantments (Bedrock ids: 15 efficiency, 16 silk touch, 17 unbreaking...) -> level.
    std::vector<std::pair<int, int>> enchants() const;
};

class Inventory {
public:
    ItemStack* getItem(int slot) const {
        return mem::callVirtual<ItemStack*, int>(this, offsets::Inventory_vt_getItem, slot);
    }
    // Client-side only: the server must be told separately (see sendInventoryMove).
    void setItem(int slot, const ItemStack& stack) {
        mem::callVirtual<void, int, const ItemStack&>(this, offsets::Inventory_vt_setItem, slot, stack);
    }
};

class PlayerInventory {
public:
    int& selectedSlot() { return mem::member<int>(this, offsets::PlayerInventory_selectedSlot); }
    Inventory* inventory() const { return mem::member<Inventory*>(this, offsets::PlayerInventory_inventory); }
};

// Vtable order of GameMode (index 0 is the destructor).
class GameMode {
public:
    bool startDestroyBlock(const BlockPos& pos, Face face, bool& hasDestroyedBlock) {
        return mem::callVirtual<bool, const BlockPos&, uint8_t, bool&>(this, 1, pos, static_cast<uint8_t>(face),
                                                                       hasDestroyedBlock);
    }
    // 26.52: continueDestroyBlock(pos, face, Vec3 const& playerPos, bool& hasDestroyedBlock)
    bool continueDestroyBlock(const BlockPos& pos, Face face, const Vec3& playerPos, bool& hasDestroyedBlock) {
        return mem::callVirtual<bool, const BlockPos&, uint8_t, const Vec3&, bool&>(
            this, 3, pos, static_cast<uint8_t>(face), playerPos, hasDestroyedBlock);
    }
    void stopDestroyBlock(const BlockPos& pos) { mem::callVirtual<void, const BlockPos&>(this, 4, pos); }
    // Right click on a block face: places the held block, opens doors... (sends an
    // ItemUseInventoryTransaction). 26.52: buildBlock(pos, face, bool offhand?, bool isSimTick);
    // the transaction's trigger type is isSimTick + 1 (1 = player input). A wrong value makes
    // the integrated server drop the connection ("bat" error).
    bool buildBlock(const BlockPos& pos, Face face) {
        return mem::callVirtual<bool, const BlockPos&, uint8_t, bool, bool>(this, 6, pos, static_cast<uint8_t>(face),
                                                                             false, false);
    }
};

class Dimension {
public:
    const std::string& name() const { return mem::member<std::string>(this, offsets::Dimension_name); }
};

class Actor {
public:
    EntityContext& entityContext() { return mem::member<EntityContext>(this, offsets::Actor_entityContext); }

    template <GameComponentType T>
    T* tryGet() {
        return entityContext().tryGet<T>();
    }

    StateVectorComponent* stateVector() const {
        return mem::member<StateVectorComponent*>(this, offsets::Actor_stateVector);
    }
    AABBShapeComponent* aabbShape() const { return mem::member<AABBShapeComponent*>(this, offsets::Actor_aabbShape); }
    ActorRotationComponent* rotation() const {
        return mem::member<ActorRotationComponent*>(this, offsets::Actor_rotation);
    }
    Dimension* dimension() const { return mem::member<std::shared_ptr<Dimension>>(this, offsets::Actor_dimension).get(); }

    uint32_t typeId() {
        auto* c = tryGet<ActorTypeComponent>();
        return c ? c->type : 0;
    }
    bool isPlayer() { return typeId() == offsets::ActorType_Player; }
    bool hasComponent(uint32_t typeHash) { return entityContext().has(typeHash); }
    // ActorEquipmentComponent: unique_ptr<SimpleContainer> hand, armor, ... The armor container
    // (slots 0 head, 1 chest, 2 legs, 3 feet) shares Container::getItem's vtable index with
    // Inventory.
    // A player attribute's current {min, max, value} by name ("minecraft:player.hunger"...).
    struct AttributeValue {
        float min, max, value;
    };
    std::optional<AttributeValue> attribute(std::string_view name) {
        auto* c = static_cast<const uint8_t*>(entityContext().raw(componentHash("AttributesComponent")));
        if (!c || !mem::isReadable(c, offsets::Attributes_instances + 16)) return std::nullopt;
        auto begin = mem::member<const uint8_t*>(c, offsets::Attributes_instances);
        auto end = mem::member<const uint8_t*>(c, offsets::Attributes_instances + 8);
        if (!begin || end < begin || (end - begin) % offsets::AttributeInstance_size != 0 || end - begin > 0x10000) return std::nullopt;
        for (const uint8_t* inst = begin; inst < end; inst += offsets::AttributeInstance_size) {
            if (!mem::isReadable(inst, offsets::AttributeInstance_size)) return std::nullopt;
            auto attr = mem::member<const uint8_t*>(inst, offsets::AttributeInstance_attribute);
            if (!mem::isReadable(attr, offsets::Attribute_name + sizeof(std::string))) continue;
            const std::string& n = mem::member<std::string>(attr, offsets::Attribute_name);
            if (n.size() > 64 || !mem::isReadable(n.data(), n.size())) continue;
            if (n == name) return mem::member<AttributeValue>(inst, offsets::AttributeInstance_current);
        }
        return std::nullopt;
    }
    // Amplifier of an active effect (Bedrock effect id: 3 haste, 4 mining fatigue...), -1 if none.
    int effectAmplifier(uint32_t id) {
        auto* c = static_cast<const uint8_t*>(entityContext().raw(componentHash("MobEffectsComponent")));
        if (!c || !mem::isReadable(c, 16)) return -1;
        auto begin = mem::member<const uint8_t*>(c, 0);
        auto end = mem::member<const uint8_t*>(c, 8);
        if (!begin || end < begin || (end - begin) % offsets::MobEffectInstance_size != 0) return -1;
        const uint8_t* inst = begin + id * offsets::MobEffectInstance_size;
        if (inst >= end || !mem::isReadable(inst, offsets::MobEffectInstance_size)) return -1;
        if (mem::member<uint32_t>(inst, 0) != id || mem::member<int32_t>(inst, offsets::MobEffectInstance_duration) <= 0) return -1;
        return mem::member<int32_t>(inst, offsets::MobEffectInstance_amplifier);
    }
    Inventory* armorContainer() {
        auto* c = static_cast<Inventory* const*>(entityContext().raw(componentHash("ActorEquipmentComponent")));
        if (!c || !mem::isReadable(c + 1, 8)) return nullptr;
        Inventory* armor = c[1];
        if (!mem::isReadable(armor, 16) || mem::member<uint8_t>(armor, 8) != offsets::ContainerType_Armor) return nullptr;
        return armor;
    }
    bool isDead() { return hasComponent(componentHash("IsDeadFlagComponent")); }
    bool isSleeping() { return hasComponent(componentHash("PlayerIsSleepingFlagComponent")); }
    // MovementAbilitiesComponent: flags at +4, bit 0 = flying (creative flight), bits 1-3 set
    // when the player may fly.
    bool isFlying() {
        auto* c = static_cast<const uint8_t*>(entityContext().raw(componentHash("MovementAbilitiesComponent")));
        return c && (c[offsets::MovementAbilities_flags] & 1);
    }
    bool isItem() { return typeId() == offsets::ActorType_ItemEntity; }
    uint64_t runtimeId() {
        auto* c = tryGet<RuntimeIDComponent>();
        return c ? c->runtimeId : 0;
    }
    // "minecraft:zombie"
    std::string identifier() {
        auto* c = tryGet<ActorDefinitionIdentifierComponent>();
        if (!c) return {};
        const auto& id = c->identifier;  // fullName is "minecraft:zombie<initEvent>"
        return (id.nameSpace.empty() ? "minecraft" : id.nameSpace) + ":" + id.identifier;
    }

    // Eye position for players; feet = aabb min y.
    Vec3 position() const { return stateVector()->pos; }
    Vec3 feet() const {
        const AABB& box = aabbShape()->box;
        return {(box.min.x + box.max.x) * 0.5f, box.min.y, (box.min.z + box.max.z) * 0.5f};
    }
};

class Player : public Actor {
public:
    PlayerInventory* supplies() const { return mem::member<PlayerInventory*>(this, offsets::Player_supplies); }
    GameMode* gameMode() const { return mem::member<GameMode*>(this, offsets::Player_gameMode); }
    const std::string& name() const { return mem::member<std::string>(this, offsets::Player_name); }
    MoveInputComponent* moveInput() { return tryGet<MoveInputComponent>(); }
};

class LocalPlayer : public Player {};

// Frees memory allocated by the game (its operator delete / custom allocator).
void gameFree(void* ptr);
// Allocates with the game's allocator (for objects the game will delete).
void* gameAlloc(std::size_t size);

// Asks the server to move slotA's stack to slotB (0-8 hotbar, 9-35 main inventory), swapping
// them when slotB is not empty: an ItemStackRequest like the inventory screen's. Returns the
// request id (negative) or 0 if nothing was sent. The client's slots do not change: call
// applyInventoryMoveLocally once the server accepted it (predicting first makes it refuse).
int sendInventoryMove(LocalPlayer& player, int slotA, int slotB);
// Same move on the client's inventory only. Tick only: item stacks need the game's item
// registry, which is only reachable during the tick.
bool applyInventoryMoveLocally(LocalPlayer& player, int slotA, int slotB);

// What the crosshair points at (the game's pick), used by its mouse clicks.
struct HitResult {
    Vec3 start;
    Vec3 end;
    int32_t type;  // 0 block, 1 entity, 3 nothing
    uint8_t face;
    BlockPos block;
    Vec3 pos;
};

class Level {
public:
    // Every actor loaded on the client.
    std::vector<Actor*> actors() const;
    const HitResult* hitResult() const { return mem::member<const HitResult*>(this, offsets::Level_hitResult); }
    // Name of the world (empty if unreadable).
    std::string name() const;
};

struct Vec4 {
    float x, y, z, w;
};

// The camera entity component read by UpdatePlayerFromCameraSystemUtil::_updatePlayer.
struct CameraComponent {
    char pad[0x30];
    Vec4 lookAngles;  // orientation quaternion
    Vec3 position;
};

// pitch/yaw in degrees <-> the camera quaternion convention of the game
Vec4 rotationToQuaternion(float pitch, float yaw);

class GuiData {
public:
    void displayClientMessage(const std::string& message);
};

class ClientInstance {
public:
    // Primary client instance, or nullptr while the game is starting.
    static ClientInstance* get();

    BlockSource* region() const { return mem::callVirtual<BlockSource*>(this, offsets::ClientInstance_vt_getRegion); }
    LocalPlayer* localPlayer() const {
        return mem::callVirtual<LocalPlayer*>(this, offsets::ClientInstance_vt_getLocalPlayer);
    }
    GuiData* guiData() const { return mem::member<GuiData*>(this, offsets::ClientInstance_guiData); }
    void* minecraftGame() const { return mem::member<void*>(this, offsets::ClientInstance_minecraftGame); }
    // The game only reads movement keys while the cursor is grabbed (in game, window focused).
    bool& cursorGrabbedFlag() const { return mem::member<bool>(minecraftGame(), offsets::MinecraftGame_cursorGrabbed); }
};

}  // namespace sdk
