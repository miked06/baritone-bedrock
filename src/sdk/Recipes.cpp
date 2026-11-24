#include "sdk/Recipes.h"

#include "core/GameVersion.h"
#include "memory/Signatures.h"
#include "core/Log.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
namespace sdk {
namespace {
using Models=std::unordered_map<std::string,std::shared_ptr<void>>;
using Controllers=std::unordered_map<const void*,std::function<void(void*)>>;
thread_local std::vector<int>* captured=nullptr;
struct CaptureScope {
    explicit CaptureScope(std::vector<int>& requests) { captured = &requests; }
    ~CaptureScope() { captured = nullptr; }
};
const Models* containerModels(LocalPlayer& player) {
    const auto* manager = static_cast<const uint8_t*>(player.containerManager());
    if (!mem::isReadable(manager, 0xC8 + sizeof(Models))) return nullptr;
    const auto* models = reinterpret_cast<const Models*>(manager + 0xC8);
    return models->size() <= 128 ? models : nullptr;
}
void* controller(LocalPlayer& player) {
    auto* manager=player.containerManager(); if (!mem::isReadable(manager,0x108)) return nullptr;
    auto* callbacks=reinterpret_cast<const Controllers*>(static_cast<const uint8_t*>(manager)+0x60);
    if (callbacks->size()>32) return nullptr;
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    for (const auto& [pointer,callback]:*callbacks) {
        if (mem::isReadable(pointer,0x200) && reinterpret_cast<uintptr_t>(*reinterpret_cast<void* const*>(pointer))==base+gameversion::profile().craftingTable &&
            reinterpret_cast<uintptr_t>(mem::vtable(pointer)[16])==base+gameversion::profile().craft) return const_cast<void*>(pointer);
    }
    return nullptr;
}
struct Slot { std::string collection; int index; };
struct AutoPlace { std::string collection; bool stop=false; };
static_assert(sizeof(Slot)==40 && sizeof(AutoPlace)==40);
const ItemStack* takeableStack(const void* control, const Slot& slot) {
    // The controller returns an ItemInstance containing the ItemStackBase.
    const auto* stack = &mem::callVirtual<const ItemStack&, const Slot&>(control, 8, slot);
    return mem::isReadable(stack, 0x80) ? stack : nullptr;
}
struct NativeResults {
    uint8_t* first=nullptr; uint8_t* last=nullptr; uint8_t* capacity=nullptr;
    ~NativeResults() {
        if (!first) return;
        const auto allocated=capacity-first;
        if (allocated<0 || allocated>64*1024 || last<first || last>capacity || (last-first)%40) return;
        for (auto* p=first;p<last;p+=40) {
            auto cap=mem::member<size_t>(p,24);
            if (cap>=16 && cap<4095) gameFree(mem::member<void*>(p,0));
        }
        // MSVC std::allocator overallocates and stores its raw allocation before
        // aligned vectors of at least 4096 bytes.
        gameFree(allocated>=4096 ? mem::member<void*>(first,-8) : first);
    }
};
void* repairController(LocalPlayer& player, bool& anvil) {
    auto* manager = player.containerManager(); if (!mem::isReadable(manager, 0x108)) return nullptr;
    const auto* callbacks = reinterpret_cast<const Controllers*>(static_cast<const uint8_t*>(manager) + 0x60);
    if (callbacks->size() > 32) return nullptr;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    for (const auto& [pointer, callback] : *callbacks) {
        if (!mem::isReadable(pointer, 0x208)) continue;
        const auto vt = reinterpret_cast<uintptr_t>(*reinterpret_cast<void* const*>(pointer));
        anvil = vt == base + gameversion::profile().anvilTable;
        if (!anvil && vt != base + gameversion::profile().grindstoneTable) continue;
        if (reinterpret_cast<uintptr_t>(mem::vtable(pointer)[8]) != base + (anvil ? gameversion::profile().anvilPreview : gameversion::profile().grindstonePreview) ||
            reinterpret_cast<uintptr_t>(mem::vtable(pointer)[15]) != base + (anvil ? gameversion::profile().anvilTake : gameversion::profile().grindstoneTake)) return nullptr;
        return const_cast<void*>(pointer);
    }
    return nullptr;
}
}
WorkstationPreview workstationPreview(LocalPlayer& player) {
    bool anvil = false; auto* control = repairController(player, anvil); if (!control) return {};
    WorkstationPreview result;
    result.method = anvil ? "anvil" : "grindstone";
    result.collection = anvil ? "anvil_result_items" : "grindstone_result_items";
    Slot slot{result.collection, 0};
    result.output = takeableStack(control, slot);
    if (!result.output) return {};
    result.levels = anvil ? mem::member<int>(control, 0xC8) : 0;
    result.materialCost = anvil ? mem::member<int>(control, 0x1F4) : 1;
    return result;
}
std::vector<int> takeWorkstationOutput(LocalPlayer& player, const WorkstationPreview& expected) {
    std::vector<int> ids; bool anvil = false;
    auto* control = repairController(player, anvil); if (!control || captured) return ids;
    const auto current = workstationPreview(player);
    if (!current.output || current.output->empty() || current.method != expected.method || current.levels != expected.levels ||
        current.materialCost != expected.materialCost || current.levels < 0) return ids;
    Slot slot{current.collection, 0};
    const std::vector<AutoPlace> destinations{{"hotbar_items", false}, {"inventory_items", false}};
    NativeResults results;
    CaptureScope scope(ids);
    // Workstation vt15 owns create-item, consume-input and XP requests. A raw
    // transfer from the result preview would omit those operations.
    mem::callVirtual<int, const Slot&, int, const std::vector<AutoPlace>&, NativeResults&>(control, 15, slot, 1, destinations, results);
    return ids;
}
std::vector<RecipeChoice> visibleRecipes(LocalPlayer& player,std::string_view item) {
    std::vector<RecipeChoice> result;
    std::unordered_set<std::string> seen;
    const auto* models = containerModels(player);
    if (!models || !controller(player)) return result;
    for (const auto& [name,model]:*models) {
        if (!name.starts_with("recipe_") || !mem::isReadable(model.get(),0x110)) continue;
        int size=mem::callVirtual<int>(model.get(),4); if (size<0 || size>4096) continue;
        for (int i=0;i<size;++i) {
            // Filtered recipe models use ItemInstance (vt11 forwards to vt10),
            // not ItemStack (vt8 returns the static empty stack).
            const auto& base=mem::callVirtual<const ItemStack&,int>(model.get(),11,i);
            if (base.empty()) continue;
            const auto& output=base.item()->name();
            if ((!item.empty() && output!=item) || !seen.insert(output).second) continue;
            result.push_back({name,output,i,base.count()});
            if (result.size()>=2048 || (!item.empty() && !result.empty())) return result;
        }
    }
    return result;
}
namespace {
constexpr std::ptrdiff_t kRecipeSelectionFlag = 0x13B7;
constexpr int kInventorySize = 36;
constexpr int kHotbarSize = 9;
constexpr std::string_view kNoCraftSpace = "Free inventory space before crafting";
const Slot kCraftOutput{"crafting_output_items", 0};
const Slot kCursor{"cursor_items", 0};
const std::string kCraftInput = "crafting_input_items";

const void* collectionModel(const Models& models, const std::string& name) {
    const auto found = models.find(name);
    if (found == models.end() || !mem::isReadable(found->second.get(), 0x110)) return nullptr;
    return found->second.get();
}

const ItemStack* modelStack(const void* model, int index) {
    const auto* stack = &mem::callVirtual<const ItemStack&, int>(model, 8, index);
    return mem::isReadable(stack, 0x23) ? stack : nullptr;
}

const ItemStack* craftSlot(const Models& models, const Slot& slot) {
    const auto* model = collectionModel(models, slot.collection);
    if (!model || mem::callVirtual<int>(model, 4) != 1) return nullptr;
    return modelStack(model, slot.index);
}

bool matchesItem(const ItemStack* stack, std::string_view item) {
    return stack && !stack->empty() && stack->item()->name() == item;
}

int craftDestination(const Inventory& inventory, const ItemStack& output) {
    int empty = -1;
    for (int i = 0; i < kInventorySize; ++i) {
        const auto* stack = inventory.getItem(i);
        if (!stack) continue;
        if (stack->empty()) {
            if (empty < 0) empty = i;
        } else if (stack->mergeCompatible(output) && stack->count() + output.count() <= stack->maxStackSize()) {
            return i;
        }
    }
    return empty;
}

bool hasCraftSpace(const Inventory& inventory, std::string_view item) {
    for (int i = 0; i < kInventorySize; ++i) {
        const auto* stack = inventory.getItem(i);
        if (stack && (stack->empty() || (matchesItem(stack, item) && !stack->itemData() &&
                                        stack->count() < stack->maxStackSize()))) return true;
    }
    return false;
}

bool canReturnCraftGrid(const Models& models, const Inventory& inventory) {
    const auto* grid = collectionModel(models, kCraftInput);
    if (!grid) return false;
    const int size = mem::callVirtual<int>(grid, 4);
    if (size != 4 && size != 9) return false;

    struct Capacity { const ItemStack* stack = nullptr; int remaining = 0; };
    std::array<Capacity, kInventorySize> slots{};
    for (int i = 0; i < kInventorySize; ++i) {
        const auto* stack = inventory.getItem(i);
        if (stack) slots[i] = {stack, stack->empty() ? 64 : stack->maxStackSize() - stack->count()};
    }
    for (int i = 0; i < size; ++i) {
        const auto* ingredient = modelStack(grid, i);
        if (!ingredient) return false;
        if (ingredient->empty()) continue;
        int remaining = ingredient->count();
        for (auto& slot : slots) {
            if (slot.remaining <= 0 || !slot.stack->mergeCompatible(*ingredient)) continue;
            const int stored = std::min(remaining, slot.remaining);
            slot.remaining -= stored;
            remaining -= stored;
            if (!remaining) break;
        }
        if (!remaining) continue;
        const auto empty = std::ranges::find_if(slots, [](const Capacity& slot) {
            return slot.stack && slot.stack->empty();
        });
        if (empty == slots.end() || remaining > ingredient->maxStackSize()) return false;
        *empty = {ingredient, ingredient->maxStackSize() - remaining};
    }
    return true;
}

void* craftingScreenController(void* manager) {
    constexpr std::ptrdiff_t sceneView = 0x40, viewController = 0x40, elementSize = 32, elementScene = 8;
    auto* client = ClientInstance::get();
    auto* stack = client ? mem::member<void*>(client, 0x5E8) : nullptr;
    if (!mem::isReadable(stack, 0x40)) return nullptr;
    const auto begin = mem::member<uintptr_t>(stack, 0x18);
    const auto end = mem::member<uintptr_t>(stack, 0x20);
    if (!begin || end <= begin) return nullptr;
    const auto bytes = end - begin;
    if (bytes % elementSize || bytes > elementSize * 32 ||
        !mem::isReadable(reinterpret_cast<const void*>(begin), bytes)) return nullptr;
    for (auto element = end; element != begin;) {
        element -= elementSize;
        auto* scene = mem::member<void*>(reinterpret_cast<const void*>(element), elementScene);
        if (!mem::isReadable(scene, sceneView + sizeof(void*))) continue;
        auto* view = mem::member<void*>(scene, sceneView);
        if (!mem::isReadable(view, viewController + sizeof(void*))) continue;
        auto* control = mem::member<void*>(view, viewController);
        if (mem::isReadable(control, kRecipeSelectionFlag + 1) &&
            mem::member<void*>(control, 0x12E0) == manager) return control;
    }
    return nullptr;
}

CraftDispatch craftTap(void* control, const Slot& target, const Slot& source, std::string_view error) {
    CraftDispatch result;
    CaptureScope capture(result.requests);
    mem::callVirtual<bool, const Slot&, const Slot&>(control, 10, target, source);
    if (result.requests.empty()) result.error = error;
    return result;
}
}

CraftDispatch prepareCraftRecipe(LocalPlayer& player, const RecipeChoice& recipe) {
    auto* control = controller(player);
    const auto* models = containerModels(player);
    auto* inventory = player.supplies()->inventory();
    if (!control || !models || !inventory || captured) return {{}, "Crafting interface is unavailable"};
    const auto* cursor = craftSlot(*models, kCursor);
    const auto* output = takeableStack(control, kCraftOutput);
    if (!cursor || !output) return {{}, "Crafting cursor or output slot is unavailable"};
    if (!cursor->empty()) return {{}, "Empty the cursor before crafting"};
    if (matchesItem(output, recipe.item)) {
        if (craftDestination(*inventory, *output) < 0) return {{}, kNoCraftSpace};
        return {};
    }

    // The recipe book clears the current grid and can drop its contents if the
    // inventory cannot take them. Check capacity before invoking that operation.
    if (!hasCraftSpace(*inventory, recipe.item)) return {{}, kNoCraftSpace};
    if (!canReturnCraftGrid(*models, *inventory))
        return {{}, "Empty the crafting grid or free space for its ingredients"};
    auto* screen = craftingScreenController(control);
    if (!screen) return {{}, "Crafting screen is unavailable"};

    CraftDispatch result;
    CaptureScope capture(result.requests);
    auto& flag = mem::member<uint8_t>(screen, kRecipeSelectionFlag);
    struct RestoreFlag { uint8_t& flag; uint8_t original; ~RestoreFlag() { flag = original; } } restore{flag, flag};
    flag = 1;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    using Select = void(*)(void*, const std::string&, int, bool);
    // Replay the recipe-book selection without taking its output.
    reinterpret_cast<Select>(base + gameversion::profile().recipeSelect)(screen, recipe.collection, recipe.slot, false);
    return result;
}

CraftDispatch takeCraftOutput(LocalPlayer& player, std::string_view item) {
    auto* control = controller(player);
    const auto* models = containerModels(player);
    auto* inventory = player.supplies()->inventory();
    if (!control || !models || !inventory || captured) return {{}, "Crafting interface is unavailable"};
    const auto* cursor = craftSlot(*models, kCursor);
    if (!cursor || !cursor->empty()) return {{}, "Crafting requires an empty cursor"};
    const auto* output = takeableStack(control, kCraftOutput);
    if (!matchesItem(output, item)) return {{}, "The recipe did not produce the requested output; check ingredients and crafting-grid size"};
    if (craftDestination(*inventory, *output) < 0) return {{}, kNoCraftSpace};
    return craftTap(control, kCursor, kCraftOutput, "The game did not send a crafting request");
}

CraftDispatch storeCraftOutput(LocalPlayer& player, std::string_view item) {
    auto* control = controller(player);
    const auto* models = containerModels(player);
    auto* inventory = player.supplies()->inventory();
    if (!control || !models || !inventory || captured) return {{}, "Crafting interface is unavailable"};
    const auto* output = craftSlot(*models, kCursor);
    if (!matchesItem(output, item)) return {{}, "The crafted item is unavailable on the cursor"};
    const int target = craftDestination(*inventory, *output);
    if (target < 0) return {{}, "Inventory changed while crafting; place the crafted item from the cursor manually"};
    const bool hotbar = target < kHotbarSize;
    return craftTap(control, {hotbar ? "hotbar_items" : "inventory_items", hotbar ? target : target - kHotbarSize},
                    kCursor, "The game did not send a request to store the crafted item");
}
bool discardInventoryStack(LocalPlayer& player,int index,int count) {
    auto* control=controller(player);
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!control || index<0 || index>=36 || count<1 || count>64 || reinterpret_cast<uintptr_t>(mem::vtable(control)[23])!=base+gameversion::profile().discard) return false;
    // The SlotData overload is vt23 in this binary (MSVC reverses these
    // overloaded virtual declarations); vt24 takes SelectedSlotInfo instead.
    Slot slot{index<9 ? "hotbar_items" : "inventory_items",index<9 ? index : index-9};
    struct Amount { int type,count; bool useMaxStackSize=false; } amount{0,count};
    static_assert(sizeof(Amount)==12);
    return mem::callVirtual<bool,const Slot&,const Amount&>(control,23,slot,amount);
}

void captureRecipeAction(void* manager,const void* action) {
    if (!mem::isReadable(action,16)) return;
    const auto kind=mem::member<uint8_t>(action,8);
    if (kind <= 2 && mem::isReadable(action, offsets::ItemStackRequestTransfer_size)) {
        const auto* source = static_cast<const uint8_t*>(action) + offsets::ItemStackRequestTransfer_src;
        const auto* target = static_cast<const uint8_t*>(action) + offsets::ItemStackRequestTransfer_dst;
        if (source[0] <= 2 || target[0] <= 2 || (source[0] >= 50 && source[0] <= 52) || (target[0] >= 50 && target[0] <= 52))
            logger::debug("Workstation action {} count {}: {}:{} -> {}:{}", kind, mem::member<uint8_t>(action,0x12),source[0],source[12],target[0],target[12]);
    }
    if (!captured) return;
    auto* request=mem::member<void*>(manager,offsets::ItemStackNetManager_currentRequest);
    if (!mem::isReadable(request,12)) return;
    int id=mem::member<int32_t>(request,8);
    logger::debug("Craft request action {} id {}",kind,id);
    if (id && captured->size() < 64 && std::ranges::find(*captured,id)==captured->end()) captured->push_back(id);
}
}
