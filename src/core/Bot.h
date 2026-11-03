#pragma once
#include <atomic>
#include <vector>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "baritone/Baritone.h"
#include "baritone/cache/Waypoints.h"
#include "baritone/selection/ClickSelection.h"
#include "baritone/schematic/SchematicWorkspace.h"
#include "baritone/schematic/SchematicCatalog.h"
#include "baritone/schematic/QuarryPlan.h"
#include "baritone/schematic/MiningPlan.h"
#include "baritone/schematic/SchematicLoadJob.h"
#include "baritone/schematic/SchematicSurvey.h"
#include "baritone/task/TaskQueue.h"
#include "baritone/task/Workflow.h"
#include "baritone/task/CheckpointWriter.h"
#include "baritone/world/PortalGraph.h"
#include "baritone/world/PortalJourney.h"
#include "command/CommandManager.h"
#include "core/InputOverride.h"
#include "core/LookBehavior.h"
#include "platform/GamePlayerContext.h"
#include "sdk/Math.h"

namespace sdk {
struct MoveInputComponent;
class LocalPlayer;
}

// Root object of the bot (Baritone's `Baritone` class). Everything that runs on the game
// thread is driven from here.
class Bot {
public:
    static Bot& get();

    // Called from the loader thread.
    bool start();
    void stop();

    // Game thread, from the hooks.
    void onFrame();            // every frame, also in menus and while paused
    void onTick(void* level);  // every client tick (20/s) while in a world and not paused
    void beforeInputTick(sdk::MoveInputComponent& input);
    void afterInputTick(sdk::MoveInputComponent& input);
    bool onChatSend(const std::string& message);  // true when the message was a bot command

    uint64_t ticks() const { return ticks_; }
    void* level() const { return level_; }  // the client Level, from the tick hook

    InputOverride& input() { return input_; }
    command::CommandManager& commands() { return commands_; }
    baritone::Baritone& baritone() { return *baritone_; }
    baritone::PathingBehavior& pathing() { return baritone_->pathing(); }
    baritone::InventoryBehavior& inventory() { return baritone_->inventory(); }
    platform::GamePlayerContext& player() { return player_; }
    baritone::WaypointStore& waypoints() { return waypoints_; }
    baritone::SchematicWorkspace& schematics() { return schematics_; }
    baritone::SchematicCatalog& placements() { return placements_; }
    void selectPlacement(const std::string& key);
    void startQuarry(baritone::Selection area, bool lighting, bool excavate);
    void startMiningPlan(baritone::Selection area, std::string method, std::string objective, bool execute);
    const baritone::QuarryPreflight& quarry() const { return quarry_; }
    void setQuarryStorage(std::string name) { quarryStorage_ = std::move(name); }
    bool schematicLoading() const { return schematicLoad_.running(); }
    void loadSchematicAsync(std::string file, baritone::BlockPos origin, bool preview);
    void cancelSchematicLoad() { schematicLoad_.cancel(); }
    void runDiagnosticAsync(std::function<void()> task);
    void startSchematicSurvey(bool materials);
    const baritone::SchematicSurvey& schematicSurvey() const { return survey_; }
    const std::string& worldName() const { return worldName_; }
    std::filesystem::path worldDataPath(const std::string& category, bool perDimension = true) const;
    baritone::PortalGraph& portals() { return portals_; }
    void startPortalJourney(const std::string& destination);
    void applyProtection(std::shared_ptr<const baritone::ProtectionPolicy> policy);
    baritone::TaskQueue& tasks() { return tasks_; }
    baritone::Workflow& workflow() { return workflow_; }
    void saveWorkflow();
    void resumeWorkflow();
    bool workflowHealthy() const { return workflowHealthy_; }
    void replaceWorkflow(baritone::Workflow flow) { workflow_ = std::move(flow); workflowHealthy_ = true; saveWorkflow(); }
    baritone::WorldIdentity identity() const;
    void saveTasks();
    void resumeTasks();
    void prepareTasks();
    baritone::RecipeBook& recipeBook() { return recipeBook_; }

    // #sel pos1 waiting for its pos2 (the selections themselves are in baritone().selections()).
    std::optional<baritone::BlockPos>& selectionPos1() { return selectionPos1_; }
    void saveSettings();
    int loadSettings();

    void requestUnload() { unloadRequested_ = true; }
    bool unloadRequested() const { return unloadRequested_; }

    // Rotations (pitch, yaw) to apply this frame, if the bot controls them: the player's
    // (what the server sees) and, in LookBehavior's CLIENT mode, the camera's.
    struct LookTarget {
        sdk::Vec2 player;
        std::optional<sdk::Vec2> camera;
    };
    std::optional<LookTarget> rotationTarget() const {
        if (const auto& a = look_.applied()) {
            LookTarget t{sdk::Vec2{a->player.pitch, a->player.yaw}, std::nullopt};
            if (a->camera) t.camera = sdk::Vec2{a->camera->pitch, a->camera->yaw};
            return t;
        }
        if (manualLook_) return LookTarget{*manualLook_, std::nullopt};
        return std::nullopt;
    }

    // Debug helpers
    void walkFor(int ticks, bool sprint);
    void jumpFor(int ticks) { cancelAll(); jumpTicks_ = ticks; }
    void lookAt(float yaw, float pitch) { manualLook_ = sdk::Vec2{pitch, yaw}; }
    void cancelAll();
    void startClickSelection();
    void stopClickSelection();

    struct TestAction {
        enum Kind { Break, Place } kind;
        baritone::BlockPos pos;
        int ticks = 0;
    };
    void startTestAction(TestAction::Kind kind, const baritone::BlockPos& pos);

    // Runs `fn` at the start of the next client tick. Commands run during a frame, where the
    // game's item registry is not reachable: anything touching item stacks must wait for the tick.
    void runInTick(std::function<void()> fn) { tickTasks_.push_back(std::move(fn)); }

private:
    void applyOutput(const baritone::MovementState& out, bool active);
    void onDeath(sdk::LocalPlayer& player);
    std::vector<std::pair<baritone::BlockPos, baritone::BlockPos>> selectionBoxes() const;
    bool wasDead_ = false;
    bool wasSleeping_ = false;
    void onSleep(sdk::LocalPlayer& player);
    // The game ignores movement keys while its cursor is not grabbed (window in the
    // background). While the bot drives, pretend it is grabbed.
    void updateFocusOverride(bool botControlling);
    std::optional<bool> savedCursorGrabbed_;
    void runTestAction();
    void releaseControl();
    void updateClickSelection();
    baritone::ClickSelection clickSelection_;
    baritone::SchematicWorkspace schematics_;
    baritone::SchematicCatalog placements_;
    std::filesystem::path placementsFile_;
    baritone::PortalGraph portals_;
    baritone::PortalJourney portalJourney_;
    void tickPortalJourney();
    std::filesystem::path portalsFile_;
    baritone::QuarryPreflight quarry_;
    baritone::SchematicSurvey quarryVerification_;
    bool quarryChecking_ = false;
    bool quarryActive_ = false, quarryExcavate_ = false, quarryLayerStarted_ = false;
    bool quarryUnloading_ = false;
    int quarryLayer_ = 0;
    uint64_t quarryDiagnosticStart_ = 0;
    std::string quarryStorage_;
    void tickQuarry();
    void tickMiningPlan();
    void tickHistory();
    void recordIntent(baritone::BlockPos position);
    std::shared_ptr<baritone::MiningPlan> miningPlan_;
    bool miningActive_ = false, miningExecute_ = false, miningStarted_ = false, miningChecking_ = false;
    uint64_t miningDiagnostic_ = 0;
    baritone::SchematicSurvey miningVerification_;
    std::optional<baritone::PlacementPreset> loadingPlacement_;
    std::string loadingSchematicFile_;
    baritone::SchematicLoadJob schematicLoad_;
    baritone::SchematicSurvey survey_;
    bool surveyMaterials_ = false, surveyReport_ = false;
    std::string protectionKey_;
    baritone::TaskQueue tasks_;
    baritone::Workflow workflow_;
    std::filesystem::path workflowFile_;
    bool workflowHealthy_ = true;
    void tickWorkflow();
    baritone::RecipeBook recipeBook_ = baritone::RecipeBook::vanillaBasics();
    baritone::CheckpointWriter taskWriter_;
    std::filesystem::path taskCheckpoint_;
    bool dispatchingTask_ = false;
    uint64_t taskDiagnosticStart_ = 0;
    baritone::GoalPtr taskGoal_;
    std::string lastLoadFailure_;
    void tickTasks();
    baritone::TaskOutcome startTask(const baritone::Task& task);
    baritone::TaskOutcome pollTask(const baritone::Task& task);
    std::mutex diagnosticMutex_;
    std::future<void> diagnosticJob_;
    bool diagnosticsStopped_ = false;
    std::string loadingSchematicName_;
    baritone::BlockPos loadingSchematicOrigin_;
    bool loadingSchematicPreview_ = true;
    uint64_t worldGeneration_ = 0, loadingSchematicGeneration_ = 0;
    void pollSchematicLoad();
    std::vector<baritone::SchematicWorkspace::PreviewCell> schematicPreview_;
    uint64_t schematicPreviewRevision_ = 0;
    std::optional<int> schematicDimension_;
    std::optional<baritone::BlockPos> clickTarget_;
    uint64_t clickSelectionStartedAt_ = 0;
    bool clickCaptureStarted_ = false;
    std::optional<TestAction> testAction_;
    std::vector<std::function<void()>> tickTasks_;

    InputOverride input_;
    command::CommandManager commands_;
    std::atomic<bool> unloadRequested_{false};

    platform::GamePlayerContext player_;
    std::unique_ptr<baritone::Baritone> baritone_;
    baritone::WaypointStore waypoints_;
    std::string worldName_;
    std::optional<baritone::BlockPos> selectionPos1_;
    bool controlling_ = false;
    int rightClickTimer_ = 0;
    int breakDelay_ = 0;  // blockBreakSpeed
    int flightStopTicks_ = 0;

    void* lastPlayer_ = nullptr;
    void* level_ = nullptr;
    uint64_t ticks_ = 0;

    int walkTicks_ = 0;
    int jumpTicks_ = 0;
    bool walkSprint_ = false;
    std::optional<sdk::Vec2> manualLook_;
    LookBehavior look_;
};
