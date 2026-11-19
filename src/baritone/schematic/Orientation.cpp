#include "baritone/schematic/Orientation.h"

#include <cmath>
#include <string>

namespace baritone {

namespace {

constexpr std::string_view kAxis = "pillar_axis";
constexpr std::string_view kVerticalHalf = "minecraft:vertical_half";
constexpr std::string_view kUpsideDown = "upside_down_bit";
constexpr std::string_view kWeirdo = "weirdo_direction";
constexpr std::string_view kCardinal = "minecraft:cardinal_direction";
constexpr std::string_view kTorch = "torch_facing_direction";

std::optional<bool> bit(std::string_view value) {
    if (value == "1" || value == "true") return true;
    if (value == "0" || value == "false") return false;
    return std::nullopt;
}

std::string_view axisOf(Direction d) {
    switch (d) {
        case Direction::Down:
        case Direction::Up: return "y";
        case Direction::North:
        case Direction::South: return "z";
        default: return "x";
    }
}

std::string_view cardinalName(Direction d) {
    switch (d) {
        case Direction::North: return "north";
        case Direction::South: return "south";
        case Direction::West: return "west";
        case Direction::East: return "east";
        default: return "";
    }
}

std::optional<Direction> cardinalFromName(std::string_view s) {
    if (s == "north") return Direction::North;
    if (s == "south") return Direction::South;
    if (s == "west") return Direction::West;
    if (s == "east") return Direction::East;
    return std::nullopt;
}

// Stairs: 0 east, 1 west, 2 south, 3 north (the direction the player faced)
std::string_view weirdoOf(Direction facing) {
    switch (facing) {
        case Direction::East: return "0";
        case Direction::West: return "1";
        case Direction::South: return "2";
        default: return "3";
    }
}

std::optional<Direction> facingFromWeirdo(std::string_view v) {
    if (v == "0") return Direction::East;
    if (v == "1") return Direction::West;
    if (v == "2") return Direction::South;
    if (v == "3") return Direction::North;
    return std::nullopt;
}

}  // namespace

bool isOrientationState(std::string_view key) {
    return key == kAxis || key == kVerticalHalf || key == kUpsideDown || key == kWeirdo || key == kCardinal || key == kTorch;
}

bool isTallPlant(std::string_view name) {
    return name == "minecraft:sunflower" || name == "minecraft:lilac" || name == "minecraft:rose_bush" ||
           name == "minecraft:peony" || name == "minecraft:tall_grass" || name == "minecraft:large_fern";
}

std::optional<bool> tallPlantUpper(const DesiredBlock& desired) {
    if (!isTallPlant(desired.name)) return std::nullopt;
    for (const auto& [key, value] : desired.states)
        if (key == "upper_block_bit") return bit(value);
    return std::nullopt;
}

bool isSecondaryPlacement(const DesiredBlock& desired) {
    for (const auto& [key, value] : desired.states) {
        if (key == "head_piece_bit" && desired.name == "minecraft:bed") return bit(value).value_or(false);
        if (key == "upper_block_bit" && (isTallPlant(desired.name) || desired.name.ends_with("_door")))
            return bit(value).value_or(false);
    }
    return false;
}

bool tallPlantHalfMatches(const DesiredBlock& desired, const BlockProps& current) {
    const auto upper = tallPlantUpper(desired);
    return !upper || upper == bit(current.state("upper_block_bit"));
}

bool blockMatches(const DesiredBlock& desired, const BlockProps& current, bool ignoreDirection) {
    if (desired.kind != DesiredBlock::Kind::Block || desired.name != current.name) return false;
    for (const auto& [key, value] : desired.states) {
        if ((key == "upper_block_bit" && (isTallPlant(desired.name) || desired.name.ends_with("_door"))) ||
            (key == "head_piece_bit" && desired.name == "minecraft:bed")) {
            if (bit(value) != bit(current.state(key))) return false;
        }
    }
    if (ignoreDirection) return true;
    for (const auto& [key, value] : desired.states) {
        if (key == "direction" && desired.name == "minecraft:bed") {
            if (value != current.state(key)) return false;
            continue;
        }
        if (!isOrientationState(key)) continue;
        std::string_view have = current.state(key);
        if (!have.empty() && have != value) return false;
    }
    return true;
}

bool placementGivesOrientation(const DesiredBlock& desired, const Placement& p) {
    for (const auto& [key, value] : desired.states) {
        if (key == kAxis) {
            if (value != axisOf(p.against)) return false;
        } else if (key == kVerticalHalf) {
            if (value != (p.topHalf ? "top" : "bottom")) return false;
        } else if (key == kUpsideDown) {
            if ((value == "1") != p.topHalf) return false;
        } else if (key == kWeirdo) {
            if (value != weirdoOf(p.playerFacing)) return false;
        } else if (key == kCardinal) {
            if (value != cardinalName(opposite(p.playerFacing))) return false;  // faces the player
        } else if (key == kTorch) {
            if (value != (p.against == Direction::Down ? "top" : cardinalName(p.against))) return false;
        }
    }
    return true;
}

std::optional<Direction> requiredFacing(const DesiredBlock& desired) {
    for (const auto& [key, value] : desired.states) {
        if (key == kWeirdo) return facingFromWeirdo(value);
        if (key == kCardinal) {
            if (auto d = cardinalFromName(value)) return opposite(*d);
        }
    }
    return std::nullopt;
}

Direction horizontalFacing(float yaw) {
    float y = std::fmod(yaw, 360.f);
    if (y < 0) y += 360.f;
    if (y >= 315.f || y < 45.f) return Direction::South;
    if (y < 135.f) return Direction::West;
    if (y < 225.f) return Direction::North;
    return Direction::East;
}

}  // namespace baritone
