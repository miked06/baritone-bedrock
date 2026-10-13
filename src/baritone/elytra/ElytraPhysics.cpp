#include "baritone/elytra/ElytraPhysics.h"

#include <algorithm>
#include <cmath>

namespace baritone::elytra {

namespace {

constexpr double kGravity = 0.08;
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

bool solidAt(BlockStateInterface& bsi, int x, int y, int z) {
    const BlockProps& p = bsi.props(x, y, z);
    return p.collides || p.fluid == Fluid::Lava;  // lava is never flown through
}

}  // namespace

FlightState step(const FlightState& s, const Rotation& look, bool boosted) {
    Vec3d dir = rotation_utils::calcLookDirection(look);
    Vec3d v = s.vel;
    if (boosted) {  // FireworkRocketEntity attached to a gliding player
        v.x += dir.x * 0.1 + (dir.x * 1.5 - v.x) * 0.5;
        v.y += dir.y * 0.1 + (dir.y * 1.5 - v.y) * 0.5;
        v.z += dir.z * 0.1 + (dir.z * 1.5 - v.z) * 0.5;
    }
    double pitchRad = look.pitch * kDegToRad;
    double horizLook = std::sqrt(dir.x * dir.x + dir.z * dir.z);
    double horizSpeed = std::sqrt(v.x * v.x + v.z * v.z);
    double lookLen = std::sqrt(dir.lengthSq());
    double cosPitch = std::cos(pitchRad);
    cosPitch = cosPitch * cosPitch * std::min(1.0, lookLen / 0.4);
    v.y += kGravity * (-1.0 + cosPitch * 0.75);
    if (v.y < 0 && horizLook > 0) {
        double k = v.y * -0.1 * cosPitch;
        v.x += dir.x * k / horizLook;
        v.y += k;
        v.z += dir.z * k / horizLook;
    }
    if (pitchRad < 0 && horizLook > 0) {  // looking up converts speed into height
        double k = horizSpeed * -std::sin(pitchRad) * 0.04;
        v.x -= dir.x * k / horizLook;
        v.y += k * 3.2;
        v.z -= dir.z * k / horizLook;
    }
    if (horizLook > 0) {
        v.x += (dir.x / horizLook * horizSpeed - v.x) * 0.1;
        v.z += (dir.z / horizLook * horizSpeed - v.z) * 0.1;
    }
    v.x *= 0.99;
    v.y *= 0.98;
    v.z *= 0.99;
    return {s.pos + v, v};
}

bool collides(BlockStateInterface& bsi, const Vec3d& pos) {
    double h = kGlideWidth / 2;
    int x0 = static_cast<int>(std::floor(pos.x - h)), x1 = static_cast<int>(std::floor(pos.x + h));
    int y0 = static_cast<int>(std::floor(pos.y)), y1 = static_cast<int>(std::floor(pos.y + kGlideHeight));
    int z0 = static_cast<int>(std::floor(pos.z - h)), z1 = static_cast<int>(std::floor(pos.z + h));
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z)
                if (solidAt(bsi, x, y, z)) return true;
    return false;
}

std::optional<std::vector<Vec3d>> simulate(BlockStateInterface& bsi, FlightState s, const Rotation& look, int ticks,
                                           int boostTicks) {
    std::vector<Vec3d> out;
    out.reserve(ticks);
    for (int t = 0; t < ticks; ++t) {
        FlightState next = step(s, look, t < boostTicks);
        // check the swept box in small steps: fast flight crosses blocks in one tick
        Vec3d delta = next.pos - s.pos;
        int parts = std::max(1, static_cast<int>(std::ceil(std::sqrt(delta.lengthSq()) / 0.5)));
        for (int i = 1; i <= parts; ++i) {
            if (collides(bsi, s.pos + delta * (static_cast<double>(i) / parts))) return std::nullopt;
        }
        s = next;
        out.push_back(s.pos);
    }
    return out;
}

bool clearLine(BlockStateInterface& bsi, const Vec3d& a, const Vec3d& b, double margin) {
    Vec3d d = b - a;
    double len = std::sqrt(d.lengthSq());
    int steps = std::max(1, static_cast<int>(std::ceil(len / 0.5)));
    for (int i = 0; i <= steps; ++i) {
        Vec3d p = a + d * (static_cast<double>(i) / steps);
        for (double ox : {-margin, 0.0, margin})
            for (double oz : {-margin, 0.0, margin})
                for (double oy : {-margin, 0.0, margin})
                    if (collides(bsi, {p.x + ox, p.y + oy, p.z + oz})) return false;
    }
    return true;
}

}  // namespace baritone::elytra
