#pragma once
#include <cstddef>
#include <cstdint>

// Member offsets and vtable indices of the game classes, for Minecraft Bedrock 26.52
// (package 1.26.5203.0). They move with updates: keep them all here and re-verify them
// with `#debug` after every update.
namespace offsets {

// Platform_GameCore global -> object with Platform_GameCore* at +0x8
inline constexpr std::ptrdiff_t WinMain_platform = 0x8;
inline constexpr std::ptrdiff_t Platform_gameHolder = 0x118;
inline constexpr std::ptrdiff_t GameHolder_minecraftGame = 0x48;
// std::map<uint8_t, {void*, shared_ptr<ClientInstance>}>
inline constexpr std::ptrdiff_t MinecraftGame_clientInstances = 0x970;

inline constexpr std::ptrdiff_t MinecraftGame_cursorGrabbed = 0x1E8;  // bool

inline constexpr std::ptrdiff_t ClientInstance_minecraftGame = 0x1A8;
inline constexpr std::size_t ClientInstance_vt_getRegion = 0x1E;
inline constexpr std::size_t ClientInstance_vt_getLocalPlayer = 0x1F;
inline constexpr std::ptrdiff_t ClientInstance_guiData = 0x650;

inline constexpr std::ptrdiff_t Actor_entityContext = 0x8;
inline constexpr std::ptrdiff_t Actor_dimension = 0x1C8;  // std::shared_ptr<Dimension>
inline constexpr std::ptrdiff_t Actor_stateVector = 0x218;
inline constexpr std::ptrdiff_t Actor_aabbShape = 0x220;
inline constexpr std::ptrdiff_t Actor_rotation = 0x228;

inline constexpr std::ptrdiff_t Player_supplies = 0x5B8;  // PlayerInventory*
inline constexpr std::ptrdiff_t Player_gameMode = 0xAA0;
inline constexpr std::ptrdiff_t Player_name = 0xBC0;  // std::string

inline constexpr std::ptrdiff_t PlayerInventory_selectedSlot = 0x10;
inline constexpr std::ptrdiff_t PlayerInventory_inventory = 0xB8;
inline constexpr std::size_t Inventory_vt_getItem = 7;
inline constexpr std::size_t Inventory_vt_setItem = 0xC;
inline constexpr std::ptrdiff_t MovementAbilities_flags = 0x4;  // bit 0: flying  // Container::setItem(int, ItemStack const&)
inline constexpr std::size_t ItemStack_size = 0x98;       // vtable at +0, virtual destructor first

// Server-authoritative inventory (ItemStackRequest), see sdk::sendInventoryMove
inline constexpr std::ptrdiff_t Player_itemStackNetManager = 0xAB8;          // ItemStackNetManagerClient*
inline constexpr std::ptrdiff_t ItemStackNetManager_enabled = 0x9;           // bool
inline constexpr std::ptrdiff_t ItemStackNetManager_currentRequest = 0x60;   // unique_ptr<ItemStackRequestData>
inline constexpr std::ptrdiff_t ItemStack_userData = 0x10;     // CompoundTag* (Damage, ench...)
inline constexpr std::ptrdiff_t Item_maxDamage = 0x150;        // short, Item vtable 0x24 getMaxDamage
inline constexpr std::ptrdiff_t ItemStack_netIdVariant = 0x80;               // ItemStackNetIdVariant (0x18 bytes)
inline constexpr std::size_t ItemStackRequestTransfer_size = 0x68;
inline constexpr std::ptrdiff_t ItemStackRequestTransfer_src = 0x18;         // ItemStackRequestSlotInfo (0x28 bytes)
inline constexpr std::ptrdiff_t ItemStackRequestTransfer_dst = 0x40;
inline constexpr uint8_t ContainerType_Armor = 18;
// AttributesComponent (BaseAttributeMap as a flat map): vector<uint32> ids at +0, vector of
// AttributeInstance (0x88 bytes) at +0x18. AttributeInstance: +0x08 const Attribute*, floats
// default {min, max, value} at +0x68, current {min, max, value} at +0x74. Attribute: +0x08
// HashedString {u64 hash, std::string name}.
inline constexpr std::ptrdiff_t Attributes_instances = 0x18;
// MobEffectsComponent: vector<MobEffectInstance> indexed by effect id (0x90 bytes each; id 0 =
// no effect). MobEffectInstance: +0 u32 id, +4 int duration (ticks), +0x20 int amplifier.
inline constexpr std::size_t MobEffectInstance_size = 0x90;
inline constexpr std::ptrdiff_t MobEffectInstance_duration = 0x04;
inline constexpr std::ptrdiff_t MobEffectInstance_amplifier = 0x20;
inline constexpr std::size_t AttributeInstance_size = 0x88;
inline constexpr std::ptrdiff_t AttributeInstance_attribute = 0x08;
inline constexpr std::ptrdiff_t AttributeInstance_current = 0x74;
inline constexpr std::ptrdiff_t Attribute_name = 0x10;  // Container+8 (ContainerType), 19 = hand
inline constexpr uint8_t ContainerEnumName_Hotbar = 28;                      // network values
inline constexpr uint8_t ContainerEnumName_Inventory = 29;

inline constexpr std::size_t BlockSource_vt_getBlockLayer = 1;  // getBlock(BlockPos const&, uint32 layer)
inline constexpr std::size_t BlockSource_vt_getBlock = 2;       // getBlock(BlockPos const&)
inline constexpr std::size_t BlockSource_vt_hasChunksAt = 0x10;  // hasChunksAt(BlockPos const&, int radius, bool)
inline constexpr std::ptrdiff_t BlockSource_maxHeight = 0x38;   // int16
inline constexpr std::ptrdiff_t BlockSource_minHeight = 0x3A;   // int16

inline constexpr std::ptrdiff_t Block_legacy = 0x68;
inline constexpr std::ptrdiff_t Block_serializationId = 0xF0;  // CompoundTag {name, states, version}
inline constexpr std::ptrdiff_t BlockLegacy_namespacedId = 0xE0;  // HashedString

inline constexpr std::ptrdiff_t Item_namespacedId = 0x120;  // HashedString

inline constexpr std::ptrdiff_t Dimension_name = 0x20;

inline constexpr std::ptrdiff_t Level_levelData = 0x90;      // std::shared_ptr<LevelData>
inline constexpr std::ptrdiff_t LevelData_levelName = 0x2A8;  // std::string

// std::vector<Actor*> Level::getRuntimeActorList() const (returned by hidden pointer)
inline constexpr std::size_t Level_vt_getRuntimeActorList = 0x145;
// HitResult* (what the crosshair points at), returned by Level vtable 0x14F getHitResult
inline constexpr std::ptrdiff_t Level_hitResult = 0x1E8;

inline constexpr std::ptrdiff_t ItemActor_stack = 0x3B0;  // ItemStack of a dropped item entity

inline constexpr uint32_t ActorType_Player = 319;
inline constexpr uint32_t ActorType_ItemEntity = 64;

}  // namespace offsets
