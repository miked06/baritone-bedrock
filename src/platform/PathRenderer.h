#pragma once
#include <mutex>
#include <optional>
#include <vector>

#include "baritone/pathing/goals/Goal.h"

namespace sdk {
class LevelRenderer;
class ScreenContext;
}  // namespace sdk

namespace platform {

// Draws the paths, goals and the block being broken in the world (Baritone's
// PathRenderer). update() on the game thread, render() from the render hook.
class PathRenderer {
public:
    static PathRenderer& get();

    struct Frame {
        std::vector<baritone::BlockPos> path, next, calculating;
        std::size_t pathPosition = 0;                        // drawn from 3 movements before it
        std::vector<baritone::BlockPos> mostRecent;           // colorMostRecentConsidered
        std::vector<baritone::BlockPos> toBreak, toPlace, toWalkInto;
        std::optional<baritone::Vec3d> player;                // GoalYLevel box center
        baritone::GoalPtr goal;
        std::optional<baritone::BlockPos> breaking;
        std::optional<baritone::BlockPos> selPos1;                                  // #sel pos1 waiting for pos2
        std::vector<std::pair<baritone::BlockPos, baritone::BlockPos>> selections;  // pos1, pos2
        std::optional<std::pair<baritone::BlockPos, baritone::BlockPos>> build;  // schematic being built
        std::vector<baritone::BlockPos> elytraPath;
        std::optional<baritone::Vec3d> elytraAim;         // point the elytra steers to
        std::optional<baritone::BlockPos> elytraLanding;
        std::vector<baritone::Vec3d> elytraSimulation;    // elytraRenderSimulation
    };

    void update(Frame frame);
    void clear() { update({}); }
    void render(sdk::LevelRenderer* renderer, sdk::ScreenContext* screen);

private:
    std::mutex mutex_;
    Frame frame_;
};

}  // namespace platform
