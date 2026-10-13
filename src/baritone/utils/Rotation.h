#pragma once
#include <cmath>

#include "baritone/utils/BlockPos.h"

namespace baritone {

// yaw: 0 = +Z (south), 90 = -X (west). pitch: positive looks down. Same as Java and Bedrock.
struct Rotation {
    float yaw = 0, pitch = 0;

    Rotation add(const Rotation& o) const { return {yaw + o.yaw, pitch + o.pitch}; }
    Rotation subtract(const Rotation& o) const { return {yaw - o.yaw, pitch - o.pitch}; }
    Rotation withPitch(float p) const { return {yaw, p}; }
    Rotation clamp() const { return {yaw, std::fmax(-90.f, std::fmin(90.f, pitch))}; }
    Rotation normalize() const { return {normalizeYaw(yaw), pitch}; }
    Rotation normalizeAndClamp() const { return normalize().clamp(); }

    bool isReallyCloseTo(const Rotation& o) const {
        return yawIsReallyClose(o) && std::fabs(pitch - o.pitch) < 0.01f;
    }
    bool yawIsReallyClose(const Rotation& o) const {
        float a = normalizeYaw(yaw), b = normalizeYaw(o.yaw);
        return std::fabs(a - b) < 0.01f || std::fabs(a - b + 360.f) < 0.01f || std::fabs(a - b - 360.f) < 0.01f;
    }

    static float normalizeYaw(float yaw) {
        float n = std::fmod(yaw, 360.f);
        if (n < -180.f) n += 360.f;
        if (n >= 180.f) n -= 360.f;
        return n;
    }

    // Java Rotation.yawDistanceFromOffset: the signed difference, taking the short way across +-180.
    static float yawDistanceFromOffset(float yaw, float offsetYaw) {
        if (((yaw > 0) != (offsetYaw > 0)) && ((yaw > 90 || yaw < -90) != (offsetYaw > 90 || offsetYaw < -90))) {
            return yaw < 0 ? 360 + (yaw - offsetYaw) : 360 - (yaw - offsetYaw);
        }
        return yaw - offsetYaw;
    }
};

namespace rotation_utils {

// Rotation to look from orig at dest, wrapped to be close to current (Baritone's
// RotationUtils.calcRotationFromVec3d).
Rotation calcRotationFromVec3d(const Vec3d& orig, const Vec3d& dest, const Rotation& current);
Rotation calcRotationFromVec3d(const Vec3d& orig, const Vec3d& dest);
Rotation wrapAnglesToRelative(const Rotation& current, const Rotation& target);
// Unit look vector.
Vec3d calcLookDirection(const Rotation& rotation);

}  // namespace rotation_utils
}  // namespace baritone
