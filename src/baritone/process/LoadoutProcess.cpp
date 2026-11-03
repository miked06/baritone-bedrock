#include "baritone/process/LoadoutProcess.h"
#include "baritone/Baritone.h"
#include <algorithm>

namespace baritone {
void LoadoutProcess::start(std::string name, LoadoutStore::Profile rules) {
    if (!baritone_.player().valid() || baritone_.player().containerOpen() || baritone_.player().inventoryMovePending())
        throw std::invalid_argument("Enter a world, close containers and wait for inventory actions first");
    name_ = std::move(name); rules_ = std::move(rules); waiting_.reset(); failure_.clear();
    active_ = true; succeeded_ = false; ticks_ = 0;
}
bool LoadoutProcess::isActive() { return active_ && !baritone_.pauser().paused(); }
std::optional<PathingCommand> LoadoutProcess::onTick(bool, bool safe) {
    if (!active_) return {};
    auto stop = [&](const std::string& text, bool error) -> std::optional<PathingCommand> {
        if (error) failure_ = text;
        active_ = false; baritone_.logDirect(text, error);
        return PathingCommand{nullptr, PathingCommandType::CancelAndSetGoal};
    };
    if (++ticks_ > 600) return stop("Loadout inventory actions timed out; inspect the current inventory", true);
    auto& player = baritone_.player();
    if (!safe || player.inventoryMovePending() || player.slotChangePending()) return PathingCommand{nullptr, PathingCommandType::RequestPause};
    if (waiting_) {
        if (!LoadoutStore::suitable(player.hotbar(waiting_->hotbarSlot), *waiting_))
            return stop("The loadout inventory move was not confirmed; no repeated swap was attempted", true);
        waiting_.reset();
    }
    for (const auto& [item, rule] : rules_) {
        if (rule.hotbarSlot < 0 || LoadoutStore::suitable(player.hotbar(rule.hotbarSlot), rule)) continue;
        int best = -1, durability = -1;
        for (int slot = 0; slot < 36; ++slot) {
            const auto stack = player.hotbar(slot);
            if (!LoadoutStore::suitable(stack, rule)) continue;
            const int remaining = stack.maxDamage > 0 ? int(int64_t(stack.maxDamage - stack.damage) * 100 / stack.maxDamage) : 100;
            if (remaining > durability) { best = slot; durability = remaining; }
        }
        if (best < 0) return stop("No suitable " + item + " is available for the loadout hotbar", true);
        if (!player.swapSlots(best, rule.hotbarSlot)) return stop("The normal inventory swap could not be submitted", true);
        waiting_ = rule;
        return PathingCommand{nullptr, PathingCommandType::RequestPause};
    }
    succeeded_ = true;
    return stop("Loadout hotbar equipped; item quantities are checked with #loadout check", false);
}
}
