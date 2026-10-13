#include "baritone/Baritone.h"

#include "baritone/Settings.h"

#include <cmath>

namespace baritone {

Baritone::Baritone(PlayerContext& player, Notify notify)
    : player_(player),
      notify_(notify),
      inventory_(player),
      pathing_(player, inventory_, notify),
      control_(*this),
      customGoal_(*this),
      getToBlock_(*this),
      mine_(*this),
      follow_(*this),
      explore_(*this),
      farm_(*this),
      builder_(*this),
      pause_(*this),
      backfill_(*this),
      elytra_(*this),
      inventoryPauser_(*this) {
    // same priorities as Baritone: all default (-1), ties broken by activation order
    control_.registerProcess(&customGoal_);
    control_.registerProcess(&getToBlock_);
    control_.registerProcess(&mine_);
    control_.registerProcess(&follow_);
    control_.registerProcess(&explore_);
    control_.registerProcess(&farm_);
    control_.registerProcess(&builder_);
    control_.registerProcess(&pause_);
    control_.registerProcess(&backfill_);
    control_.registerProcess(&elytra_);
    control_.registerProcess(&inventoryPauser_);
    inventory_.setStationaryCheck([this] { return inventoryPauser_.stationaryForInventoryMove(); });
}

std::optional<PathingCommand> InventoryPauserProcess::onTick(bool, bool isSafeToCancel) {
    safeToCancelLastTick_ = isSafeToCancel;
    if (pauseRequestedLastTick_) {
        pauseRequestedLastTick_ = false;
        Vec3d v = baritone_.player().velocity();
        if (std::sqrt(v.x * v.x + v.z * v.z) < 0.00001) ticksOfStationary_++;
        return PathingCommand{nullptr, PathingCommandType::RequestPause};
    }
    ticksOfStationary_ = 0;
    return PathingCommand{nullptr, PathingCommandType::Defer};
}

namespace {
Notify g_notifier;
}

void Baritone::setNotifier(Notify notifier) { g_notifier = std::move(notifier); }

void Baritone::logNotification(const std::string& message, bool error) {
    if (Settings::get().desktopNotifications && g_notifier) g_notifier(message, error);
}

Baritone::~Baritone() {
    shutdown();
}

void Baritone::shutdown() {
    pathing_.shutdown();
    mine_.shutdown();
    getToBlock_.shutdown();
    farm_.shutdown();
    elytra_.shutdown();
}

void Baritone::cancelEverything() {
    pause_.setPaused(false);
    control_.cancelEverything();
    pathing_.cancelEverything();
    controls_ = MovementState{};
}

BlockPos Baritone::playerFeet() {
    CalculationContext* c = liveContext();
    if (!c) return {};
    return ExecContext{player_, *c, inventory_, {}}.playerFeet();
}

void Baritone::tick(std::shared_ptr<const WorldSnapshot> world) {
    world_ = world;
    controls_ = MovementState{};
    pathing_.beginTick(std::move(world));
    if (!world_ || !player_.valid()) return;
    inventory_.tick();
    control_.preTick();  // processes may set controls_ directly (e.g. mining what's in reach)
    std::function<void(CalculationContext&)> customizer;
    if (Process* p = control_.mostRecentInControl()) {
        customizer = [p](CalculationContext& c) { p->customizeContext(c); };
        if (pathing_.liveContext()) customizer(*pathing_.liveContext());
    }
    pathing_.setContextCustomizer(std::move(customizer));
    pathing_.tickPath();
    if (pathing_.producedOutput()) controls_ = pathing_.output();
    control_.postTick();
}

}  // namespace baritone
