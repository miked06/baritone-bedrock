#include "baritone/process/StorageProcess.h"
#include "baritone/Baritone.h"
#include "baritone/cache/WorldScanner.h"
#include "baritone/inventory/StorageTransfers.h"
#include <algorithm>
#include <fstream>
#include <format>
#include <iomanip>
#include <ctime>
#include <tuple>
namespace baritone {
void StorageProcess::addLocation(std::string name, BlockPos position) {
    if (name.empty() || name.size() > 128 || name.find_first_of("\r\n") != std::string::npos || locations_.size() >= 256)
        throw std::invalid_argument("Storage names must be 1..128 characters; at most 256 locations");
    if (locations_.contains(name)) throw std::invalid_argument("Storage location already exists");
    locations_.emplace(std::move(name), position);
}
void StorageProcess::removeLocation(const std::string& name) {
    if (!locations_.erase(name)) throw std::invalid_argument("Unknown storage location");
    observations_.erase(name);
}
void StorageProcess::save(const std::filesystem::path& file) const {
    std::filesystem::create_directories(file.parent_path()); auto tmp = file; tmp += ".tmp";
    std::ofstream out(tmp); out << "BARITONE_STORAGE 1\n";
    for (const auto& [name, pos] : locations_) out << std::quoted(name) << ' ' << pos.x << ' ' << pos.y << ' ' << pos.z << '\n';
    out.close(); if (!out) throw std::runtime_error("Could not save storage locations"); std::filesystem::rename(tmp, file);
}
void StorageProcess::load(const std::filesystem::path& file) {
    onLostControl(); locations_.clear(); observations_.clear();
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
    if (count < 1 || count > 2304) throw std::invalid_argument("Transfer quantity must be 1..2304");
    const auto normalized = BlockFilter::normalize(item);
    if (!withdraw && count > baritone_.player().countItem(normalized) - baritone_.loadouts().reserved(normalized))
        throw std::invalid_argument("Deposit would use the active loadout's reserved quantity; adjust the rule or disable the loadout first");
    begin(location, returnToStart); mode_ = Mode::Transfer;
    item_ = normalized; remaining_ = requested_ = count; withdraw_ = withdraw;
}
void StorageProcess::begin(const std::string& location, bool returnToStart) {
    if (active_ || baritone_.player().containerOpen() || baritone_.player().inventoryMovePending())
        throw std::invalid_argument("Stop the current storage task and wait for its container to close first");
    auto found = locations_.find(location);
    if (found == locations_.end()) throw std::invalid_argument("Unknown storage location");
    onLostControl(); target_ = found->second; origin_ = baritone_.playerFeet();
    location_ = location; return_ = returnToStart; remaining_ = requested_ = 0;
    succeeded_ = false; failure_.clear(); active_ = true; timeout_ = operations_ = warmup_ = 0;
    refreshAttempted_ = refreshClosing_ = false; sortMoves_.clear();
}
void StorageProcess::scan(const std::string& location, bool returnToStart) { begin(location, returnToStart); mode_ = Mode::Scan; }
void StorageProcess::sort(const std::string& location, bool returnToStart) { begin(location, returnToStart); mode_ = Mode::Sort; }
void StorageProcess::onLostControl() {
    baritone_.player().closeOwnedContainer(); active_ = waiting_ = closing_ = false;
}
bool StorageProcess::isActive() { return active_ && !baritone_.pauser().paused(); }
std::string StorageProcess::displayName() const {
    if (mode_ != Mode::Transfer) return std::string(mode_ == Mode::Scan ? "Inspecting " : "Sorting ") + location_;
    return std::format("Storage {} {}: {}/{}", withdraw_ ? "withdraw" : "deposit", item_, requested_ - remaining_, requested_);
}
bool StorageProcess::observe(const std::vector<ContainerSlot>& slots) {
    StorageObservation current; current.observedAt = std::time(nullptr);
    for (const auto& slot : slots) if (storageSlot(slot)) {
        ++current.slots;
        if (!slot.stack.empty()) current.items[slot.stack.name] += slot.stack.count;
    }
    if (!current.slots) return false;
    observations_[location_] = std::move(current); return true;
}
bool StorageProcess::issue(const ContainerSlot& source, const ContainerSlot& target, int count) {
    if (!baritone_.player().transferContainerItem(source, target, count)) return false;
    sentSource_ = source; sentTarget_ = target; sent_ = count;
    sourceAfter_ = source.stack.count - count; targetAfter_ = (target.stack.empty() ? 0 : target.stack.count) + count;
    waiting_ = true; timeout_ = 0; refreshAttempted_ = false; ++operations_;
    return true;
}
bool StorageProcess::observedTransfer(const std::vector<ContainerSlot>& slots) const {
    auto matches = [&](const ContainerSlot& key, int count) {
        const auto found = std::ranges::find_if(slots, [&](const ContainerSlot& slot) {
            return slot.playerInventory == key.playerInventory && slot.collection == key.collection && slot.slot == key.slot;
        });
        return found != slots.end() && (count == 0 ? found->stack.empty() : found->stack.name == sentSource_.stack.name && found->stack.count == count);
    };
    return matches(sentSource_, sourceAfter_) && matches(sentTarget_, targetAfter_);
}
std::optional<StorageProcess::PlannedMove> StorageProcess::sortMove(const std::vector<ContainerSlot>& slots) {
    if (!sortMoves_.empty()) {
        auto move = sortMoves_.front();
        auto current = [&](const ContainerSlot& key) -> ContainerSlot {
            auto found = std::ranges::find_if(slots, [&](const ContainerSlot& slot) {
                return slot.playerInventory == key.playerInventory && slot.collection == key.collection && slot.slot == key.slot;
            });
            if (found == slots.end()) throw std::invalid_argument("A sorting slot disappeared");
            return *found;
        };
        move.source = current(move.source); move.target = current(move.target);
        if (move.source.stack.name != sortMoves_.front().source.stack.name || move.source.stack.count != move.count || !move.target.stack.empty())
            throw std::invalid_argument("Sorting state changed; inspect the intermediate slot before resuming");
        return move;
    }
    std::vector<ContainerSlot> storage;
    for (const auto& slot : slots) if (storageSlot(slot)) storage.push_back(slot);
    std::ranges::sort(storage, [](const auto& a, const auto& b) { return std::tie(a.collection, a.slot) < std::tie(b.collection, b.slot); });
    for (std::size_t to = 0; to < storage.size(); ++to) {
        if (storage[to].stack.empty()) continue;
        for (std::size_t from = to + 1; from < storage.size(); ++from) {
            if (storage[from].stack.empty()) continue;
            const int count = std::min(storage[from].stack.count, mergeRoom(storage[from].stack, storage[to].stack));
            if (count > 0) return PlannedMove{storage[from], storage[to], count};
        }
    }
    for (std::size_t to = 0; to < storage.size(); ++to) {
        std::size_t best = to;
        for (std::size_t from = to + 1; from < storage.size(); ++from)
            if (!storage[from].stack.empty() && (storage[best].stack.empty() || storage[from].stack.name < storage[best].stack.name)) best = from;
        if (best == to) continue;
        const auto a = storage[to], b = storage[best];
        if (a.stack.empty()) return PlannedMove{b, a, b.stack.count};
        auto scratch = std::ranges::find_if(slots, [](const auto& slot) { return slot.stack.empty() && (slot.playerInventory || storageSlot(slot)); });
        if (scratch == slots.end()) throw std::invalid_argument("Sorting needs one empty container or inventory slot as temporary space");
        ContainerSlot emptyA = a, emptyB = b, filledScratch = *scratch;
        emptyA.stack = {}; emptyB.stack = {}; filledScratch.stack = a.stack;
        sortMoves_.push_back({a, *scratch, a.stack.count});
        sortMoves_.push_back({b, emptyA, b.stack.count});
        sortMoves_.push_back({filledScratch, emptyB, a.stack.count});
        return sortMoves_.front();
    }
    return {};
}
std::optional<PathingCommand> StorageProcess::onTick(bool failed, bool safe) {
    if (!active_) return std::nullopt;
    auto& player = baritone_.player();
    auto fail = [&](std::string reason) -> std::optional<PathingCommand> {
        failure_ = std::move(reason); baritone_.logDirect(failure_, true); onLostControl();
        return PathingCommand{nullptr, PathingCommandType::CancelAndSetGoal};
    };
    if (failed) return fail("No path to storage location or return point");
    if (!locations_.contains(location_)) return fail("The active storage location was removed");
    if (closing_) {
        player.closeOwnedContainer();
        if (player.containerOpen() || player.inventoryMovePending()) {
            if (++timeout_ > 140) return fail("The owned container did not close; inspect the current screen");
            return PathingCommand{nullptr, PathingCommandType::RequestPause};
        }
        timeout_ = 0;
        if (return_ && origin_.distSq(baritone_.playerFeet()) > 1)
            return PathingCommand{std::make_shared<GoalNear>(origin_, 1), PathingCommandType::RevalidateGoalAndPath};
        succeeded_ = true; active_ = false;
        baritone_.logDirect(std::string(mode_ == Mode::Scan ? "Storage inspection complete" : mode_ == Mode::Sort ? "Container sorting complete" : "Storage transfer complete") + (return_ ? "; returned to the start position" : ""));
        return PathingCommand{nullptr, PathingCommandType::CancelAndSetGoal};
    }
    if (refreshClosing_) {
        player.closeOwnedContainer();
        if (player.containerOpen() || player.inventoryMovePending()) {
            if (++timeout_ > 140) return fail("The container could not close for content verification");
            return PathingCommand{nullptr, PathingCommandType::RequestPause};
        }
        refreshClosing_ = false; warmup_ = 0;
    }
    if (!player.ownedContainerReady()) {
        if (target_.distSq(baritone_.playerFeet()) > 9)
            return PathingCommand{std::make_shared<GoalGetToBlock>(target_), PathingCommandType::RevalidateGoalAndPath};
        if (!safe) return PathingCommand{nullptr, PathingCommandType::Defer};
        if (++timeout_ > 120) return fail(player.transferError().empty() ? "Container could not be opened; check reach and line of sight" : player.transferError());
        player.openContainerAt(target_);
        return PathingCommand{nullptr, PathingCommandType::RequestPause};
    }
    if (++warmup_ < 3) return PathingCommand{nullptr, PathingCommandType::RequestPause};
    if (waiting_) {
        if (player.transferState() == TransferState::Rejected) return fail(player.transferError());
        if (player.transferState() == TransferState::Accepted) {
            const auto slots = player.containerSlots();
            if (!observedTransfer(slots)) {
                if (++timeout_ > 140) return fail("The transfer was accepted but updated slot contents were not observed; inspect the container");
                if (!refreshAttempted_ && timeout_ >= 5) {
                    refreshAttempted_ = refreshClosing_ = true; player.closeOwnedContainer();
                }
                return PathingCommand{nullptr, PathingCommandType::RequestPause};
            }
            observe(slots);
            if (mode_ == Mode::Transfer) remaining_ -= sent_;
            else if (!sortMoves_.empty()) sortMoves_.pop_front();
            waiting_ = false; timeout_ = 0;
            if (mode_ == Mode::Transfer && !remaining_) { closing_ = true; player.closeOwnedContainer(); }
        } else if (++timeout_ > 140) return fail("Storage transfer response timed out");
        return PathingCommand{nullptr, PathingCommandType::RequestPause};
    }
    if (player.inventoryMovePending()) return PathingCommand{nullptr, PathingCommandType::RequestPause};
    const auto slots = player.containerSlots();
    if (!observe(slots)) return fail("The open block has no supported chest, barrel or shulker storage slots");
    if (mode_ == Mode::Scan) { closing_ = true; return PathingCommand{nullptr, PathingCommandType::RequestPause}; }
    if (mode_ == Mode::Sort) {
        if (operations_ >= 4096) return fail("Sorting action limit reached; inspect the current container before restarting");
        try {
            const auto move = sortMove(slots);
            if (!move) { closing_ = true; return PathingCommand{nullptr, PathingCommandType::RequestPause}; }
            if (!issue(move->source, move->target, move->count) && ++timeout_ > 100) return fail("A sorting transfer could not be submitted");
        } catch (const std::exception& error) { return fail(error.what()); }
        return PathingCommand{nullptr, PathingCommandType::RequestPause};
    }
    if (!withdraw_) {
        try {
            if (remaining_ > player.countItem(item_) - baritone_.loadouts().reserved(item_))
                return fail("Inventory changed and the remaining deposit would use reserved loadout items");
        } catch (const std::exception& error) { return fail(error.what()); }
    }
    const auto transfer = planStorageTransfer(slots, item_, remaining_, withdraw_);
    if (!transfer) return fail(std::format("No supported source/destination for {}; {} still needed. Use matching plain stacks or an empty destination", item_, remaining_));
    if (!issue(slots[transfer->source], slots[transfer->destination], transfer->count) && ++timeout_ > 100)
        return fail("Could not submit the storage transfer");
    return PathingCommand{nullptr, PathingCommandType::RequestPause};
}
}
