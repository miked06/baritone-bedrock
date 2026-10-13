#include "baritone/pathing/path/PathExecutor.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <unordered_set>

#include "baritone/Settings.h"
#include "baritone/behavior/InventoryBehavior.h"
#include "baritone/pathing/movement/MovementHelper.h"

namespace baritone {

namespace {

double distanceToCenter(const Vec3d& p, const BlockPos& b) {
    double dx = p.x - (b.x + 0.5), dy = p.y - (b.y + 0.5), dz = p.z - (b.z + 0.5);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

double flatDistanceToCenter(const Vec3d& p, const BlockPos& b) {
    double dx = p.x - (b.x + 0.5), dz = p.z - (b.z + 0.5);
    return std::sqrt(dx * dx + dz * dz);
}

bool fullyPassable(ExecContext& ctx, const BlockPos& p) {
    return helper::fullyPassable(ctx.calc, p.x, p.y, p.z, ctx.props(p));
}

bool isType(const Movement* m, MovementType t) { return m && m->type() == t; }

// Java PathExecutor.skipNow: centered on the traverse and nothing to bonk on, so the
// traverse can be skipped into the sprint ascend.
bool skipNow(ExecContext& ctx, const Movement* current) {
    BlockPos dir = current->direction();
    Vec3d pos = ctx.player.position();
    double offTarget = std::fabs(dir.x * (current->src().z + 0.5 - pos.z)) + std::fabs(dir.z * (current->src().x + 0.5 - pos.x));
    if (offTarget > 0.1) return false;
    BlockPos headBonk = (current->src() - dir).above(2);
    if (fullyPassable(ctx, headBonk)) return true;
    double flatDist = std::fabs(dir.x * (headBonk.x + 0.5 - pos.x)) + std::fabs(dir.z * (headBonk.z + 0.5 - pos.z));
    return flatDist > 0.8;
}

bool sprintableAscend(ExecContext& ctx, Movement* current, Movement* next, Movement* nextnext) {
    if (!Settings::get().sprintAscends) return false;
    BlockPos cd = current->direction(), nd = next->direction(), nnd = nextnext->direction();
    if (cd != nd.below()) return false;
    if (nnd.x != nd.x || nnd.z != nd.z) return false;
    if (!ctx.canWalkOn(current->dest().below())) return false;
    if (!ctx.canWalkOn(next->dest().below())) return false;
    if (!next->toBreak(ctx.calc).empty()) return false;  // it's breaking
    for (int x = 0; x < 2; x++)
        for (int y = 0; y < 3; y++) {
            BlockPos chk = current->src().above(y);
            if (x == 1) chk = chk + cd;
            if (!fullyPassable(ctx, chk)) return false;
        }
    if (helper::avoidWalkingInto(ctx.props(current->src().above(3)))) return false;
    return !helper::avoidWalkingInto(ctx.props(next->dest().above(2)));
}

bool canSprintFromDescendInto(ExecContext& ctx, Movement* current, Movement* next) {
    if (isType(next, MovementType::Descend) && next->direction() == current->direction()) return true;
    if (!ctx.canWalkOn(current->dest() + current->direction())) return false;
    if (isType(next, MovementType::Traverse) && next->direction() == current->direction()) return true;
    return isType(next, MovementType::Diagonal) && Settings::get().allowOvershootDiagonalDescend;
}

BlockPos cross(const BlockPos& a, const BlockPos& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

}  // namespace

PathExecutor::PathExecutor(PathPtr path) : path_(std::move(path)) {
    for (const MovementSpec& spec : path_->movements()) movements_.push_back(Movement::create(spec));
}

void PathExecutor::cancel(std::string reason) {
    clearKeys();
    failed_ = true;
    failReason_ = std::move(reason);
    pathPosition_ = path_->length() + 3;
}

void PathExecutor::onChangeInPathPosition() {
    clearKeys();
    ticksOnCurrent_ = 0;
}

double PathExecutor::ticksRemainingFrom(std::size_t position) const {
    double sum = 0;
    for (std::size_t i = position; i < movements_.size(); ++i) sum += movements_[i]->getCost();
    return sum;
}

std::pair<double, BlockPos> PathExecutor::closestPathPos(ExecContext& ctx) {
    double best = -1;
    BlockPos bestPos;
    Vec3d pos = ctx.player.position();
    for (auto& m : movements_) {
        for (const BlockPos& p : m->validPositions()) {
            double d = distanceToCenter(pos, p);
            if (best < 0 || d < best) {
                best = d;
                bestPos = p;
            }
        }
    }
    return {best, bestPos};
}

bool PathExecutor::possiblyOffPath(ExecContext& ctx, const std::pair<double, BlockPos>& status, double leniency) {
    if (status.first <= leniency) return false;
    // in the middle of a fall we are far from both ends without being off the path
    if (pathPosition_ < movements_.size() && movements_[pathPosition_]->type() == MovementType::Fall &&
        pathPosition_ + 1 < path_->length()) {
        return flatDistanceToCenter(ctx.player.position(), path_->positions()[pathPosition_ + 1]) >= leniency;
    }
    return true;
}

// Java shouldPause: the path being calculated goes back through where we stand, so wait
// for it instead of walking on and coming back.
bool PathExecutor::shouldPause(ExecContext& ctx) {
    if (!ctx.inProgressBest) return false;
    if (!ctx.player.onGround()) return false;
    BlockPos feet = ctx.playerFeet();
    if (!ctx.canWalkOn(feet.below())) return false;
    if (!ctx.canWalkThrough(feet) || !ctx.canWalkThrough(feet.above())) return false;
    if (!movements_[pathPosition_]->safeToCancel(ctx)) return false;
    const std::vector<BlockPos>& positions = *ctx.inProgressBest;
    if (positions.size() < 3) return false;  // not long enough yet to justify pausing
    return std::find(positions.begin() + 1, positions.end(), feet) != positions.end();
}

// Java onTick (recalcBP): the blocks the rest of the path breaks, places and walks into.
// Only used for rendering, so only computed when it is drawn.
void PathExecutor::updateBlockSets(ExecContext& ctx) {
    const Settings& s = Settings::get();
    toBreak_.clear();
    toPlace_.clear();
    toWalkInto_.clear();
    if (!s.renderPath || !s.renderSelectionBoxes) return;
    std::unordered_set<BlockPos> seenBreak, seenPlace, seenWalk;
    auto add = [](std::vector<BlockPos>& out, std::unordered_set<BlockPos>& seen, const std::vector<BlockPos>& in) {
        for (const BlockPos& p : in)
            if (seen.insert(p).second) out.push_back(p);
    };
    for (std::size_t i = pathPosition_; i < movements_.size(); ++i) {
        Movement* m = movements_[i].get();
        add(toBreak_, seenBreak, m->toBreak(ctx.calc));
        add(toPlace_, seenPlace, m->toPlace(ctx.calc));
        add(toWalkInto_, seenWalk, m->toWalkInto(ctx.calc));
    }
}

bool PathExecutor::onTick(ExecContext& ctx) {
    struct Depth {
        int& d;
        Depth(int& x) : d(x) { ++d; }
        ~Depth() { --d; }
    } depthGuard(depth_);
    if (depth_ > 8) return false;  // safety against skip loops

    if (pathPosition_ + 1 == path_->length()) pathPosition_++;
    if (pathPosition_ >= path_->length()) {
        clearKeys();
        return true;
    }
    Movement* movement = movements_[pathPosition_].get();
    BlockPos whereAmI = ctx.playerFeet();
    if (!movement->validPositions().contains(whereAmI)) {
        for (std::size_t i = 0; i < pathPosition_ && i < movements_.size(); ++i) {
            // lagged back: go back to that movement
            if (movements_[i]->validPositions().contains(whereAmI)) {
                std::size_t previous = pathPosition_;
                pathPosition_ = i;
                for (std::size_t j = pathPosition_; j <= previous && j < movements_.size(); ++j) movements_[j]->reset();
                onChangeInPathPosition();
                onTick(ctx);
                return false;
            }
        }
        for (std::size_t i = pathPosition_ + 3; i + 1 < path_->length() && i < movements_.size(); ++i) {
            // skipped ahead (e.g. fell further than planned)
            if (movements_[i]->validPositions().contains(whereAmI)) {
                pathPosition_ = i - 1;
                onChangeInPathPosition();
                onTick(ctx);
                return false;
            }
        }
    }
    auto status = closestPathPos(ctx);
    if (possiblyOffPath(ctx, status, kMaxDistFromPath)) {
        if (++ticksAway_ > kMaxTicksAway) {
            cancel("trop loin du chemin depuis trop longtemps");
            return false;
        }
    } else {
        ticksAway_ = 0;
    }
    if (possiblyOffPath(ctx, status, kMaxMaxDistFromPath)) {
        cancel(std::format("trop loin du chemin ({:.1f} blocs)", status.first));
        return false;
    }

    updateBlockSets(ctx);

    if (pathPosition_ + 1 < movements_.size()) {
        const BlockPos& nextDest = movements_[pathPosition_ + 1]->dest();
        if (!ctx.calc.isLoadedLive(nextDest.x, nextDest.z)) {
            clearKeys();  // destination at the edge of loaded chunks: wait
            return true;
        }
    }

    bool canCancel = movement->safeToCancel(ctx);
    if (!costEstimateIndex_ || *costEstimateIndex_ != pathPosition_) {
        costEstimateIndex_ = pathPosition_;
        currentMovementOriginalCostEstimate_ = movement->getCost();
        for (int i = 1; i < Settings::get().costVerificationLookahead && pathPosition_ + i + 1 < path_->length(); ++i) {
            if (movements_[pathPosition_ + i]->calculateCost(ctx.calc) >= costs::COST_INF && canCancel) {
                cancel("un mouvement futur est devenu impossible");
                return true;
            }
        }
    }
    double currentCost = movement->recalculateCost(ctx.calc);
    if (currentCost >= costs::COST_INF && canCancel) {
        cancel("le mouvement actuel est devenu impossible");
        return true;
    }
    if (!movement->calculatedWhileLoaded &&
        currentCost - currentMovementOriginalCostEstimate_ > Settings::get().maxCostIncrease && canCancel) {
        cancel("le cout du mouvement a trop augmente");
        return true;
    }
    if (shouldPause(ctx)) {
        clearKeys();  // the path being calculated is a backtrack through here
        return true;
    }

    MovementStatus movementStatus = movement->update(ctx);
    if (movementStatus == MovementStatus::Unreachable || movementStatus == MovementStatus::Failed) {
        cancel(std::format("mouvement {} -> {} impossible", movement->src(), movement->dest()));
        return true;
    }
    if (movementStatus == MovementStatus::Success) {
        pathPosition_++;
        onChangeInPathPosition();
        onTick(ctx);
        return true;
    }
    output_ = movement->state();
    bool requested = output_.input(Input::Sprint);
    output_.setInput(Input::Sprint, false);  // we'll take it from here
    sprintNextTick_ = shouldSprintNextTick(ctx, requested);
    output_.setInput(Input::Sprint, sprintNextTick_);
    ticksOnCurrent_++;
    if (ticksOnCurrent_ > currentMovementOriginalCostEstimate_ + Settings::get().movementTimeoutTicks) {
        cancel(std::format("le mouvement prend trop de temps ({} ticks, {:.0f} prevus)", ticksOnCurrent_,
                           currentMovementOriginalCostEstimate_));
        return true;
    }
    return canCancel;
}

// Java PathExecutor.shouldSprintNextTick: sprints through traverse -> ascend, descend ->
// descend/traverse, and falls that continue straight on.
bool PathExecutor::shouldSprintNextTick(ExecContext& ctx, bool requested) {
    if (!ctx.calc.canSprint) return false;  // allowSprint off, or too hungry
    Movement* current = movements_[pathPosition_].get();
    auto at = [&](std::size_t i) -> Movement* { return i < movements_.size() ? movements_[i].get() : nullptr; };

    // traverse requests sprinting, so this check comes first
    if (isType(current, MovementType::Traverse) && pathPosition_ + 3 < path_->length()) {
        Movement* next = at(pathPosition_ + 1);
        Movement* nextnext = at(pathPosition_ + 2);
        if (isType(next, MovementType::Ascend) && nextnext && sprintableAscend(ctx, current, next, nextnext)) {
            if (skipNow(ctx, current)) {
                pathPosition_++;  // skipping traverse to straight ascend
                onChangeInPathPosition();
                onTick(ctx);
                output_.setInput(Input::Jump, true);
                return true;
            }
        }
    }
    if (requested) return true;  // the movement asked for it

    // descend and ascend don't request sprinting: they don't know what comes after them
    if (isType(current, MovementType::Descend)) {
        if (pathPosition_ + 2 < path_->length()) {
            Movement* next = at(pathPosition_ + 1);
            if (next && helper::canUseFrostWalker(ctx.calc, ctx.props(next->dest().below())) &&
                (isType(next, MovementType::Traverse) || isType(next, MovementType::Parkour))) {
                bool couldPlaceInstead = Settings::get().allowPlace && ctx.inventory.hasGenericThrowaway() &&
                                         isType(next, MovementType::Parkour);
                BlockPos up = current->direction().above();
                bool sameFlatDirection = up + next->direction() != BlockPos{0, 0, 0} && cross(up, next->direction()) == BlockPos{0, 0, 0};
                if (sameFlatDirection && !couldPlaceInstead) current->forceSafeMode();
            }
        }
        if (current->descendSafeMode(ctx) && !current->descendSkipToAscend(ctx)) return false;  // unsafe
        if (pathPosition_ + 2 < path_->length()) {
            Movement* next = at(pathPosition_ + 1);
            if (isType(next, MovementType::Ascend) && current->direction().above() == next->direction().below()) {
                // a descend then an ascend in the same direction
                pathPosition_++;
                onChangeInPathPosition();
                onTick(ctx);
                return true;
            }
            if (next && canSprintFromDescendInto(ctx, current, next)) {
                if (isType(next, MovementType::Descend) && pathPosition_ + 3 < path_->length()) {
                    Movement* nextnext = at(pathPosition_ + 2);
                    if (isType(nextnext, MovementType::Descend) && !canSprintFromDescendInto(ctx, next, nextnext)) return false;
                }
                if (ctx.playerFeet() == current->dest()) {
                    pathPosition_++;
                    onChangeInPathPosition();
                    onTick(ctx);
                }
                return true;
            }
        }
    }
    if (isType(current, MovementType::Ascend) && pathPosition_ != 0) {
        Movement* prev = at(pathPosition_ - 1);
        if (isType(prev, MovementType::Descend) && prev->direction().above() == current->direction().below()) {
            BlockPos center = current->src().above();
            // playerFeet adds 0.1251 for soul sand; 0.07 for farmland
            if (ctx.player.position().y >= center.y - 0.07) {
                output_.setInput(Input::Jump, false);
                return true;
            }
        }
        if (pathPosition_ + 2 < path_->length() && isType(prev, MovementType::Traverse) &&
            sprintableAscend(ctx, prev, current, at(pathPosition_ + 1))) {
            return true;
        }
    }
    if (isType(current, MovementType::Fall)) {
        if (auto data = overrideFall(ctx, current)) {
            const BlockPos& fallDest = data->second;
            const auto& positions = path_->positions();
            auto it = std::find(positions.begin(), positions.end(), fallDest);
            if (it == positions.end()) return false;  // illegal destination
            if (ctx.playerFeet() == fallDest) {
                pathPosition_ = static_cast<std::size_t>(it - positions.begin());
                onChangeInPathPosition();
                onTick(ctx);
                return true;
            }
            clearKeys();
            // not ticking the movement: open doors ourselves... (Java), then aim further on
            output_.setTarget({rotation_utils::calcRotationFromVec3d(ctx.playerHead(), data->first, ctx.playerRotations()), false});
            output_.setInput(Input::MoveForward, true);
            return true;
        }
    }
    return false;
}

// Java overrideFall: a short fall followed by traverses in the same direction is run as
// one long forward walk.
std::optional<std::pair<Vec3d, BlockPos>> PathExecutor::overrideFall(ExecContext& ctx, Movement* movement) {
    BlockPos dir = movement->direction();
    if (dir.y < -3) return std::nullopt;
    if (!movement->toBreak(ctx.calc).empty()) return std::nullopt;  // it's breaking
    BlockPos flatDir{dir.x, 0, dir.z};
    std::size_t i;
    for (i = pathPosition_ + 1; i + 1 < path_->length() && i < pathPosition_ + 3; i++) {
        Movement* next = movements_[i].get();
        if (next->type() != MovementType::Traverse) break;
        if (flatDir != next->direction()) break;
        bool blocked = false;
        for (int y = next->dest().y; y <= movement->src().y + 1 && !blocked; y++) {
            if (!fullyPassable(ctx, {next->dest().x, y, next->dest().z})) blocked = true;
        }
        if (blocked) break;
        if (!ctx.canWalkOn(next->dest().below())) break;
    }
    i--;
    if (i == pathPosition_) return std::nullopt;  // no valid extension exists
    double len = static_cast<double>(i - pathPosition_) - 0.4;
    int steps = static_cast<int>(i - pathPosition_);
    return std::pair{Vec3d{flatDir.x * len + movement->dest().x + 0.5, static_cast<double>(movement->dest().y),
                           flatDir.z * len + movement->dest().z + 0.5},
                     movement->dest().offset(flatDir.x * steps, 0, flatDir.z * steps)};
}

bool PathExecutor::snipsnapIfPossible(ExecContext& ctx) {
    if (!ctx.player.onGround() && !ctx.isLiquid(ctx.playerFeet())) return false;
    if (ctx.player.velocity().y < -0.1) return false;
    const auto& positions = path_->positions();
    auto it = std::find(positions.begin(), positions.end(), ctx.playerFeet());
    if (it == positions.end()) return false;
    pathPosition_ = static_cast<std::size_t>(it - positions.begin());  // jump directly to current position
    clearKeys();
    return true;
}

std::unique_ptr<PathExecutor> PathExecutor::trySplice(std::unique_ptr<PathExecutor> current, const PathExecutor* next) {
    if (!next) return cutIfTooLong(std::move(current));
    // Java SplicedPath.trySplice(first, second, allowOverlapCutoff = false)
    const Path& first = *current->path_;
    const Path& second = *next->path_;
    if (first.dest() != second.src()) return cutIfTooLong(std::move(current));
    std::unordered_set<BlockPos> secondPos(second.positions().begin(), second.positions().end());
    for (std::size_t i = 0; i + 1 < first.length(); ++i) {
        if (secondPos.contains(first.positions()[i])) return cutIfTooLong(std::move(current));  // overlapping
    }
    std::vector<BlockPos> positions(first.positions().begin(), first.positions().end());
    std::vector<MovementSpec> specs(first.movements().begin(), first.movements().end());
    positions.insert(positions.end(), second.positions().begin() + 1, second.positions().end());
    specs.insert(specs.end(), second.movements().begin(), second.movements().end());
    auto spliced = std::make_unique<PathExecutor>(
        Path::fromParts(std::move(positions), std::move(specs), first.goal(), first.numNodesConsidered() + second.numNodesConsidered()));
    // keep the movements already being executed (their state), then next's ones
    for (std::size_t i = 0; i < current->movements_.size(); ++i) spliced->movements_[i] = std::move(current->movements_[i]);
    spliced->pathPosition_ = current->pathPosition_;
    spliced->currentMovementOriginalCostEstimate_ = current->currentMovementOriginalCostEstimate_;
    spliced->costEstimateIndex_ = current->costEstimateIndex_;
    spliced->ticksOnCurrent_ = current->ticksOnCurrent_;
    spliced->ticksAway_ = current->ticksAway_;
    spliced->sprintNextTick_ = current->sprintNextTick_;
    spliced->output_ = current->output_;
    return spliced;
}

std::unique_ptr<PathExecutor> PathExecutor::cutIfTooLong(std::unique_ptr<PathExecutor> current) {
    const Settings& s = Settings::get();
    if (static_cast<int>(current->pathPosition_) <= s.maxPathHistoryLength) return current;
    std::size_t cutoff = static_cast<std::size_t>(std::max(0, s.pathHistoryCutoffAmount));
    if (cutoff == 0 || cutoff >= current->pathPosition_) return current;
    const Path& path = *current->path_;
    std::vector<BlockPos> positions(path.positions().begin() + cutoff, path.positions().end());
    std::vector<MovementSpec> specs(path.movements().begin() + cutoff, path.movements().end());
    auto trimmed = std::make_unique<PathExecutor>(
        Path::fromParts(std::move(positions), std::move(specs), path.goal(), path.numNodesConsidered()));
    for (std::size_t i = cutoff; i < current->movements_.size(); ++i) trimmed->movements_[i - cutoff] = std::move(current->movements_[i]);
    trimmed->pathPosition_ = current->pathPosition_ - cutoff;
    trimmed->currentMovementOriginalCostEstimate_ = current->currentMovementOriginalCostEstimate_;
    if (current->costEstimateIndex_) trimmed->costEstimateIndex_ = *current->costEstimateIndex_ - cutoff;
    trimmed->ticksOnCurrent_ = current->ticksOnCurrent_;
    trimmed->ticksAway_ = current->ticksAway_;
    trimmed->sprintNextTick_ = current->sprintNextTick_;
    trimmed->output_ = current->output_;
    return trimmed;
}

}  // namespace baritone
