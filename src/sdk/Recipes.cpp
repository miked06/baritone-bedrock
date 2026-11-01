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
    if (!captured || !mem::isReadable(action,16)) return;
    const auto kind=mem::member<uint8_t>(action,8);
    if (kind<12 || kind>15) return;
    auto* request=mem::member<void*>(manager,offsets::ItemStackNetManager_currentRequest);
    if (!mem::isReadable(request,12)) return;
    int id=mem::member<int32_t>(request,8);
    logger::debug("Craft request action {} id {}",kind,id);
    if (id && std::ranges::find(*captured,id)==captured->end()) captured->push_back(id);
}
}
