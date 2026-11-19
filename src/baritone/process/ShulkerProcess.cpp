#include "baritone/process/ShulkerProcess.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>

#include "baritone/Baritone.h"
#include "baritone/Settings.h"
#include "baritone/inventory/StorageTransfers.h"

namespace baritone {
namespace {

bool isShulker(std::string_view name) { return name == "minecraft:shulker_box" || name.ends_with("_shulker_box"); }

std::optional<std::map<int, ShulkerItem>> observeContents(const std::vector<ContainerSlot>& slots) {
    std::array<bool, 27> seen{};
    std::map<int, ShulkerItem> items;
    for (const auto& slot : slots) {
        if (!storageSlot(slot)) continue;
        if (slot.slot < 0 || slot.slot >= 27 || seen[slot.slot]) return std::nullopt;
        seen[slot.slot] = true;
        if (slot.stack.empty()) continue;
        if (!slot.stack.stackKey.ends_with(":0") || slot.stack.count > 64 || isShulker(slot.stack.name)) return std::nullopt;
        items.emplace(slot.slot, ShulkerItem{slot.stack.name, slot.stack.count});
    }
    if (!std::ranges::all_of(seen, [](bool present) { return present; })) return std::nullopt;
    return items;
}

bool recoveryAreaClear(PlayerContext& player, BlockPos pos) {
    const auto center = Vec3d::center(pos);
    return std::ranges::none_of(player.entities(), [&](const auto& entity) {
        return entity.isItem && (entity.itemName.empty() || isShulker(entity.itemName)) && (entity.position - center).lengthSq() <= 36;
    });
}

}  // namespace

void ShulkerProcess::start(int slot, std::string item, int count, BlockPos position) {
    auto& player = baritone_.player();
    const auto& world = baritone_.world();
    if (!world || !player.valid() || player.isCreative() || player.containerOpen() || player.inventoryMovePending() || player.ridingState().value_or(false))
        throw std::invalid_argument("Start on foot in a Survival world with all containers closed");
    if (slot < 0 || slot >= 9 || count < 1 || count > 1728) throw std::invalid_argument("Use hotbar slot 1..9 and a withdrawal of 1..1728 items");
    if (std::abs(int64_t(position.x)) > 30000000 || std::abs(int64_t(position.z)) > 30000000 ||
        position.y <= world->minY() || position.y >= world->maxY() - 1 || position.distSq(baritone_.playerFeet()) > 9)
        throw std::invalid_argument("Choose a reachable shulker position within three blocks");
    auto box = player.portableShulker(slot);
    if (!box) throw std::invalid_argument("The selected box has unsupported or unreadable contents");
    item = BlockFilter::normalize(std::move(item));
    int available = 0;
    for (const auto& [index, stack] : box->items) if (stack.name == item) available += stack.count;
    if (available < count) throw std::invalid_argument("The box does not hold that quantity of the requested item");
    if (!recoveryAreaClear(player, position)) throw std::invalid_argument("Pick up dropped shulker boxes around the placement before starting");
    expected_ = std::move(*box);
    slot_ = slot;
    previousSlot_ = player.selectedSlot();
    position_ = position;
    item_ = std::move(item);
    remaining_ = count;
    leftInWorld_ = false;
    advance(Phase::Place);
}

bool ShulkerProcess::isActive() { return phase_ != Phase::Idle && !baritone_.pauser().paused(); }

void ShulkerProcess::onLostControl() {
    if (phase_ == Phase::Idle) return;
    auto& player = baritone_.player();
    if (player.valid()) {
        player.stopBreaking();
        player.closeOwnedContainer();
        if (!player.containerOpen() && !player.inventoryMovePending() && player.selectedSlot() != previousSlot_)
            player.selectSlot(previousSlot_);
    }
    if (leftInWorld_) baritone_.logDirect(std::format("Shulker recovery was not confirmed. Check the box or dropped item at {}", position_), true);
    advance(Phase::Idle);
}

void ShulkerProcess::customizeContext(CalculationContext& ctx) {
    ctx.allowBreak = false;
    ctx.allowBreakAnyway.clear();
    ctx.allowPlace = false;
}

std::string ShulkerProcess::displayName() const {
    constexpr const char* stages[]{"Portable shulker idle", "Placing shulker", "Confirming shulker placement", "Opening shulker",
        "Withdrawing supplies", "Confirming withdrawal", "Closing shulker", "Recovering shulker", "Picking up shulker"};
    return stages[static_cast<int>(phase_)];
}

std::optional<PathingCommand> ShulkerProcess::onTick(bool failed, bool safe) try {
    if (!isActive()) return {};
    const PathingCommand pause{nullptr, PathingCommandType::RequestPause};
    const auto stop = [&](std::string message, bool error = true) -> PathingCommand {
        baritone_.logDirect(message, error);
        onLostControl();
        return {nullptr, PathingCommandType::CancelAndSetGoal};
    };
    auto& player = baritone_.player();
    auto* ctx = baritone_.liveContext();
    if (failed || !ctx || player.isCreative() || !player.valid() || ++ticks_ > 140)
        return stop("Portable shulker step timed out or the player/world is unavailable");
    if (!ctx->isLoadedLive(position_.x, position_.z) || !ctx->permitsModify(position_))
        return stop("The shulker position is protected or unloaded");
    if (!safe) return pause;
    const auto& current = ctx->props(position_.x, position_.y, position_.z);
    auto aimedAt = [&](Vec3d point) {
        const auto eye = player.eyePosition();
        return rayTrace(ctx->bsi, eye, rotation_utils::calcRotationFromVec3d(eye, point, player.rotation()), player.blockReachDistance());
    };

    if (phase_ == Phase::Place) {
        if (!Settings::get().allowPlace || !Settings::get().allowBreak || player.containerOpen() || player.inventoryMovePending())
            return stop("Portable storage requires allowPlace, allowBreak and a closed inventory");
        const auto below = position_.below();
        const auto& floor = ctx->props(below.x, below.y, below.z);
        if (current.type != BlockType::Air || ctx->props(position_.x, position_.y + 1, position_.z).type != BlockType::Air ||
            !floor.fullCube || floor.hasFluid() || floor.type == BlockType::Magma || floor.type == BlockType::Cactus ||
            !ctx->permitsModify(below) || movement_helper::intersectsPlayer(player, position_))
            return stop("Choose two empty cells above a safe solid block, clear of the player");
        auto hit = aimedAt({position_.x + .5, position_.y - .001, position_.z + .5});
        if (!hit || hit->pos != below || hit->face != Direction::Up) return stop("The floor under the shulker is outside clear reach");
        if (player.portableShulker(slot_) != expected_) return stop("The selected shulker changed before placement");
        if (player.selectedSlot() != slot_) { player.selectSlot(slot_); return pause; }
        if (player.slotChangePending()) return pause;
        player.useItemOn(hit->pos, hit->face, hit->hit);
        leftInWorld_ = true;
        advance(Phase::Confirm);
        return pause;
    }
    if (phase_ == Phase::Confirm) {
        if (current.name == expected_.block && player.hotbar(slot_).empty()) advance(Phase::Open);
        else if (current.type != BlockType::Air && current.name != expected_.block) return stop("A different block appeared at the placement");
        return pause;
    }
    if (phase_ == Phase::Pickup) {
        int matches = 0;
        for (int slot = 0; slot < 36; ++slot) if (player.portableShulker(slot) == expected_) ++matches;
        if (matches > 1) return stop("Multiple copies of the recovered box appeared; verify the inventory");
        if (matches == 1) { leftInWorld_ = false; return stop("Portable shulker withdrawal complete; the box and its remaining contents are back in inventory", false); }
        if (ticks_ < 5 || baritone_.playerFeet().distSq(position_) == 0) return pause;
        if (current.type != BlockType::Air) return stop("The shulker return path changed");
        return PathingCommand{std::make_shared<GoalBlock>(position_), PathingCommandType::RevalidateGoalAndPath};
    }
    if (phase_ == Phase::Break && current.type == BlockType::Air) {
        player.stopBreaking();
        advance(Phase::Pickup);
        return pause;
    }
    if (current.name != expected_.block) return stop("The block at the shulker position changed");

    if (phase_ == Phase::Open) {
        if (player.ownedContainerReady()) advance(Phase::Transfer);
        else player.openContainerAt(position_);
        return pause;
    }
    if (phase_ == Phase::Transfer || phase_ == Phase::Receipt) {
        if (!player.ownedContainerReady()) return stop("The owned shulker screen closed before completion");
        if (ticks_ < 3) return pause;
        auto slots = player.containerSlots();
        const auto observed = observeContents(slots);
        if (phase_ == Phase::Receipt) {
            if (player.transferState() == TransferState::Rejected) return stop("Shulker transfer rejected: " + player.transferError());
            if (player.transferState() != TransferState::Accepted || observed != afterTransfer_.items ||
                player.countItem(item_) != inventoryBefore_ + sent_) return pause;
            expected_ = std::move(afterTransfer_);
            remaining_ -= sent_;
            advance(Phase::Transfer);
            return pause;
        }
        if (observed != expected_.items) return stop("The open box contents differ from the recorded inventory snapshot");
        if (!remaining_) { player.closeOwnedContainer(); advance(Phase::Close); return pause; }
        if (player.inventoryMovePending()) return pause;
        std::erase_if(slots, [&](const auto& slot) { return slot.playerInventory && slot.slot == slot_; });
        const auto transfer = planStorageTransfer(slots, item_, remaining_, true);
        if (!transfer) return stop("No inventory room for the remaining withdrawal and the recovered box");
        const auto& source = slots[transfer->source];
        sent_ = transfer->count;
        afterTransfer_ = expected_;
        auto& changed = afterTransfer_.items.at(source.slot);
        changed.count -= sent_;
        if (!changed.count) afterTransfer_.items.erase(source.slot);
        inventoryBefore_ = player.countItem(item_);
        if (!player.transferContainerItem(source, slots[transfer->destination], sent_)) return stop("Could not submit the shulker withdrawal");
        advance(Phase::Receipt);
        return pause;
    }
    if (phase_ == Phase::Close) {
        if (player.containerOpen() || player.inventoryMovePending()) { player.closeOwnedContainer(); return pause; }
        if (!player.hotbar(slot_).empty() || !recoveryAreaClear(player, position_)) return stop("Clear the original slot and nearby shulker item drops before recovery");
        for (int slot = 0; slot < 36; ++slot)
            if (player.portableShulker(slot) == expected_) return stop("An identical box is already in inventory; recovery cannot be confirmed unambiguously");
        advance(Phase::Break);
    }
    if (!Settings::get().allowBreak || player.containerOpen()) return stop("The shulker is still placed; enable breaking and close the inventory before recovery");
    const auto hit = aimedAt(Vec3d::center(position_));
    if (!hit || hit->pos != position_) return stop("The shulker is outside clear breaking reach");
    if (!player.slotChangePending()) player.startOrContinueBreaking(position_, hit->face);
    return pause;
} catch (const std::exception& error) {
    baritone_.logDirect(std::format("{} failed: {}", displayName(), error.what()), true);
    onLostControl();
    return PathingCommand{nullptr, PathingCommandType::CancelAndSetGoal};
}

}
