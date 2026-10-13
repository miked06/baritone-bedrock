#pragma once
#include <string>
#include <cstdint>
#include <format>
#include <functional>

namespace baritone {

enum class Direction : uint8_t { Down, Up, North, South, West, East };

struct DirOffset {
    int x, y, z;
};

constexpr DirOffset offsetOf(Direction d) {
    switch (d) {
        case Direction::Down: return {0, -1, 0};
        case Direction::Up: return {0, 1, 0};
        case Direction::North: return {0, 0, -1};
        case Direction::South: return {0, 0, 1};
        case Direction::West: return {-1, 0, 0};
        case Direction::East: return {1, 0, 0};
    }
    return {0, 0, 0};
}

constexpr Direction opposite(Direction d) {
    switch (d) {
        case Direction::Down: return Direction::Up;
        case Direction::Up: return Direction::Down;
        case Direction::North: return Direction::South;
        case Direction::South: return Direction::North;
        case Direction::West: return Direction::East;
        case Direction::East: return Direction::West;
    }
    return d;
}

// Baritone's HORIZONTALS_BUT_ALSO_DOWN_____SO_EVERY_DIRECTION_EXCEPT_UP
inline constexpr Direction kPlaceAgainstDirs[5] = {Direction::North, Direction::South, Direction::East,
                                                   Direction::West, Direction::Down};
inline constexpr Direction kHorizontals[4] = {Direction::North, Direction::South, Direction::West, Direction::East};

// Block position (Baritone's BetterBlockPos).
struct BlockPos {
    int x = 0, y = 0, z = 0;

    constexpr bool operator==(const BlockPos&) const = default;
    constexpr auto operator<=>(const BlockPos&) const = default;
    constexpr BlockPos offset(int dx, int dy, int dz) const { return {x + dx, y + dy, z + dz}; }
    constexpr BlockPos offset(Direction d, int n = 1) const {
        DirOffset o = offsetOf(d);
        return {x + o.x * n, y + o.y * n, z + o.z * n};
    }
    constexpr BlockPos above(int n = 1) const { return {x, y + n, z}; }
    constexpr BlockPos below(int n = 1) const { return {x, y - n, z}; }
    constexpr BlockPos operator-(const BlockPos& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr BlockPos operator+(const BlockPos& o) const { return {x + o.x, y + o.y, z + o.z}; }

    constexpr int64_t distSq(const BlockPos& o) const {
        int64_t dx = x - o.x, dy = y - o.y, dz = z - o.z;
        return dx * dx + dy * dy + dz * dz;
    }

    // Same hash as BetterBlockPos.longHash so node maps behave like Baritone's.
    static constexpr int64_t longHash(int x, int y, int z) {
        uint64_t hash = 3241;
        hash = 3457689ULL * hash + static_cast<uint64_t>(static_cast<int64_t>(x));
        hash = 8734625ULL * hash + static_cast<uint64_t>(static_cast<int64_t>(y));
        hash = 2873465ULL * hash + static_cast<uint64_t>(static_cast<int64_t>(z));
        return static_cast<int64_t>(hash);
    }
    constexpr int64_t longHash() const { return longHash(x, y, z); }
};

struct Vec3d {
    double x = 0, y = 0, z = 0;

    constexpr Vec3d operator+(const Vec3d& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3d operator-(const Vec3d& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3d operator*(double f) const { return {x * f, y * f, z * f}; }
    double lengthSq() const { return x * x + y * y + z * z; }

    static constexpr Vec3d center(const BlockPos& p) { return {p.x + 0.5, p.y + 0.5, p.z + 0.5}; }
};

}  // namespace baritone

template <>
struct std::hash<baritone::BlockPos> {
    size_t operator()(const baritone::BlockPos& p) const noexcept { return static_cast<size_t>(p.longHash()); }
};

namespace baritone {
// Java SettingsUtil.maybeCensor: coordinates shown as "<censored>" with censorCoordinates,
// except while an UncensoredScope is alive (goal keys, which must stay comparable).
bool shouldCensorCoordinates();
std::string maybeCensor(int coord);
struct UncensoredScope {
    UncensoredScope();
    ~UncensoredScope();
    UncensoredScope(const UncensoredScope&) = delete;
    UncensoredScope& operator=(const UncensoredScope&) = delete;
};
}  // namespace baritone

template <>
struct std::formatter<baritone::BlockPos> : std::formatter<std::string> {
    auto format(const baritone::BlockPos& p, auto& ctx) const {
        // censorCoordinates (Java BetterBlockPos.toString / SettingsUtil.maybeCensor)
        if (baritone::shouldCensorCoordinates()) return std::format_to(ctx.out(), "<censored> <censored> <censored>");
        return std::format_to(ctx.out(), "{} {} {}", p.x, p.y, p.z);
    }
};
