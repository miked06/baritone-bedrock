#pragma once
#include <memory>
#include <string>

#include "baritone/behavior/InventoryBehavior.h"
#include "baritone/behavior/PathingBehavior.h"
#include "baritone/process/BackfillProcess.h"
#include "baritone/process/BuilderProcess.h"
#include "baritone/process/CustomGoalProcess.h"
#include "baritone/process/ElytraProcess.h"
#include "baritone/process/ExploreProcess.h"
#include "baritone/process/FarmProcess.h"
#include "baritone/process/FollowProcess.h"
#include "baritone/process/GetToBlockProcess.h"
#include "baritone/process/MineProcess.h"
#include "baritone/process/PathingControlManager.h"
#include "baritone/process/PauseProcess.h"
#include "baritone/selection/SelectionManager.h"

namespace baritone {

// Root of the bot (Baritone's `Baritone` class): behaviors, processes and the controls
// (keys, clicks, rotation) to apply this tick. Game thread only.
class Baritone {
public:
    Baritone(PlayerContext& player, Notify notify);
    ~Baritone();

    // One game tick: processes decide, the current path executes, the controls are ready.
    void tick(std::shared_ptr<const WorldSnapshot> world);
    void shutdown();
    void cancelEverything();  // #stop

    // What to press / where to look this tick (paths and processes write here).
    MovementState& controls() { return controls_; }
    const MovementState& controls() const { return controls_; }

    PlayerContext& player() { return player_; }
    InventoryBehavior& inventory() { return inventory_; }
    PathingBehavior& pathing() { return pathing_; }
    PathingControlManager& control() { return control_; }
    CustomGoalProcess& customGoal() { return customGoal_; }
    GetToBlockProcess& getToBlock() { return getToBlock_; }
    MineProcess& mine() { return mine_; }
    FollowProcess& follow() { return follow_; }
    ExploreProcess& explore() { return explore_; }
    FarmProcess& farm() { return farm_; }
    BuilderProcess& builder() { return builder_; }
    ElytraProcess& elytra() { return elytra_; }
    PauseProcess& pauser() { return pause_; }
    SelectionManager& selections() { return selections_; }

    const std::shared_ptr<const WorldSnapshot>& world() const { return world_; }
    // Live context of this tick (nullptr outside a world).
    CalculationContext* liveContext() { return pathing_.liveContext(); }
    BlockPos playerFeet();
    void logDirect(const std::string& message, bool error = false) { notify_(message, error); }
    // Java Helper.logNotification: a desktop notification when desktopNotifications is on
    void logNotification(const std::string& message, bool error = false);
    // Where notifications go (the platform's: Windows notifications).
    static void setNotifier(Notify notifier);

private:
    PlayerContext& player_;
    Notify notify_;
    InventoryBehavior inventory_;
    PathingBehavior pathing_;
    PathingControlManager control_;
    CustomGoalProcess customGoal_;
    GetToBlockProcess getToBlock_;
    MineProcess mine_;
    FollowProcess follow_;
    ExploreProcess explore_;
    FarmProcess farm_;
    BuilderProcess builder_;
    PauseProcess pause_;
    SelectionManager selections_;
    BackfillProcess backfill_;
    ElytraProcess elytra_;
    InventoryPauserProcess inventoryPauser_;
    MovementState controls_;
    std::shared_ptr<const WorldSnapshot> world_;
};

}  // namespace baritone
