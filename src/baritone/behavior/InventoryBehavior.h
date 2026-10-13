#pragma once
#include <functional>
#include <optional>
#include <utility>

#include "baritone/api/PlayerContext.h"
#include "baritone/pathing/movement/CalculationContext.h"

namespace baritone {

// Hotbar management (Baritone's InventoryBehavior + ToolSet): throwaway blocks to place
// and the best tool to break a block. Bedrock's inventory is server-authoritative, so for
// now only the hotbar is used (no moving items around).
class InventoryBehavior {
public:
    explicit InventoryBehavior(PlayerContext& player) : player_(player) {}
    // InventoryPauserProcess.stationaryForInventoryMove (inventoryMoveOnlyIfStationary)
    void setStationaryCheck(std::function<bool()> check) { stationaryCheck_ = std::move(check); }
    // Java BuilderProcess.placeAt: the block the build wants at a position, if any (so that a
    // path placing a block there uses the schematic's block, not only throwaway blocks)
    void setPlaceAt(std::function<std::optional<std::string>(int, int, int)> placeAt) { placeAt_ = std::move(placeAt); }

    bool hasGenericThrowaway();
    // Finds a throwaway block in the hotbar and (if select) holds it.
    bool selectThrowawayForLocation(bool select, int x, int y, int z);
    int findSlot(std::string_view itemName);
    // A hotbar item matching `pred`; holds it if `select` (Baritone's throwaway(select, pred)).
    bool selectItem(const std::function<bool(const HotbarStack&)>& pred, bool select);
    bool hasWaterBucket() { return findSlot("minecraft:water_bucket") >= 0; }

    // Holds the hotbar item that breaks `block` the fastest.
    void switchToBestToolFor(const BlockProps& block);
    int bestSlotFor(const BlockProps& block, bool preferSilkTouch);
    double potionAmplifier();
    static int materialCost(const HotbarStack& s);

    // Every tick (Baritone's InventoryBehavior.onTick, with allowInventory): the best pickaxe
    // goes to slot 0, a throwaway block to slot 8 when the hotbar has none.
    void tick();

    // What the path calculation may assume (copied: safe to use on the calc thread).
    PlayerAbilities abilities();

    // Break speed (fraction per tick) of a hotbar item against a block, vanilla formula.
    static double breakSpeed(const HotbarStack& item, const BlockProps& block);

    // Java attemptToPutOnHotbar: moves a main inventory slot to a hotbar slot (false: not now)
    bool requestSwapWithHotBar(int inInventory, int inHotbar);
private:
    // allowInventory: main inventory slot (9-35) of a matching item, or -1
    int findInMainInventory(const std::function<bool(const HotbarStack&)>& pred);
    int bestPickaxeSlot();
    int waterBucketInInventory();
    bool waterBucketToHotbar();

    PlayerContext& player_;
    std::function<bool()> stationaryCheck_;
    std::function<std::optional<std::string>(int, int, int)> placeAt_;
    int ticksSinceLastMove_ = 1000;
    std::optional<std::pair<int, int>> lastRequestedMove_;
};

}  // namespace baritone
