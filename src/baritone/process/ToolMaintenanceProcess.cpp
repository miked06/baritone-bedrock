#include "baritone/process/ToolMaintenanceProcess.h"
#include "baritone/Baritone.h"
namespace baritone {
void ToolMaintenanceProcess::enable(int percent) {
    if (percent < 1 || percent > 90) throw std::invalid_argument("Tool replacement threshold must be 1..90 percent remaining");
    percent_ = percent; enabled_ = true; waiting_ = -1; ticks_ = 0;
}
bool ToolMaintenanceProcess::permits(const HotbarStack& stack) const {
    return !enabled_ || stack.empty() || stack.maxDamage <= 0 || int64_t(stack.maxDamage - stack.damage) * 100 > int64_t(stack.maxDamage) * percent_;
}
bool ToolMaintenanceProcess::isActive() {
    if (!enabled_ || baritone_.pauser().paused()) return false;
    if (waiting_ >= 0) return true;
    for (int i = 0; i < 9; ++i) { const auto stack = baritone_.player().hotbar(i); if (stack.tool != Tool::None && !permits(stack)) return true; }
    return false;
}
std::optional<PathingCommand> ToolMaintenanceProcess::onTick(bool, bool safe) {
    auto pause = [&] { return PathingCommand{nullptr, PathingCommandType::RequestPause}; };
    auto fail = [&](std::string message) -> std::optional<PathingCommand> { waiting_ = -1; ticks_ = 0; baritone_.logDirect(message + "; bot paused, worn tools retained. Add a suitable spare, then resume", true); baritone_.pauser().setPaused(true); return pause(); };
    auto& player = baritone_.player();
    if (++ticks_ > 600) return fail("Tool maintenance inventory action timed out");
    if (!safe || player.containerOpen() || player.inventoryMovePending() || player.slotChangePending()) return pause();
    if (waiting_ >= 0) {
        const auto stack = player.hotbar(waiting_);
        if (stack.name != expected_.name || stack.damage != expected_.damage || !permits(stack)) return fail("Replacement inventory move was not confirmed");
        baritone_.logDirect("Worn tool replaced with " + stack.name + "; original retained in inventory"); waiting_ = -1; ticks_ = 0;
    }
    for (int slot = 0; slot < 9; ++slot) {
        const auto worn = player.hotbar(slot);
        if (worn.tool == Tool::None || permits(worn)) continue;
        int best = -1, remaining = -1;
        for (int from = 0; from < 36; ++from) {
            const auto spare = player.hotbar(from);
            if (spare.empty() || spare.tool != worn.tool || spare.toolTier < worn.toolTier || spare.silkTouch != worn.silkTouch || spare.efficiency < worn.efficiency || !permits(spare)) continue;
            const int durability = spare.maxDamage - spare.damage;
            if (durability > remaining) { best = from; remaining = durability; expected_ = spare; }
        }
        if (best < 0) return fail("No suitable spare for " + worn.name);
        if (!player.swapSlots(best, slot)) return fail("Could not submit a normal replacement swap");
        waiting_ = slot; ticks_ = 0; return pause();
    }
    ticks_ = 0; return PathingCommand{nullptr, PathingCommandType::Defer};
}
}
