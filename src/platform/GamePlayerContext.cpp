#include "platform/GamePlayerContext.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string_view>

#include "baritone/Settings.h"
#include "baritone/world/BlockStateInterface.h"
#include "core/Log.h"
#include "hooks/GameHooks.h"
#include "platform/WorldTracker.h"
#include "sdk/Game.h"

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

void GamePlayerContext::useHeldItemWhenTargeting() {
    if (player_) gamehooks::autoUseWhileTargeting(80);
}

bool GamePlayerContext::swapSlots(int slotA, int slotB) {
    if (!player_ || !moves_.empty()) return false;  // one move at a time
    int id = sdk::sendInventoryMove(*player_, slotA, slotB);
    logger::debug("Inventory move {} -> {}: {}", slotA, slotB, id ? std::format("request {}", id) : "not sent");
    if (!id) return false;
    moves_.push_back({id, slotA, slotB, tick_});
    return true;
}

void GamePlayerContext::onItemStackResponse(int requestId, int result) {
    for (Move& m : moves_)
        if (m.requestId == requestId) m.result = result;
}

// The server answered: apply accepted moves to the client's slots (tick context).
void GamePlayerContext::applyAnsweredMoves() {
    std::erase_if(moves_, [&](const Move& m) {
        if (m.result == 0) {
            sdk::applyInventoryMoveLocally(*player_, m.slotA, m.slotB);
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
