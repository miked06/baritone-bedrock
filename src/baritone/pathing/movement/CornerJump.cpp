#include "baritone/pathing/movement/CornerJump.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <unordered_set>

#include "baritone/pathing/movement/MovementHelper.h"

namespace baritone::movement_costs {
namespace {

constexpr double halfWidth = 0.3, height = 1.8;
constexpr std::size_t maxSteps = 21, beamWidth = 180;
constexpr double epsilon = 1e-7;

struct Box {
    std::array<double, 3> min, max;
    bool hazard;
};

struct Sample {
    std::array<double, 3> position, velocity;
    bool onGround;
};

struct Candidate {
    Sample sample;
    std::array<CornerJumpInput, maxSteps> inputs{};
    double score = 0;
};

struct Steering {
    CornerJumpInput input;
    double x, z;
};

Steering steering(CornerJumpInput input) {
    const double angle = input.yaw * std::numbers::pi / 180;
    return {input, -std::sin(angle), std::cos(angle)};
}

bool overlaps(double a, double b, double c, double d) {
    return a < d - epsilon && b > c + epsilon;
}

std::optional<Sample> step(Sample sample, Steering control, const std::vector<Box>& boxes) {
    const auto& input = control.input;
    constexpr std::array<double, 3> low{-halfWidth, 0, -halfWidth}, high{halfWidth, height, halfWidth};
    double acceleration = input.forward ? (sample.onGround ? 0.1274 : 0.02548) : 0;
    if (input.jump && sample.onGround) {
        sample.velocity[1] = 0.42;
        acceleration += 0.2;
    }
    sample.velocity[0] += control.x * acceleration;
    sample.velocity[2] += control.z * acceleration;
    const double drag = sample.onGround ? 0.546 : 0.91;
    sample.onGround = false;

    // Bedrock resolves vertical collision before the horizontal axes.
    for (int axis : {1, 0, 2}) {
        const int first = (axis + 1) % 3, second = (axis + 2) % 3;
        const double original = sample.velocity[axis];
        double motion = original;
        for (const Box& box : boxes) {
            const auto& p = sample.position;
            if (!overlaps(p[first] + low[first], p[first] + high[first], box.min[first], box.max[first]) ||
                !overlaps(p[second] + low[second], p[second] + high[second], box.min[second], box.max[second]))
                continue;
            const double before = p[axis] + low[axis], after = p[axis] + high[axis];
            if (box.hazard && overlaps(before + std::min(0.0, motion), after + std::max(0.0, motion),
                                       box.min[axis], box.max[axis])) return std::nullopt;
            if (motion > 0 && after <= box.min[axis] + epsilon)
                motion = std::min(motion, box.min[axis] - after);
            if (motion < 0 && before >= box.max[axis] - epsilon)
                motion = std::max(motion, box.max[axis] - before);
        }
        if (std::abs(motion - original) > epsilon) {
            sample.onGround = sample.onGround || (axis == 1 && original < 0);
            sample.velocity[axis] = 0;
        }
        sample.position[axis] += motion;
    }
    sample.velocity[0] *= drag;
    sample.velocity[1] = (sample.velocity[1] - 0.08) * 0.98;
    sample.velocity[2] *= drag;
    return sample;
}

Vec3d vec(const Sample& s) { return {s.position[0], s.position[1], s.position[2]}; }

float yawFor(double x, double z) {
    return static_cast<float>(std::atan2(-x, z) * 180 / std::numbers::pi);
}

uint64_t binKey(const Sample& s, Vec3d start) {
    uint64_t key = 0;
    for (double value : std::array{s.position[0] - start.x, s.position[1] - start.y,
                                  s.position[2] - start.z, s.velocity[0], s.velocity[1], s.velocity[2]}) {
        key = (key << 8) | (static_cast<uint64_t>(std::lround(value * 24)) & 255);
    }
    return key;
}

void selectBeam(std::vector<Candidate>& beam, std::vector<Candidate>& next, Vec3d start, std::size_t width) {
    const auto byScore = [](const Candidate& a, const Candidate& b) { return a.score < b.score; };
    if (next.size() > width * 5) {
        std::nth_element(next.begin(), next.begin() + width * 5, next.end(), byScore);
        next.resize(width * 5);
    }
    std::ranges::sort(next, byScore);
    beam.clear();
    std::unordered_set<uint64_t> bins;
    for (const Candidate& option : next) {
        if (!bins.insert(binKey(option.sample, start)).second) continue;
        beam.push_back(option);
        if (beam.size() == width) break;
    }
}

std::optional<CornerJump> search(const std::vector<Box>& boxes, BlockPos src, BlockPos dest,
                                 Vec3d start, int side) {
    const Vec3d forward{(dest.x - src.x) * 0.5, 0, (dest.z - src.z) * 0.5};
    const Vec3d lateral{-forward.z * side, 0, forward.x * side};
    const Vec3d center{src.x + 0.5, static_cast<double>(src.y), src.z + 0.5};
    auto dot = [](Vec3d a, Vec3d b) { return a.x * b.x + a.z * b.z; };
    const float approach = yawFor(lateral.x + forward.x * 0.176, lateral.z + forward.z * 0.176);
    const float forwardYaw = yawFor(forward.x, forward.z);
    std::array<Steering, 25> controls{};
    for (int i = 0; i < 25; ++i)
        controls[i] = steering({forwardYaw + (i - 12) * 15.f, i != 24, false});
    std::vector<Candidate> beam, next;
    beam.reserve(beamWidth);
    next.reserve(beamWidth * 26);
    for (int runup : {4, 3}) {
        for (float bend : {0.f, 4.f, -4.f, 8.f, -8.f}) {
            Candidate c{{{start.x, start.y, start.z}, {0, -0.0784, 0}, true}};
            bool valid = true;
            for (int tick = 0; tick <= runup; ++tick) {
                const auto input = CornerJumpInput{tick == runup ? forwardYaw + bend : approach, true, tick == runup};
                auto result = step(c.sample, steering(input), boxes);
                if (!result || (tick < runup && !result->onGround)) { valid = false; break; }
                c.sample = *result;
                c.inputs[tick] = input;
            }
            if (!valid || c.sample.position[1] <= src.y) continue;
            beam.clear();
            beam.push_back(c);
            for (std::size_t tick = runup + 1; tick < maxSteps && !beam.empty(); ++tick) {
                next.clear();
                for (const Candidate& parent : beam) {
                    for (const auto& control : controls) {
                        const auto& input = control.input;
                        const auto result = step(parent.sample, control, boxes);
                        if (!result || result->position[1] < src.y - 0.01) continue;
                        const Vec3d p = vec(*result);
                        const Vec3d v{result->velocity[0], 0, result->velocity[2]};
                        const double depth = dot(p - center, forward), offset = dot(p - center, lateral);
                        if (std::abs(offset) > 1.4 || depth < -0.65 || depth > 2.65) continue;
                        const double futureDepth = depth + dot(v, forward) * 1.6;
                        const double futureOffset = offset + dot(v, lateral) * 1.6;
                        if (result->onGround && depth >= 1.73 && depth <= 2.47 && offset >= -0.35 && offset <= 0.63) {
                            CornerJump plan{start, {parent.inputs.begin(), parent.inputs.begin() + tick}};
                            plan.inputs.push_back(input);
                            return plan;
                        }
                        Candidate candidate{*result, parent.inputs};
                        candidate.inputs[tick] = input;
                        const double wantedOffset = futureDepth > 1.79 ? 0.28 : 0.90;
                        candidate.score = std::abs(futureDepth - 2.05) + 0.85 * std::abs(futureOffset - wantedOffset);
                        next.push_back(candidate);
                    }
                }
                selectBeam(beam, next, start, beamWidth);
            }
        }
    }
    return std::nullopt;
}

std::optional<CornerJump> searchSteeredLanding(const std::vector<Box>& boxes, BlockPos dest,
                                               Vec3d start, float heading) {
    // Seed short sprints from the takeoff ledge. Once airborne, retain trajectories that get
    // close to the landing platform with enough time left to turn and brake.
    constexpr std::size_t width = 240;
    std::array<Steering, 25> controls{};
    std::array<Steering, 24> launches{};
    for (int i = 0; i < 24; ++i) {
        controls[i] = steering({heading + i * 15.f, true, false});
        launches[i] = steering({heading + i * 15.f, true, true});
    }
    controls.back() = steering({heading, false, false});
    const Vec3d target = Vec3d::center(dest);
    const auto score = [&](const Sample& sample) {
        return std::hypot(sample.position[0] + sample.velocity[0] * 2.2 - target.x,
                          sample.position[2] + sample.velocity[2] * 2.2 - target.z);
    };
    std::array<std::vector<Candidate>, 5> takeoffs;
    for (std::size_t dir = 0; dir < launches.size(); ++dir) {
        Candidate approach{{{start.x, start.y, start.z}, {0, -0.0784, 0}, true}};
        for (std::size_t i = 0; i < takeoffs.size() && approach.sample.onGround; ++i) {
            for (const auto& launch : launches) {
                if (i > 0 && launch.x * controls[dir].x + launch.z * controls[dir].z < 0.3) continue;
                const auto sample = step(approach.sample, launch, boxes);
                if (!sample || sample->position[1] <= start.y + 0.01) continue;
                Candidate c{*sample, approach.inputs, score(*sample)};
                c.inputs[i] = launch.input;
                takeoffs[i].push_back(c);
            }
            auto sample = step(approach.sample, controls[dir], boxes);
            if (!sample) break;
            approach.sample = *sample;
            approach.inputs[i] = controls[dir].input;
        }
    }
    std::vector<Candidate> beam, next;
    beam.reserve(width);
    next.reserve(width * controls.size());
    for (std::size_t runup = takeoffs.size(); runup-- > 0;) {
        selectBeam(beam, takeoffs[runup], start, width);
        for (std::size_t tick = runup + 1; tick < maxSteps && !beam.empty(); ++tick) {
            next.clear();
            for (const Candidate& parent : beam) {
                for (const auto& control : controls) {
                    const auto sample = step(parent.sample, control, boxes);
                    if (!sample || sample->position[1] < dest.y - 0.01) continue;
                    if (sample->onGround && std::abs(sample->position[1] - dest.y) < 0.01 &&
                        std::abs(sample->position[0] - target.x) < 0.36 &&
                        std::abs(sample->position[2] - target.z) < 0.36 &&
                        std::hypot(sample->velocity[0], sample->velocity[2]) < 0.15) {
                        CornerJump plan{start, {parent.inputs.begin(), parent.inputs.begin() + tick}};
                        plan.inputs.push_back(control.input);
                        return plan;
                    }
                    Candidate c{*sample, parent.inputs, score(*sample)};
                    c.inputs[tick] = control.input;
                    next.push_back(c);
                }
            }
            selectBeam(beam, next, start, width);
        }
    }
    return std::nullopt;
}

// A one block gap with a block at head height above it: a normal jump bumps its head 0.2
// up and the arc collapses. The simulated hop (sprint, jump under the ceiling, brake) can
// still cross it.
bool isLowCeilingHop(CalculationContext& ctx, BlockPos src, BlockPos dest) {
    const BlockPos d = dest - src;
    if (!ctx.allowParkour || !ctx.canSprint || d.y != 0 || std::abs(d.x) + std::abs(d.z) != 2 || (d.x != 0 && d.z != 0))
        return false;
    const BlockPos gap{src.x + d.x / 2, src.y, src.z + d.z / 2};
    const auto& ceiling = ctx.props(gap.x, src.y + 2, gap.z);
    return ceiling.collides && !helper::avoidWalkingInto(ceiling) && !ctx.isAvoided(ceiling) &&
           !helper::canWalkOn(ctx, gap.x, gap.y - 1, gap.z) && !helper::avoidWalkingInto(ctx.props(gap.x, gap.y - 1, gap.z)) &&
           helper::fullyPassable(ctx, gap.x, gap.y, gap.z) && helper::fullyPassable(ctx, gap.x, gap.y + 1, gap.z) &&
           helper::fullyPassable(ctx, src.x, src.y, src.z) && helper::fullyPassable(ctx, src.x, src.y + 1, src.z) &&
           helper::fullyPassable(ctx, dest.x, dest.y, dest.z) && helper::fullyPassable(ctx, dest.x, dest.y + 1, dest.z) &&
           ctx.props(src.x, src.y - 1, src.z).fullCube && ctx.props(dest.x, dest.y - 1, dest.z).fullCube &&
           helper::canParkourOn(ctx, src.below()) && helper::canParkourOn(ctx, dest.below()) && ctx.permitsSegment(src, dest);
}

}  // namespace

bool isCornerJump(CalculationContext& ctx, BlockPos src, BlockPos dest) {
    const BlockPos d = dest - src;
    const int distance = std::abs(d.x) + std::abs(d.z);
    const bool flat = d.y == 0 && (distance == 2 || distance == 3) && (d.x == 0 || d.z == 0);
    const bool lower = d.y == -1 && d.x * d.x + d.z * d.z == 10;
    const bool underLedge = d.y == -2 && std::abs(d.x) == 2 && std::abs(d.z) == 2;
    if (flat && distance == 2 && isLowCeilingHop(ctx, src, dest)) return true;
    if (!ctx.allowParkour || !ctx.canSprint || (!flat && !lower && !underLedge) ||
        -d.y > ctx.maxFallHeightNoWater || !ctx.props(dest.x, dest.y - 1, dest.z).fullCube) return false;

    const auto clearEndpoints = [&] {
        return ctx.props(src.x, src.y - 1, src.z).fullCube &&
               helper::canParkourOn(ctx, src.below()) && helper::canParkourOn(ctx, dest.below()) &&
               helper::fullyPassable(ctx, src.x, src.y, src.z) &&
               helper::fullyPassable(ctx, dest.x, dest.y, dest.z) &&
               helper::fullyPassable(ctx, dest.x, dest.y + 1, dest.z) && ctx.permitsSegment(src, dest);
    };
    if (underLedge) {
        const BlockPos sideX = src.offset(d.x / 2, 1, 0);
        const BlockPos sideZ = src.offset(0, 1, d.z / 2);
        const bool xBlocked = ctx.props(sideX.x, sideX.y, sideX.z).collides;
        const bool zBlocked = ctx.props(sideZ.x, sideZ.y, sideZ.z).collides;
        return xBlocked != zBlocked && ctx.props(dest.x, src.y + 2, dest.z).fullCube &&
               clearEndpoints() &&
               !helper::canWalkOn(ctx, src.x + d.x / 2, src.y - 1, src.z + d.z / 2);
    }

    const BlockPos forward{std::abs(d.x) > std::abs(d.z) ? d.x / std::abs(d.x) : 0, 0,
                           std::abs(d.z) > std::abs(d.x) ? d.z / std::abs(d.z) : 0};
    const BlockPos middle = dest - forward + BlockPos{0, -d.y, 0};
    if (!ctx.props(middle.x, middle.y, middle.z).collides ||
        !ctx.props(middle.x, middle.y + 1, middle.z).collides || !clearEndpoints()) return false;

    if (lower) {
        const BlockPos gap = src + forward;
        const BlockPos around = gap + forward;
        return helper::fullyPassable(ctx, around.x, around.y + 1, around.z) &&
               !helper::canWalkOn(ctx, gap.x, gap.y - 1, gap.z) &&
               !helper::canWalkOn(ctx, around.x, around.y - 2, around.z);
    }
    if (distance == 3 && helper::canWalkOn(ctx, src.x + forward.x, src.y - 1, src.z + forward.z))
        return false;

    bool openGap = false;
    for (int side : {1, -1}) {
        const BlockPos offset{-forward.z * side, 0, forward.x * side};
        const auto clearFloor = [&](BlockPos p) {
            return helper::canWalkOn(ctx, p.x, p.y - 1, p.z) &&
                   helper::fullyPassable(ctx, p.x, p.y, p.z) &&
                   helper::fullyPassable(ctx, p.x, p.y + 1, p.z);
        };
        if (clearFloor(src + offset) && clearFloor(middle + offset) && clearFloor(dest + offset))
            return false;
        const auto beside = middle + offset;
        openGap = openGap || (helper::fullyPassable(ctx, beside.x, beside.y + 1, beside.z) &&
                              !helper::canWalkOn(ctx, beside.x, beside.y - 1, beside.z));
    }
    return openGap;
}

std::optional<CornerJump> planCornerJump(CalculationContext& ctx, BlockPos src, BlockPos dest, std::optional<Vec3d> start) {
    if (!isCornerJump(ctx, src, dest)) return std::nullopt;
    const BlockPos d = dest - src;
    std::vector<Box> boxes;
    for (int x = std::min(src.x, dest.x) - 1; x <= std::max(src.x, dest.x) + 1; ++x) {
        for (int z = std::min(src.z, dest.z) - 1; z <= std::max(src.z, dest.z) + 1; ++z) {
            if (!ctx.isLoadedLive(x, z)) return std::nullopt;
            for (int y = std::min(src.y, dest.y) - 1; y <= src.y + 3; ++y) {
                const auto& p = ctx.props(x, y, z);
                bool hazard = helper::avoidWalkingInto(p) || ctx.isAvoided(p) || !ctx.permitsSegment(src, {x,y,z});
                if (!p.collides && !hazard) continue;
                boxes.push_back({{double(x), y + (p.topSlab ? 0.5 : 0), double(z)},
                                 {x + 1.0, y + std::max(hazard ? 1.0 : 0.0, double(p.height)), z + 1.0},
                                 hazard});
            }
        }
    }
    const auto validStart = [&](Vec3d origin) {
        return std::ranges::none_of(boxes, [&](const Box& b) {
            return overlaps(origin.x - halfWidth, origin.x + halfWidth, b.min[0], b.max[0]) &&
                   overlaps(origin.y, origin.y + height, b.min[1], b.max[1]) &&
                   overlaps(origin.z - halfWidth, origin.z + halfWidth, b.min[2], b.max[2]);
        });
    };
    const bool longFlat = d.y == 0 && std::abs(d.x) + std::abs(d.z) == 3;
    if (d.y < 0 || longFlat || isLowCeilingHop(ctx, src, dest)) {
        using Offsets = std::array<std::pair<double, double>, 4>;
        const Offsets stages = longFlat ? Offsets{{{0.28, 0.28}, {-0.28, 0.28}, {0.28, -0.28}, {-0.28, -0.28}}}
                                      : Offsets{{{0.28, 0.28}, {0.28, 0.0}, {0.28, -0.28}, {0.0, 0.28}}};
        for (const auto& [sideX, sideZ] : stages) {
            const Vec3d staging{src.x + 0.5 + sideX * (d.x > 0 ? 1 : -1), double(src.y),
                                src.z + 0.5 + sideZ * (d.z > 0 ? 1 : -1)};
            const Vec3d origin = start.value_or(staging);
            if (validStart(origin)) {
                const float heading = std::round(yawFor(d.x, d.z) / 15) * 15;
                if (auto plan = searchSteeredLanding(boxes, dest, origin, heading)) return plan;
            }
            if (start) break;
        }
        return std::nullopt;
    }
    for (int side : {1, -1}) {
        const BlockPos beside = src.offset(d.x / 2 - d.z / 2 * side, 0, d.z / 2 + d.x / 2 * side);
        if (ctx.props(beside.x, beside.y + 1, beside.z).fullCube) continue;
        const Vec3d staging{src.x + 0.5 - d.x * 0.225 + d.z * side * 0.0275, double(src.y),
                            src.z + 0.5 - d.z * 0.225 - d.x * side * 0.0275};
        const Vec3d origin = start.value_or(staging);
        if (!validStart(origin)) continue;
        if (auto plan = search(boxes, src, dest, origin, side)) return plan;
    }
    return std::nullopt;
}

}  // namespace baritone::movement_costs
