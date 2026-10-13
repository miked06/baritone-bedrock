#include "baritone/process/MineProcess.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "baritone/Baritone.h"
#include "baritone/Settings.h"
#include "baritone/pathing/movement/MovementHelper.h"

namespace baritone {

using Clock = std::chrono::steady_clock;

void MineProcess::mine(int quantity, BlockFilter filter) {
    onLostControl();
    if (!Settings::get().allowBreak) {
        // Java filterFilter: without allowBreak, only the blocks of allowBreakAnyway
        std::vector<std::string> allowed;
        for (const std::string& n : filter.names()) {
            const auto& anyway = Settings::get().allowBreakAnyway;
            if (std::ranges::find(anyway, n) != anyway.end()) allowed.push_back(n);
        }
        if (allowed.empty()) {
            baritone_.logDirect("Impossible de miner avec allowBreak a false si le bloc n'est pas dans allowBreakAnyway", true);
            return;
        }
        filter = BlockFilter(allowed);
    }
    filter_ = std::move(filter);
    desiredQuantity_ = quantity;
    tickCount_ = 0;
    if (baritone_.world() && !Settings::get().legitMine) {  // legitMine: only what is seen (addNearby)
        knownOreLocations_ = searchWorld(*baritone_.world(), *filter_, baritone_.playerFeet(),
                                              Settings::get().mineMaxOreLocationsCount, Settings::get().maxScanChunkRadius);
    }
    warnIfNoPickaxe();
    if (baritone_.world()) {
        // ores of the other dimension: the bot would dig tunnels forever
        bool nether = baritone_.world()->dimension() == 1, possible = false;
        for (const std::string& n : filter_->names()) {
            bool netherOre = n == "minecraft:quartz_ore" || n == "minecraft:nether_gold_ore" || n == "minecraft:ancient_debris";
            if (!n.ends_with("_ore") && n != "minecraft:ancient_debris") possible = true;  // not an ore: anywhere
            else if (netherOre == nether) possible = true;
        }
        if (!possible) {
            baritone_.logDirect(std::format("Attention : {} n'existe pas {}", filter_->toString(),
                                            nether ? "dans le Nether" : "dans cette dimension (minerai du Nether)"),
                                true);
        }
    }
    if (knownOreLocations_.empty() && !Settings::get().exploreForBlocks) {
        baritone_.logDirect("Aucun " + filter_->toString() + " connu, #mine annule", true);
        if (Settings::get().notificationOnMineFail) baritone_.logNotification("Aucun " + filter_->toString() + " connu, #mine annule", true);
        onLostControl();
    }
}

// In survival an ore needs a good enough pickaxe to drop anything, and digging by hand is so
// slow that the paths stay on the surface: say so instead of wandering.
void MineProcess::warnIfNoPickaxe() {
    PlayerContext& p = baritone_.player();
    if (p.isCreative()) return;
    static const std::vector<std::pair<std::string_view, int>> needs{
        {"nether_gold", 1}, {"diamond", 3}, {"emerald", 3}, {"gold", 3}, {"redstone", 3}, {"iron", 2}, {"copper", 2},
        {"lapis", 2},   {"ancient_debris", 4}, {"_ore", 1}, {"stone", 1}, {"deepslate", 1},
    };
    int need = 0;
    for (const std::string& name : filter_->names()) {
        for (const auto& [part, tier] : needs) {
            if (name.find(part) != std::string::npos) {
                need = std::max(need, tier);
                break;
            }
        }
    }
    if (need == 0) return;
    int best = 0;
    for (int i = 0; i < 9; ++i) {
        HotbarStack s = p.hotbar(i);
        if (s.tool == Tool::Pickaxe) best = std::max(best, s.toolTier);
    }
    static constexpr const char* names[] = {"", "en bois", "en pierre", "en fer", "en diamant"};
    if (best < need) {
        baritone_.logDirect(std::format("Attention : il faut au moins une pioche {} dans la barre d'objets pour miner {} "
                                        "(sinon le bot creuse a peine et reste en surface)",
                                        names[need], filter_->toString()),
                            true);
    }
}

void MineProcess::onLostControl() {
    filter_.reset();
    desiredQuantity_ = 0;
    knownOreLocations_.clear();
    blacklist_.clear();
    anticipatedDrops_.clear();
    branchPoint_.reset();
    announcedDescent_ = false;
    scan_.wait();
}

std::string MineProcess::displayName() const {
    return "Mine " + (filter_ ? filter_->toString() : std::string());
}

int MineProcess::countHave() {
    int total = 0;
    for (const std::string& name : filter_->dropNames()) total += baritone_.player().countItem(name);
    return total;
}

std::vector<BlockPos> MineProcess::droppedItemsScan() {
    std::vector<BlockPos> out;
    if (!Settings::get().mineScanDroppedItems) return out;
    for (const EntityInfo& e : baritone_.player().entities()) {
        if (!e.isItem) continue;
        if (!e.itemName.empty() && !filter_->dropNames().contains(e.itemName)) continue;
        out.push_back({static_cast<int>(std::floor(e.position.x)), static_cast<int>(std::floor(e.position.y)),
                       static_cast<int>(std::floor(e.position.z))});
    }
    for (const auto& [pos, when] : anticipatedDrops_) out.push_back(pos);
    return out;
}

// Ores we just broke: their drops are about to appear there (Louca's anticipated drops).
void MineProcess::updateLoucaSystem() {
    // Java: the known ore under the crosshair will drop its item for a little while
    CalculationContext* ctx = baritone_.liveContext();
    auto now = Clock::now();
    ExecContext ec{baritone_.player(), *ctx, baritone_.inventory(), baritone_.playerFeet()};
    if (auto hit = ec.objectMouseOver()) {
        if (std::ranges::find(knownOreLocations_, hit->pos) != knownOreLocations_.end()) {
            anticipatedDrops_[hit->pos] = now + std::chrono::milliseconds(Settings::get().mineDropLoiterDurationMSThanksLouca);
        }
    }
    std::erase_if(anticipatedDrops_, [&](const auto& e) { return e.second < now; });
}

// Java isNextToAir (allowOnlyExposedOres): air, water or lava within the manhattan radius
bool MineProcess::isNextToAir(CalculationContext& ctx, const BlockPos& pos) {
    int radius = Settings::get().allowOnlyExposedOresDistance;
    for (int dx = -radius; dx <= radius; dx++)
        for (int dy = -radius; dy <= radius; dy++)
            for (int dz = -radius; dz <= radius; dz++) {
                if (std::abs(dx) + std::abs(dy) + std::abs(dz) > radius) continue;
                const BlockProps& p = ctx.props(pos.x + dx, pos.y + dy, pos.z + dz);
                if (p.type == BlockType::Air || p.type == BlockType::Water || p.type == BlockType::Lava) return true;
            }
    return false;
}

// Java addNearby (legitMine): only ores the player could see from here (raytrace within 20
// blocks), or diagonal to a known one with legitMineIncludeDiagonals
bool MineProcess::addNearby() {
    CalculationContext* ctx = baritone_.liveContext();
    if (!ctx) return false;
    auto dropped = droppedItemsScan();
    knownOreLocations_.insert(knownOreLocations_.end(), dropped.begin(), dropped.end());
    BlockPos feet = baritone_.playerFeet();
    ExecContext ec{baritone_.player(), *ctx, baritone_.inventory(), feet};
    constexpr int kSearchDist = 10;
    constexpr double kFakedReach = 20;
    for (int x = feet.x - kSearchDist; x <= feet.x + kSearchDist; x++)
        for (int y = feet.y - kSearchDist; y <= feet.y + kSearchDist; y++)
            for (int z = feet.z - kSearchDist; z <= feet.z + kSearchDist; z++) {
                if (!filter_->matches(ctx->props(x, y, z))) continue;
                BlockPos pos{x, y, z};
                bool diagonal = Settings::get().legitMineIncludeDiagonals &&
                                std::ranges::any_of(knownOreLocations_, [&](const BlockPos& ore) { return ore.distSq(pos) <= 2; });
                if (diagonal || movement_helper::reachable(ec, pos, false, kFakedReach)) knownOreLocations_.push_back(pos);
            }
    knownOreLocations_ = prune(*ctx, knownOreLocations_, dropped);
    return true;
}

bool MineProcess::plausibleToBreak(CalculationContext& ctx, const BlockPos& pos) {
    const BlockProps& p = ctx.props(pos.x, pos.y, pos.z);
    if (helper::getMiningDurationTicks(ctx, pos.x, pos.y, pos.z, p, true) >= costs::COST_INF) return false;
    if (helper::avoidBreaking(ctx, pos.x, pos.y, pos.z, p)) return false;
    // bedrock above and below makes it implausible
    return !(ctx.props(pos.x, pos.y + 1, pos.z).name == "minecraft:bedrock" &&
             ctx.props(pos.x, pos.y - 1, pos.z).name == "minecraft:bedrock");
}

std::vector<BlockPos> MineProcess::prune(CalculationContext& ctx, std::vector<BlockPos> locs,
                                         const std::vector<BlockPos>& dropped) {
    std::ranges::sort(locs, [](const BlockPos& a, const BlockPos& b) {
        return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z);
    });
    locs.erase(std::unique(locs.begin(), locs.end()), locs.end());
    // Java prune: drops next to an ore we'll mine anyway are not separate goals
    std::vector<BlockPos> drops = dropped;
    std::erase_if(drops, [&](const BlockPos& drop) {
        return std::ranges::any_of(locs, [&](const BlockPos& pos) {
            return pos.distSq(drop) <= 9 && filter_->matches(ctx.props(pos.x, pos.y, pos.z)) && plausibleToBreak(ctx, pos);
        });
    });
    const Settings& st = Settings::get();
    int minY = st.minYLevelWhileMining + ctx.bsi.minY();
    std::erase_if(locs, [&](const BlockPos& pos) {
        bool isDrop = std::ranges::find(drops, pos) != drops.end();
        // not loaded (from the cache), still the block, or a drop to pick up
        if (ctx.isLoadedLive(pos.x, pos.z) && !filter_->matches(ctx.props(pos.x, pos.y, pos.z)) && !isDrop) return true;
        if (!plausibleToBreak(ctx, pos)) return true;
        if (st.allowOnlyExposedOres && !isNextToAir(ctx, pos)) return true;
        if (pos.y < minY || pos.y > st.maxYLevelWhileMining) return true;
        return std::ranges::find(blacklist_, pos) != blacklist_.end();
    });
    BlockPos feet = baritone_.playerFeet();
    std::ranges::sort(locs, [&](const BlockPos& a, const BlockPos& b) { return a.distSq(feet) < b.distSq(feet); });
    if (static_cast<int>(locs.size()) > Settings::get().mineMaxOreLocationsCount) {
        locs.resize(Settings::get().mineMaxOreLocationsCount);
    }
    return locs;
}

bool MineProcess::internalMiningGoal(const BlockPos& pos, CalculationContext& ctx, const std::vector<BlockPos>& locs) {
    if (std::ranges::find(locs, pos) != locs.end()) return true;
    const BlockProps& p = ctx.props(pos.x, pos.y, pos.z);
    if (Settings::get().internalMiningAirException && p.type == BlockType::Air) return true;
    return filter_->matches(p) && plausibleToBreak(ctx, pos);
}

// Where to stand to mine `loc` efficiently, given the ores around it (Baritone's coalesce).
GoalPtr MineProcess::coalesce(const BlockPos& loc, const std::vector<BlockPos>& locs, CalculationContext& ctx) {
    bool assumeVerticalShaftMine = !ctx.props(loc.x, loc.y + 1, loc.z).falling;
    if (!Settings::get().forceInternalMining) {
        if (assumeVerticalShaftMine) return std::make_shared<GoalThreeBlocks>(loc);
        return std::make_shared<GoalTwoBlocks>(loc);
    }
    bool upwardGoal = internalMiningGoal(loc.above(), ctx, locs);
    bool downwardGoal = internalMiningGoal(loc.below(), ctx, locs);
    bool doubleDownwardGoal = internalMiningGoal(loc.below(2), ctx, locs);
    if (upwardGoal == downwardGoal) {
        if (doubleDownwardGoal && assumeVerticalShaftMine) return std::make_shared<GoalThreeBlocks>(loc);
        return std::make_shared<GoalTwoBlocks>(loc);
    }
    if (upwardGoal) return std::make_shared<GoalBlock>(loc);  // feet in this one, head in the one above
    if (doubleDownwardGoal && assumeVerticalShaftMine) return std::make_shared<GoalTwoBlocks>(loc.below());
    return std::make_shared<GoalBlock>(loc.below());
}

// The level to branch mine at: legitMineYLevel (-59, diamonds), or, while it is left at that
// default, the level where the wanted ore is the most common (Bedrock 1.18+ generation).
int MineProcess::branchY() const {
    int y = Settings::get().legitMineYLevel;
    if (y != -59) return y;
    if (baritone_.world() && baritone_.world()->dimension() == 1) y = 15;  // nether: ancient debris level, under the lava sea
    static const std::vector<std::pair<std::string, int>> levels{
        {"minecraft:iron_ore", 16},       {"minecraft:gold_ore", -16},     {"minecraft:lapis_ore", 0},
        {"minecraft:coal_ore", 48},       {"minecraft:copper_ore", 48},    {"minecraft:emerald_ore", 48},
        {"minecraft:ancient_debris", 15}, {"minecraft:quartz_ore", 30},    {"minecraft:nether_gold_ore", 30},
    };
    for (const auto& [ore, level] : levels) {
        if (filter_->matchesName(ore)) y = std::max(y, level);  // several ores: the highest of their levels
    }
    // inside the world (the nether has no deepslate layer and a roof)
    if (auto* ctx = baritone_.liveContext()) y = std::clamp(y, ctx->bsi.minY() + 5, ctx->bsi.maxY() - 10);
    return y;
}

std::optional<PathingCommand> MineProcess::updateGoal() {
    CalculationContext& ctx = *baritone_.liveContext();
    if (!knownOreLocations_.empty()) {
        auto locs = prune(ctx, knownOreLocations_, droppedItemsScan());
        if (!locs.empty()) {
            std::vector<GoalPtr> goals;
            for (const BlockPos& loc : locs) goals.push_back(coalesce(loc, locs, ctx));
            knownOreLocations_ = std::move(locs);
            return PathingCommand{std::make_shared<GoalComposite>(std::move(goals)),
                                  Settings::get().legitMine ? PathingCommandType::ForceRevalidateGoalAndPath
                                                            : PathingCommandType::RevalidateGoalAndPath};
        }
        knownOreLocations_.clear();
    }
    if (!Settings::get().legitMine && !Settings::get().exploreForBlocks) return std::nullopt;
    // nothing known: go down to the mining level, then branch mine away from where we got there
    // (Baritone's branchPointRunaway keeping legitMineYLevel; first going straight to the level
    // makes the bot dig down instead of wandering on the surface)
    int y = branchY();
    BlockPos feet = baritone_.playerFeet();
    if (!branchPoint_) {
        if (std::abs(feet.y - y) > 1) {
            if (!announcedDescent_) {
                baritone_.logDirect(std::format("Aucun {} connu : je vais en Y={} puis je creuse des galeries",
                                                filter_->toString(), y), false);
                announcedDescent_ = true;
            }
            return PathingCommand{std::make_shared<GoalYLevel>(y), PathingCommandType::RevalidateGoalAndPath};
        }
        branchPoint_ = feet;
        baritone_.logDirect(std::format("Niveau Y={} atteint : minage en galerie", y), false);
    }
    return PathingCommand{std::make_shared<GoalRunAway>(1, y, std::vector<BlockPos>{*branchPoint_}, true),
                          PathingCommandType::RevalidateGoalAndPath};
}

std::optional<PathingCommand> MineProcess::onTick(bool calcFailed, bool isSafeToCancel) {
    if (!filter_ || !baritone_.liveContext()) return std::nullopt;
    if (desiredQuantity_ > 0) {
        int have = countHave();
        if (have >= desiredQuantity_) {
            baritone_.logDirect(std::format("J'ai {} {}, #mine termine", have, filter_->toString()), false);
            cancel();
            return std::nullopt;
        }
    }
    if (calcFailed) {
        if (!knownOreLocations_.empty() && Settings::get().blacklistClosestOnFailure) {
            baritone_.logDirect("Pas de chemin vers " + filter_->toString() + ", le plus proche est ignore", true);
            if (Settings::get().notificationOnMineFail) baritone_.logNotification("Pas de chemin vers " + filter_->toString() + ", le plus proche est ignore", true);
            BlockPos feet = baritone_.playerFeet();
            auto closest = std::ranges::min_element(knownOreLocations_, [&](const BlockPos& a, const BlockPos& b) {
                return a.distSq(feet) < b.distSq(feet);
            });
            blacklist_.push_back(*closest);
            knownOreLocations_.erase(closest);
        } else {
            baritone_.logDirect("Pas de chemin vers " + filter_->toString() + ", #mine annule", true);
            if (Settings::get().notificationOnMineFail) baritone_.logNotification("Pas de chemin vers " + filter_->toString() + ", #mine annule", true);
            cancel();
            return std::nullopt;
        }
    }
    updateLoucaSystem();
    if (Settings::get().legitMine && !addNearby()) {
        cancel();
        return std::nullopt;
    }

    // background rescans
    if (auto result = scan_.poll()) {
        auto dropped = droppedItemsScan();
        result->insert(result->end(), dropped.begin(), dropped.end());
        result->insert(result->end(), knownOreLocations_.begin(), knownOreLocations_.end());
        knownOreLocations_ = std::move(*result);
    }
    int interval = Settings::get().mineGoalUpdateInterval;
    if (interval != 0 && tickCount_++ % interval == 0 && !Settings::get().legitMine) {
        scan_.start(baritone_.world(), *filter_, baritone_.playerFeet(), Settings::get().mineMaxOreLocationsCount,
                    Settings::get().maxScanChunkRadius);
    }

    // an ore straight above us in a shaft: just break it
    CalculationContext& ctx = *baritone_.liveContext();
    BlockPos feet = baritone_.playerFeet();
    std::optional<BlockPos> shaft;
    for (const BlockPos& p : knownOreLocations_) {
        if (p.x != feet.x || p.z != feet.z || p.y < feet.y) continue;
        if (ctx.props(p.x, p.y, p.z).type == BlockType::Air) continue;
        if (!shaft || p.distSq(feet.above()) < shaft->distSq(feet.above())) shaft = p;
    }
    if (shaft && baritone_.player().onGround()) {
        const BlockProps& state = ctx.props(shaft->x, shaft->y, shaft->z);
        if (!helper::avoidBreaking(ctx, shaft->x, shaft->y, shaft->z, state)) {
            ExecContext ec{baritone_.player(), ctx, baritone_.inventory(), feet};
            if (auto rot = movement_helper::reachable(ec, *shaft); rot && isSafeToCancel) {
                MovementState& c = baritone_.controls();
                c.setTarget({*rot, true});
                movement_helper::switchToBestToolFor(ec, state);
                if (ec.isLookingAt(*shaft) || ec.playerRotations().isReallyCloseTo(*rot)) c.setInput(Input::ClickLeft, true);
                return PathingCommand{nullptr, PathingCommandType::RequestPause};
            }
        }
    }
    auto command = updateGoal();
    if (!command) {
        baritone_.logDirect("Plus de " + filter_->toString() + " a portee, #mine termine", false);
        cancel();
        return std::nullopt;
    }
    return command;
}

}  // namespace baritone
