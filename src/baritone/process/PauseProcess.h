#pragma once
#include "baritone/process/Process.h"

namespace baritone {

// #pause / #resume: a temporary process above everything else that keeps the bot still
// without forgetting what it was doing (Baritone's pause process in ExampleBaritoneControl).
class PauseProcess : public Process {
public:
    using Process::Process;

    void setPaused(bool paused) { paused_ = paused; }
    bool paused() const { return paused_; }

    bool isActive() override { return paused_; }
    std::optional<PathingCommand> onTick(bool, bool) override {
        return PathingCommand{nullptr, PathingCommandType::RequestPause};
    }
    bool isTemporary() const override { return true; }
    void onLostControl() override {}
    double priority() const override { return 0; }  // Java: DEFAULT_PRIORITY + 1
    std::string displayName() const override { return "En pause"; }

private:
    bool paused_ = false;
};

// Java InventoryPauserProcess: with inventoryMoveOnlyIfStationary, an inventory move asks
// for a pause and happens once the player has been still for more than a tick.
class InventoryPauserProcess : public Process {
public:
    using Process::Process;

    // Called by InventoryBehavior before a move: true when it may happen now.
    bool stationaryForInventoryMove() {
        pauseRequestedLastTick_ = true;
        return safeToCancelLastTick_ && ticksOfStationary_ > 1;
    }

    bool isActive() override { return true; }
    std::optional<PathingCommand> onTick(bool, bool isSafeToCancel) override;
    bool isTemporary() const override { return true; }
    void onLostControl() override {}
    double priority() const override { return 5.1; }  // slightly higher than backfill
    std::string displayName() const override { return "Pause inventaire"; }

private:
    bool pauseRequestedLastTick_ = false;
    bool safeToCancelLastTick_ = false;
    int ticksOfStationary_ = 0;
};

}  // namespace baritone

