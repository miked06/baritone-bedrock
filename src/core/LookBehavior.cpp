#include "core/LookBehavior.h"

#include <cmath>
#include <numeric>

#include "baritone/Settings.h"

using baritone::Rotation;
using baritone::Settings;

namespace {

// Java AbstractAimProcessor: the game turns by whole mouse steps. Bedrock's sensitivity
// isn't read; the default one (0.5) gives steps of 0.15 degrees.
float mouseToAngle(double mouseDelta) {
    const double f = 0.5 * 0.6 + 0.2;
    return static_cast<float>(mouseDelta * f * f * f * 8.0) * 0.15f;
}

float calculateMouseMove(float current, float target) {
    float delta = target - current;
    double deltaPx = std::round(delta / mouseToAngle(1));
    return current + mouseToAngle(deltaPx);
}

// Java nudgeToLevel: don't keep an exactly unchanged pitch far from level
float nudgeToLevel(float pitch) {
    if (pitch < -20) return pitch + 1;
    if (pitch > 10) return pitch - 1;
    return pitch;
}

}  // namespace

void LookBehavior::tick() {
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    const Settings& s = Settings::get();
    randomYawOffset_ = (unit(rng_) - 0.5) * s.randomLooking;
    randomPitchOffset_ = (unit(rng_) - 0.5) * s.randomLooking;
    double random = unit(rng_) - 0.5;
    if (std::fabs(random) < 0.1) random *= 4;
    randomYawOffset_ += random * s.randomLooking113;
}

Rotation LookBehavior::peekRotation(const Rotation& target, const Rotation& prev) const {
    float desiredYaw = target.yaw;
    float desiredPitch = target.pitch;
    if (desiredPitch == prev.pitch) desiredPitch = nudgeToLevel(desiredPitch);
    desiredYaw += static_cast<float>(randomYawOffset_);
    desiredPitch += static_cast<float>(randomPitchOffset_);
    Rotation r{calculateMouseMove(prev.yaw, desiredYaw), calculateMouseMove(prev.pitch, desiredPitch)};
    r.pitch = std::clamp(r.pitch, -90.f, 90.f);
    return r;
}

void LookBehavior::updateTarget(const Rotation& target, bool blockInteract, bool gliding, const Rotation& current) {
    const Settings& s = Settings::get();
    // Java Target.Mode.resolve
    Mode mode;
    if (gliding) mode = s.elytraFreeLook ? Mode::Server : Mode::Client;
    else if (s.freeLook) mode = blockInteract ? (s.blockFreeLook ? Mode::Server : Mode::Client) : (s.antiCheatCompatibility ? Mode::Server : Mode::None);
    else mode = Mode::Client;

    Applied out;
    out.player = peekRotation(target, current);
    // smoothing buffers of the wanted rotations (Java onPlayerUpdate POST)
    smoothYaw_.push_back(target.yaw);
    smoothPitch_.push_back(target.pitch);
    while (static_cast<int>(smoothYaw_.size()) > std::max(1, s.smoothLookTicks)) smoothYaw_.pop_front();
    while (static_cast<int>(smoothPitch_.size()) > std::max(1, s.smoothLookTicks)) smoothPitch_.pop_front();
    if (mode == Mode::Client) {
        Rotation camera = out.player;
        if (gliding ? s.elytraSmoothLook : s.smoothLook) {
            camera.yaw = std::accumulate(smoothYaw_.begin(), smoothYaw_.end(), 0.f) / smoothYaw_.size();
            if (gliding) camera.pitch = std::accumulate(smoothPitch_.begin(), smoothPitch_.end(), 0.f) / smoothPitch_.size();
        }
        out.camera = camera;
    }
    applied_ = out;
}

void LookBehavior::clear() {
    applied_.reset();
}
