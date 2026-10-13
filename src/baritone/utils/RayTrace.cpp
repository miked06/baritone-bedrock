#include "baritone/utils/RayTrace.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace baritone {

std::optional<LocalBox> outlineBox(const BlockProps& p) {
    if (p.type == BlockType::Air) return std::nullopt;
    if (p.hasFluid() && !p.collides) {
        // pure liquid: not targetable. Waterlogged plants (seagrass, kelp) still are.
        if (p.type == BlockType::Water || p.type == BlockType::Lava || p.type == BlockType::BubbleColumn) {
            return std::nullopt;
        }
    }
    if (!p.collides) return LocalBox{0.2, 0.0, 0.2, 0.8, 0.8, 0.8};  // plants, torches...
    if (p.topSlab) return LocalBox{0, 0.5, 0, 1, 1, 1};
    double h = std::clamp(static_cast<double>(p.height), 0.0625, 1.0);
    return LocalBox{0, 0, 0, 1, h, 1};
}

namespace {

// Slab test of a ray against a box. Returns the entry distance and the entered face.
std::optional<std::pair<double, Direction>> intersect(const Vec3d& o, const Vec3d& d, const LocalBox& b, double maxT) {
    double tmin = 0, tmax = maxT;
    Direction face = Direction::Up;
    const double origin[3] = {o.x, o.y, o.z};
    const double dir[3] = {d.x, d.y, d.z};
    const double lo[3] = {b.minX, b.minY, b.minZ};
    const double hi[3] = {b.maxX, b.maxY, b.maxZ};
    static constexpr Direction negFace[3] = {Direction::West, Direction::Down, Direction::North};
    static constexpr Direction posFace[3] = {Direction::East, Direction::Up, Direction::South};
    for (int axis = 0; axis < 3; ++axis) {
        if (std::fabs(dir[axis]) < 1e-12) {
            if (origin[axis] < lo[axis] || origin[axis] > hi[axis]) return std::nullopt;
            continue;
        }
        double inv = 1.0 / dir[axis];
        double t1 = (lo[axis] - origin[axis]) * inv;
        double t2 = (hi[axis] - origin[axis]) * inv;
        Direction enter = negFace[axis];
        if (t1 > t2) {
            std::swap(t1, t2);
            enter = posFace[axis];
        }
        if (t1 > tmin) {
            tmin = t1;
            face = enter;
        }
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return std::nullopt;
    }
    return std::make_pair(tmin, face);
}

}  // namespace

std::optional<BlockHit> rayTrace(BlockStateInterface& bsi, const Vec3d& start, const Rotation& rotation, double reach) {
    Vec3d dir = rotation_utils::calcLookDirection(rotation);
    int x = static_cast<int>(std::floor(start.x));
    int y = static_cast<int>(std::floor(start.y));
    int z = static_cast<int>(std::floor(start.z));
    int stepX = dir.x > 0 ? 1 : -1, stepY = dir.y > 0 ? 1 : -1, stepZ = dir.z > 0 ? 1 : -1;
    auto boundary = [](double s, double d, int cell) {
        if (std::fabs(d) < 1e-12) return std::numeric_limits<double>::infinity();
        double next = d > 0 ? cell + 1 : cell;
        return (next - s) / d;
    };
    double tMaxX = boundary(start.x, dir.x, x), tMaxY = boundary(start.y, dir.y, y), tMaxZ = boundary(start.z, dir.z, z);
    double tDeltaX = std::fabs(dir.x) < 1e-12 ? std::numeric_limits<double>::infinity() : 1 / std::fabs(dir.x);
    double tDeltaY = std::fabs(dir.y) < 1e-12 ? std::numeric_limits<double>::infinity() : 1 / std::fabs(dir.y);
    double tDeltaZ = std::fabs(dir.z) < 1e-12 ? std::numeric_limits<double>::infinity() : 1 / std::fabs(dir.z);

    double t = 0;
    while (t <= reach) {
        const BlockProps& p = bsi.props(x, y, z);
        if (auto box = outlineBox(p)) {
            Vec3d local{start.x - x, start.y - y, start.z - z};
            if (auto hit = intersect(local, dir, *box, reach)) {
                Vec3d point = start + dir * hit->first;
                return BlockHit{{x, y, z}, hit->second, point};
            }
        }
        if (tMaxX < tMaxY && tMaxX < tMaxZ) {
            t = tMaxX;
            x += stepX;
            tMaxX += tDeltaX;
        } else if (tMaxY < tMaxZ) {
            t = tMaxY;
            y += stepY;
            tMaxY += tDeltaY;
        } else {
            t = tMaxZ;
            z += stepZ;
            tMaxZ += tDeltaZ;
        }
    }
    return std::nullopt;
}

Vec3d blockCenter(BlockStateInterface& bsi, const BlockPos& pos) {
    const BlockProps& p = bsi.props(pos.x, pos.y, pos.z);
    if (!p.collides || p.type == BlockType::Air) return Vec3d::center(pos);
    auto box = outlineBox(p);
    double yCenter = box ? (box->minY + box->maxY) / 2 : 0.5;
    if (p.type == BlockType::Fire) yCenter = 0;  // look at the bottom of fire to put it out
    return {pos.x + 0.5, pos.y + yCenter, pos.z + 0.5};
}

}  // namespace baritone
