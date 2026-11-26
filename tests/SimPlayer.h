#pragma once
// A TestPlayer that really moves: Bedrock (here the same as Java) walking, sprinting, jumping,
// ladders and sneaking at edges, against the collision boxes of the blocks, driven by the
// bot's MovementState. Lets the movement executors run offline on terrain dumps before
// anything is tried in game, where a missed jump can fall into the void.
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

#include "TestPlayer.h"
#include "baritone/pathing/movement/Movement.h"
#include "baritone/world/BlockStateInterface.h"

namespace test {

class SimPlayer : public TestPlayer {
public:
    explicit SimPlayer(std::shared_ptr<const baritone::WorldSnapshot> world) : bsi_(std::move(world)) {}

    bool isSneaking() const override { return sneaking_; }
    int ticks = 0;
    std::vector<baritone::Vec3d> trace;  // feet position after each tick

    // One game tick with the bot's outputs.
    void step(const baritone::MovementState& out) {
        using baritone::Input;
        ++ticks;
        if (out.target.rotation) facing = *out.target.rotation;
        sneaking_ = out.input(Input::Sneak);
        double forward = (out.input(Input::MoveForward) ? 1.0 : 0.0) - (out.input(Input::MoveBack) ? 1.0 : 0.0);
        double strafe = (out.input(Input::MoveLeft) ? 1.0 : 0.0) - (out.input(Input::MoveRight) ? 1.0 : 0.0);
        if (sneaking_) {
            forward *= 0.3;
            strafe *= 0.3;
        }
        sprinting_ = out.input(Input::Sprint) && forward > 0 && !sneaking_ && (sprinting_ || grounded);
        if (forward <= 0) sprinting_ = false;
        const double yaw = facing.yaw * std::numbers::pi / 180;
        const bool ladder = onLadder();
        if (out.input(Input::Jump) && grounded && !ladder && onLadderTop()) {
            motion.y = 0.2;  // Bedrock's client: the feet above a ladder's cell, a jump is a climb
        } else if (out.input(Input::Jump) && grounded && !ladder) {
            motion.y = 0.42;
            if (sprinting_) {
                motion.x += -std::sin(yaw) * 0.2;
                motion.z += std::cos(yaw) * 0.2;
            }
        }
        const double speed = grounded ? (sprinting_ ? 0.13 : 0.1) : (sprinting_ ? 0.026 : 0.02);
        double length = strafe * strafe + forward * forward;
        if (length >= 1e-4) {
            length = std::max(1.0, std::sqrt(length));
            const double s = strafe / length * speed, f = forward / length * speed;
            motion.x += s * std::cos(yaw) - f * std::sin(yaw);
            motion.z += f * std::cos(yaw) + s * std::sin(yaw);
        }
        if (ladder) {  // Java LivingEntity.handleOnClimbable
            motion.x = std::clamp(motion.x, -0.15, 0.15);
            motion.z = std::clamp(motion.z, -0.15, 0.15);
            motion.y = std::max(motion.y, -0.15);
            if (sneaking_ && motion.y < 0) motion.y = 0;
        }
        if (sneaking_ && grounded) backOffFromEdge();
        const bool wasGrounded = grounded;
        bool horizontalCollision = false;
        move(wasGrounded, horizontalCollision);
        if (onLadder() && (horizontalCollision || out.input(Input::Jump))) motion.y = 0.2;
        motion.y = (motion.y - 0.08) * 0.98;
        const double friction = wasGrounded ? 0.546 : 0.91;
        motion.x *= friction;
        motion.z *= friction;
        trace.push_back(feet);
    }

private:
    struct Box {
        double x0, y0, z0, x1, y1, z1;
    };
    static constexpr double kHalf = 0.3, kHeight = 1.8, kStep = 0.5625, kEps = 1e-7;

    bool onLadder() {
        return bsi_.props(static_cast<int>(std::floor(feet.x)), static_cast<int>(std::floor(feet.y)),
                          static_cast<int>(std::floor(feet.z))).type == baritone::BlockType::Ladder;
    }

    bool onLadderTop() {
        return bsi_.props(static_cast<int>(std::floor(feet.x)), static_cast<int>(std::floor(feet.y - 0.01)),
                          static_cast<int>(std::floor(feet.z))).type == baritone::BlockType::Ladder;
    }

    // The collision box of a block, in world coordinates (nothing for blocks without collision).
    bool boxOf(int x, int y, int z, Box& b) {
        const baritone::BlockProps& p = bsi_.props(x, y, z);
        if (!p.collides || p.height <= 0) return false;
        double x0 = 0, x1 = 1, z0 = 0, z1 = 1, y0 = 0, y1 = p.height;
        if (p.type == baritone::BlockType::Ladder) {
            constexpr double t = 0.1875;
            const auto f = p.state("facing_direction");
            if (f == "2") z0 = 1 - t;
            else if (f == "3") z1 = t;
            else if (f == "4") x0 = 1 - t;
            else if (f == "5") x1 = t;
        } else if (p.hasSmallFootprint() || p.type == baritone::BlockType::Honey) {
            const double hx = p.collisionHalfX(), hz = p.collisionHalfZ();
            x0 = 0.5 - hx, x1 = 0.5 + hx, z0 = 0.5 - hz, z1 = 0.5 + hz;
            if (p.isHorizontalRod()) y0 = 0.375;
        } else if (p.topSlab) {
            y0 = 0.5;
        }
        b = {x + x0, y + y0, z + z0, x + x1, y + y1, z + z1};
        return true;
    }

    std::vector<Box> boxesAround(double reach) {
        std::vector<Box> boxes;
        Box b;
        for (int x = static_cast<int>(std::floor(feet.x - kHalf - reach)); x <= std::floor(feet.x + kHalf + reach); ++x)
            for (int z = static_cast<int>(std::floor(feet.z - kHalf - reach)); z <= std::floor(feet.z + kHalf + reach); ++z)
                for (int y = static_cast<int>(std::floor(feet.y - reach)) - 1; y <= std::floor(feet.y + kHeight + reach); ++y)
                    if (boxOf(x, y, z, b)) boxes.push_back(b);
        return boxes;
    }

    static bool overlaps(double a0, double a1, double b0, double b1) { return a0 < b1 - kEps && a1 > b0 + kEps; }

    // Moves the player's box along one axis against the boxes (Java Shapes.collide).
    static double clip(const std::vector<Box>& boxes, const std::array<double, 3>& p, int axis, double motion) {
        const std::array<double, 3> lo{p[0] - kHalf, p[1], p[2] - kHalf}, hi{p[0] + kHalf, p[1] + kHeight, p[2] + kHalf};
        for (const Box& b : boxes) {
            const std::array<double, 3> b0{b.x0, b.y0, b.z0}, b1{b.x1, b.y1, b.z1};
            bool across = true;
            for (int other = 0; other < 3; ++other)
                if (other != axis && !overlaps(lo[other], hi[other], b0[other], b1[other])) across = false;
            if (!across) continue;
            if (motion > 0 && hi[axis] <= b0[axis] + kEps) motion = std::min(motion, b0[axis] - hi[axis]);
            if (motion < 0 && lo[axis] >= b1[axis] - kEps) motion = std::max(motion, b1[axis] - lo[axis]);
        }
        return motion;
    }

    // Bedrock resolves the vertical axis first, then x and z; a blocked horizontal move on the
    // ground tries again one step higher (Java maxUpStep).
    void move(bool wasGrounded, bool& horizontalCollision) {
        const auto boxes = boxesAround(std::max({std::abs(motion.x), std::abs(motion.y), std::abs(motion.z)}) + 1);
        auto attempt = [&](std::array<double, 3> p, std::array<double, 3> m, std::array<double, 3>& done) {
            for (int axis : {1, 0, 2}) {
                done[axis] = clip(boxes, p, axis, m[axis]);
                p[axis] += done[axis];
            }
            return p;
        };
        std::array<double, 3> done{};
        const std::array<double, 3> start{feet.x, feet.y, feet.z}, wanted{motion.x, motion.y, motion.z};
        std::array<double, 3> end = attempt(start, wanted, done);
        const bool blocked = std::abs(done[0] - wanted[0]) > kEps || std::abs(done[2] - wanted[2]) > kEps;
        if (blocked && (wasGrounded || (done[1] != wanted[1] && wanted[1] < 0))) {
            std::array<double, 3> up{}, raised = start;
            raised[1] += clip(boxes, start, 1, kStep);
            std::array<double, 3> stepped = attempt(raised, {wanted[0], 0, wanted[2]}, up);
            std::array<double, 3> down{};
            stepped[1] += clip(boxes, stepped, 1, -(stepped[1] - start[1]) + std::min(0.0, wanted[1]));
            if (std::hypot(up[0], up[2]) > std::hypot(done[0], done[2]) + 1e-6) {
                end = stepped;
                done = {up[0], stepped[1] - start[1], up[2]};
            }
        }
        horizontalCollision = std::abs(done[0] - wanted[0]) > kEps || std::abs(done[2] - wanted[2]) > kEps;
        const bool verticalCollision = std::abs(done[1] - wanted[1]) > kEps;
        grounded = verticalCollision && wanted[1] < 0;
        if (std::abs(done[0] - wanted[0]) > kEps) motion.x = 0;
        if (std::abs(done[2] - wanted[2]) > kEps) motion.z = 0;
        if (verticalCollision) motion.y = 0;
        feet = {end[0], end[1], end[2]};
    }

    // Java Player.maybeBackOffFromEdge: sneaking never walks off a block.
    void backOffFromEdge() {
        const auto boxes = boxesAround(1);
        auto supported = [&](double dx, double dz) {
            const double x0 = feet.x + dx - kHalf, x1 = feet.x + dx + kHalf, z0 = feet.z + dz - kHalf, z1 = feet.z + dz + kHalf;
            const double y0 = feet.y - kStep, y1 = feet.y;
            return std::ranges::any_of(boxes, [&](const Box& b) {
                return overlaps(x0, x1, b.x0, b.x1) && overlaps(y0, y1, b.y0, b.y1) && overlaps(z0, z1, b.z0, b.z1);
            });
        };
        constexpr double s = 0.05;
        double dx = motion.x, dz = motion.z;
        while (dx != 0 && !supported(dx, 0)) dx = std::abs(dx) < s ? 0 : dx - std::copysign(s, dx);
        while (dz != 0 && !supported(0, dz)) dz = std::abs(dz) < s ? 0 : dz - std::copysign(s, dz);
        while (dx != 0 && dz != 0 && !supported(dx, dz)) {
            dx = std::abs(dx) < s ? 0 : dx - std::copysign(s, dx);
            dz = std::abs(dz) < s ? 0 : dz - std::copysign(s, dz);
        }
        motion.x = dx;
        motion.z = dz;
    }

    baritone::BlockStateInterface bsi_;
    bool sneaking_ = false, sprinting_ = false;
};

}  // namespace test
