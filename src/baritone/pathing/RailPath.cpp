#include "baritone/pathing/RailPath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

namespace baritone {
namespace {

struct End { Direction direction; int height = 0; };
using Shape = std::array<End, 2>;
using enum Direction;

// rail_direction: straight, ascending toward the named side, then curves.
constexpr Shape shapes[] = {
    {{{North}, {South}}}, {{{West}, {East}}}, {{{West}, {East, 1}}}, {{{East}, {West, 1}}},
    {{{South}, {North, 1}}}, {{{North}, {South, 1}}}, {{{South}, {East}}}, {{{South}, {West}}},
    {{{North}, {West}}}, {{{North}, {East}}}
};

const BlockProps* live(const WorldSnapshot& world, BlockPos pos) {
    if (pos.y < world.minY() || pos.y >= world.maxY()) return nullptr;
    auto chunk = world.chunk(pos.x >> 4, pos.z >> 4);
    return chunk ? &BlockRegistry::get().props(chunk->get(pos.x & 15, pos.y, pos.z & 15)) : nullptr;
}

bool safeEmpty(const BlockProps* block) {
    if (!block || block->collides || block->hasFluid()) return false;
    switch (block->type) {
        case BlockType::Fire: case BlockType::Cobweb: case BlockType::PowderSnow:
        case BlockType::SweetBerryBush: case BlockType::EndPortal: return false;
        default: return true;
    }
}

const Shape* usableShape(const WorldSnapshot& world, BlockPos pos) {
    if (std::abs(int64_t(pos.x)) > 30000000 || std::abs(int64_t(pos.z)) > 30000000 ||
        pos.y <= world.minY() || pos.y >= world.maxY() - 2) return nullptr;
    const auto* rail = live(world, pos);
    if (!rail || rail->hasFluid() || (rail->name != "minecraft:rail" && rail->name != "minecraft:golden_rail" &&
        rail->name != "minecraft:detector_rail")) return nullptr;
    const auto value = rail->state("rail_direction");
    if (value.size() != 1 || value.front() < '0' || value.front() > '9') return nullptr;
    const int index = value.front() - '0';
    if (rail->name != "minecraft:rail" && index > 5) return nullptr;
    if (!safeEmpty(live(world, pos.above())) || !safeEmpty(live(world, pos.above(2)))) return nullptr;
    if (auto policy = world.protection(); policy && !policy->permitsSegment(pos, pos.above(2))) return nullptr;
    return &shapes[index];
}

bool connects(BlockPos from, End end, BlockPos to, const Shape& other) {
    return std::ranges::any_of(other, [&](End at) {
        return at.direction == opposite(end.direction) && from.y + end.height == to.y + at.height;
    });
}

}  // namespace

bool railUsable(const WorldSnapshot& world, BlockPos rail) { return usableShape(world, rail) != nullptr; }

bool railsConnected(const WorldSnapshot& world, BlockPos from, BlockPos to) {
    const auto* first = usableShape(world, from);
    const auto* second = usableShape(world, to);
    if (!first || !second) return false;
    if (auto policy = world.protection(); policy && !policy->permitsSegment(from, to)) return false;
    return std::ranges::any_of(*first, [&](End end) {
        const auto neighbor = from.offset(end.direction);
        return neighbor.x == to.x && neighbor.z == to.z && connects(from, end, to, *second);
    });
}

bool railHasStoppingPlatform(const WorldSnapshot& world, BlockPos pos) {
    const auto* shape = usableShape(world, pos);
    if (!shape || shape->front().height || shape->back().height) return false;
    const auto* rail = live(world, pos);
    const auto powered = rail->state("rail_data_bit");
    if (rail->name != "minecraft:golden_rail" || (powered != "0" && powered != "false")) return false;

    const auto policy = world.protection();
    for (auto direction : kHorizontals) {
        if (direction == shape->front().direction || direction == shape->back().direction) continue;
        const auto exit = pos.offset(direction);
        if (!safeEmpty(live(world, exit)) || !safeEmpty(live(world, exit.above()))) return false;
        const auto* floor = live(world, exit.below());
        if (!floor || (!floor->fullCube && !floor->solidTop) || floor->hasFluid() || floor->type == BlockType::Magma ||
            (policy && !policy->permitsSegment(exit, exit.above()))) return false;
    }
    return true;
}

std::optional<BlockPos> railBelow(const WorldSnapshot& world, Vec3d position) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
        std::abs(position.x) > 30000000 || std::abs(position.z) > 30000000 ||
        position.y < world.minY() || position.y >= world.maxY()) return std::nullopt;
    const BlockPos at{int(std::floor(position.x)), int(std::floor(position.y)), int(std::floor(position.z))};
    for (int down = 0; down <= 1; ++down)
        if (railUsable(world, at.below(down))) return at.below(down);
    return std::nullopt;
}

VehiclePath findRailPath(const WorldSnapshot& world, BlockPos from, BlockPos to) {
    if (!railUsable(world, from) || !railUsable(world, to)) return {{}, "Both stations need loaded, unobstructed rails"};
    std::vector<BlockPos> queue{from};
    std::unordered_map<BlockPos, BlockPos> parent{{from, from}};
    constexpr std::size_t limit = 512;
    for (std::size_t i = 0; i < queue.size() && i < limit; ++i) {
        const auto current = queue[i];
        if (current == to) {
            VehiclePath route;
            for (auto at = to; at != from; at = parent.at(at)) route.points.push_back(at);
            route.points.push_back(from);
            std::ranges::reverse(route.points);
            return route;
        }
        const auto* shape = usableShape(world, current);
        if (!shape) continue;
        for (auto end : *shape) {
            for (int rise = -1; rise <= 1; ++rise) {
                const auto neighbor = current.offset(end.direction).above(rise);
                if (parent.contains(neighbor) || !railsConnected(world, current, neighbor)) continue;
                parent.emplace(neighbor, current);
                queue.push_back(neighbor);
            }
        }
    }
    return {{}, "No connected rail route found within 512 loaded rails"};
}

}  // namespace baritone
