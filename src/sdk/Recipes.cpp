#include "sdk/Recipes.h"
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
        if (mem::isReadable(pointer,0x200) && reinterpret_cast<uintptr_t>(*reinterpret_cast<void* const*>(pointer))==base+0xF1B4690 &&
            reinterpret_cast<uintptr_t>(mem::vtable(pointer)[16])==base+0x3A4C510) return const_cast<void*>(pointer);
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
        anvil = vt == base + 0xF499460;
        if (!anvil && vt != base + 0xF497C50) continue;
        if (reinterpret_cast<uintptr_t>(mem::vtable(pointer)[8]) != base + (anvil ? 0xCC9C7B0 : 0xCC57970) ||
            reinterpret_cast<uintptr_t>(mem::vtable(pointer)[15]) != base + (anvil ? 0xCC9A9D0 : 0xCC57F10)) return nullptr;
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
std::vector<int> craftRecipeOnce(LocalPlayer& player,const RecipeChoice& recipe) {
    std::vector<int> ids; auto* control=controller(player); if (!control || captured) return ids;
    Slot slot{recipe.collection,recipe.slot};
    const std::vector<AutoPlace> destinations{{"hotbar_items",false},{"inventory_items",false}};
    NativeResults results;
    struct CaptureScope { explicit CaptureScope(std::vector<int>& ids) { captured=&ids; } ~CaptureScope() { captured=nullptr; } } scope(ids);
    mem::callVirtual<int,const Slot&,int,const std::vector<AutoPlace>&,NativeResults&>(control,16,slot,2,destinations,results);
    return ids;
}
bool discardInventoryStack(LocalPlayer& player,int index,int count) {
    auto* control=controller(player);
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!control || index<0 || index>=36 || count<1 || count>64 || reinterpret_cast<uintptr_t>(mem::vtable(control)[23])!=base+0x3A285A0) return false;
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
