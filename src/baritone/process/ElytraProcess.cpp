#include "baritone/process/ElytraProcess.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "baritone/Baritone.h"
#include "baritone/Settings.h"
#include "baritone/elytra/ElytraPhysics.h"

namespace baritone {

namespace {

constexpr const char* kFirework = "minecraft:firework_rocket";
constexpr const char* kElytra = "minecraft:elytra";
constexpr int kBoostTicks = 30;          // a firework pushes for ~1.5 s
constexpr int kRepathTicks = 100;        // recalculate as chunks load during the flight
constexpr double kLandingDistance = 40;  // start descending this far from the destination
constexpr int kSegment = 400;            // longest path calculated at once
constexpr int kCruiseHeight = 40;        // flying height above the destination on long trips

double horizontal(const Vec3d& a, const Vec3d& b) {
    double dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

}  // namespace

void ElytraProcess::start(BlockPos destination, bool knownY) {
    onLostControl();
    destination_ = destination;
    knownY_ = knownY;
    state_ = State::Takeoff;
    const Settings& s = Settings::get();
    allowTight_ = s.elytraAllowTightSpaces;
    allowAboveBuildLimit_ = s.elytraAllowAboveBuildLimit;
    allowAboveRoof_ = s.elytraAllowAboveRoof;
}

void ElytraProcess::resetState() {
    if (!destination_) return;
    BlockPos destination = *destination_;
    bool knownY = knownY_;
    bool flying = state_ != State::Takeoff;
    start(destination, knownY);
    if (flying) toldToJump_ = true;  // already in the air: no take off message
}

std::optional<std::string> ElytraProcess::pathTo(const GoalPtr& goal) {
    BlockPos dest;
    bool knownY = false;
    if (auto xz = std::dynamic_pointer_cast<const GoalXZ>(goal)) {
        dest = {xz->x(), 64, xz->z()};
    } else if (auto block = std::dynamic_pointer_cast<const GoalBlock>(goal)) {
        dest = block->pos();
        knownY = true;
    } else {
        return "Le goal doit etre un GoalXZ ou un GoalBlock";
    }
    if (knownY && !isSupportedPos(dest)) return "Le goal doit etre dans les limites du vol a l'elytre";
    if (!isSupportedPos(baritone_.playerFeet())) return "Le joueur doit etre dans les limites du vol a l'elytre";
    bool flying = destination_ && state_ != State::Takeoff;
    start(dest, knownY);
    if (flying) toldToJump_ = true;
    return std::nullopt;
}

bool ElytraProcess::isSupportedPos(const BlockPos& pos) const {
    auto world = baritone_.world();
    if (!world) return false;
    const Settings& s = Settings::get();
    bool nether = world->dimension() == 1;
    int minY = world->minY();
    int maxY = nether && !s.elytraAllowAboveRoof ? 127 : std::min(minY + 384, world->maxY());
    bool enforceMaxY = nether ? !(s.elytraAllowAboveRoof && s.elytraAllowAboveBuildLimit) : !s.elytraAllowAboveBuildLimit;
    return pos.y >= minY && (!enforceMaxY || pos.y < maxY);
}

void ElytraProcess::shutdown() {
    if (finder_) finder_->cancel();
    if (calc_.valid()) calc_.wait();
}

void ElytraProcess::onLostControl() {
    shutdown();
    calc_ = {};
    finder_.reset();
    destination_.reset();
    path_.clear();
    ticks_ = 0;
    lastPathTick_ = -1000;
    boostTicks_ = 0;
    takeoffTicks_ = 0;
    lastJumpPress_ = -100;
    toldToJump_ = false;
    clickedAt_ = -1;
    failedClicks_ = 0;
    setbackTicks_ = 0;
    swapAt_ = -1000;
    warnedNoEmergency_ = false;
    emergency_ = false;
    flyStartTick_ = 0;
    failedTakeoffs_ = 0;
    lastPos_.reset();
    simulation_.clear();
}

std::string ElytraProcess::displayName() const {
    return destination_ ? std::format("Elytre vers {} {}", destination_->x, destination_->z) : "Elytre";
}

void ElytraProcess::finish(const char* message, bool error) {
    baritone_.logDirect(message, error);
    onLostControl();
}

// Where to land: the destination, on top of the ground if its height is unknown.
BlockPos ElytraProcess::landingPoint() {
    BlockPos d = *destination_;
    if (knownY_) return d;
    BlockStateInterface bsi(baritone_.world());
    if (!bsi.isLoaded(d.x, d.z)) return {d.x, static_cast<int>(baritone_.player().position().y), d.z};
    auto groundAt = [&](int x, int z) -> std::optional<BlockPos> {
        for (int y = bsi.maxY() - 1; y > bsi.minY(); --y) {
            const BlockProps& p = bsi.props(x, y, z);
            if (p.collides || p.hasFluid()) {
                // elytraAllowLandOnNetherFortress: nether bricks are no landing spot otherwise
                if (!Settings::get().elytraAllowLandOnNetherFortress && p.name.find("nether_brick") != std::string::npos) return std::nullopt;
                return BlockPos{x, y + 1, z};
            }
        }
        return std::nullopt;
    };
    if (auto g = groundAt(d.x, d.z)) return *g;
    for (int r = 1; r <= 16; ++r)  // the closest column around that is fine
        for (int dx = -r; dx <= r; ++dx)
            for (int dz = -r; dz <= r; ++dz) {
                if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                if (auto g = groundAt(d.x + dx, d.z + dz)) return *g;
            }
    return d;
}

// Java findSafeLandingSpot: the closest column around `around` whose top block is solid ground
// (no water, lava, magma, fire, cactus...), within 64 blocks. `fromY`: where to start looking
// down in the nether.
std::optional<BlockPos> ElytraProcess::findSafeLandingSpot(const Vec3d& around, int fromY) {
    BlockStateInterface bsi(baritone_.world());
    int cx = static_cast<int>(std::floor(around.x)), cz = static_cast<int>(std::floor(around.z));
    // the surface: from the top of the world, except under the nether roof (from our height)
    int top = baritone_.world()->dimension() == 1 ? std::min(bsi.maxY() - 1, fromY) : bsi.maxY() - 1;
    auto safeAt = [&](int x, int z) -> std::optional<BlockPos> {
        if (!bsi.isLoaded(x, z)) return std::nullopt;
        for (int y = top; y > bsi.minY(); --y) {
            const BlockProps& p = bsi.props(x, y, z);
            if (p.type == BlockType::Air) continue;
            if (!p.collides || p.hasFluid() || p.type == BlockType::Magma || p.type == BlockType::Fire ||
                p.name.find("cactus") != std::string::npos || p.name.find("berry") != std::string::npos ||
                p.name.find("campfire") != std::string::npos || p.name.find("leaves") != std::string::npos)
                return std::nullopt;
            if (!Settings::get().elytraAllowLandOnNetherFortress && p.name.find("nether_brick") != std::string::npos) return std::nullopt;
            if (bsi.props(x, y + 1, z).collides || bsi.props(x, y + 2, z).collides) return std::nullopt;
            return BlockPos{x, y + 1, z};
        }
        return std::nullopt;
    };
    for (int r = 0; r <= 64; r += (r < 16 ? 1 : 2))
        for (int dx = -r; dx <= r; ++dx)
            for (int dz = -r; dz <= r; ++dz) {
                if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                if (auto g = safeAt(cx + dx, cz + dz)) return g;
            }
    return std::nullopt;
}

// The highest cruising height: under the bedrock roof in the nether (unless
// elytraAllowAboveRoof), no limit elsewhere.
int ElytraProcess::cruiseCeiling() const {
    auto world = baritone_.world();
    if (world && world->dimension() == 1 && !Settings::get().elytraAllowAboveRoof) return world->maxY() - 12;
    return 100000;
}

// Java elytraAllowAboveBuildLimit / elytraLongDistanceThreshold: long trips in the overworld
// and the end fly above the build limit, where nothing is in the way.
bool ElytraProcess::aboveBuildLimit(double tripDistance) const {
    auto world = baritone_.world();
    const Settings& s = Settings::get();
    return world && world->dimension() != 1 && s.elytraAllowAboveBuildLimit && tripDistance > s.elytraLongDistanceThreshold;
}

void ElytraProcess::requestPath(const BlockPos& from) {
    if (calc_.valid()) return;
    BlockPos goal = landingPoint().offset(0, 2, 0);
    double dx = goal.x - from.x, dz = goal.z - from.z, dist = std::sqrt(dx * dx + dz * dz);
    const Settings& st = Settings::get();
    elytra::ElytraPathFinder::Options options;
    options.clearance = st.elytraAllowTightSpaces ? 0 : 1;
    options.useCache = st.elytraUseCache;
    if (baritone_.world()->dimension() == 1 && st.elytraAllowAboveRoof) options.maxY = baritone_.world()->maxY() + 64;
    if (dist > kLandingDistance * 2) {
        // cruise: well above the ground on the way, the landing spiral brings us down
        int cruise = std::min(std::max(from.y, goal.y + kCruiseHeight), cruiseCeiling());
        if (aboveBuildLimit(dist)) {
            cruise = baritone_.world()->maxY() + 8;
            options.maxY = baritone_.world()->maxY() + 40;
        }
        double k = std::min(1.0, (dist - kLandingDistance) / dist);
        if (dist > kSegment) k = kSegment / dist;  // far away: a point on the way
        goal = {from.x + static_cast<int>(dx * k), cruise, from.z + static_cast<int>(dz * k)};
    }
    finder_ = std::make_shared<elytra::ElytraPathFinder>(baritone_.world(), options);
    if (st.elytraChatSpam) baritone_.logDirect(std::format("Elytre : chemin {} -> {}", from, goal), false);
    auto finder = finder_;
    calc_ = std::async(std::launch::async, [finder, from, goal] { return finder->calculate(from, goal, 3.0, 500); });
    lastPathTick_ = ticks_;
}

void ElytraProcess::pollPath() {
    if (!calc_.valid() || calc_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    elytra::FlightPathResult r = calc_.get();
    if (r.path.size() > 1) path_ = std::move(r.path);
}

// The furthest point of the path we can fly to in a straight line.
std::optional<Vec3d> ElytraProcess::steeringTarget(const Vec3d& pos) {
    if (path_.empty()) return std::nullopt;
    BlockStateInterface bsi(baritone_.world());
    std::size_t closest = 0;
    double best = 1e18;
    for (std::size_t i = 0; i < path_.size(); ++i) {
        double d = (Vec3d::center(path_[i]) - pos).lengthSq();
        if (d < best) {
            best = d;
            closest = i;
        }
    }
    double margin = Settings::get().elytraMinimumAvoidance;
    std::optional<Vec3d> target;
    for (std::size_t i = closest; i < path_.size(); ++i) {
        Vec3d c = Vec3d::center(path_[i]);
        if ((c - pos).lengthSq() > 50.0 * 50.0) break;
        if (elytra::clearLine(bsi, pos, c, margin)) target = c;
        else if (target) break;
    }
    if (!target) target = Vec3d::center(path_[std::min(closest + 1, path_.size() - 1)]);
    return target;
}

// Baritone's pitch solver: try pitches, simulate the flight, keep the one that gets closest
// to the target without hitting anything. Steep dives are kept out of the range: they build
// up a speed that kills on impact.
Rotation ElytraProcess::solvePitch(const Vec3d& pos, const Vec3d& vel, const Vec3d& target, bool& collisionFree,
                                   float minPitch, float maxPitch) {
    BlockStateInterface bsi(baritone_.world());
    Rotation toward = rotation_utils::calcRotationFromVec3d(pos, target, baritone_.player().rotation());
    Rotation best = toward.withPitch(-30);
    double bestScore = 1e18;
    collisionFree = false;
    const Settings& st = Settings::get();
    int simTicks = std::max(1, st.elytraSimulationTicks);
    float range = static_cast<float>(st.elytraPitchRange);  // Java: deviation from the line of sight
    for (int pass = 0; pass < 2 && !collisionFree; ++pass) {
        float lo = pass == 0 ? std::max(minPitch, toward.pitch - range) : -70;
        float hi = pass == 0 ? std::min(maxPitch, toward.pitch + range) : 70;  // pass 1: anything that avoids a crash
        if (lo > hi) std::swap(lo, hi);
        for (float pitch = lo; pitch <= hi; pitch += 5) {
            Rotation r = toward.withPitch(pitch);
            auto sim = elytra::simulate(bsi, {pos, vel}, r, simTicks, boostTicks_);
            if (!sim) continue;
            double score = 1e18;
            for (const Vec3d& p : *sim) score = std::min(score, (p - target).lengthSq());
            if (score < bestScore) {
                bestScore = score;
                best = r;
                collisionFree = true;
                simulation_ = *sim;  // elytraRenderSimulation
            }
        }
    }
    return best;
}

// Final approach: circle down around the landing point while too high, then glide in on a
// gentle slope and pull up near the ground to lose speed.
Rotation ElytraProcess::landingLook(const Vec3d& pos, const Vec3d& vel, const BlockPos& landing, bool& collisionFree) {
    double lx = landing.x + 0.5, lz = landing.z + 0.5, ly = landing.y;
    double d = std::hypot(lx - pos.x, lz - pos.z), h = pos.y - ly;
    double speed = std::sqrt(vel.lengthSq());
    Vec3d aim;
    float minPitch = -15, maxPitch = 35;
    bool spiral = h > std::max(8.0, d * 0.6);
    if (spiral) {
        // spiral: a point ahead on a 24 block circle around the landing point, well lower
        // (about one block down per tick)
        double angle = std::atan2(pos.z - lz, pos.x - lx) + 0.6;
        aim = {lx + std::cos(angle) * 24, pos.y - 20, lz + std::sin(angle) * 24};
        minPitch = 20;
        maxPitch = 45;
    } else {
        aim = {lx, ly + 1.0, lz};
    }
    if (speed > (spiral ? 1.9 : 1.3)) maxPitch = spiral ? 20.f : 15.f;  // too fast: no steeper dive
    if (!spiral && h < 5 && speed > 0.8) maxPitch = 0;              // flare before touching down
    aim_ = aim;
    return solvePitch(pos, vel, aim, collisionFree, std::min(minPitch, maxPitch), maxPitch);
}

int ElytraProcess::fireworkCount() { return baritone_.player().countItem(kFirework); }

// Java shouldLandForSafety: the worn elytra is almost broken (elytraMinimumDurability) or
// there are elytraMinFireworksBeforeLanding fireworks or less.
bool ElytraProcess::shouldLandForSafety() {
    PlayerContext& p = baritone_.player();
    const Settings& s = Settings::get();
    HotbarStack chest = p.armor(1);
    // gliding with nothing read in the chest slot: the armor is unknown, judge the fireworks only
    if (!(chest.empty() && p.isGliding())) {
        if (chest.name != kElytra || (chest.maxDamage > 0 && chest.maxDamage - chest.damage < s.elytraMinimumDurability)) return true;
    }
    return fireworkCount() <= s.elytraMinFireworksBeforeLanding;
}

// Java trySwapElytra (elytraAutoSwap): the worn elytra is at elytraMinimumDurability or less,
// put on one that is better. Bedrock: using an elytra from the hand swaps it with the worn
// one (the game's own armor swap), so it is brought to the hand and right clicked.
void ElytraProcess::trySwapElytra() {
    PlayerContext& p = baritone_.player();
    const Settings& s = Settings::get();
    if (!s.elytraAutoSwap || ticks_ - swapAt_ < 20) return;
    HotbarStack chest = p.armor(1);
    if (chest.name != kElytra || chest.maxDamage - chest.damage > s.elytraMinimumDurability) return;
    auto good = [&s](const HotbarStack& h) { return h.name == kElytra && h.maxDamage - h.damage > s.elytraMinimumDurability; };
    if (!good(p.hotbar(p.selectedSlot()))) {
        baritone_.inventory().selectItem(good, true);  // from the inventory too, a tick or two
        return;
    }
    if (p.useHeldItem()) {
        swapAt_ = ticks_;
        if (s.elytraChatSpam || s.chatDebug) baritone_.logDirect("Elytre : echange de l'elytre usee", false);
    }
}

bool ElytraProcess::useFirework() {
    PlayerContext& p = baritone_.player();
    if (p.hotbar(p.selectedSlot()).name != kFirework) {
        baritone_.inventory().selectItem([](const HotbarStack& s) { return s.name == kFirework; }, true);
        return false;
    }
    // a right click through the game's input; after 3 clicks without any speed gain (game
    // window not taking the click...), the player fires the rockets and the bot keeps steering
    if (setbackTicks_ > 0) return false;  // elytraFireworkSetbackUseDelay
    if (failedClicks_ < 3 && p.useHeldItem()) {
        boostTicks_ = 16;  // until the rocket shows up (then: as long as it exists)
        clickedAt_ = ticks_;
        return true;
    }
    if (ticks_ - lastFireworkHint_ >= 100) {
        baritone_.logDirect("Elytre : lance une fusee (clic droit), je garde le cap", false);
        lastFireworkHint_ = ticks_;
    }
    boostTicks_ = 10;
    return false;
}

std::optional<PathingCommand> ElytraProcess::onTick(bool, bool) {
    if (!destination_ || !baritone_.world() || !baritone_.liveContext()) return std::nullopt;
    {
        // Java: these settings change the path, plan it again from scratch
        const Settings& st = Settings::get();
        const char* changed = nullptr;
        if (allowTight_ != st.elytraAllowTightSpaces) changed = "elytraAllowTightSpaces";
        else if (allowAboveBuildLimit_ != st.elytraAllowAboveBuildLimit) changed = "elytraAllowAboveBuildLimit";
        else if (allowAboveRoof_ != st.elytraAllowAboveRoof && baritone_.world()->dimension() == 1) changed = "elytraAllowAboveRoof";
        if (changed) {
            baritone_.logDirect(std::format("Reglage {} modifie, chemin recalcule depuis zero", changed), false);
            resetState();
            if (!destination_) return std::nullopt;
        }
    }
    ++ticks_;
    if (boostTicks_ > 0) --boostTicks_;
    pollPath();
    PlayerContext& p = baritone_.player();
    MovementState& controls = baritone_.controls();
    const Settings& s = Settings::get();
    Vec3d pos = p.position(), vel = p.velocity();
    // a server setback (position moved much more than the velocity): wait before a firework
    if (setbackTicks_ > 0) --setbackTicks_;
    if (lastPos_ && (pos - *lastPos_).lengthSq() > std::max(16.0, 4 * vel.lengthSq() + 4)) {
        setbackTicks_ = Settings::get().elytraFireworkSetbackUseDelay;
        if (Settings::get().elytraChatSpam) baritone_.logDirect("Elytre : recul du serveur detecte", true);
    }
    lastPos_ = pos;
    {
        // a firework pushing the player is a rocket entity at the player (fired by the bot or by
        // the player); a sudden speed gain also counts, in case the entity is not seen
        double speedNow = std::sqrt(vel.lengthSq());
        bool rocket = false;
        if (p.isGliding()) {
            for (const EntityInfo& e : p.entities())
                if (e.type.find("firework") != std::string::npos && (e.position - pos).lengthSq() < 5 * 5) rocket = true;
        }
        if (rocket || (p.isGliding() && speedNow - lastSpeed_ > 0.08)) {
            // while the rocket exists it pushes; afterwards a couple of ticks of margin
            boostTicks_ = std::max(boostTicks_, rocket ? 3 : kBoostTicks - 2);
            if (clickedAt_ >= 0) failedClicks_ = 0;
            clickedAt_ = -1;
        } else if (clickedAt_ >= 0 && ticks_ - clickedAt_ > 15) {
            ++failedClicks_;  // the click did not fire a rocket
            if (s.chatDebug) baritone_.logDirect(std::format("Elytre : clic sans fusee ({})", failedClicks_));
            clickedAt_ = -1;
            boostTicks_ = 0;
        }
        lastSpeed_ = speedNow;
    }
    BlockPos landing = landingPoint();
    double toDestination = horizontal(pos, Vec3d::center(landing));
    PathingCommand pause{nullptr, PathingCommandType::RequestPause};

    if (!p.isGliding()) {
        if (state_ != State::Takeoff) {
            if ((p.onGround() || p.isInWater()) && toDestination > kLandingDistance * 2) {
                // came down far from the destination: wait for the next take off
                if (ticks_ - flyStartTick_ < 40 && ++failedTakeoffs_ >= 5) {
                    // Java AUTO_JUMP_FAILURE_MSG
                    finish("Elytre : impossible de decoller d'ici. Pars d'un endroit plus haut, pres d'un surplomb, "
                           "ou desactive elytraAutoJump et decolle toi-meme", true);
                    return std::nullopt;
                }
                baritone_.logDirect(std::format("Elytre : pose a {:.0f} blocs du but, redecolle pour continuer", toDestination), true);
                state_ = State::Takeoff;
                toldToJump_ = true;
                path_.clear();
                return PathingCommand{nullptr, PathingCommandType::Defer};
            }
            if (p.onGround() || p.isInWater()) {
                // landed: walk the rest of the way
                GoalPtr goal = knownY_ ? GoalPtr(std::make_shared<GoalBlock>(*destination_))
                                       : GoalPtr(std::make_shared<GoalXZ>(destination_->x, destination_->z));
                bool arrived = goal->isInGoal(baritone_.playerFeet());
                finish(arrived ? "Elytre : arrive" : "Elytre : atterri, fin du trajet a pied", false);
                if (!arrived) baritone_.customGoal().setGoalAndPath(goal);
                return std::nullopt;
            }
            // in the air without gliding: open it again, with single presses (a double tap would
            // toggle creative flight instead)
            if (p.velocity().y < 0 && ticks_ - lastJumpPress_ >= 12) {
                controls.setInput(Input::Jump, true);
                lastJumpPress_ = ticks_;
            }
            return pause;
        }
        if (!s.elytraAutoJump) {
            // the player takes off: wait without touching the keys, take over once gliding
            if (!toldToJump_) {
                baritone_.logDirect("Elytre : decolle (saut puis saut en l'air), je prends la main des que tu planes", false);
                toldToJump_ = true;
            }
            return PathingCommand{nullptr, PathingCommandType::Defer};  // keys left to the player
        }
        if (p.onGround() && shouldLandForSafety()) {  // Java LOCATE_JUMP
            finish("Elytre : pas de decollage, l'elytre est presque cassee ou il n'y a presque plus de fusees "
                   "(atterrissage d'urgence immediat sinon)", true);
            return std::nullopt;
        }
        if (p.onGround() && p.hotbar(p.selectedSlot()).name != kFirework && fireworkCount() > 0) {
            // hold a firework before jumping: it must go off as soon as the elytra opens
            baritone_.inventory().selectItem([](const HotbarStack& h) { return h.name == kFirework; }, true);
            return pause;
        }
        if (p.onGround()) {
            // jump (held while on the ground: one press, not a double tap)
            controls.setInput(Input::Jump, true);
            lastJumpPress_ = ticks_;
            takeoffTicks_ = 0;
            return pause;
        }
        // in the air: once falling, one more short press opens the elytra (single presses only,
        // a double tap toggles creative flight)
        if (++takeoffTicks_ > 80) {
            finish("Elytre : impossible de planer (elytre equipee ?)", true);
            return std::nullopt;
        }
        if (vel.y < 0 && ticks_ - lastJumpPress_ >= 8) {  // after the double tap window, still in the air
            controls.setInput(Input::Jump, true);
            lastJumpPress_ = ticks_;
        }
        return pause;
    }

    if (state_ == State::Takeoff) {
        state_ = State::Flying;
        flyStartTick_ = ticks_;
        // hold the fireworks right away: the first one is often needed within a few ticks
        baritone_.inventory().selectItem([](const HotbarStack& h) { return h.name == kFirework; }, true);
        baritone_.logDirect(std::format("Elytre : en vol vers {} {} ({} fusees)", destination_->x, destination_->z, fireworkCount()), false);
        // Java START_FLYING: a firework at once, a jump from the ground is only ~1 block high
        if (s.elytraAutoJump && boostTicks_ == 0 && p.hotbar(p.selectedSlot()).name == kFirework) useFirework();
    }
    int fireworks = fireworkCount();
    if (state_ == State::Flying && toDestination < kLandingDistance) state_ = State::Landing;
    trySwapElytra();
    if (state_ == State::Flying && !emergency_ && shouldLandForSafety()) {
        if (s.elytraAllowEmergencyLand) {
            baritone_.logDirect(std::format("Elytre : atterrissage d'urgence - elytre presque cassee ou presque plus de fusees ({})", fireworks), true);
            // Java: a safe landing spot (solid ground, no water) a bit ahead, reached with the
            // normal approach and landing spiral rather than a dive
            emergency_ = true;
            double flat = std::max(1e-3, std::hypot(vel.x, vel.z));
            Vec3d ahead{pos.x + vel.x / flat * 50, pos.y, pos.z + vel.z / flat * 50};
            auto spot = findSafeLandingSpot(ahead, static_cast<int>(pos.y));
            if (spot) baritone_.logDirect(std::format("Elytre : point d'atterrissage sur {}", *spot), false);
            destination_ = spot.value_or(BlockPos{static_cast<int>(ahead.x), 0, static_cast<int>(ahead.z)});
            knownY_ = spot.has_value();
            path_.clear();
        } else if (!warnedNoEmergency_) {
            baritone_.logDirect("Elytre : elytre presque cassee ou presque plus de fusees, je continue (elytraAllowEmergencyLand false)", true);
            warnedNoEmergency_ = true;
        }
    }

    // path: recalculated regularly, the world loads while we fly
    BlockPos here{static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y + 0.3)), static_cast<int>(std::floor(pos.z))};
    bool nearEnd = !path_.empty() && (Vec3d::center(path_.back()) - pos).lengthSq() < 30.0 * 30.0;
    if (path_.empty() || ticks_ - lastPathTick_ > kRepathTicks || (nearEnd && state_ == State::Flying)) requestPath(here);

    bool clear = false;
    landing_ = landing;
    Rotation look;
    if (state_ == State::Landing) {
        look = landingLook(pos, vel, landing, clear);
    } else {
        // no path yet: straight towards the destination at cruising height, never at the ground
        Vec3d toward = Vec3d::center(landing) - pos;
        double flat = std::max(1.0, std::hypot(toward.x, toward.z));
        Vec3d cruise{pos.x + toward.x / flat * 30,
                     std::min(std::max(pos.y, landing.y + static_cast<double>(kCruiseHeight)), static_cast<double>(cruiseCeiling())),
                     pos.z + toward.z / flat * 30};
        aim_ = steeringTarget(pos).value_or(cruise);
        // without a firework pushing, diving only loses height: glide, unless the target is well below
        float maxPitch = boostTicks_ == 0 && aim_->y > pos.y - 10 ? 15.f : 40.f;
        look = solvePitch(pos, vel, *aim_, clear, -40, maxPitch);
    }
    controls.setTarget({look, true});

    double speed = std::sqrt(vel.lengthSq());
    double threshold = s.elytraFireworkSpeed * (s.elytraConserveFireworks ? 0.8 : 1.0);
    bool landingSoon = state_ == State::Landing;
    // also when we must climb (the target is above) or the ground is close below
    BlockStateInterface bsi(baritone_.world());
    bool groundClose = false;
    for (int dy = 1; dy <= 4 && !groundClose; ++dy) groundClose = bsi.props(here.x, here.y - dy, here.z).collides;
    bool mustClimb = aim_ && aim_->y > pos.y + 4 && speed < threshold * 1.5;
    if (!landingSoon && boostTicks_ == 0 && fireworks > 0 && (clear || !s.elytraConserveFireworks) &&
        (speed < threshold || mustClimb || groundClose)) {
        useFirework();
    }
    return pause;
}

}  // namespace baritone
