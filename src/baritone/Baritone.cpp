#include "baritone/Baritone.h"

#include "baritone/Settings.h"

#include <cmath>

namespace baritone {

Baritone::Baritone(PlayerContext& player, Notify notify)
    : player_(player),
      notify_([this, notify = std::move(notify)](const std::string& message, bool error) {
          diagnostics_.record(message, error); notify(message, error);
      }),
      inventory_(player),
      pathing_(player, inventory_, notify_),
      control_(*this),
      customGoal_(*this),
      getToBlock_(*this),
      mine_(*this),
      follow_(*this),
      explore_(*this),
      farm_(*this),
      builder_(*this),
      pause_(*this),
      survival_(*this),
      animals_(*this),
      storage_(*this),
      trees_(*this),
      vehicle_(*this),
      smelt_(*this),
      craft_(*this),
      production_(*this),
      produce_(*this),
      workshop_(*this),
      loadout_(*this),
      logistics_(*this),
      construction_(*this),
      maintenance_(*this),
      undo_(*this),
      repair_(*this),
      mending_(*this),
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
    control_.registerProcess(&survival_);
    control_.registerProcess(&animals_);
    control_.registerProcess(&storage_);
    control_.registerProcess(&trees_);
    control_.registerProcess(&vehicle_);
    control_.registerProcess(&smelt_);
    control_.registerProcess(&craft_);
    control_.registerProcess(&production_);
    control_.registerProcess(&produce_);
    control_.registerProcess(&workshop_);
    control_.registerProcess(&loadout_);
    control_.registerProcess(&logistics_);
    control_.registerProcess(&construction_);
    control_.registerProcess(&maintenance_);
    control_.registerProcess(&undo_);
    control_.registerProcess(&repair_);
    control_.registerProcess(&mending_);
    control_.registerProcess(&backfill_);
    control_.registerProcess(&elytra_);
    control_.registerProcess(&inventoryPauser_);
    inventory_.setStationaryCheck([this] { return inventoryPauser_.stationaryForInventoryMove(); });
    inventory_.setPlaceAt([this](int x, int y, int z) -> std::optional<std::string> {
        if (!world_ || !builder_.isActive()) return std::nullopt;
        BlockStateInterface bsi(world_);
        return builder_.placeAt(x, y, z, bsi.props(x, y, z));
    });
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
    vehicle_.shutdown();
    production_.shutdown();
    produce_.shutdown();
}

void Baritone::cancelEverything() {
    autoRestock_ = restockWaiting_ = false;
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
    tickRestock();
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

void Baritone::autoRestock(bool enabled, std::string source) {
    if (enabled && !source.empty() && !storage_.locations().contains(source)) throw std::invalid_argument("Unknown supply location");
    autoRestock_ = enabled; restockSource_ = std::move(source);
}
void Baritone::tickRestock() {
    if (pause_.paused()) return;
    if (restockWaiting_) {
        if (logistics_.isActive()) return;
        restockWaiting_ = false;
        if (!logistics_.succeeded()) { autoRestock_ = false; logDirect("Automatic builder restocking stopped: " + logistics_.failure(), true); return; }
        if (builder_.isActive() && builder_.session() == restockSession_) builder_.resume();
    }
    if (!autoRestock_ || !builder_.isActive() || !builder_.isPaused() || builder_.missingMaterials().empty() || logistics_.isActive()) return;
    int free = 0;
    for (int slot = 0; slot < 36; ++slot) if (player_.hotbar(slot).empty()) ++free;
    if (!free) { autoRestock_ = false; logDirect("Builder restocking needs an empty inventory slot", true); return; }
    std::map<std::string, int> batch;
    for (const auto& [block, count] : builder_.missingMaterials()) {
        std::string item = block;
        if (item == "minecraft:wall_torch") item = "minecraft:torch";
        if (item == "minecraft:lit_furnace") item = "minecraft:furnace";
        if (item == "minecraft:standing_sign" || item == "minecraft:wall_sign") item = "minecraft:oak_sign";
        batch[item] = player_.countItem(item) + std::min(count, 64);
        if (--free == 0) break;
    }
    try {
        logistics_.start(std::move(batch), restockSource_); restockSession_ = builder_.session(); restockWaiting_ = true;
    } catch (const std::exception& e) { autoRestock_ = false; logDirect(e.what(), true); }
}

}  // namespace baritone
