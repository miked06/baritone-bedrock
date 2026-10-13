#include "baritone/process/BuilderProcess.h"

#include <algorithm>
#include <format>

#include "baritone/Baritone.h"
#include "baritone/Settings.h"
#include "baritone/pathing/movement/MovementHelper.h"
#include "baritone/schematic/Orientation.h"

namespace baritone {

namespace {

bool contains(const BlockList& list, const std::string& name) { return std::ranges::find(list, name) != list.end(); }

std::string withNamespace(std::string_view n) {
    std::string s(n);
    return s.find(':') == std::string::npos ? "minecraft:" + s : s;
}

// "from>to" entries of buildSubstitutes / buildValidSubstitutes
std::vector<std::string> substitutesFor(const BlockList& list, const std::string& name) {
    std::vector<std::string> out;
    for (const std::string& entry : list) {
        std::size_t sep = entry.find('>');
        if (sep == std::string::npos) continue;
        if (withNamespace(std::string_view(entry).substr(0, sep)) == name) out.push_back(withNamespace(std::string_view(entry).substr(sep + 1)));
    }
    return out;
}

// Java sameBlockstate: same block and orientation, minus buildIgnoreDirection and
// buildIgnoreProperties. (Bedrock: only the states the placement decides are compared;
// the others, like fence connections, follow the neighbours.)
bool sameBlockstate(const DesiredBlock& desired, const BlockProps& current) {
    const Settings& s = Settings::get();
    if (desired.kind != DesiredBlock::Kind::Block || desired.name != current.name) return false;
    if (s.buildIgnoreDirection) return true;
    for (const auto& [key, value] : desired.states) {
        if (!isOrientationState(key)) continue;
        if (std::ranges::find(s.buildIgnoreProperties, key) != s.buildIgnoreProperties.end()) continue;
        std::string_view have = current.state(key);
        if (!have.empty() && have != value) return false;
    }
    return true;
}

bool isAir(const BlockProps& p) { return p.type == BlockType::Air; }
bool isLiquid(const BlockProps& p) { return p.type == BlockType::Water || p.type == BlockType::Lava; }

// buildSkipBlocks (Java: a mask on the desired block)
class SkipBlocksSchematic : public Schematic {
public:
    explicit SkipBlocksSchematic(SchematicPtr inner) : inner_(std::move(inner)) {}
    BlockPos size() const override { return inner_->size(); }
    DesiredBlock desired(int x, int y, int z, const std::string& current) const override {
        DesiredBlock d = inner_->desired(x, y, z, current);
        if (d.kind == DesiredBlock::Kind::Block && contains(Settings::get().buildSkipBlocks, d.name)) return DesiredBlock::any();
        return d;
    }

private:
    SchematicPtr inner_;
};

// Java SelectionSchematic (buildOnlySelection): only what is inside a selection
class SelectionSchematic : public Schematic {
public:
    SelectionSchematic(SchematicPtr inner, BlockPos origin, std::vector<Selection> selections)
        : inner_(std::move(inner)), origin_(origin), selections_(std::move(selections)) {}
    BlockPos size() const override { return inner_->size(); }
    DesiredBlock desired(int x, int y, int z, const std::string& current) const override {
        BlockPos world = origin_.offset(x, y, z);
        if (std::ranges::none_of(selections_, [&](const Selection& s) { return s.contains(world); })) return DesiredBlock::any();
        return inner_->desired(x, y, z, current);
    }

private:
    SchematicPtr inner_;
    BlockPos origin_;
    std::vector<Selection> selections_;
};

// Java JankyGoalComposite: in either goal, heading for the primary one
class JankyGoalComposite : public Goal {
public:
    JankyGoalComposite(GoalPtr primary, GoalPtr fallback) : primary_(std::move(primary)), fallback_(std::move(fallback)) {}
    bool isInGoal(int x, int y, int z) const override { return primary_->isInGoal(x, y, z) || fallback_->isInGoal(x, y, z); }
    double heuristic(int x, int y, int z) const override { return primary_->heuristic(x, y, z); }
    std::string toString() const override { return "JankyComposite Primary: " + primary_->toString() + " Fallback: " + fallback_->toString(); }
    std::string key() const override { return "Janky(" + primary_->key() + "," + fallback_->key() + ")"; }

private:
    GoalPtr primary_, fallback_;
};

// Java BuilderProcess.GoalAdjacent: next to the position, not in it nor in the block
// placed against, not lower than one below unless allowSameLevel
class GoalAdjacentPlace : public GoalGetToBlock {
public:
    GoalAdjacentPlace(BlockPos pos, BlockPos no, bool allowSameLevel) : GoalGetToBlock(pos), no_(no), allowSameLevel_(allowSameLevel) {}
    bool isInGoal(int x, int y, int z) const override {
        if (BlockPos{x, y, z} == pos() || BlockPos{x, y, z} == no_) return false;
        if (!allowSameLevel_ && y == pos().y - 1) return false;
        if (y < pos().y - 1) return false;
        return GoalGetToBlock::isInGoal(x, y, z);
    }
    double heuristic(int x, int y, int z) const override { return pos().y * 100 + GoalGetToBlock::heuristic(x, y, z); }
    std::string toString() const override { return std::format("GoalAdjacent{{{}}}", pos()); }
    std::string key() const override {
        UncensoredScope raw;
        return std::format("Adjacent{{{}|{}|{}}}", pos(), no_, allowSameLevel_);
    }

private:
    BlockPos no_;
    bool allowSameLevel_;
};

// Java BuilderProcess.GoalPlace: standing on the position (pillaring into it)
class GoalPlace : public GoalBlock {
public:
    explicit GoalPlace(BlockPos placeAt) : GoalBlock(placeAt.above()) {}
    double heuristic(int x, int y, int z) const override { return pos().y * 100 + GoalBlock::heuristic(x, y, z); }
    std::string toString() const override { return std::format("GoalPlace{{{}}}", pos()); }
    std::string key() const override {
        UncensoredScope raw;
        return std::format("Place{{{}}}", pos());
    }
};

// A goal minus some positions (Bedrock: spots known to be useless for a placement).
class GoalExcept : public Goal {
public:
    GoalExcept(GoalPtr inner, std::vector<BlockPos> excluded) : inner_(std::move(inner)), excluded_(std::move(excluded)) {}
    bool isInGoal(int x, int y, int z) const override {
        return inner_->isInGoal(x, y, z) && std::ranges::find(excluded_, BlockPos{x, y, z}) == excluded_.end();
    }
    double heuristic(int x, int y, int z) const override { return inner_->heuristic(x, y, z); }
    std::string toString() const override { return "GoalExcept{" + inner_->toString() + "}"; }

private:
    GoalPtr inner_;
    std::vector<BlockPos> excluded_;
};

Vec3d faceCenter(const BlockPos& pos, Direction against, bool topHalf) {
    BlockPos a = pos.offset(against);
    Vec3d c{(pos.x + a.x + 1.0) * 0.5, (pos.y + a.y + 1.0) * 0.5, (pos.z + a.z + 1.0) * 0.5};
    if (against != Direction::Up && against != Direction::Down) c.y = pos.y + (topHalf ? 0.75 : 0.25);
    return c;
}

int rotationTurns(const std::string& r) {
    if (r == "CLOCKWISE_90") return 1;
    if (r == "CLOCKWISE_180") return 2;
    if (r == "COUNTERCLOCKWISE_90") return 3;
    return 0;
}

int mirrorMode(const std::string& m) {
    if (m == "LEFT_RIGHT") return 1;
    if (m == "FRONT_BACK") return 2;
    return 0;
}

constexpr Direction kAllFaces[] = {Direction::Down, Direction::North, Direction::South, Direction::West, Direction::East, Direction::Up};

}  // namespace

// ---- setup ------------------------------------------------------------------------

void BuilderProcess::build(std::string name, SchematicPtr schematic, BlockPos origin, bool fromFile) {
    const Settings& s = Settings::get();
    name_ = std::move(name);
    BlockPos rawSize = schematic->size();
    // Java build(File) -> applyMapArtAndSelection
    if (fromFile && s.mapArtMode) schematic = std::make_shared<MapArtSchematic>(std::move(schematic));
    if (fromFile && s.buildOnlySelection) {
        schematic = std::make_shared<SelectionSchematic>(std::move(schematic), origin, baritone_.selections().all());
    }
    // Java build(schematic): substitutes (in placeable/valid), mirror, rotation, skip mask
    if (int m = mirrorMode(s.buildSchematicMirror)) schematic = std::make_shared<MirroredSchematic>(std::move(schematic), m);
    if (int t = rotationTurns(s.buildSchematicRotation)) schematic = std::make_shared<RotatedSchematic>(std::move(schematic), t);
    schematic = std::make_shared<SkipBlocksSchematic>(std::move(schematic));
    if (s.schematicOrientationX) origin.x += rawSize.x;
    if (s.schematicOrientationY) origin.y += rawSize.y;
    if (s.schematicOrientationZ) origin.z += rawSize.z;
    schematic_ = std::move(schematic);
    origin_ = origin;
    paused_ = false;
    layer_ = s.startAtLayer;
    stopAtHeight_ = rawSize.y;
    if (s.buildOnlySelection && fromFile) {
        const auto& sels = baritone_.selections().all();
        if (sels.empty()) {
            baritone_.logDirect("buildOnlySelection est actif mais il n'y a aucune selection", true);
            stopAtHeight_ = 0;
        } else if (s.buildInLayers) {
            int minim = INT32_MAX, maxim = INT32_MIN;
            for (const auto& sel : sels) {
                minim = std::min(minim, sel.min().y);
                maxim = std::max(maxim, sel.max().y);
            }
            int startAtHeight = s.layerOrder ? origin_.y + rawSize.y - maxim : minim - origin_.y;
            stopAtHeight_ = (s.layerOrder ? origin_.y + rawSize.y - minim : maxim - origin_.y) + 1;
            layer_ = std::max(layer_, startAtHeight / std::max(1, s.layerHeight));
        }
    }
    numRepeats_ = 0;
    observedCompleted_.clear();
    incorrectPositions_.reset();
    ticks_ = 0;
    clickedLeftLastTick_ = false;
    badSpots_.clear();
    stuckHere_.clear();
}

void BuilderProcess::onLostControl() {
    incorrectPositions_.reset();
    failedCalcs_ = 0;
    name_.clear();
    schematic_.reset();
    layer_ = Settings::get().startAtLayer;
    numRepeats_ = 0;
    paused_ = false;
    observedCompleted_.clear();
}

std::string BuilderProcess::displayName() const {
    return paused_ ? "Construction en pause" : "Construction de " + name_;
}

std::optional<std::pair<BlockPos, BlockPos>> BuilderProcess::bounds() const {
    if (!schematic_) return std::nullopt;
    BlockPos s = schematic_->size();
    return std::make_pair(origin_, origin_.offset(s.x - 1, s.y - 1, s.z - 1));
}

// buildInLayers: the schematic y range built so far (Java: the realSchematic wrapper)
std::pair<int, int> BuilderProcess::layerBounds() const {
    const Settings& s = Settings::get();
    int height = schematic_->size().y;
    if (!s.buildInLayers) return {0, height - 1};
    int lh = std::max(1, s.layerHeight);
    if (s.layerOrder) return {height - layer_ * lh, height - 1};  // top to bottom
    return {0, layer_ * lh - 1};
}

std::optional<DesiredBlock> BuilderProcess::getSchematic(int x, int y, int z, const BlockProps& current) const {
    int rx = x - origin_.x, ry = y - origin_.y, rz = z - origin_.z;
    if (!schematic_->inBounds(rx, ry, rz)) return std::nullopt;
    auto [minY, maxY] = layerBounds();
    if (ry < minY || ry > maxY) return std::nullopt;
    DesiredBlock d = schematic_->desired(rx, ry, rz, current.name);
    if (d.kind == DesiredBlock::Kind::Any) return std::nullopt;
    return d;
}

std::optional<std::string> BuilderProcess::placeAt(int x, int y, int z, const BlockProps& current) const {
    if (!schematic_) return std::nullopt;
    auto d = getSchematic(x, y, z, current);
    if (!d || d->kind != DesiredBlock::Kind::Block) return std::nullopt;
    return d->name;
}

// Java BuilderProcess.valid
bool BuilderProcess::valid(const BlockProps& current, const std::optional<DesiredBlock>& desired, bool itemVerify) {
    if (!desired) return true;
    const Settings& s = Settings::get();
    bool desiredAir = desired->kind == DesiredBlock::Kind::Air;
    if (isLiquid(current) && s.okIfWater) return true;
    if (isAir(current) && desiredAir) return true;
    if (isAir(current) && !desiredAir && contains(s.okIfAir, desired->name)) return true;
    if (desiredAir && contains(s.buildIgnoreBlocks, current.name)) return true;
    if (!isAir(current) && s.buildIgnoreExisting && !itemVerify) return true;
    if (!desiredAir && contains(substitutesFor(s.buildValidSubstitutes, desired->name), current.name) && !itemVerify) return true;
    // a buildSubstitutes block placed instead of the missing one (Java: SubstituteSchematic)
    if (!desiredAir && contains(substitutesFor(s.buildSubstitutes, desired->name), current.name)) return true;
    return sameBlockstate(*desired, current);
}

bool BuilderProcess::haveItemFor(const std::string& block, bool wholeInventory) {
    auto pred = [&](const HotbarStack& s) { return s.isBlock && s.name == block; };
    if (!wholeInventory) {
        for (int i = 0; i < 9; ++i)
            if (pred(baritone_.player().hotbar(i))) return true;
        return false;
    }
    for (int i = 0; i < 36; ++i)
        if (pred(baritone_.player().hotbar(i))) return true;
    return false;
}

// The block itself, or the first buildSubstitutes alternative we have.
std::optional<DesiredBlock> BuilderProcess::placeable(const DesiredBlock& wanted, bool wholeInventory) {
    if (haveItemFor(wanted.name, wholeInventory)) return wanted;
    for (const std::string& alt : substitutesFor(Settings::get().buildSubstitutes, wanted.name)) {
        if (haveItemFor(alt, wholeInventory)) {
            DesiredBlock b = wanted;
            b.name = alt;
            return b;
        }
    }
    return std::nullopt;
}

// Java BuilderCalculationContext
void BuilderProcess::customizeContext(CalculationContext& ctx) {
    if (!schematic_) return;
    ctx.jumpPenalty += 10;
    ctx.backtrackCostFavoringCoefficient = 1;
    // copies: the path calculation runs on another thread
    auto self = std::make_shared<const BuilderProcess*>(this);
    SchematicPtr sch = schematic_;
    BlockPos origin = origin_;
    auto [minY, maxY] = layerBounds();
    std::unordered_set<std::string> hotbar;
    for (int i = 0; i < 9; ++i) {
        HotbarStack s = baritone_.player().hotbar(i);
        if (s.isBlock) hotbar.insert(s.name);
    }
    auto getSchematic = [sch, origin, minY, maxY](int x, int y, int z, const BlockProps& current) -> std::optional<DesiredBlock> {
        int rx = x - origin.x, ry = y - origin.y, rz = z - origin.z;
        if (!sch->inBounds(rx, ry, rz) || ry < minY || ry > maxY) return std::nullopt;
        DesiredBlock d = sch->desired(rx, ry, rz, current.name);
        if (d.kind == DesiredBlock::Kind::Any) return std::nullopt;
        return d;
    };
    double placeBlockCost = ctx.placeBlockCost;
    bool hasThrowaway = ctx.hasThrowaway;
    double placeMult = Settings::get().placeIncorrectBlockPenaltyMultiplier;
    double breakCorrectMult = Settings::get().breakCorrectBlockPenaltyMultiplier;
    bool allowBreak = ctx.allowBreak;
    auto allowBreakAnyway = ctx.allowBreakAnyway;
    ctx.placeCostOverride = [=](int x, int y, int z, const BlockProps& current) {
        auto d = getSchematic(x, y, z, current);
        if (d) {
            if (d->kind == DesiredBlock::Kind::Air) return placeBlockCost * placeMult;  // we'll have to break it eventually
            if (hotbar.contains(d->name)) return 0.0;  // free to place a block where it should go
            if (!hasThrowaway) return costs::COST_INF;
            return placeBlockCost * 1.5 * placeMult;
        }
        return hasThrowaway ? placeBlockCost : costs::COST_INF;
    };
    ctx.breakMultiplierOverride = [=](int x, int y, int z, const BlockProps& current) {
        if (!allowBreak && !allowBreakAnyway.contains(current.name)) return costs::COST_INF;
        auto d = getSchematic(x, y, z, current);
        if (d) {
            if (d->kind == DesiredBlock::Kind::Air) return 1.0;
            return valid(current, d, false) ? breakCorrectMult : 1.0;
        }
        return 1.0;
    };
}

// ---- incorrect positions (Java recalc / fullRecalc / recalcNearby / trim) -------------

bool BuilderProcess::recalc(CalculationContext& ctx) {
    if (!incorrectPositions_) {
        incorrectPositions_.emplace();
        fullRecalc(ctx);
        if (incorrectPositions_->empty()) return false;
    }
    recalcNearby(ctx);
    if (incorrectPositions_->empty()) fullRecalc(ctx);
    return !incorrectPositions_->empty();
}

void BuilderProcess::trim() {
    BlockPos feet = baritone_.playerFeet();
    std::unordered_set<BlockPos> copy;
    for (const BlockPos& p : *incorrectPositions_)
        if (p.distSq(feet) <= 200) copy.insert(p);
    if (!copy.empty()) incorrectPositions_ = std::move(copy);
}

void BuilderProcess::recalcNearby(CalculationContext& ctx) {
    BlockPos center = baritone_.playerFeet();
    int radius = Settings::get().builderTickScanRadius;
    for (int dx = -radius; dx <= radius; dx++)
        for (int dy = -radius; dy <= radius; dy++)
            for (int dz = -radius; dz <= radius; dz++) {
                int x = center.x + dx, y = center.y + dy, z = center.z + dz;
                const BlockProps& current = ctx.props(x, y, z);
                auto desired = getSchematic(x, y, z, current);
                if (!desired) continue;
                BlockPos pos{x, y, z};
                if (valid(current, desired, false)) {
                    incorrectPositions_->erase(pos);
                    observedCompleted_.insert(pos.longHash());
                } else {
                    incorrectPositions_->insert(pos);
                    observedCompleted_.erase(pos.longHash());
                }
            }
}

void BuilderProcess::fullRecalc(CalculationContext& ctx) {
    incorrectPositions_.emplace();
    BlockPos size = schematic_->size();
    int incorrectSize = Settings::get().incorrectSize;
    for (int y = 0; y < size.y; y++)
        for (int z = 0; z < size.z; z++)
            for (int x = 0; x < size.x; x++) {
                int bx = x + origin_.x, by = y + origin_.y, bz = z + origin_.z;
                const BlockProps& current = ctx.props(bx, by, bz);
                auto desired = getSchematic(bx, by, bz, current);
                if (!desired) continue;
                int64_t hash = BlockPos::longHash(bx, by, bz);
                if (ctx.isLoadedLive(bx, bz)) {  // in render distance, not just in the cache
                    if (valid(current, desired, false)) {
                        observedCompleted_.insert(hash);
                    } else {
                        incorrectPositions_->insert({bx, by, bz});
                        observedCompleted_.erase(hash);
                        if (static_cast<int>(incorrectPositions_->size()) > incorrectSize) return;
                    }
                    continue;
                }
                // out of render distance: assume it still needs work unless seen done
                if (!observedCompleted_.contains(hash)) {
                    incorrectPositions_->insert({bx, by, bz});
                    if (static_cast<int>(incorrectPositions_->size()) > incorrectSize) return;
                }
            }
}

// ---- acting near the player -----------------------------------------------------------

// Java toBreakNearPlayer: a wrong block within 5 blocks that can be reached
std::optional<std::pair<BlockPos, Rotation>> BuilderProcess::toBreakNearPlayer(ExecContext& ec) {
    BlockPos center = baritone_.playerFeet();
    BlockPos pathStart = baritone_.pathing().pathStart();
    bool fromAbove = Settings::get().breakFromAbove;
    for (int dx = -5; dx <= 5; dx++)
        for (int dy = fromAbove ? -1 : 0; dy <= 5; dy++)
            for (int dz = -5; dz <= 5; dz++) {
                int x = center.x + dx, y = center.y + dy, z = center.z + dz;
                if (dy == -1 && x == pathStart.x && z == pathStart.z) continue;  // don't mine what supports us
                const BlockProps& current = ec.calc.props(x, y, z);
                auto desired = getSchematic(x, y, z, current);
                if (!desired) continue;
                if (!isAir(current) && !isLiquid(current) && !valid(current, desired, false) && !current.unbreakable) {
                    BlockPos pos{x, y, z};
                    if (auto rot = movement_helper::reachable(ec, pos)) return std::pair{pos, *rot};
                }
            }
    return std::nullopt;
}

// Places `block` at `pos` if a face that gives the right orientation is in reach (Java
// possibleToPlace + hasAnyItemThatWouldPlace, with Bedrock's orientation rules).
bool BuilderProcess::tryPlace(const BlockPos& pos, const DesiredBlock& block, ExecContext& ec, bool act) {
    CalculationContext& ctx = ec.calc;
    double reach = baritone_.player().blockReachDistance();
    bool ignoreDirection = Settings::get().buildIgnoreDirection;
    for (Direction d : kAllFaces) {
        BlockPos against = pos.offset(d);
        if (!helper::canPlaceAgainst(ctx.props(against.x, against.y, against.z))) continue;
        bool side = d != Direction::Up && d != Direction::Down;
        for (bool topHalf : {false, true}) {
            if (!side && topHalf) continue;
            bool top = side ? topHalf : d == Direction::Up;  // the bottom face of the block above gives a top half
            Vec3d face = faceCenter(pos, d, topHalf);
            Vec3d eyes = ec.playerHead();
            Rotation rot = rotation_utils::calcRotationFromVec3d(eyes, face, ec.playerRotations());
            if (!ignoreDirection && !placementGivesOrientation(block, {d, top, horizontalFacing(rot.yaw)})) continue;
            auto hit = rayTrace(ctx.bsi, eyes, rot, reach);
            if (!hit || hit->pos != against || hit->pos.offset(hit->face) != pos) continue;
            if (!act) return true;
            baritone_.inventory().selectItem([&](const HotbarStack& s) { return s.isBlock && s.name == block.name; }, true);
            MovementState& controls = baritone_.controls();
            controls.setTarget({rot, true});
            controls.setInput(Input::Sneak, true);
            auto now = ec.objectMouseOver();
            if ((now && now->pos == against && now->pos.offset(now->face) == pos && (!side || (now->hit.y - pos.y >= 0.5) == topHalf)) ||
                ec.playerRotations().isReallyCloseTo(rot)) {
                controls.setInput(Input::ClickRight, true);
            }
            return true;
        }
    }
    return false;
}

// Java searchForPlacables: the positions to fill within 5 blocks (below the head)
bool BuilderProcess::searchForPlacables(ExecContext& ec, bool isSafeToCancel, std::vector<DesiredBlock>& desirableOnHotbar) {
    BlockPos center = baritone_.playerFeet();
    CalculationContext& ctx = ec.calc;
    bool canAct = isSafeToCancel && baritone_.player().onGround() && ticks_ <= 0;
    bool blockedByPlayer = false;
    for (int dx = -5; dx <= 5; dx++)
        for (int dy = -5; dy <= 1; dy++)
            for (int dz = -5; dz <= 5; dz++) {
                int x = center.x + dx, y = center.y + dy, z = center.z + dz;
                const BlockProps& current = ctx.props(x, y, z);
                auto desired = getSchematic(x, y, z, current);
                if (!desired || desired->kind != DesiredBlock::Kind::Block) continue;
                if (!helper::isReplaceable(ctx, x, y, z, current) || valid(current, desired, false)) continue;
                if (dy == 1 && isAir(ctx.props(x, y + 1, z))) continue;
                desirableOnHotbar.push_back(*desired);
                BlockPos pos{x, y, z};
                auto block = placeable(*desired, false);
                if (!block) continue;
                // Java placementPlausible: the block must not intersect the player
                if (movement_helper::intersectsPlayer(baritone_.player(), pos)) {
                    BlockPos feet = baritone_.playerFeet();
                    if (pos == feet || pos == feet.above()) {
                        // standing in it: this spot is no good for it, the path moves us out
                        auto& bad = badSpots_[pos];
                        if (std::ranges::find(bad, feet) == bad.end()) bad.push_back(feet);
                    } else if (++recenterTicks_ <= 20) {
                        blockedByPlayer = true;  // only overlapping it: step back to our block's middle
                    }
                    continue;
                }
                if (tryPlace(pos, *block, ec, canAct)) {
                    if (!canAct) return false;
                    stuckHere_.erase(pos);
                    recenterTicks_ = 0;
                    return true;
                }
                // Bedrock: where we stand was supposed to work for it: give the spot up after a while
                BlockPos feet = baritone_.playerFeet();
                if (canAct && placementGoal(pos, *block, ctx)->isInGoal(feet.x, feet.y, feet.z) && ++stuckHere_[pos] >= 10) {
                    badSpots_[pos].push_back(feet);
                    stuckHere_.erase(pos);
                }
            }
    if (blockedByPlayer && canAct) {
        // Bedrock: standing on the edge of the block to fill: step back to the middle of our block
        movement_helper::moveTowards(ec, baritone_.controls(), center);
        baritone_.controls().setInput(Input::Sneak, true);  // slowly, without falling off
        return true;
    }
    return false;
}

// ---- goals (Java assemble / placementGoal / breakGoal) -------------------------------

GoalPtr BuilderProcess::breakGoal(const BlockPos& pos, CalculationContext& ctx) const {
    if (Settings::get().goalBreakFromAbove && isAir(ctx.props(pos.x, pos.y + 1, pos.z)) && isAir(ctx.props(pos.x, pos.y + 2, pos.z))) {
        class Above : public GoalGetToBlock {
        public:
            using GoalGetToBlock::GoalGetToBlock;
            bool isInGoal(int x, int y, int z) const override {
                if (y > pos().y || BlockPos{x, y, z} == pos()) return false;
                return GoalGetToBlock::isInGoal(x, y, z);
            }
        };
        return std::make_shared<JankyGoalComposite>(std::make_shared<GoalBreak>(pos), std::make_shared<Above>(pos.above()));
    }
    return std::make_shared<GoalBreak>(pos);
}

GoalPtr BuilderProcess::placementGoal(const BlockPos& pos, const DesiredBlock& block, CalculationContext& ctx) const {
    GoalPtr goal;
    bool ignoreDirection = Settings::get().buildIgnoreDirection;
    auto facing = ignoreDirection ? std::nullopt : requiredFacing(block);
    if (facing) {
        // Bedrock: blocks that face the player are placed from the side they must face
        std::vector<GoalPtr> spots;
        for (int dist = 1; dist <= 2; ++dist) {
            BlockPos behind = pos.offset(*facing, -dist);
            for (int dy = -1; dy <= 1; ++dy) spots.push_back(std::make_shared<GoalBlock>(behind.offset(0, dy, 0)));
        }
        goal = std::make_shared<GoalComposite>(std::move(spots));
    } else if (!isAir(ctx.props(pos.x, pos.y, pos.z))) {
        goal = std::make_shared<GoalPlace>(pos);
    } else {
        // Java: next to a block it can be placed against (Bedrock: one whose face gives the
        // right orientation)
        bool allowSameLevel = !isAir(ctx.props(pos.x, pos.y + 1, pos.z));
        static constexpr Direction exceptUp[] = {Direction::Down, Direction::North, Direction::South, Direction::West, Direction::East};
        for (Direction d : exceptUp) {
            BlockPos against = pos.offset(d);
            if (!helper::canPlaceAgainst(ctx.props(against.x, against.y, against.z))) continue;
            bool side = d != Direction::Down;
            bool usable = ignoreDirection;
            for (bool top : {false, true}) usable |= placementGivesOrientation(block, {d, side ? top : false, Direction::North});
            if (!usable) continue;
            goal = std::make_shared<GoalAdjacentPlace>(pos, against, allowSameLevel);
            break;
        }
        if (!goal) goal = std::make_shared<GoalPlace>(pos);
    }
    auto bad = badSpots_.find(pos);
    if (bad == badSpots_.end()) return goal;
    return std::make_shared<GoalExcept>(std::move(goal), bad->second);
}

GoalPtr BuilderProcess::assemble(CalculationContext& ctx, bool wholeInventory, bool logMissing) {
    std::vector<std::pair<BlockPos, DesiredBlock>> placeableList;
    std::vector<BlockPos> breakable, sourceLiquids, flowingLiquids, outOfBounds;
    std::map<std::string, int> missing;
    for (const BlockPos& pos : *incorrectPositions_) {
        const BlockProps& state = ctx.props(pos.x, pos.y, pos.z);
        if (isAir(state)) {
            auto desired = getSchematic(pos.x, pos.y, pos.z, state);
            if (!desired) {
                outOfBounds.push_back(pos);
            } else if (desired->kind == DesiredBlock::Kind::Block) {
                if (auto b = placeable(*desired, wholeInventory)) placeableList.emplace_back(pos, *b);
                else missing[desired->name]++;
            }
        } else if (isLiquid(state)) {
            (helper::possiblyFlowing(state) ? flowingLiquids : sourceLiquids).push_back(pos);
        } else {
            breakable.push_back(pos);
        }
    }
    for (const BlockPos& p : outOfBounds) incorrectPositions_->erase(p);
    std::vector<GoalPtr> toBreak;
    for (const BlockPos& pos : breakable) toBreak.push_back(breakGoal(pos, ctx));
    std::vector<GoalPtr> toPlace;
    auto isPlaceable = [&](const BlockPos& p) {
        return std::ranges::any_of(placeableList, [&](const auto& e) { return e.first == p; });
    };
    for (const auto& [pos, block] : placeableList) {
        if (!isPlaceable(pos.below()) && !isPlaceable(pos.below(2))) toPlace.push_back(placementGoal(pos, block, ctx));
    }
    for (const BlockPos& pos : sourceLiquids) toPlace.push_back(std::make_shared<GoalBlock>(pos.above()));
    if (!toPlace.empty()) {
        if (Settings::get().chatDebug) {
            std::string list;
            for (std::size_t i = 0; i < toPlace.size() && i < 6; ++i) list += toPlace[i]->toString() + " ";
            baritone_.logDirect(std::format("Builder : {} a poser ({}), {} a casser", toPlace.size(), list, toBreak.size()), false);
        }
        return std::make_shared<JankyGoalComposite>(std::make_shared<GoalComposite>(std::move(toPlace)),
                                                    std::make_shared<GoalComposite>(std::move(toBreak)));
    }
    if (toBreak.empty()) {
        if (logMissing && !missing.empty()) {
            baritone_.logDirect("Materiaux manquants, au moins :", true);
            for (const auto& [block, count] : missing) baritone_.logDirect(std::format("{}x {}", count, block), true);
        }
        if (logMissing && !flowingLiquids.empty()) {
            baritone_.logDirect("Liquides impossibles a remplacer, au moins en :", true);
            for (const BlockPos& p : flowingLiquids) baritone_.logDirect(std::format("{}", p), true);
        }
        return nullptr;
    }
    return std::make_shared<GoalComposite>(std::move(toBreak));
}

// ---- tick -----------------------------------------------------------------------------

std::optional<PathingCommand> BuilderProcess::onTick(bool calcFailed, bool isSafeToCancel) {
    return tick(calcFailed, isSafeToCancel, 0);
}

std::optional<PathingCommand> BuilderProcess::tick(bool calcFailed, bool isSafeToCancel, int recursions) {
    if (!schematic_ || !baritone_.liveContext()) return std::nullopt;
    if (recursions > 100) return PathingCommand{nullptr, PathingCommandType::SetGoalAndPath};  // onTick calls itself
    const Settings& s = Settings::get();
    // Java: 5 ticks after a break click before placing
    if (recursions == 0) {
        if (clickedLeftLastTick_) ticks_ = 5;
        else ticks_--;
        clickedLeftLastTick_ = false;
    }
    if (paused_) return PathingCommand{nullptr, PathingCommandType::CancelAndSetGoal};
    CalculationContext& ctx = *baritone_.liveContext();
    ExecContext ec{baritone_.player(), ctx, baritone_.inventory(), baritone_.playerFeet()};
    MovementState& controls = baritone_.controls();

    if (!recalc(ctx)) {
        int lh = std::max(1, s.layerHeight);
        if (s.buildInLayers && layer_ * lh < stopAtHeight_) {
            baritone_.logDirect(std::format("Couche {}", layer_), false);
            layer_++;
            incorrectPositions_.reset();
            return tick(calcFailed, isSafeToCancel, recursions + 1);
        }
        Vec3i repeat = s.buildRepeat;
        int max = s.buildRepeatCount;
        numRepeats_++;
        if (repeat == Vec3i{} || (max != -1 && numRepeats_ >= max)) {
            baritone_.logDirect("Construction terminee", false);
            if (s.notificationOnBuildFinished) baritone_.logNotification("Construction terminee", false);
            onLostControl();
            return std::nullopt;
        }
        layer_ = 0;
        origin_ = origin_ + repeat;
        incorrectPositions_.reset();
        if (!s.buildRepeatSneaky) observedCompleted_.clear();  // Java schematic.reset()
        baritone_.logDirect(std::format("Repetition de la construction de {} : nouvelle origine {}", repeat, origin_), false);
        return tick(calcFailed, isSafeToCancel, recursions + 1);
    }
    if (s.distanceTrim) trim();

    if (auto toBreak = toBreakNearPlayer(ec); toBreak && isSafeToCancel && baritone_.player().onGround()) {
        auto [pos, rot] = *toBreak;
        controls.setTarget({rot, true});
        movement_helper::switchToBestToolFor(ec, ctx.props(pos.x, pos.y, pos.z));
        if (baritone_.player().isSneaking()) controls.setInput(Input::Sneak, true);
        if (ec.isLookingAt(pos) || ec.playerRotations().isReallyCloseTo(rot)) {
            controls.setInput(Input::ClickLeft, true);
            clickedLeftLastTick_ = true;
        }
        return PathingCommand{nullptr, PathingCommandType::CancelAndSetGoal};
    }
    std::vector<DesiredBlock> desirableOnHotbar;
    if (searchForPlacables(ec, isSafeToCancel, desirableOnHotbar)) return PathingCommand{nullptr, PathingCommandType::CancelAndSetGoal};

    if (s.allowInventory) {
        // Java: blocks the build needs that are only in the main inventory go to the hotbar,
        // on a slot that holds nothing useful
        std::vector<int> usefulSlots;
        std::vector<std::string> noValidHotbarOption;
        for (const DesiredBlock& d : desirableOnHotbar) {
            bool found = false;
            for (int i = 0; i < 9 && !found; ++i) {
                HotbarStack h = baritone_.player().hotbar(i);
                if (h.isBlock && h.name == d.name) {
                    usefulSlots.push_back(i);
                    found = true;
                }
            }
            if (!found) noValidHotbarOption.push_back(d.name);
        }
        for (int i = 9; i < 36; ++i) {
            HotbarStack h = baritone_.player().hotbar(i);
            if (!h.isBlock || std::ranges::find(noValidHotbarOption, h.name) == noValidHotbarOption.end()) continue;
            int target = -1;
            for (int slot = 8; slot >= 0 && target < 0; --slot)
                if (std::ranges::find(usefulSlots, slot) == usefulSlots.end()) target = slot;
            if (target >= 0 && !baritone_.inventory().requestSwapWithHotBar(i, target)) {
                return PathingCommand{nullptr, PathingCommandType::RequestPause};
            }
            break;
        }
    }

    GoalPtr goal = assemble(ctx, false, false);
    if (!goal) {
        goal = assemble(ctx, true, true);  // far away: assume the whole inventory can be used
        if (!goal) {
            if (s.skipFailedLayers && s.buildInLayers && layer_ * std::max(1, s.layerHeight) < schematic_->size().y) {
                baritone_.logDirect(std::format("Couche impossible a construire, je passe a la suivante (couche {})", layer_), true);
                layer_++;
                incorrectPositions_.reset();
                return tick(calcFailed, isSafeToCancel, recursions + 1);
            }
            baritone_.logDirect("Impossible de continuer. En pause : #resume pour reprendre, #cancel pour annuler", true);
            paused_ = true;
            return PathingCommand{nullptr, PathingCommandType::RequestPause};
        }
    }
    // Java keeps retrying silently; tell the player once why nothing happens. The usual cause
    // is a part of the build that can only be reached by pillaring or bridging without
    // throwaway blocks (acceptableThrowawayItems) in the hotbar.
    if (calcFailed) {
        if (++failedCalcs_ == 5) {
            baritone_.logDirect("Construction : aucun chemin vers les blocs a poser. Il faut peut-etre des blocs jetables "
                                "(acceptableThrowawayItems : terre, cobble...) pour monter ou faire un pont.", true);
        }
    } else if (baritone_.pathing().isPathing()) {
        failedCalcs_ = 0;
    }
    return PathingCommand{goal, PathingCommandType::ForceRevalidateGoalAndPath};
}

}  // namespace baritone
