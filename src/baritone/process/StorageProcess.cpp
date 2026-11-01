#include "baritone/process/StorageProcess.h"
#include "baritone/Baritone.h"
#include "baritone/cache/WorldScanner.h"
#include <algorithm>
#include <fstream>
#include <format>
#include <iomanip>
namespace baritone {
void StorageProcess::addLocation(std::string name, BlockPos position) {
    if (name.empty() || name.size() > 128 || name.find_first_of("\r\n") != std::string::npos || locations_.size() >= 256)
        throw std::invalid_argument("Storage names must be 1..128 characters; at most 256 locations");
    if (locations_.contains(name)) throw std::invalid_argument("Storage location already exists");
    locations_.emplace(std::move(name), position);
}
void StorageProcess::removeLocation(const std::string& name) {
    if (!locations_.erase(name)) throw std::invalid_argument("Unknown storage location");
}
void StorageProcess::save(const std::filesystem::path& file) const {
    std::filesystem::create_directories(file.parent_path()); auto tmp = file; tmp += ".tmp";
    std::ofstream out(tmp); out << "BARITONE_STORAGE 1\n";
    for (const auto& [name, pos] : locations_) out << std::quoted(name) << ' ' << pos.x << ' ' << pos.y << ' ' << pos.z << '\n';
    out.close(); if (!out) throw std::runtime_error("Could not save storage locations"); std::filesystem::rename(tmp, file);
}
void StorageProcess::load(const std::filesystem::path& file) {
    onLostControl(); locations_.clear();
    if (!std::filesystem::exists(file)) return;
    if (std::filesystem::file_size(file) > 128 * 1024) throw std::invalid_argument("Storage file is too large");
    std::ifstream in(file); std::string magic; int version;
    if (!(in >> magic >> version) || magic != "BARITONE_STORAGE" || version != 1) throw std::invalid_argument("Invalid storage file");
    std::map<std::string, BlockPos> parsed;
    while (in >> std::ws && in.peek() != EOF) {
        std::string name; BlockPos pos;
        if (!(in >> std::quoted(name) >> pos.x >> pos.y >> pos.z) || name.empty() || name.size() > 128 || parsed.size() >= 256 || !parsed.emplace(name, pos).second)
            throw std::invalid_argument("Invalid storage entry");
    }
    locations_ = std::move(parsed);
}
void StorageProcess::start(const std::string& location, std::string item, int count, bool withdraw, bool returnToStart) {
    if (active_ || baritone_.player().containerOpen() || baritone_.player().inventoryMovePending())
        throw std::invalid_argument("Stop the current storage task and wait for its container to close first");
    auto found = locations_.find(location);
    if (found == locations_.end()) throw std::invalid_argument("Unknown storage location");
    if (count < 1 || count > 2304) throw std::invalid_argument("Transfer quantity must be 1..2304");
    onLostControl(); target_ = found->second; origin_ = baritone_.playerFeet(); item_ = BlockFilter::normalize(std::move(item));
    location_ = location; remaining_ = requested_ = count; withdraw_ = withdraw; return_ = returnToStart;
    succeeded_ = false; failure_.clear(); active_ = true; timeout_ = 0;
}
void StorageProcess::onLostControl() {
    baritone_.player().closeOwnedContainer(); active_ = waiting_ = closing_ = false;
}
bool StorageProcess::isActive() { return active_ && !baritone_.pauser().paused(); }
std::string StorageProcess::displayName() const {
    return std::format("Storage {} {}: {}/{}", withdraw_ ? "withdraw" : "deposit", item_, requested_ - remaining_, requested_);
}
std::optional<PathingCommand> StorageProcess::onTick(bool failed, bool safe) {
    if (!active_) return std::nullopt;
    auto& player = baritone_.player();
    auto fail = [&](std::string reason) -> std::optional<PathingCommand> {
        failure_ = std::move(reason); baritone_.logDirect(failure_, true); onLostControl();
        return PathingCommand{nullptr, PathingCommandType::CancelAndSetGoal};
    };
    if (failed) return fail("No path to storage location or return point");
    if (closing_) {
        player.closeOwnedContainer();
        if (player.containerOpen() || player.inventoryMovePending()) return PathingCommand{nullptr, PathingCommandType::RequestPause};
        if (return_ && origin_.distSq(baritone_.playerFeet()) > 1)
            return PathingCommand{std::make_shared<GoalNear>(origin_, 1), PathingCommandType::RevalidateGoalAndPath};
        succeeded_ = true; active_ = false; baritone_.logDirect("Storage transfer complete; returned to the task's start position");
        return PathingCommand{nullptr, PathingCommandType::CancelAndSetGoal};
    }
    if (!player.ownedContainerReady()) {
        if (target_.distSq(baritone_.playerFeet()) > 9)
            return PathingCommand{std::make_shared<GoalGetToBlock>(target_), PathingCommandType::RevalidateGoalAndPath};
        if (!safe) return PathingCommand{nullptr, PathingCommandType::Defer};
        if (++timeout_ > 120) return fail(player.transferError().empty() ? "Container could not be opened; check reach and line of sight" : player.transferError());
        player.openContainerAt(target_);
        return PathingCommand{nullptr, PathingCommandType::RequestPause};
    }
    if (waiting_) {
        if (player.transferState() == TransferState::Rejected) return fail(player.transferError());
        if (player.transferState() == TransferState::Accepted) {
            remaining_ -= sent_; waiting_ = false; timeout_ = 0;
            if (!remaining_) { closing_ = true; player.closeOwnedContainer(); }
        } else if (++timeout_ > 140) return fail("Storage transfer response timed out");
        return PathingCommand{nullptr, PathingCommandType::RequestPause};
    }
    if (player.inventoryMovePending()) return PathingCommand{nullptr, PathingCommandType::RequestPause};
    const auto slots = player.containerSlots();
    auto source = std::ranges::find_if(slots, [&](const ContainerSlot& slot) {
        return slot.playerInventory != withdraw_ && !slot.stack.empty() && slot.stack.name == item_;
    });
    if (source == slots.end()) return fail(std::format("Storage source has no more {}; {} still needed", item_, remaining_));
    auto destination = std::ranges::find_if(slots, [&](const ContainerSlot& slot) {
        // Workstation input/output slots are not storage destinations.
        bool storage = slot.collection.find("container") != std::string::npos || slot.collection.find("barrel") != std::string::npos || slot.collection.find("shulker") != std::string::npos;
        return slot.playerInventory == withdraw_ && slot.stack.empty() && (slot.playerInventory || storage);
    });
    if (destination == slots.end()) return fail("No empty destination slot; partial stacks are preserved");
    sent_ = std::min(remaining_, source->stack.count);
    if (player.transferContainerItem(*source, *destination, sent_)) { waiting_ = true; timeout_ = 0; }
    else if (++timeout_ > 100) return fail("Could not submit the storage transfer");
    return PathingCommand{nullptr, PathingCommandType::RequestPause};
}
}
