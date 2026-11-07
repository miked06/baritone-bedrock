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
#include "baritone/process/SurvivalProcess.h"
#include "baritone/process/AnimalProcess.h"
#include "baritone/process/StorageProcess.h"
#include "baritone/process/TreeProcess.h"
#include "baritone/process/VehicleProcess.h"
#include "baritone/process/SmeltProcess.h"
#include "baritone/process/CraftProcess.h"
#include "baritone/process/ProductionProcess.h"
#include "baritone/process/SupplyProductionProcess.h"
#include "baritone/process/FurnacePoolProcess.h"
#include "baritone/process/LoadoutProcess.h"
#include "baritone/process/LogisticsProcess.h"
#include "baritone/process/ConstructionProcess.h"
#include "baritone/process/ToolMaintenanceProcess.h"
#include "baritone/process/UndoProcess.h"
#include "baritone/process/RepairProcess.h"
#include "baritone/process/MendingProcess.h"
#include "baritone/selection/SelectionManager.h"
#include "baritone/task/Diagnostics.h"

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
    SurvivalProcess& survival() { return survival_; }
    AnimalProcess& animals() { return animals_; }
    StorageProcess& storage() { return storage_; }
    TreeProcess& trees() { return trees_; }
    VehicleProcess& vehicle() { return vehicle_; }
    SmeltProcess& smelt() { return smelt_; }
    CraftProcess& craft() { return craft_; }
    ProductionProcess& production() { return production_; }
    SupplyProductionProcess& produce() { return produce_; }
    FurnacePoolProcess& workshop() { return workshop_; }
    LoadoutProcess& loadout() { return loadout_; }
    LoadoutStore& loadouts() { return loadouts_; }
    LogisticsProcess& logistics() { return logistics_; }
    ConstructionProcess& construction() { return construction_; }
    ToolMaintenanceProcess& maintenance() { return maintenance_; }
    ActionHistory& history() { return history_; }
    UndoProcess& undo() { return undo_; }
    RepairProcess& repair() { return repair_; }
    MendingProcess& mending() { return mending_; }
    void autoRestock(bool enabled, std::string source = {});
    bool autoRestockEnabled() const { return autoRestock_; }
    SelectionManager& selections() { return selections_; }
    Diagnostics& diagnostics() { return diagnostics_; }

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
    Diagnostics diagnostics_;
    Notify notify_;
    InventoryBehavior inventory_;
    LoadoutStore loadouts_;
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
    SurvivalProcess survival_;
    AnimalProcess animals_;
    StorageProcess storage_;
    TreeProcess trees_;
    VehicleProcess vehicle_;
    SmeltProcess smelt_;
    CraftProcess craft_;
    ProductionProcess production_;
    SupplyProductionProcess produce_;
    FurnacePoolProcess workshop_;
    LoadoutProcess loadout_;
    LogisticsProcess logistics_;
    ConstructionProcess construction_;
    ToolMaintenanceProcess maintenance_;
    ActionHistory history_;
    UndoProcess undo_;
    RepairProcess repair_;
    MendingProcess mending_;
    SelectionManager selections_;
    BackfillProcess backfill_;
    ElytraProcess elytra_;
    InventoryPauserProcess inventoryPauser_;
    MovementState controls_;
    std::shared_ptr<const WorldSnapshot> world_;
    bool autoRestock_ = false, restockWaiting_ = false;
    std::string restockSource_;
    uint64_t restockSession_ = 0;
    void tickRestock();
};

}  // namespace baritone
