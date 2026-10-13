#include "baritone/utils/Rotation.h"

#include <numbers>

namespace baritone::rotation_utils {

namespace {
constexpr double kRadToDeg = 180.0 / std::numbers::pi;
constexpr double kDegToRad = std::numbers::pi / 180.0;
}  // namespace

Rotation calcRotationFromVec3d(const Vec3d& orig, const Vec3d& dest) {
    double dx = orig.x - dest.x, dy = orig.y - dest.y, dz = orig.z - dest.z;
    double yaw = std::atan2(dx, -dz);
    double dist = std::sqrt(dx * dx + dz * dz);
    double pitch = std::atan2(dy, dist);
    return {static_cast<float>(yaw * kRadToDeg), static_cast<float>(pitch * kRadToDeg)};
}

Rotation wrapAnglesToRelative(const Rotation& current, const Rotation& target) {
    if (current.yawIsReallyClose(target)) return {current.yaw, target.pitch};
    return target.subtract(current).normalize().add(current);
}

Rotation calcRotationFromVec3d(const Vec3d& orig, const Vec3d& dest, const Rotation& current) {
    return wrapAnglesToRelative(current, calcRotationFromVec3d(orig, dest));
}

Vec3d calcLookDirection(const Rotation& r) {
    double f = std::cos(-r.yaw * kDegToRad - std::numbers::pi);
    double f1 = std::sin(-r.yaw * kDegToRad - std::numbers::pi);
    double f2 = -std::cos(-r.pitch * kDegToRad);
    double f3 = std::sin(-r.pitch * kDegToRad);
    return {f1 * f2, f3, f * f2};
}

}  // namespace baritone::rotation_utils
