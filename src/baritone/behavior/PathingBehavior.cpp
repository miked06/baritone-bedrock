#include "baritone/behavior/PathingBehavior.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <unordered_set>

#include "baritone/Settings.h"
#include "baritone/cache/WorldScanner.h"
#include "baritone/pathing/movement/MovementHelper.h"

namespace baritone {

PathingBehavior::PathingBehavior(PlayerContext& player, InventoryBehavior& inventory, Notify notify)
    : player_(player), inventory_(inventory), notify_(std::move(notify)) {}

PathingBehavior::~PathingBehavior() {
    shutdown();
}

void PathingBehavior::shutdown() {
    {
        std::lock_guard lock(calcLock_);
        if (inProgress_) inProgress_->cancel();
    }
    if (calcThread_.joinable()) calcThread_.join();
}

// ---- state ----------------------------------------------------------------

GoalPtr PathingBehavior::goal() const {
    std::lock_guard lock(planLock_);
    return goal_;
}

bool PathingBehavior::isPathing() const {
    std::lock_guard lock(planLock_);
    return current_ != nullptr;
}

bool PathingBehavior::hasInProgress() const {
    std::lock_guard lock(calcLock_);
    return inProgress_ != nullptr;
}

std::optional<std::pair<BlockPos, GoalPtr>> PathingBehavior::currentPathInfo() const {
    std::lock_guard lock(planLock_);
    if (!current_) return std::nullopt;
    return std::make_pair(current_->path()->dest(), current_->path()->goal());
}

std::vector<BlockPos> PathingBehavior::currentMovementToBreak() const {
    std::lock_guard lock(planLock_);
    if (!current_ || current_->finished() || current_->failed()) return {};
    Movement* m = current_->currentMovement();
    return m ? m->toBreakAll() : std::vector<BlockPos>{};
}

std::optional<double> PathingBehavior::estimatedTicksToGoal() {
    std::lock_guard lock(planLock_);
    if (!goal_ || !etaStart_ || !liveCalc_) return std::nullopt;
    BlockPos feet = ExecContext{player_, *liveCalc_, inventory_, {}}.playerFeet();
    if (goal_->isInGoal(feet.x, feet.y, feet.z)) {
        etaStart_ = feet;  // resetEstimatedTicksToGoal
        ticksElapsedSoFar_ = 0;
        return 0.0;
    }
    if (ticksElapsedSoFar_ == 0) return std::nullopt;
    double current = goal_->heuristic(feet.x, feet.y, feet.z);
    double start = goal_->heuristic(etaStart_->x, etaStart_->y, etaStart_->z);
    if (current == start) return std::nullopt;
    return std::fabs(current) * ticksElapsedSoFar_ / std::fabs(start - current);
}

std::optional<double> PathingBehavior::ticksRemainingInSegment() const {
    std::lock_guard lock(planLock_);
    if (!current_) return std::nullopt;
    return current_->ticksRemainingFrom(current_->position() + 1);
}

// ---- control (Baritone's secretInternal*) ---------------------------------

void PathingBehavior::secretInternalSetGoal(GoalPtr goal) {
    std::lock_guard lock(planLock_);
    goal_ = std::move(goal);
}

bool PathingBehavior::secretInternalSetGoalAndPath(GoalPtr goal) {
    secretInternalSetGoal(goal);
    if (!goal || !liveCalc_) return false;
    std::lock_guard plan(planLock_);
    if (goal->isInGoal(ExecContext{player_, *liveCalc_, inventory_, {}}.playerFeet()) ||
        goal->isInGoal(expectedSegmentStart_)) {
        return false;
    }
    if (current_) return false;
    std::lock_guard calc(calcLock_);
    if (inProgress_) return false;
    findPathInNewThread(expectedSegmentStart_, true);
    return true;
}

void PathingBehavior::secretInternalSegmentCancel() {
    std::lock_guard plan(planLock_);
    {
        std::lock_guard calc(calcLock_);
        if (inProgress_) inProgress_->cancel();
    }
    if (current_) {
        current_.reset();
        next_.reset();
        player_.stopBreaking();
    }
    output_ = MovementState{};
}

void PathingBehavior::cancelSegmentIfSafe() {
    if (safeToCancel_) secretInternalSegmentCancel();
}

void PathingBehavior::softCancelIfSafe() {
    std::lock_guard plan(planLock_);
    {
        std::lock_guard calc(calcLock_);
        if (inProgress_) inProgress_->cancel();  // only cancel ours
    }
    if (!safeToCancel_) return;
    current_.reset();
    next_.reset();
    cancelRequested_ = true;
}

void PathingBehavior::cancelEverything() {
    secretInternalSegmentCancel();
    secretInternalSetGoal(nullptr);
    cancelRequested_ = true;
}

BlockPos PathingBehavior::pathStart() {
    if (!liveCalc_) return {};
    ExecContext ctx{player_, *liveCalc_, inventory_, {}};
    BlockPos feet = ctx.playerFeet();
    if (!ctx.canWalkOn(feet.below())) {
        Vec3d pos = player_.position();
        if (player_.onGround()) {
            // probably sneaking off the edge of a block: find the block we stand on
            std::vector<BlockPos> closest;
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz) closest.push_back(feet.offset(dx, 0, dz));
            auto d = [&](const BlockPos& p) {
                double x = p.x + 0.5 - pos.x, z = p.z + 0.5 - pos.z;
                return x * x + z * z;
            };
            std::ranges::sort(closest, [&](const BlockPos& a, const BlockPos& b) { return d(a) < d(b); });
            for (int i = 0; i < 4; ++i) {
                const BlockPos& support = closest[i];
                double xDist = std::fabs(support.x + 0.5 - pos.x), zDist = std::fabs(support.z + 0.5 - pos.z);
                if (xDist > 0.8 && zDist > 0.8) continue;
                if (ctx.canWalkOn(support.below()) && ctx.canWalkThrough(support) && ctx.canWalkThrough(support.above())) {
                    return support;
                }
            }
        } else if (ctx.canWalkOn(feet.below(2))) {
            return feet.below();  // midair, falling onto the block below
        }
    }
    return feet;
}

// ---- calculation ------------------------------------------------------------

namespace {

// Java Avoidance: every Mob (living creature), except a spider in the light, a zombified
// piglin nobody hit and an enderman that isn't angry. Their light/anger is not read on
// Bedrock: those three are left out, like when they are calm.
bool isAvoidedMob(const EntityInfo& e) {
    if (e.isPlayer || e.isItem) return false;
    static const std::unordered_set<std::string_view> notMobs{
        "minecraft:xp_orb", "minecraft:arrow", "minecraft:thrown_trident", "minecraft:snowball", "minecraft:egg",
        "minecraft:ender_pearl", "minecraft:fireball", "minecraft:small_fireball", "minecraft:dragon_fireball",
        "minecraft:wither_skull", "minecraft:wither_skull_dangerous", "minecraft:fireworks_rocket", "minecraft:tnt",
        "minecraft:falling_block", "minecraft:minecart", "minecraft:chest_minecart", "minecraft:hopper_minecart",
        "minecraft:tnt_minecart", "minecraft:command_block_minecart", "minecraft:boat", "minecraft:chest_boat",
        "minecraft:armor_stand", "minecraft:painting", "minecraft:leash_knot", "minecraft:lightning_bolt",
        "minecraft:area_effect_cloud", "minecraft:splash_potion", "minecraft:lingering_potion", "minecraft:xp_bottle",
        "minecraft:fishing_hook", "minecraft:eye_of_ender_signal", "minecraft:ender_crystal", "minecraft:shulker_bullet",
        "minecraft:llama_spit", "minecraft:evocation_fang", "minecraft:wind_charge_projectile",
        "minecraft:breeze_wind_charge_projectile", "minecraft:ominous_item_spawner", "minecraft:agent", "minecraft:npc",
        "minecraft:tripod_camera", "minecraft:balloon", "minecraft:ice_bomb", "minecraft:item",
        // neutral while calm (see above)
        "minecraft:spider", "minecraft:cave_spider", "minecraft:enderman", "minecraft:zombie_pigman",
    };
    return !notMobs.contains(e.type);
}

}  // namespace

void PathingBehavior::findPathInNewThread(const BlockPos& start, bool talkAboutIt) {
    GoalPtr goal = goal_;
    if (!goal || !world_) return;
    const Settings& s = Settings::get();
    long primary = (current_ ? s.planAheadPrimaryTimeoutMS : s.primaryTimeoutMS) * timeoutMultiplier_;
    long failure = (current_ ? s.planAheadFailureTimeoutMS : s.failureTimeoutMS) * timeoutMultiplier_;

    // Long distance: a goal position in an unloaded chunk becomes its x/z only (simplifyUnloadedYCoord)
    GoalPtr transformed = goal;
    if (s.simplifyUnloadedYCoord) {
        std::optional<BlockPos> pos;
        if (auto g = std::dynamic_pointer_cast<const GoalBlock>(goal)) pos = g->pos();
        else if (auto g2 = std::dynamic_pointer_cast<const GoalTwoBlocks>(goal)) pos = g2->pos();
        else if (auto g3 = std::dynamic_pointer_cast<const GoalGetToBlock>(goal)) pos = g3->pos();
        else if (auto g4 = std::dynamic_pointer_cast<const GoalNear>(goal)) pos = g4->pos();
        if (pos && !world_->isLoaded(pos->x >> 4, pos->z >> 4)) transformed = std::make_shared<GoalXZ>(pos->x, pos->z);
    }
    auto context = std::make_unique<CalculationContext>(world_, inventory_.abilities(), true);
    if (customizer_) customizer_(*context);
    Favoring favoring(current_ ? current_->path() : nullptr, context->backtrackCostFavoringCoefficient);
    if (s.avoidance) {  // Java Avoidance.create
        if (s.mobSpawnerAvoidanceCoefficient != 1.0) {
            BlockFilter spawners(std::vector<std::string>{"minecraft:mob_spawner"});
            for (const BlockPos& spawner : searchWorld(*world_, spawners, start, 16, s.maxScanChunkRadius)) {
                favoring.avoidSphere(spawner, s.mobSpawnerAvoidanceRadius, s.mobSpawnerAvoidanceCoefficient);
            }
        }
        if (s.mobAvoidanceCoefficient != 1.0) {
            for (const EntityInfo& e : player_.entities()) {
                if (!isAvoidedMob(e)) continue;
                BlockPos p{static_cast<int>(std::floor(e.position.x)), static_cast<int>(std::floor(e.position.y)),
                           static_cast<int>(std::floor(e.position.z))};
                favoring.avoidSphere(p, s.mobAvoidanceRadius, s.mobAvoidanceCoefficient);
            }
        }
    }
    auto finder = std::make_shared<AStarPathFinder>(start, transformed, std::move(context), std::move(favoring));
    inProgress_ = finder;
    if (calcThread_.joinable()) calcThread_.join();  // finished: inProgress_ was null
    calcThread_ = std::thread([this, finder, start, goal, primary, failure, talkAboutIt] {
        PathCalculationResult result = finder->calculate(primary, failure);
        std::lock_guard plan(planLock_);
        if (result.path) {
            auto executor = std::make_unique<PathExecutor>(result.path);
            if (!current_) {
                const auto& positions = result.path->positions();
                if (std::ranges::find(positions, expectedSegmentStart_) != positions.end()) {
                    current_ = std::move(executor);
                    etaStart_ = start;  // resetEstimatedTicksToGoal(start)
                    ticksElapsedSoFar_ = 0;
                    if (talkAboutIt && Settings::get().chatDebug) {
                        notify_(std::format("{} : {} blocs, {} noeuds", goal->isInGoal(result.path->dest()) ? "Chemin" : "Segment",
                                            result.path->length(), result.path->numNodesConsidered()),
                                false);
                    }
                }
            } else if (!next_ && result.path->src() == current_->path()->dest()) {
                next_ = std::move(executor);
            }
        } else if (result.type == PathResultType::Failure || result.type == PathResultType::Exception) {
            if (!current_) calcFailed_ = true;
            if (result.type == PathResultType::Exception) notify_("Erreur pendant le calcul : " + result.error, true);
        }
        std::lock_guard calc(calcLock_);
        if (inProgress_ == finder) inProgress_.reset();
    });
}

// ---- tick --------------------------------------------------------------------

void PathingBehavior::beginTick(std::shared_ptr<const WorldSnapshot> world) {
    world_ = std::move(world);
    output_ = MovementState{};
    producedOutput_ = false;
    calcFailedLastTick_ = calcFailed_.exchange(false);
    if (!world_) {
        liveCalc_.reset();
        return;
    }
    liveCalc_ = std::make_unique<CalculationContext>(world_, inventory_.abilities(), false);
    std::lock_guard plan(planLock_);
    expectedSegmentStart_ = pathStart();
}

void PathingBehavior::tickPath() {
    ++ticksElapsedSoFar_;
    pausedThisTick_ = false;
    if (!liveCalc_) return;
    if (pauseRequested_ && safeToCancel_) {
        pauseRequested_ = false;
        if (unpausedLastTick_) player_.stopBreaking();
        unpausedLastTick_ = false;
        pausedThisTick_ = true;
        return;
    }
    pauseRequested_ = false;
    unpausedLastTick_ = true;
    if (cancelRequested_) {
        cancelRequested_ = false;
        player_.stopBreaking();
    }

    std::lock_guard plan(planLock_);
    {
        std::lock_guard calc(calcLock_);
        if (inProgress_) {
            // calculating from a position we're no longer at (and not continuing the current path)?
            BlockPos calcFrom = inProgress_->start();
            BlockPos feet = ExecContext{player_, *liveCalc_, inventory_, {}}.playerFeet();
            if ((!current_ || current_->path()->dest() != calcFrom) && calcFrom != feet && calcFrom != expectedSegmentStart_) {
                auto best = inProgress_->bestPathSoFar();
                bool contains = std::ranges::find(best, feet) != best.end() ||
                                std::ranges::find(best, expectedSegmentStart_) != best.end();
                if (!contains) inProgress_->cancel();
            }
        }
    }
    if (!current_) {
        safeToCancel_ = true;  // isSafeToCancel: no path being walked
        return;
    }

    ExecContext ctx{player_, *liveCalc_, inventory_, expectedSegmentStart_};
    std::vector<BlockPos> inProgressBest;
    {
        std::lock_guard calc(calcLock_);
        if (inProgress_) inProgressBest = inProgress_->bestPathSoFar();
    }
    if (!inProgressBest.empty()) ctx.inProgressBest = &inProgressBest;
    safeToCancel_ = current_->onTick(ctx);
    output_ = current_->output();
    producedOutput_ = true;

    if (current_->failed() || current_->finished()) {
        if (current_->failed() && current_->failReason() && Settings::get().chatDebug) {
            notify_("Segment annule : " + *current_->failReason(), true);
        }
        current_.reset();
        output_ = MovementState{};
        safeToCancel_ = true;
        BlockPos feet = ctx.playerFeet();
        if (!goal_ || goal_->isInGoal(feet)) {
            next_.reset();
            if (goal_ && onArrival_) onArrival_();  // PathEvent.AT_GOAL
            return;
        }
        if (next_) {
            const auto& pos = next_->path()->positions();
            if (std::ranges::find(pos, feet) == pos.end() && std::ranges::find(pos, expectedSegmentStart_) == pos.end()) {
                next_.reset();  // the planned next segment doesn't start where we are
            }
        }
        if (next_) {
            current_ = std::move(next_);
            current_->onTick(ctx);
            output_ = current_->output();
            return;
        }
        std::lock_guard calc(calcLock_);
        if (inProgress_) return;  // the next segment is still being calculated
        findPathInNewThread(expectedSegmentStart_, true);
        return;
    }

    // the planned next segment already contains our feet: splice into it now
    if (safeToCancel_ && next_ && next_->snipsnapIfPossible(ctx)) {
        current_ = std::move(next_);
        current_->onTick(ctx);
        output_ = current_->output();
        return;
    }
    if (Settings::get().splicePath) current_ = PathExecutor::trySplice(std::move(current_), next_.get());
    // current is in progress: plan the next segment when it's almost over
    if (next_ && current_->path()->dest() == next_->path()->dest()) next_.reset();
    std::lock_guard calc(calcLock_);
    if (inProgress_ || next_) return;
    if (!goal_ || goal_->isInGoal(current_->path()->dest())) return;
    if (current_->ticksRemainingFrom(current_->position() + 1) < Settings::get().planningTickLookahead) {
        findPathInNewThread(current_->path()->dest(), false);
    }
}

// ---- debug / rendering ---------------------------------------------------------

std::string PathingBehavior::debugState() const {
    std::lock_guard lock(planLock_);
    if (!current_) return "idle";
    Movement* m = current_->currentMovement();
    if (!m) return "done";
    static constexpr const char* types[] = {"Traverse", "Ascend", "Descend", "Fall", "Diagonal", "Pillar", "Downward", "Parkour"};
    static constexpr const char* statuses[] = {"Prepping", "Waiting", "Running", "Success", "Unreachable", "Failed", "Canceled"};
    return std::format("{} {} -> {} {}", types[static_cast<int>(m->type())], m->src(), m->dest(),
                       statuses[static_cast<int>(m->state().status)]);
}

std::vector<BlockPos> PathingBehavior::currentPathPositions() const {
    std::lock_guard lock(planLock_);
    return current_ ? current_->path()->positions() : std::vector<BlockPos>{};
}

std::vector<BlockPos> PathingBehavior::nextPathPositions() const {
    std::lock_guard lock(planLock_);
    return next_ ? next_->path()->positions() : std::vector<BlockPos>{};
}

std::vector<BlockPos> PathingBehavior::calculationInProgress() const {
    std::lock_guard lock(calcLock_);
    return inProgress_ ? inProgress_->bestPathSoFar() : std::vector<BlockPos>{};
}

std::vector<BlockPos> PathingBehavior::mostRecentConsidered() const {
    std::lock_guard lock(calcLock_);
    return inProgress_ ? inProgress_->pathToMostRecentNodeConsidered() : std::vector<BlockPos>{};
}

std::size_t PathingBehavior::currentPathPosition() const {
    std::lock_guard lock(planLock_);
    return current_ ? current_->position() : 0;
}

PathingBehavior::BlockSets PathingBehavior::currentBlockSets() const {
    std::lock_guard lock(planLock_);
    if (!current_) return {};
    return {current_->toBreak(), current_->toPlace(), current_->toWalkInto()};
}

}  // namespace baritone
