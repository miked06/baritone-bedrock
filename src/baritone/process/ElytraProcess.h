#pragma once
#include <future>
#include <optional>
#include <vector>

#include "baritone/elytra/ElytraPathFinder.h"
#include "baritone/Settings.h"
#include "baritone/process/Process.h"
#include "baritone/utils/Rotation.h"

namespace baritone {

// #elytra: flies to a goal with an elytra and fireworks (Baritone's ElytraProcess). Plans a
// path through the air, steers by simulating the flight for several pitches each tick,
// fires rockets when slow, then lands and walks the rest of the way.
class ElytraProcess : public Process {
public:
    using Process::Process;

    // x/z of the destination; y if known (a block goal), else the ground there.
    void start(BlockPos destination, bool knownY);
    // Java resetState: the same destination, planned again from scratch
    void resetState();
    // Java pathTo(Goal): a GoalXZ (the height is picked while flying) or a GoalBlock. Returns
    // the error, if any. Keeps flying when already in the air.
    std::optional<std::string> pathTo(const GoalPtr& goal);
    std::optional<BlockPos> currentDestination() const { return destination_; }
    // Java isSupportedPos: inside the height the flight may use (nether: under the roof
    // unless elytraAllowAboveRoof)
    bool isSupportedPos(const BlockPos& pos) const;
    bool isActive() override { return destination_.has_value(); }
    std::optional<PathingCommand> onTick(bool calcFailed, bool isSafeToCancel) override;
    void onLostControl() override;
    double priority() const override { return 0; }  // above walking processes
    std::string displayName() const override;
    void shutdown();

    // Rendering
    std::vector<BlockPos> path() const { return path_; }
    std::optional<Vec3d> aim() const { return destination_ ? aim_ : std::nullopt; }
    std::optional<BlockPos> landing() const { return destination_ ? landing_ : std::nullopt; }
    // elytraRenderSimulation: the simulated flight of the chosen pitch
    std::vector<Vec3d> simulation() const { return destination_ && Settings::get().elytraRenderSimulation ? simulation_ : std::vector<Vec3d>{}; }

private:
    enum class State { Takeoff, Flying, Landing };
    void requestPath(const BlockPos& from);
    void pollPath();
    BlockPos landingPoint();
    std::optional<BlockPos> findSafeLandingSpot(const Vec3d& around, int fromY);
    int cruiseCeiling() const;
    bool aboveBuildLimit(double tripDistance) const;
    std::optional<Vec3d> steeringTarget(const Vec3d& pos);
    Rotation solvePitch(const Vec3d& pos, const Vec3d& vel, const Vec3d& target, bool& collisionFree, float minPitch,
                        float maxPitch);
    Rotation landingLook(const Vec3d& pos, const Vec3d& vel, const BlockPos& landing, bool& collisionFree);
    bool useFirework();
    bool shouldLandForSafety();
    void trySwapElytra();
    int fireworkCount();
    void finish(const char* message, bool error);

    std::optional<BlockPos> destination_;
    bool knownY_ = false;
    State state_ = State::Takeoff;
    std::vector<BlockPos> path_;
    std::future<elytra::FlightPathResult> calc_;
    std::shared_ptr<elytra::ElytraPathFinder> finder_;
    int ticks_ = 0;
    int lastPathTick_ = -1000;
    int boostTicks_ = 0;       // a firework is (probably) still pushing
    int takeoffTicks_ = 0;
    int lastJumpPress_ = -100;
    int lastFireworkHint_ = -1000;
    int clickedAt_ = -1;     // tick of the last automatic firework click, until its boost shows
    int failedClicks_ = 0;   // clicks in a row without a boost
    double lastSpeed_ = 0;  // a sudden speed gain = a firework the player fired
    std::optional<Vec3d> aim_;        // where the solver steered this tick
    std::optional<BlockPos> landing_;  // single presses at least 12 ticks apart
    bool toldToJump_ = false;
    int setbackTicks_ = 0;
    std::optional<Vec3d> lastPos_;
    std::vector<Vec3d> simulation_;
    int swapAt_ = -1000;
    // settings the path was planned with (Java: changing one plans again)
    bool allowTight_ = false, allowAboveBuildLimit_ = false, allowAboveRoof_ = false;          // tick of the last elytraAutoSwap click
    bool warnedNoEmergency_ = false;
    bool emergency_ = false;  // landing for safety, once
    int flyStartTick_ = 0;     // tick gliding started
    int failedTakeoffs_ = 0;   // back on the ground right after taking off
};

}  // namespace baritone
