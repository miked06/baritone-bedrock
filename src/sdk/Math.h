#pragma once
#include <cmath>
#include <cstdint>
#include <format>

namespace sdk {

struct Vec2 {
    float x = 0.f, y = 0.f;
};

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(float f) const { return {x * f, y * f, z * f}; }
    float length() const { return std::sqrt(x * x + y * y + z * z); }
};

struct BlockPos {
    int32_t x = 0, y = 0, z = 0;

    constexpr bool operator==(const BlockPos&) const = default;
    constexpr BlockPos offset(int dx, int dy, int dz) const { return {x + dx, y + dy, z + dz}; }
    constexpr BlockPos below() const { return {x, y - 1, z}; }
    constexpr BlockPos above() const { return {x, y + 1, z}; }

    static BlockPos floor(const Vec3& v) {
        return {static_cast<int32_t>(std::floor(v.x)), static_cast<int32_t>(std::floor(v.y)),
                static_cast<int32_t>(std::floor(v.z))};
    }
};

struct AABB {
    Vec3 min, max;
};

}  // namespace sdk

template <>
struct std::formatter<sdk::BlockPos> : std::formatter<std::string> {
    auto format(const sdk::BlockPos& p, auto& ctx) const {
        return std::format_to(ctx.out(), "{} {} {}", p.x, p.y, p.z);
    }
};

template <>
struct std::formatter<sdk::Vec3> : std::formatter<std::string> {
    auto format(const sdk::Vec3& v, auto& ctx) const {
        return std::format_to(ctx.out(), "{:.2f} {:.2f} {:.2f}", v.x, v.y, v.z);
    }
};
