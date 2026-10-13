#pragma once
#include <atomic>
#include <vector>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "baritone/Baritone.h"
#include "baritone/cache/Waypoints.h"
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
    const std::string& worldName() const { return worldName_; }

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
    void lookAt(float yaw, float pitch) { manualLook_ = sdk::Vec2{pitch, yaw}; }
    void cancelAll();

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
    bool walkSprint_ = false;
    std::optional<sdk::Vec2> manualLook_;
    LookBehavior look_;
};
