#include "sdk/Game.h"

#include <cmath>
#include <cstring>
#include <map>
#include <memory>

#include "memory/Signatures.h"

namespace sdk {

namespace {

struct ClientInterface {
    void* clientInterface;
    std::shared_ptr<ClientInstance> instance;
};
static_assert(sizeof(ClientInterface) == 0x18);

}  // namespace

ClientInstance* ClientInstance::get() {
    static ClientInstance* cached = nullptr;
    if (cached) return cached;

    void* winMain = *sigs::PlatformGameCore.as<void**>();
    if (!winMain) return nullptr;
    void* platform = mem::member<void*>(winMain, offsets::WinMain_platform);
    if (!platform) return nullptr;
    void* holder = mem::member<void*>(platform, offsets::Platform_gameHolder);
    if (!holder) return nullptr;
    void* game = mem::member<void*>(holder, offsets::GameHolder_minecraftGame);
    if (!game) return nullptr;

    const auto& clients = mem::member<std::map<uint8_t, ClientInterface>>(game, offsets::MinecraftGame_clientInstances);
    auto it = clients.find(0);
    if (it == clients.end()) return nullptr;
    cached = it->second.instance.get();
    return cached;
}

void gameFree(void* ptr) {
    if (!ptr) return;
    sigs::GameOperatorDelete.as<void(__fastcall*)(void*)>()(ptr);
}

void* gameAlloc(std::size_t size) {
    // operator delete reads the allocator from a global: mov rcx, [rip+global] at +0x15
    uintptr_t del = sigs::GameOperatorDelete.address;
    auto global = reinterpret_cast<void* const*>(del + 0x1C + *reinterpret_cast<const int32_t*>(del + 0x18));
    void* allocator = *global;
    return allocator ? mem::callVirtual<void*, std::size_t>(allocator, 1, size) : nullptr;
}

namespace {

// ItemStackRequestSlotInfo: FullContainerName {name, optional<uint> dynamicId}, slot, net id
void fillSlotInfo(uint8_t* info, int slot, const ItemStack* stack) {
    std::memset(info, 0, 0x28);
    info[0x0] = slot < 9 ? offsets::ContainerEnumName_Hotbar : offsets::ContainerEnumName_Inventory;
    info[0xC] = static_cast<uint8_t>(slot);
    std::memcpy(info + 0x10, reinterpret_cast<const uint8_t*>(stack) + offsets::ItemStack_netIdVariant, 0x18);
}

// A copy of an ItemStack in caller-provided storage (the game's copy: base copy, vtable, net id).
struct StackCopy {
    alignas(16) uint8_t bytes[offsets::ItemStack_size];
    explicit StackCopy(const ItemStack& src) {
        sigs::ItemStackBase_copy.as<void*(__fastcall*)(void*, const void*)>()(bytes, &src);
        std::memcpy(bytes, &src, sizeof(uintptr_t));  // ItemStack vtable
        std::memcpy(bytes + offsets::ItemStack_netIdVariant,
                    reinterpret_cast<const uint8_t*>(&src) + offsets::ItemStack_netIdVariant, 0x18);
    }
    ~StackCopy() { mem::callVirtual<void*, uint32_t>(bytes, 0, 0u); }  // destructor, no delete
    StackCopy(const StackCopy&) = delete;
    StackCopy& operator=(const StackCopy&) = delete;
    const ItemStack& get() const { return *reinterpret_cast<const ItemStack*>(bytes); }
};

}  // namespace

int sendInventoryMove(LocalPlayer& player, int slotA, int slotB) {
    if (slotA < 0 || slotA >= 36 || slotB < 0 || slotB >= 36 || slotA == slotB) return 0;
    PlayerInventory* supplies = player.supplies();
    Inventory* inv = supplies ? supplies->inventory() : nullptr;
    const ItemStack* a = inv ? inv->getItem(slotA) : nullptr;
    const ItemStack* b = inv ? inv->getItem(slotB) : nullptr;
    if (!a || !b || a->empty()) return 0;
    void* manager = mem::member<void*>(&player, offsets::Player_itemStackNetManager);
    if (!mem::isReadable(manager, 0x70) || !mem::member<bool>(manager, offsets::ItemStackNetManager_enabled)) return 0;
    if (mem::member<void*>(manager, offsets::ItemStackNetManager_currentRequest)) return 0;  // one being built

    auto* action = static_cast<uint8_t*>(gameAlloc(offsets::ItemStackRequestTransfer_size));
    if (!action) return 0;
    std::memset(action, 0, offsets::ItemStackRequestTransfer_size);
    *reinterpret_cast<uintptr_t*>(action) = sigs::ItemStackRequestTransfer_vtable.address;
    action[0x10] = 1;
    if (b->empty()) {
        // the server refuses a swap with an empty slot: place the whole stack there instead
        action[0x8] = 1;  // ItemStackRequestActionType::Place
        action[0x11] = 1;
        action[0x12] = a->count();
    } else {
        action[0x8] = 2;  // ItemStackRequestActionType::Swap
    }
    fillSlotInfo(action + offsets::ItemStackRequestTransfer_src, slotA, a);
    fillSlotInfo(action + offsets::ItemStackRequestTransfer_dst, slotB, b);

    sigs::ItemStackNet_beginRequest.as<void(__fastcall*)(void*, bool)>()(manager, false);
    // ItemStackRequestData: request id (int) at +8
    void* request = mem::member<void*>(manager, offsets::ItemStackNetManager_currentRequest);
    int id = request ? mem::member<int32_t>(request, 8) : 0;
    void* owned = action;  // std::unique_ptr passed by value: the callee keeps or deletes it
    sigs::ItemStackNet_addRequestAction.as<void(__fastcall*)(void*, void**)>()(manager, &owned);
    sigs::ItemStackNet_endRequest.as<void(__fastcall*)(void*)>()(manager);  // queued, sent this tick
    return id;
}

bool applyInventoryMoveLocally(LocalPlayer& player, int slotA, int slotB) {
    if (slotA < 0 || slotA >= 36 || slotB < 0 || slotB >= 36 || slotA == slotB) return false;
    PlayerInventory* supplies = player.supplies();
    Inventory* inv = supplies ? supplies->inventory() : nullptr;
    const ItemStack* a = inv ? inv->getItem(slotA) : nullptr;
    const ItemStack* b = inv ? inv->getItem(slotB) : nullptr;
    if (!a || !b) return false;
    StackCopy copyA(*a), copyB(*b);
    inv->setItem(slotA, copyB.get());
    inv->setItem(slotB, copyA.get());
    return true;
}

std::vector<Actor*> Level::actors() const {
    // The game builds the vector with its own allocator: read it, then free its buffer
    // with the game's operator delete.
    struct RawVector {
        Actor** first = nullptr;
        Actor** last = nullptr;
        Actor** end = nullptr;
    } raw;
    mem::callVirtual<RawVector*, RawVector*>(this, offsets::Level_vt_getRuntimeActorList, &raw);
    std::vector<Actor*> out(raw.first, raw.last);
    gameFree(raw.first);
    return out;
}

std::string Level::name() const {
    void* data = mem::member<std::shared_ptr<void>>(this, offsets::Level_levelData).get();
    if (!mem::isReadable(data, offsets::LevelData_levelName + 0x20)) return {};
    const auto& name = mem::member<std::string>(data, offsets::LevelData_levelName);
    if (name.size() > 256 || !mem::isReadable(name.data(), name.size() + 1)) return {};
    return name;
}

Vec4 rotationToQuaternion(float pitch, float yaw) {
    // Euler (pitch, yaw, roll = 0) to quaternion, in the component order the game uses.
    constexpr float kDegToRad = 3.14159265358979f / 180.f;
    float cx = std::cos(pitch * kDegToRad * 0.5f), sx = std::sin(pitch * kDegToRad * 0.5f);
    float cy = std::cos(yaw * kDegToRad * 0.5f), sy = std::sin(yaw * kDegToRad * 0.5f);
    return {-sx * sy, cx * cy, sx * cy, cx * sy};
}

void GuiData::displayClientMessage(const std::string& message) {
    using Fn = void(__fastcall*)(GuiData*, const std::string&, std::optional<std::string>, bool);
    // The optional is passed by value and destroyed by the game: keep it empty so no
    // memory allocated by our CRT is freed by the game's.
    sigs::GuiData_displayMessage.as<Fn>()(this, message, std::nullopt, false);
}

// ---- item durability and enchantments ----
// Pure lookups in the item's user data (a CompoundTag), the same calls the game makes to
// show the durability bar: no allocation, nothing written.
namespace {

struct TagView {
    const void* tag;
    uint8_t id() const { return mem::callVirtual<uint8_t>(tag, 5); }  // Tag::getId (vtable +0x28)
};

const void* compoundGet(const void* compound, std::string_view key) {
    using GetFn = const void*(__fastcall*)(const void* compound, const std::string_view* key);
    auto fn = reinterpret_cast<GetFn>(sigs::CompoundTag_get.address);
    if (!fn || !compound) return nullptr;
    return fn(compound, &key);
}

int shortOrByte(const void* tag) {
    if (!tag) return 0;
    uint8_t id = TagView{tag}.id();
    if (id == 1) return mem::member<int8_t>(tag, 8);   // ByteTag
    if (id == 2) return mem::member<int16_t>(tag, 8);  // ShortTag
    if (id == 3) return mem::member<int32_t>(tag, 8);  // IntTag
    return 0;
}

}  // namespace

int ItemStack::damage() const {
    using DamageFn = int16_t(__fastcall*)(const ItemStack*);
    auto fn = reinterpret_cast<DamageFn>(sigs::ItemStackBase_getDamageValue.address);
    return fn && !empty() ? fn(this) : 0;
}

int ItemStack::maxDamage() const {
    const Item* it = item();
    return it ? mem::member<uint16_t>(it, offsets::Item_maxDamage) : 0;
}

std::vector<std::pair<int, int>> ItemStack::enchants() const {
    std::vector<std::pair<int, int>> out;
    if (empty()) return out;
    const void* userData = mem::member<const void*>(this, offsets::ItemStack_userData);
    const void* list = compoundGet(userData, "ench");
    if (!list || TagView{list}.id() != 9) return out;  // ListTag
    // ListTag: std::vector<std::unique_ptr<Tag>> at +8
    auto begin = mem::member<const void* const*>(list, 0x8);
    auto end = mem::member<const void* const*>(list, 0x10);
    if (!begin || end < begin || end - begin > 64) return out;
    for (auto it = begin; it != end; ++it) {
        const void* entry = *it;
        if (!entry || TagView{entry}.id() != 10) continue;  // CompoundTag
        int id = shortOrByte(compoundGet(entry, "id"));
        int lvl = shortOrByte(compoundGet(entry, "lvl"));
        out.emplace_back(id, lvl);
    }
    return out;
}

}  // namespace sdk

