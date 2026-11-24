#include "sdk/Recipes.h"
#include "sdk/Containers.h"

#include "core/GameVersion.h"
#include "memory/Signatures.h"
#include "core/Log.h"
#include <Windows.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
namespace sdk {
namespace {
using Models=std::unordered_map<std::string,std::shared_ptr<void>>;
using Controllers=std::unordered_map<const void*,std::function<void(void*)>>;
thread_local std::vector<int>* captured=nullptr;
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
    result.output = &mem::callVirtual<const ItemStack&, const Slot&>(control, 8, slot);
    // getTakeableItemStackBase returns an ItemInstance (0x80), not an ItemStack.
    if (!mem::isReadable(result.output, 0x80)) return {};
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
    struct CaptureScope { explicit CaptureScope(std::vector<int>& ids) { captured = &ids; } ~CaptureScope() { captured = nullptr; } } scope(ids);
    // Workstation vt15 owns create-item, consume-input and XP requests. A raw
    // transfer from the result preview would omit those operations.
    mem::callVirtual<int, const Slot&, int, const std::vector<AutoPlace>&, NativeResults&>(control, 15, slot, 1, destinations, results);
    return ids;
}
std::vector<RecipeChoice> visibleRecipes(LocalPlayer& player,std::string_view item) {
    std::vector<RecipeChoice> result;
    std::unordered_set<std::string> seen;
    auto* manager=player.containerManager(); if (!controller(player) || !mem::isReadable(manager,0x108)) return result;
    const auto* models=reinterpret_cast<const Models*>(static_cast<const uint8_t*>(manager)+0xC8);
    if (models->size()>128) return result;
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
// The recipe-book tap thunk (RVA 0x506E6F0) sets screen+0x13B7 and calls the
// non-virtual CraftingScreenController::_handleRecipeSelect (RVA 0x5029980) with
// takeOutput=false, which fills the grid. The output-slot tap (manager vt10) then
// emits the craft request; a real tap leaves the output on the cursor, so the same
// handler is called again to place it into the inventory.
void* craftingScreenController(void* manager) {
    if (!manager) return nullptr;
    constexpr std::ptrdiff_t sceneView=0x40, viewController=0x40, elementSize=32, elementScene=8;
    auto* client=ClientInstance::get();
    auto* stack=client?mem::member<void*>(client,0x5E8):nullptr;
    if (!mem::isReadable(stack,0x40)) return nullptr;
    auto* begin=mem::member<uint8_t*>(stack,0x18);
    auto* end=mem::member<uint8_t*>(stack,0x20);
    if (!begin || !end || end<begin) return nullptr;
    const auto bytes=static_cast<size_t>(end-begin);
    if (bytes%elementSize || bytes>elementSize*32) return nullptr;
    for (auto* element=end;element!=begin;) {
        element-=elementSize;
        auto* scene=mem::member<void*>(element,elementScene);
        if (!mem::isReadable(scene,sceneView+sizeof(void*))) continue;
        auto* view=mem::member<void*>(scene,sceneView);
        if (!mem::isReadable(view,viewController+sizeof(void*))) continue;
        auto* control=mem::member<void*>(view,viewController);
        if (!mem::isReadable(control,0x12E8)) continue;
        if (mem::member<void*>(control,0x12E0)==manager) return control;
    }
    return nullptr;
}
std::vector<int> craftRecipeOnce(LocalPlayer& player,const RecipeChoice& recipe) {
    std::vector<int> ids; auto* control=controller(player); if (!control || captured) return ids;
    auto* screen=craftingScreenController(control); if (!screen) return ids;
    auto* manager=mem::member<void*>(screen,0x12E0); if (!manager) return ids;
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    using Select=void(*)(void*,std::string const&,int,bool);
    struct CaptureScope { explicit CaptureScope(std::vector<int>& ids) { captured=&ids; } ~CaptureScope() { captured=nullptr; } } scope(ids);
    const auto flag=mem::member<uint8_t>(screen,0x13B7); mem::member<uint8_t>(screen,0x13B7)=1;
    reinterpret_cast<Select>(base+gameversion::profile().recipeSelect)(screen,recipe.collection,recipe.slot,false);
    Slot output{"crafting_output_items",0}, cursor{"cursor_items",0};
    mem::callVirtual<bool,const Slot&,const Slot&>(manager,10,cursor,output);
    const auto cursorRef=containerSlotRef(player,"cursor_items",0);
    const auto* stack=containerItem(player,cursorRef);
    if (stack && !stack->empty() && stack->item()->name()==recipe.item) {
        int target=-1;
        for (int i=0;i<36 && target<0;++i) {
            const auto* dest=containerItem(player,inventorySlotRef(i));
            if (dest && (dest->empty() || (dest->mergeCompatible(*stack) && dest->count()+stack->count()<=dest->maxStackSize()))) target=i;
        }
        if (target>=0) mem::callVirtual<bool,const Slot&,const Slot&>(manager,10,Slot{target<9?"hotbar_items":"inventory_items",target<9?target:target-9},cursor);
    }
    mem::member<uint8_t>(screen,0x13B7)=flag;
    return ids;
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
