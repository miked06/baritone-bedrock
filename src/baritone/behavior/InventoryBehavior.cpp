#include "baritone/behavior/InventoryBehavior.h"

#include <algorithm>
#include <limits>
#include <vector>

#include "baritone/Settings.h"

namespace baritone {

namespace {

// Minimum tool tier to get drops (0 = any tool or hand).
int requiredTier(const BlockProps& block) {
    const std::string& n = block.name;
    auto has = [&](std::string_view s) { return n.find(s) != std::string::npos; };
    if (has("obsidian") || has("ancient_debris") || has("netherite_block") || has("respawn_anchor")) return 4;
    if (has("diamond_ore") || has("emerald_ore") || has("gold_ore") || has("redstone_ore") ||
        has("diamond_block") || has("emerald_block") || has("gold_block")) {
        return 3;
    }
    if (has("iron_ore") || has("copper_ore") || has("lapis_ore") || has("iron_block") || has("lapis_block")) return 2;
    return block.requiresTool ? 1 : 0;
}

bool isThrowaway(const HotbarStack& stack) {
    if (stack.empty() || !stack.isBlock) return false;
    const auto& list = Settings::get().acceptableThrowawayItems;
    return std::ranges::find(list, stack.name) != list.end();
}

}  // namespace

double InventoryBehavior::breakSpeed(const HotbarStack& item, const BlockProps& block) {
    if (block.hardness < 0 || block.unbreakable) return 0;
    if (block.hardness == 0) return std::numeric_limits<double>::infinity();
    double speed = 1;
    bool rightTool = !item.empty() && item.tool != Tool::None && item.tool == block.bestTool;
    if (rightTool) speed = item.toolSpeed;
    if (!item.empty() && item.tool == Tool::Sword && block.type == BlockType::Cobweb) speed = 15;
    if (!item.empty() && item.tool == Tool::Shears &&
        (block.type == BlockType::Cobweb || block.name.find("leaves") != std::string::npos)) {
        speed = 15;
    }
    if (!item.empty() && item.tool == Tool::Shears && block.name.find("wool") != std::string::npos) speed = 5;
    if (speed > 1 && item.efficiency > 0) speed += item.efficiency * item.efficiency + 1;
    bool canHarvest = !block.requiresTool || (rightTool && item.toolTier >= requiredTier(block)) ||
                      (block.type == BlockType::Cobweb && (item.tool == Tool::Sword || item.tool == Tool::Shears));
    return speed / block.hardness / (canHarvest ? 30.0 : 100.0);
}

int InventoryBehavior::findSlot(std::string_view itemName) {
    for (int i = 0; i < 9; ++i) {
        if (player_.hotbar(i).name == itemName) return i;
    }
    return -1;
}

// Baritone's throwaway(select, desired, allowInventory)
bool InventoryBehavior::selectItem(const std::function<bool(const HotbarStack&)>& pred, bool select) {
    HotbarStack held = player_.hotbar(player_.selectedSlot());
    if (!held.empty() && pred(held)) return true;
    for (int i = 0; i < 9; ++i) {
        HotbarStack s = player_.hotbar(i);
        if (s.empty() || !pred(s)) continue;
        if (select) player_.selectSlot(i);
        return true;
    }
    if (int slot = findInMainInventory(pred); slot >= 0) {
        if (select) {
            requestSwapWithHotBar(slot, 7);  // arrives in a few ticks
            player_.selectSlot(7);
        }
        return true;
    }
    return false;
}

bool InventoryBehavior::hasGenericThrowaway() {
    return selectItem(isThrowaway, false);
}

bool InventoryBehavior::selectThrowawayForLocation(bool select, int, int, int) {
    return selectItem(isThrowaway, select);
}

int InventoryBehavior::findInMainInventory(const std::function<bool(const HotbarStack&)>& pred) {
    if (!Settings::get().allowInventory) return -1;
    for (int i = 9; i < 36; ++i) {
        HotbarStack s = player_.hotbar(i);
        if (!s.empty() && pred(s)) return i;
    }
    return -1;
}

bool InventoryBehavior::requestSwapWithHotBar(int inInventory, int inHotbar) {
    const Settings& s = Settings::get();
    lastRequestedMove_ = {inInventory, inHotbar};
    if (ticksSinceLastMove_ < s.ticksBetweenInventoryMoves) return false;
    if (s.inventoryMoveOnlyIfStationary && (!stationaryCheck_ || !stationaryCheck_())) return false;
    if (!player_.swapSlots(inInventory, inHotbar)) return false;
    ticksSinceLastMove_ = 0;
    lastRequestedMove_.reset();
    return true;
}

int InventoryBehavior::bestPickaxeSlot() {
    int best = -1;
    double bestSpeed = -1;
    for (int i = 0; i < 36; ++i) {
        HotbarStack s = player_.hotbar(i);
        if (s.empty() || s.tool != Tool::Pickaxe) continue;
        if (s.toolSpeed > bestSpeed) {
            bestSpeed = s.toolSpeed;
            best = i;
        }
    }
    return best;
}

// Slot (9-35) of a water bucket of the main inventory, -1 if none.
int InventoryBehavior::waterBucketInInventory() {
    for (int i = 9; i < 36; ++i)
        if (player_.hotbar(i).name == "minecraft:water_bucket") return i;
    return -1;
}

// Bedrock addition: a water bucket of the main inventory goes to the hotbar, so that falls can
// be clutched (allowWaterBucketFall) even without allowInventory. Into an empty slot, else one
// that holds neither a tool nor a throwaway block.
bool InventoryBehavior::waterBucketToHotbar() {
    if (hasWaterBucket()) return false;
    int from = waterBucketInInventory();
    if (from < 0) return false;
    int to = -1;
    for (int i = 0; i < 9 && to < 0; ++i)
        if (player_.hotbar(i).empty()) to = i;
    for (int i = 7; i >= 1 && to < 0; --i) {
        HotbarStack s = player_.hotbar(i);
        if (s.tool == Tool::None && !isThrowaway(s) && s.name != "minecraft:bucket") to = i;
    }
    if (to < 0) to = 7;
    return requestSwapWithHotBar(from, to);
}

void InventoryBehavior::tick() {
    if (!player_.valid()) return;
    ++ticksSinceLastMove_;
    if (player_.inventoryMovePending()) return;
    if (Settings::get().allowWaterBucketFall && waterBucketToHotbar()) return;
    if (!Settings::get().allowInventory) return;
    // firstValidThrowaway: the first throwaway of the whole inventory is not on the hotbar
    int throwaway = -1;
    for (int i = 0; i < 36 && throwaway < 0; ++i)
        if (isThrowaway(player_.hotbar(i))) throwaway = i;
    if (throwaway >= 9 && requestSwapWithHotBar(throwaway, 8)) return;
    int pick = bestPickaxeSlot();
    if (pick >= 9 && requestSwapWithHotBar(pick, 0)) return;
    if (lastRequestedMove_) requestSwapWithHotBar(lastRequestedMove_->first, lastRequestedMove_->second);
}

// Java ToolSet.getMaterialCost: wood 0, stone 1, iron 2, gold 3, diamond 4, netherite 5.
int InventoryBehavior::materialCost(const HotbarStack& s) {
    static constexpr std::pair<std::string_view, int> materials[] = {
        {"minecraft:wooden_", 0}, {"minecraft:stone_", 1}, {"minecraft:iron_", 2},
        {"minecraft:golden_", 3}, {"minecraft:diamond_", 4}, {"minecraft:netherite_", 5},
    };
    for (auto [prefix, cost] : materials)
        if (s.name.starts_with(prefix)) return cost;
    return -1;
}

// Java ToolSet.getBestSlot: the fastest hotbar item for the block; on equal speed the
// cheapest material, silk touch kept (preferSilkTouch: preferred). Swords are skipped
// without useSwordToMine, tools about to break with itemSaver.
int InventoryBehavior::bestSlotFor(const BlockProps& block, bool preferSilkTouch) {
    const Settings& st = Settings::get();
    int best = 0;
    double highestSpeed = -std::numeric_limits<double>::infinity();
    int lowestCost = std::numeric_limits<int>::min();
    bool bestSilkTouch = false;
    for (int i = 0; i < 9; ++i) {
        HotbarStack item = player_.hotbar(i);
        if (!st.useSwordToMine && item.tool == Tool::Sword) continue;
        if (st.itemSaver && item.maxDamage > 1 && item.damage + st.itemSaverThreshold >= item.maxDamage) continue;
        double speed = breakSpeed(item, block);
        bool silkTouch = item.silkTouch;
        if (speed > highestSpeed) {
            highestSpeed = speed;
            best = i;
            lowestCost = materialCost(item);
            bestSilkTouch = silkTouch;
        } else if (speed == highestSpeed) {
            int cost = materialCost(item);
            if ((cost < lowestCost && (silkTouch || !bestSilkTouch)) || (preferSilkTouch && !bestSilkTouch && silkTouch)) {
                highestSpeed = speed;
                best = i;
                lowestCost = cost;
                bestSilkTouch = silkTouch;
            }
        }
    }
    return best;
}

// Java ToolSet potionAmplifier (considerPotionEffects): Haste / Mining Fatigue.
double InventoryBehavior::potionAmplifier() {
    if (!Settings::get().considerPotionEffects) return 1;
    double speed = 1;
    if (int haste = player_.effectAmplifier("haste"); haste >= 0) speed *= 1 + (haste + 1) * 0.2;
    switch (player_.effectAmplifier("mining_fatigue")) {
        case -1: break;
        case 0: speed *= 0.3; break;
        case 1: speed *= 0.09; break;
        case 2: speed *= 0.0027; break;
        default: speed *= 0.00081; break;
    }
    return speed;
}

void InventoryBehavior::switchToBestToolFor(const BlockProps& block) {
    // Java MovementHelper.switchToBestToolFor
    if (!Settings::get().autoTool || Settings::get().assumeExternalAutoTool) return;
    int slot = bestSlotFor(block, Settings::get().preferSilkTouch);
    if (slot != player_.selectedSlot()) player_.selectSlot(slot);
}

PlayerAbilities InventoryBehavior::abilities() {
    PlayerAbilities a;
    // a bucket of the main inventory counts: tick() brings it to the hotbar right away
    a.hasWaterBucket = hasWaterBucket() || (Settings::get().allowWaterBucketFall && waterBucketInInventory() >= 0);
    a.hasThrowaway = hasGenericThrowaway();
    a.foodLevel = player_.foodLevel();
    a.frostWalker = player_.armor(3).frostWalker;
    if (player_.isCreative()) {  // everything breaks at once, except unbreakable blocks
        a.breakSpeed = [](const BlockProps& block) { return block.hardness < 0 || block.unbreakable ? 0.0 : 1.0; };
        return a;
    }
    // Java ToolSet.getBestDestructionTime: the speed of the tool getBestSlot picks (a copy of
    // the hotbar: the calculation runs on another thread), times the potion amplifier
    std::vector<HotbarStack> hotbar;
    for (int i = 0; i < 9; ++i) hotbar.push_back(player_.hotbar(i));
    const Settings& st = Settings::get();
    a.breakSpeed = [hotbar = std::move(hotbar), amplifier = potionAmplifier(), useSword = st.useSwordToMine,
                    itemSaver = st.itemSaver, threshold = st.itemSaverThreshold](const BlockProps& block) {
        double best = breakSpeed(HotbarStack{}, block);  // bare hand (an empty slot)
        for (const HotbarStack& item : hotbar) {
            if (item.empty()) continue;
            if (!useSword && item.tool == Tool::Sword) continue;
            if (itemSaver && item.maxDamage > 1 && item.damage + threshold >= item.maxDamage) continue;
            best = std::max(best, breakSpeed(item, block));
        }
        return best * amplifier;
    };
    return a;
}

}  // namespace baritone
