#include "platform/GamePlayerContext.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string_view>

#include "baritone/Settings.h"
#include "baritone/world/BlockStateInterface.h"
#include "baritone/utils/RayTrace.h"
#include "core/Log.h"
#include "hooks/GameHooks.h"
#include "platform/WorldTracker.h"
#include "sdk/Game.h"
#include "sdk/Recipes.h"

namespace platform {

using baritone::BlockPos;
using baritone::Vec3d;

namespace {

Vec3d toVec(const sdk::Vec3& v) {
    return {v.x, v.y, v.z};
}

sdk::BlockPos toSdk(const BlockPos& p) {
    return {p.x, p.y, p.z};
}

struct ToolInfo {
    baritone::Tool tool = baritone::Tool::None;
    int tier = 0;
    float speed = 1;
};

ToolInfo toolOf(std::string_view name) {
    using baritone::Tool;
    ToolInfo t;
    if (name.ends_with("_pickaxe")) t.tool = Tool::Pickaxe;
    else if (name.ends_with("_axe")) t.tool = Tool::Axe;
    else if (name.ends_with("_shovel")) t.tool = Tool::Shovel;
    else if (name.ends_with("_hoe")) t.tool = Tool::Hoe;
    else if (name.ends_with("_sword")) t.tool = Tool::Sword;
    else if (name == "minecraft:shears") {
        t.tool = Tool::Shears;
        t.speed = 1.5f;
        return t;
    } else return t;
    struct Tier {
        std::string_view prefix;
        int tier;
        float speed;
    };
    static constexpr Tier tiers[] = {{"minecraft:wooden_", 1, 2}, {"minecraft:stone_", 2, 4},
                                     {"minecraft:iron_", 3, 6},   {"minecraft:golden_", 1, 12},
                                     {"minecraft:diamond_", 4, 8}, {"minecraft:netherite_", 5, 9},
                                     {"minecraft:copper_", 2, 5}};
    for (const Tier& tier : tiers) {
        if (name.starts_with(tier.prefix)) {
            t.tier = tier.tier;
            t.speed = t.tool == Tool::Sword ? 1.5f : tier.speed;
        }
    }
    return t;
}

}  // namespace

Vec3d GamePlayerContext::position() const {
    return toVec(player_->feet());
}

Vec3d GamePlayerContext::eyePosition() const {
    return toVec(player_->stateVector()->pos);  // the state vector of players is at eye height
}

Vec3d GamePlayerContext::velocity() const {
    return toVec(player_->stateVector()->velocity);
}

baritone::Rotation GamePlayerContext::rotation() const {
    const sdk::Vec2& r = player_->rotation()->rotation;
    return {r.y, r.x};
}

bool GamePlayerContext::onGround() const {
    // Standing on something: the top of a colliding block under one corner of the
    // bounding box is at feet level and we're not going up.
    if (velocity().y > 0.01) return false;
    auto snapshot = WorldTracker::get().snapshot();
    if (!snapshot) return false;
    baritone::BlockStateInterface bsi(snapshot);
    const sdk::AABB& box = player_->aabbShape()->box;
    double feetY = box.min.y;
    int by = static_cast<int>(std::floor(feetY - 0.01));
    const double xs[2] = {box.min.x + 0.001, box.max.x - 0.001};
    const double zs[2] = {box.min.z + 0.001, box.max.z - 0.001};
    for (double x : xs) {
        for (double z : zs) {
            int bx = static_cast<int>(std::floor(x)), bz = static_cast<int>(std::floor(z));
            for (int y : {by, by - 1}) {  // fences are 1.5 high
                const baritone::BlockProps& p = bsi.props(bx, y, bz);
                if (!p.collides) continue;
                double top = y + (p.topSlab ? 1.0 : static_cast<double>(p.height));
                if (std::fabs(feetY - top) < 0.02) return true;
            }
        }
    }
    return false;
}

bool GamePlayerContext::isSneaking() const {
    auto* input = player_->moveInput();
    return input && input->sneaking;
}

bool GamePlayerContext::isInWater() const {
    auto snapshot = WorldTracker::get().snapshot();
    if (!snapshot) return false;
    baritone::BlockStateInterface bsi(snapshot);
    Vec3d p = position();
    return bsi.props(static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z)))
               .fluid == baritone::Fluid::Water;
}

double GamePlayerContext::blockReachDistance() const {
    return baritone::Settings::get().blockReachDistance;
}

int GamePlayerContext::selectedSlot() const {
    return player_->supplies()->selectedSlot();
}

void GamePlayerContext::selectSlot(int slot) {
    if (slot < 0 || slot > 8) return;
    int& selected = player_->supplies()->selectedSlot();
    if (selected == slot) return;
    // LocalPlayer's tick notices the change and sends the MobEquipmentPacket itself.
    if (breaking_) stopBreaking();
    selected = slot;
    slotChangedAt_ = tick_;
}

namespace {
baritone::HotbarStack describe(const sdk::ItemStack* stack);
}

baritone::HotbarStack GamePlayerContext::hotbar(int slot) const {
    if (!player_ || slot<0 || slot>=36) return {};
    auto* inv = player_->supplies()->inventory();
    if (!inv) return {};
    return describe(inv->getItem(slot));
}

baritone::HotbarStack GamePlayerContext::armor(int slot) const {
    if (!player_ || slot < 0 || slot > 3) return {};
    auto* armor = player_->armorContainer();
    return armor ? describe(armor->getItem(slot)) : baritone::HotbarStack{};
}

namespace {
baritone::HotbarStack describe(const sdk::ItemStack* stack) {
    baritone::HotbarStack out;
    if (!stack || stack->empty()) return out;
    out.name = stack->item()->name();
    out.count = stack->count();
    out.isBlock = stack->block() != nullptr;
    ToolInfo t = toolOf(out.name);
    out.tool = t.tool;
    out.toolTier = t.tier;
    out.toolSpeed = t.speed;
    if (out.tool != baritone::Tool::None || stack->maxDamage() > 0) {
        out.maxDamage = stack->maxDamage();
        out.damage = out.maxDamage > 0 ? stack->damage() : 0;
        for (auto [id, lvl] : stack->enchants()) {
            if (id == 15) out.efficiency = lvl;  // Bedrock enchantment ids
            if (id == 16) out.silkTouch = lvl > 0;
            if (id == 25) out.frostWalker = lvl;
            if (id == 7) out.depthStrider = lvl;
        }
    }
    return out;
}
}  // namespace

bool GamePlayerContext::isGliding() const {
    // FallFlyTicksComponent: ticks spent gliding, present only while gliding
    if (!player_) return false;
    auto* ticks = static_cast<const int32_t*>(player_->entityContext().raw(componentHash("FallFlyTicksComponent")));
    // the component can stay a moment after touching the ground: on the ground is never gliding
    return ticks && *ticks > 0 && !onGround();
}

bool GamePlayerContext::isFlying() const {
    return player_ && player_->isFlying();
}

int GamePlayerContext::effectAmplifier(std::string_view effect) const {
    // Bedrock effect ids
    static constexpr std::pair<std::string_view, uint32_t> ids[] = {
        {"speed", 1}, {"slowness", 2}, {"haste", 3}, {"mining_fatigue", 4}, {"strength", 5},
        {"jump_boost", 8}, {"water_breathing", 13}, {"invisibility", 14}, {"night_vision", 16},
        {"levitation", 24}, {"conduit_power", 26}, {"slow_falling", 27},
    };
    if (!player_) return -1;
    for (auto [name, id] : ids)
        if (name == effect) return player_->effectAmplifier(id);
    return -1;
}

int GamePlayerContext::foodLevel() const {
    if (!player_) return 20;
    auto hunger = player_->attribute("minecraft:player.hunger");
    return hunger ? static_cast<int>(hunger->value) : 20;
}

bool GamePlayerContext::isCreative() const {
    // ActorGameTypeComponent starts with the GameType: 0 survival, 1 creative, 2 adventure, 6 spectator
    if (!player_) return false;
    auto* type = static_cast<const int32_t*>(player_->entityContext().raw(componentHash("ActorGameTypeComponent")));
    return type && *type == 1;
}

bool GamePlayerContext::useHeldItem() {
    // a right click through the game's mouse queue: the game itself uses the item and tells the server
    if (!player_) return false;
    gamehooks::requestClick(2);
    return true;
}
float GamePlayerContext::health() const {
    if (!player_) return 0;
    auto value = player_->attribute("minecraft:health"); return value ? value->value : 20;
}
bool GamePlayerContext::holdUseItem(bool held) {
    gamehooks::holdUseItem(held ? 150 : 0); return valid();
}
std::optional<bool> GamePlayerContext::ridingState() const { return player_ ? player_->dataFlag(2) : std::nullopt; }
bool GamePlayerContext::interactEntity(uint64_t id) { return actOnEntity(id, false); }
bool GamePlayerContext::attackEntity(uint64_t id) { return actOnEntity(id, true); }
bool GamePlayerContext::actOnEntity(uint64_t id, bool attack) {
    if (!player_ || !level_ || inventoryMovePending() || slotChangePending()) return false;
    auto world = WorldTracker::get().snapshot(); if (!world) return false;
    for (auto* actor : level_->actors()) {
        if (!actor || actor == player_ || actor->runtimeId() != id || actor->isPlayer() || !actor->aabbShape() || actor->isDead()) continue;
        const auto& box = actor->aabbShape()->box;
        sdk::Vec3 hit{(box.min.x + box.max.x) * .5f, (box.min.y + box.max.y) * .5f, (box.min.z + box.max.z) * .5f};
        const auto target = toVec(hit), eye = eyePosition();
        if (!std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z) ||
            std::abs(target.x)>30000000 || std::abs(target.z)>30000000 || std::abs(target.y)>1000000) return false;
        if ((target - eye).lengthSq() > blockReachDistance() * blockReachDistance()) return false;
        const BlockPos pos{int(std::floor(target.x)), int(std::floor(box.min.y)), int(std::floor(target.z))};
        if (auto policy = world->protection(); policy && !policy->permitsModify(pos)) return false;
        baritone::BlockStateInterface blocks(world);
        auto aim = baritone::rotation_utils::calcRotationFromVec3d(eye, target, rotation());
        auto block = baritone::rayTrace(blocks, eye, aim, blockReachDistance());
        if (block && (block->hit - eye).lengthSq() + .01 < (target - eye).lengthSq()) return false;
        // This API reports dispatch. The native return value does not confirm
        // the server's outcome (feeding can succeed while it is false); processes
        // confirm breeding/death/riding state separately from live metadata.
        if (attack) player_->gameMode()->attack(*actor, hit);
        else player_->gameMode()->interact(*actor, hit);
        return true;
    }
    return false;
}

void GamePlayerContext::useHeldItemWhenTargeting() {
    if (player_) gamehooks::autoUseWhileTargeting(80);
}

bool GamePlayerContext::swapSlots(int slotA, int slotB) {
    if (!player_ || inventoryMovePending() || slotA < 0 || slotA >= 36 || slotB < 0 || slotB >= 36 || slotA == slotB)
        return false;
    if (hotbar(slotA).empty()) return false;
    if ((slotA >= 9 || slotB >= 9) && !player_->containerManager()) {
        // 26.60: the server only exposes InventoryContainer while its screen is open.
        // Use the normal open/close flow; never pretend a refused move succeeded locally.
        player_->openInventory();
        openingMove_ = OpeningMove{slotA, slotB, tick_};
        return true;
    }
    auto snapshot=sdk::snapshotContainerTransfer(*player_,sdk::inventorySlotRef(slotA),sdk::inventorySlotRef(slotB));
    int id = sdk::sendInventoryMove(*player_, slotA, slotB);
    logger::debug("Inventory move {} -> {}: {}", slotA, slotB, id ? std::format("request {}", id) : "not sent");
    if (!id) return false;
    Move move{id,slotA,slotB,tick_}; move.snapshot=std::move(snapshot); moves_.push_back(std::move(move));
    return true;
}

void GamePlayerContext::advanceInventoryOpen() {
    const auto move = *openingMove_;
    if (void* manager = player_->containerManager(); manager && tick_ - move.started >= 3) {
        ownedInventory_ = manager;
        openingMove_.reset();
        auto snapshot=sdk::snapshotContainerTransfer(*player_,sdk::inventorySlotRef(move.slotA),sdk::inventorySlotRef(move.slotB));
        int id = sdk::sendInventoryMove(*player_, move.slotA, move.slotB);
        logger::debug("Inventory move {} -> {} after opening inventory: request {}", move.slotA, move.slotB, id);
        if (id) { Move pending{id,move.slotA,move.slotB,tick_}; pending.snapshot=std::move(snapshot); moves_.push_back(std::move(pending)); }
        else closeOwnedInventory();
    } else if (tick_ - move.started > 100) {
        openingMove_.reset();
        logger::warn("Inventory screen did not open; move cancelled");
    }
}

void GamePlayerContext::closeOwnedInventory() {
    if (!ownedInventory_) return;
    if (player_->containerManager() == ownedInventory_) {
        player_->closeInventory();
        inventoryClosedAt_ = tick_;
    }
    ownedInventory_ = nullptr;
}

void GamePlayerContext::onItemStackResponse(int requestId, int result, const void* response) {
    if (craft_ && craft_->responses.contains(requestId)) craft_->responses[requestId]=result;
    if (transfer_ && transfer_->id == requestId) {
        transfer_->result = result;
        if (result == 0) transfer_->receipt = sdk::containerTransferReceipt(response, transfer_->source, transfer_->target);
    }
    for (Move& m : moves_)
        if (m.requestId == requestId) {
            m.result = result;
            if (result==0) m.receipt=sdk::containerTransferReceipt(response,sdk::inventorySlotRef(m.slotA),sdk::inventorySlotRef(m.slotB));
        }
}

// The server answered: apply accepted moves to the client's slots (tick context).
void GamePlayerContext::applyAnsweredMoves() {
    std::erase_if(moves_, [&](const Move& m) {
        if (m.result == 0) {
            if (!sdk::applyInventorySwap(*player_,m.slotA,m.slotB,m.receipt,m.snapshot))
                logger::warn("Inventory swap receipt could not be reconciled; close and reopen the inventory before retrying");
            slotChangedAt_ = tick_;  // like a slot change: let the server settle before clicking
            return true;
        }
        if (m.result > 0) {
            logger::warn("Inventory move {} -> {} refused by the server (result {})", m.slotA, m.slotB, m.result);
            return true;
        }
        if (tick_ - m.sentAt > 100) {
            logger::warn("Inventory move {} -> {}: no answer from the server", m.slotA, m.slotB);
            return true;
        }
        return false;
    });
    if (moves_.empty()) closeOwnedInventory();
}

int GamePlayerContext::countItem(std::string_view name) const {
    auto* inv = player_->supplies()->inventory();
    if (!inv) return 0;
    int total = 0;
    for (int i = 0; i < 36; ++i) {
        const sdk::ItemStack* stack = inv->getItem(i);
        if (stack && !stack->empty() && stack->item()->name() == name) total += stack->count();
    }
    return total;
}

bool GamePlayerContext::containerOpen() const { return player_ && player_->containerManager(); }
bool GamePlayerContext::ownedContainerReady() const { return ownedContainer_ && player_ && player_->containerManager() == ownedContainer_; }
bool GamePlayerContext::openContainerAt(BlockPos pos) {
    if (!player_ || !region_ || isSneaking() || inventoryMovePending()) return false;
    if (ownedContainerReady()) return true;
    if (containerOpening_) return true;
    if (containerOpen()) { transferError_ = "Close the current container before starting automatic storage"; return false; }
    auto world = WorldTracker::get().snapshot(); if (!world) return false;
    if (auto policy = world->protection(); policy && !policy->permitsModify(pos)) { transferError_ = "Container is in a protected area"; return false; }
    const auto target = baritone::Vec3d::center(pos);
    if ((target - eyePosition()).lengthSq() > blockReachDistance() * blockReachDistance()) return false;
    const auto* block = region_->getBlock(toSdk(pos)); if (!block) return false;
    const auto name = block->serializationId().string("name").value_or("");
    if (name != "minecraft:chest" && name != "minecraft:trapped_chest" && name != "minecraft:barrel" && name.find("shulker_box") == std::string::npos &&
        name != "minecraft:furnace" && name != "minecraft:lit_furnace" && name != "minecraft:crafting_table") {
        transferError_ = "The registered block is not a supported container or workstation"; return false;
    }
    baritone::BlockStateInterface blocks(world);
    auto aim = baritone::rotation_utils::calcRotationFromVec3d(eyePosition(), target, rotation());
    auto hit = baritone::rayTrace(blocks, eyePosition(), aim, blockReachDistance());
    if (!hit || hit->pos != pos) return false;
    player_->gameMode()->buildBlock(toSdk(pos), static_cast<sdk::Face>(hit->face));
    containerOpening_ = std::pair{pos, tick_}; containerClosing_ = false; transferError_.clear();
    return true;
}
void GamePlayerContext::closeOwnedContainer() { containerClosing_ = true; }
std::vector<baritone::ContainerSlot> GamePlayerContext::containerSlots() const {
    std::vector<baritone::ContainerSlot> result;
    if (!player_ || !containerOpen()) return result;
    for (int i = 0; i < 36; ++i) result.push_back({"player", i, true, hotbar(i)});
    for (const auto& view : sdk::openContainerViews(*player_)) {
        if (view.networkName != 7 && view.networkName != 30 && view.networkName != 58 &&
            view.networkName != 24 && view.networkName != 25 && view.networkName != 26 && view.networkName != 45 && view.networkName != 46) continue;
        for (int i = 0; i < view.size; ++i) {
            auto ref = sdk::containerSlotRef(*player_, view.name, i);
            result.push_back({view.name, i, false, describe(sdk::containerItem(*player_, ref)), view.networkName});
        }
    }
    return result;
}
bool GamePlayerContext::transferContainerItem(const baritone::ContainerSlot& source, const baritone::ContainerSlot& target, int count) {
    if (!ownedContainerReady() || inventoryMovePending()) return false;
    auto a = source.playerInventory ? sdk::inventorySlotRef(source.slot) : sdk::containerSlotRef(*player_, source.collection, source.slot);
    auto b = target.playerInventory ? sdk::inventorySlotRef(target.slot) : sdk::containerSlotRef(*player_, target.collection, target.slot);
    auto* from = sdk::containerItem(*player_, a); auto* to = sdk::containerItem(*player_, b);
    if (!from || !to || from->empty() || !to->empty()) return false;
    int before = from->count(), after = to->count();
    auto snapshot = sdk::snapshotContainerTransfer(*player_, a, b);
    if (!snapshot.source || !snapshot.target) return false;
    int id = sdk::sendContainerTransfer(*player_, a, b, count); if (!id) return false;
    Transfer transfer; transfer.id = id; transfer.count = count; transfer.fromCount = before; transfer.toCount = after;
    transfer.sent = tick_; transfer.source = a; transfer.target = b; transfer.snapshot = std::move(snapshot); transfer_ = std::move(transfer);
    transferState_ = baritone::TransferState::Pending; transferError_.clear(); return true;
}
std::vector<baritone::RecipeOption> GamePlayerContext::recipes(std::string_view item) const {
    std::vector<baritone::RecipeOption> result;
    if (player_) for (auto& r:sdk::visibleRecipes(*player_,item)) result.push_back({std::move(r.collection),std::move(r.item),r.slot,r.count});
    return result;
}
bool GamePlayerContext::craftRecipe(const baritone::RecipeOption& recipe) {
    if (!containerOpen() || inventoryMovePending()) return false;
    Craft pending; pending.item=recipe.item; pending.before=countItem(recipe.item); pending.sent=tick_;
    auto ids=sdk::craftRecipeOnce(*player_,{recipe.collection,recipe.item,recipe.slot,recipe.count});
    if (ids.empty()) { craftError_="The recipe did not produce a normal crafting request; check ingredients and crafting-grid size"; return false; }
    for (auto id:ids) pending.responses.emplace(id,-1);
    craft_=std::move(pending); craftState_=baritone::TransferState::Pending; craftError_.clear(); return true;
}
void GamePlayerContext::advanceContainer() {
    if (craft_) {
        const bool refused=std::ranges::any_of(craft_->responses,[](const auto& r) { return r.second>0; });
        const bool answered=std::ranges::all_of(craft_->responses,[](const auto& r) { return r.second==0; });
        if (refused || tick_-craft_->sent>100) {
            craftState_=baritone::TransferState::Rejected; craftError_=refused ? "Crafting request refused by the server" : "Crafting output/response timed out; inspect inventory before retrying"; craft_.reset();
        } else if (answered && countItem(craft_->item)>craft_->before) { craftState_=baritone::TransferState::Accepted; craft_.reset(); slotChangedAt_=tick_; }
    }
    if (containerOpening_) {
        if (player_->containerManager()) { ownedContainer_ = player_->containerManager(); containerOpening_.reset(); }
        else if (tick_ - containerOpening_->second > 100) { containerOpening_.reset(); transferError_ = "Container did not open"; }
    }
    if (transfer_) {
        if (transfer_->result == 0) {
            bool applied = sdk::applyContainerTransfer(*player_, transfer_->source, transfer_->target, transfer_->count,
                transfer_->fromCount, transfer_->toCount, transfer_->receipt, transfer_->snapshot);
            transferState_ = applied ? baritone::TransferState::Accepted : baritone::TransferState::Rejected;
            if (!applied) transferError_ = "Server accepted the move but its slot receipt could not be reconciled; close and reopen the container";
            transfer_.reset(); slotChangedAt_ = tick_;
        } else if (transfer_->result > 0 || tick_ - transfer_->sent > 100) {
            transferState_ = baritone::TransferState::Rejected;
            transferError_ = transfer_->result > 0 ? "Container transfer refused by the server" : "Container transfer response timed out";
            transfer_.reset();
        }
    }
    if (containerClosing_ && !transfer_ && !craft_) {
        if (ownedContainer_ && player_->containerManager() == ownedContainer_) { player_->closeInventory(); inventoryClosedAt_ = tick_; }
        ownedContainer_ = nullptr; containerOpening_.reset(); containerClosing_ = false;
    }
}

std::vector<baritone::EntityInfo> GamePlayerContext::entities() const {
    std::vector<baritone::EntityInfo> out;
    if (!level_) return out;
    for (sdk::Actor* actor : level_->actors()) {
        if (!actor || actor == player_) continue;
        if (!actor->stateVector() || !actor->aabbShape()) continue;
        baritone::EntityInfo e;
        e.id = actor->runtimeId();
        e.type = actor->identifier();
        e.isPlayer = actor->isPlayer();
        e.isItem = actor->isItem();
        e.baby = actor->dataFlag(11); e.inLove = actor->dataFlag(7);
        e.tamed = actor->dataFlag(28); e.saddled = actor->dataFlag(8); e.dead = actor->isDead();
        if (auto health = actor->attribute("minecraft:health")) e.health = health->value;
        if (e.isPlayer) e.name = static_cast<sdk::Player*>(actor)->name();
        if (e.isItem) {
            const auto& stack = mem::member<sdk::ItemStack>(actor, offsets::ItemActor_stack);
            if (mem::isReadable(&stack, 0x30) && !stack.empty() && mem::isReadable(stack.item(), 0x150)) {
                e.itemName = stack.item()->name();
                e.itemCount = stack.count();
            }
        }
        e.position = toVec(actor->feet());
        out.push_back(std::move(e));
    }
    return out;
}

bool GamePlayerContext::startOrContinueBreaking(const BlockPos& pos, baritone::Direction face) {
    if (auto world = WorldTracker::get().snapshot(); world) {
        if (auto policy = world->protection(); policy && !policy->permitsModify(pos)) { stopBreaking(); return false; }
    }
    sdk::GameMode* gm = player_->gameMode();
    auto isAirNow = [this](const BlockPos& p) {
        const sdk::Block* b = region_ ? region_->getBlock(toSdk(p)) : nullptr;
        return b && mem::isReadable(b->legacy(), 0x30) && b->legacy()->name() == "minecraft:air";
    };
    bool destroyed = false;
    auto f = static_cast<sdk::Face>(face);
    if (breaking_ && *breaking_ != pos && hitTicks_ > 1 && isAirNow(*breaking_)) {
        // the block hit over several ticks broke (the client removes it a tick after the last
        // hit, so this shows when moving on to the next block): Java's break delay starts now
        WorldTracker::get().refreshAround(*breaking_, 1);
        breaking_.reset();
        hitTicks_ = 0;
        return true;
    }
    if (!breaking_ || *breaking_ != pos) {
        if (breaking_) gm->stopDestroyBlock(toSdk(*breaking_));
        gm->startDestroyBlock(toSdk(pos), f, destroyed);
        breaking_ = pos;
        hitTicks_ = 1;
    } else {
        gm->continueDestroyBlock(toSdk(pos), f, player_->stateVector()->pos, destroyed);
        ++hitTicks_;
        if (!destroyed && isAirNow(pos)) {
            destroyed = true;
            WorldTracker::get().refreshAround(pos, 1);
            breaking_.reset();
            hitTicks_ = 0;
            return true;
        }
    }
    if (destroyed) {
        breaking_.reset();
        WorldTracker::get().refreshAround(pos, 1);
        bool multiTick = hitTicks_ > 1;
        hitTicks_ = 0;
        return multiTick;
    }
    return false;
}

void GamePlayerContext::stopBreaking() {
    if (!breaking_) return;
    player_->gameMode()->stopDestroyBlock(toSdk(*breaking_));
    breaking_.reset();
}

void GamePlayerContext::useItemOn(const BlockPos& pos, baritone::Direction face, const Vec3d&) {
    if (auto world = WorldTracker::get().snapshot(); world) {
        if (auto policy = world->protection(); policy && (!policy->permitsModify(pos) || !policy->permitsModify(pos.offset(face)))) return;
    }
    baritone::HotbarStack held = hotbar(selectedSlot());
    if (held.count > 0 && !held.isBlock) {
        // items (bone meal, seeds, buckets...): GameMode::buildBlock only places blocks, so a real
        // right click on the block the player looks at (the caller checked it is `pos`)
        gamehooks::requestClick(2);
        clickRefreshes_.emplace_back(pos, tick_ + 4);
        clickRefreshes_.emplace_back(pos, tick_ + 10);  // the server's answer, on a slow connection
    } else {
        player_->gameMode()->buildBlock(toSdk(pos), static_cast<sdk::Face>(face));
    }
    WorldTracker::get().refreshAround(pos, 1);
}

void GamePlayerContext::refreshClickedBlocks() {
    std::erase_if(clickRefreshes_, [this](const auto& r) {
        if (r.second > tick_) return false;
        WorldTracker::get().refreshAround(r.first, 1);
        return true;
    });
}

void GamePlayerContext::refreshWorld(const BlockPos& center, int radius) {
    WorldTracker::get().refreshAround(center, radius);
}

}  // namespace platform
