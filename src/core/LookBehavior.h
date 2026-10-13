#pragma once
#include <deque>
#include <optional>
#include <random>

#include "baritone/utils/Rotation.h"

// Baritone's LookBehavior: where the player (server side) and the camera (client side) look
// while the bot controls the rotation.
//
// Java modes, from freeLook / blockFreeLook / elytraFreeLook / antiCheatCompatibility:
//  - CLIENT: the player and the camera both turn;
//  - SERVER: only the player turns (what the server sees), the camera stays where you put it;
//  - NONE: nothing turns, only the movement keys follow the target. Bedrock moves the player
//    along its own rotation, so NONE is done like SERVER (the camera still stays free).
// The aim is nudged like Java's AimProcessor (randomLooking, randomLooking113, mouse steps)
// and the camera can be smoothed (smoothLook, elytraSmoothLook).
class LookBehavior {
public:
    enum class Mode { Client, Server, None };

    // One game tick (Java TickEvent IN): new random offsets.
    void tick();
    // The rotation a movement or process wants this tick (Java updateTarget).
    void updateTarget(const baritone::Rotation& target, bool blockInteract, bool gliding, const baritone::Rotation& current);
    void clear();

    // What to apply each frame: the player's rotation and, in CLIENT mode, the camera's.
    struct Applied {
        baritone::Rotation player;
        std::optional<baritone::Rotation> camera;
    };
    const std::optional<Applied>& applied() const { return applied_; }

private:
    baritone::Rotation peekRotation(const baritone::Rotation& target, const baritone::Rotation& prev) const;

    std::optional<Applied> applied_;
    std::deque<float> smoothYaw_, smoothPitch_;
    std::mt19937_64 rng_{std::random_device{}()};
    double randomYawOffset_ = 0, randomPitchOffset_ = 0;
};
