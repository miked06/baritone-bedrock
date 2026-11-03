#include "baritone/process/RepairProcess.h"
#include "baritone/Baritone.h"
#include <format>
namespace baritone {
void RepairProcess::start(std::string method, BlockPos station, int toolSlot, int materialSlot, int count, int maxLevels, bool execute) {
    auto& p = baritone_.player();
    if (active_ || !p.valid() || p.containerOpen() || p.inventoryMovePending() || toolSlot < 0 || toolSlot >= 36 || materialSlot < 0 || materialSlot >= 36 || toolSlot == materialSlot)
        throw std::invalid_argument("Close containers, finish inventory actions and choose two distinct slots 0..35");
    const auto tool = p.hotbar(toolSlot), material = p.hotbar(materialSlot);
    validateRepairInputs(method, tool, material, count, maxLevels);
    int free = 0; for (int i = 0; i < 36; ++i) free += p.hotbar(i).empty();
    if (free < 2) throw std::invalid_argument("Repair needs two empty inventory slots for normal output/input recovery");
    tool_ = tool; material_ = material; method_ = std::move(method); station_ = station; origin_ = baritone_.playerFeet();
    toolSlot_ = toolSlot; materialSlot_ = materialSlot; count_ = count; maxLevels_ = maxLevels; execute_ = execute;
    ticks_ = phaseTicks_ = 0; phase_ = Phase::Open; active_ = true; succeeded_ = false; failure_.clear(); expected_ = {};
    originalTools_ = matchingTools(); originalMaterials_ = p.countItem(material_.name);
}
bool RepairProcess::isActive() { return active_ && !baritone_.pauser().paused(); }
void RepairProcess::onLostControl() { if (active_) baritone_.player().closeOwnedContainer(); active_ = false; }
int RepairProcess::matchingTools() const {
    int count = 0; for (int i = 0; i < 36; ++i) { auto s = baritone_.player().hotbar(i); if (sameRepairItem(s, tool_)) count += s.count; } return count;
}
std::optional<ContainerSlot> RepairProcess::inputSlot(bool material) const {
    const int network = (method_ == "anvil" ? 0 : 50) + int(material);
    for (const auto& slot : baritone_.player().containerSlots()) if (!slot.playerInventory && slot.networkName == network && slot.slot == 0) return slot;
    return {};
}
std::optional<PathingCommand> RepairProcess::onTick(bool failed, bool safe) {
    if (!active_) return {};
    auto& p = baritone_.player(); ++phaseTicks_;
    const PathingCommand pause{nullptr, PathingCommandType::RequestPause};
    auto fail = [&](std::string reason) -> std::optional<PathingCommand> {
        failure_ = std::move(reason); baritone_.logDirect(failure_ + "; inspect returned workstation inputs before retrying", true); onLostControl();
        return PathingCommand{nullptr, PathingCommandType::CancelAndSetGoal};
    };
    if (failed || ++ticks_ > 20 * 60 * 10) return fail("Repair stopped: path failure or ten-minute deadline");
    if (phase_ == Phase::Return) {
        if (origin_.distSq(baritone_.playerFeet()) > 1) return PathingCommand{std::make_shared<GoalNear>(origin_, 1), PathingCommandType::RevalidateGoalAndPath};
        active_ = false; succeeded_ = true;
        baritone_.logDirect(execute_ ? "Repair complete; server response and improved inventory output confirmed" : "Repair preview complete; inputs returned without taking the result");
        return PathingCommand{nullptr, PathingCommandType::CancelAndSetGoal};
    }
    if (phase_ == Phase::Close) {
        p.closeOwnedContainer();
        if (p.containerOpen() || p.inventoryMovePending()) { if (phaseTicks_ > 180) return fail("Repair interface did not close"); return pause; }
        if (!execute_ && (matchingTools() != originalTools_ || p.countItem(material_.name) != originalMaterials_))
            return fail("Preview input recovery could not be confirmed");
        next(Phase::Return); return pause;
    }
    if (!p.ownedContainerReady()) {
        if (phase_ != Phase::Open) return fail("The repair interface closed during the operation");
        if (station_.distSq(baritone_.playerFeet()) > 9) { phaseTicks_ = 0; return PathingCommand{std::make_shared<GoalGetToBlock>(station_), PathingCommandType::RevalidateGoalAndPath}; }
        if (!safe) return pause;
        if (phaseTicks_ > 200) return fail("Could not open the registered repair station");
        p.openContainerAt(station_); return pause;
    }
    if (phase_ == Phase::Open) {
        auto preview = p.repairPreview();
        auto input = inputSlot(false), material = inputSlot(true);
        if (!preview.supported || preview.method != method_ || !input || !material) return fail("The registered block is not a supported " + method_ + " interface");
        if (!input->stack.empty() || !material->stack.empty()) return fail("Repair requires initially empty workstation inputs");
        next(Phase::Tool); return pause;
    }
    if (phase_ == Phase::ToolReceipt || phase_ == Phase::MaterialReceipt) {
        if (p.transferState() == TransferState::Rejected) return fail(p.transferError());
        if (phaseTicks_ > 160) return fail("Repair input transfer was not confirmed; no repeated transfer was attempted");
        if (p.transferState() != TransferState::Accepted || p.inventoryMovePending()) return pause;
        const bool material = phase_ == Phase::MaterialReceipt;
        auto target = inputSlot(material); const auto source = p.hotbar(material ? materialSlot_ : toolSlot_);
        const int sourceCount = material ? material_.count - count_ : 0;
        if (!target || target->stack.count != (material ? count_ : 1) || !sameRepairItem(target->stack, material ? material_ : tool_) ||
            (sourceCount == 0 ? !source.empty() : source.count != sourceCount || !sameRepairItem(source, material_))) return pause;
        next(material ? Phase::Preview : Phase::Material); return pause;
    }
    if (p.inventoryMovePending() || !safe) return pause;
    if (phase_ == Phase::Tool || phase_ == Phase::Material) {
        const bool material = phase_ == Phase::Material; const int slot = material ? materialSlot_ : toolSlot_;
        const auto current = p.hotbar(slot); const auto& original = material ? material_ : tool_;
        auto target = inputSlot(material);
        if (!sameRepairItem(current, original) || current.count != original.count || !target || !target->stack.empty()) return fail("Repair inventory/input changed before submission");
        if (!p.transferContainerItem({"player", slot, true, current}, *target, material ? count_ : 1)) return fail("Could not submit the normal repair input transfer");
        next(material ? Phase::MaterialReceipt : Phase::ToolReceipt); return pause;
    }
    if (phase_ == Phase::Preview) {
        if (phaseTicks_ < 6) return pause;
        auto preview = p.repairPreview();
        if (preview.output.empty() && phaseTicks_ < 80) return pause;
        try { validateRepairOutput(preview, method_, tool_, count_, maxLevels_); }
        catch (const std::exception& error) { return fail(error.what()); }
        auto input = inputSlot(false), material = inputSlot(true);
        if (!input || !material || !sameRepairItem(input->stack, tool_) || !sameRepairItem(material->stack, material_) || material->stack.count != count_)
            return fail("Repair inputs changed after preview calculation");
        baritone_.logDirect(std::format("Native {} repair preview: damage {} -> {}, cost {} levels, {} material item(s)",method_,tool_.damage,preview.output.damage,preview.levels,preview.materialCost));
        expected_ = preview;
        if (!execute_) { next(Phase::Close); return pause; }
        if (!p.takeRepairOutput(expected_)) return fail(p.repairError().empty() ? "The verified repair could not be submitted" : p.repairError());
        next(Phase::Result); return pause;
    }
    if (phase_ == Phase::Result) {
        if (p.repairState() == TransferState::Rejected) return fail(p.repairError());
        if (phaseTicks_ > 180) return fail("Repair response or consumed-input state was not confirmed");
        auto input = inputSlot(false), material = inputSlot(true);
        const int remaining = count_ - expected_.materialCost;
        if (p.repairState() != TransferState::Accepted || !input || !input->stack.empty() || !material ||
            (remaining == 0 ? !material->stack.empty() : material->stack.count != remaining || !sameRepairItem(material->stack, material_))) return pause;
        next(Phase::Close); return pause;
    }
    return pause;
}
}
