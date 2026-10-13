#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "baritone/utils/BlockPos.h"

namespace baritone {

// Where the path should end (Baritone's api.pathing.goals). Goals are immutable and shared
// between the game thread and the calculation thread.
class Goal {
public:
    virtual ~Goal() = default;
    virtual bool isInGoal(int x, int y, int z) const = 0;
    // Estimated cost (ticks) from x,y,z to the goal. Must be fast: called for every node.
    virtual double heuristic(int x, int y, int z) const = 0;
    // Heuristic value at the goal, used to compare goals (usually 0).
    virtual double heuristic() const { return 0; }
    virtual std::string toString() const = 0;
    // Identifies the goal by value (Baritone compares goals with equals()).
    virtual std::string key() const {
        UncensoredScope raw;  // keys compare goals: never censored
        return toString();
    }
    bool sameAs(const Goal& other) const { return key() == other.key(); }

    bool isInGoal(const BlockPos& p) const { return isInGoal(p.x, p.y, p.z); }
    double heuristic(const BlockPos& p) const { return heuristic(p.x, p.y, p.z); }
};

using GoalPtr = std::shared_ptr<const Goal>;

class GoalYLevel : public Goal {
public:
    explicit GoalYLevel(int level) : level_(level) {}
    bool isInGoal(int, int y, int) const override { return y == level_; }
    double heuristic(int, int y, int) const override { return calculate(level_, y); }
    std::string toString() const override;
    static double calculate(int goalY, int currentY);
    int level() const { return level_; }

private:
    int level_;
};

class GoalXZ : public Goal {
public:
    GoalXZ(int x, int z) : x_(x), z_(z) {}
    bool isInGoal(int x, int, int z) const override { return x == x_ && z == z_; }
    double heuristic(int x, int, int z) const override { return calculate(x - x_, z - z_); }
    std::string toString() const override;
    static double calculate(double xDiff, double zDiff);
    int x() const { return x_; }
    int z() const { return z_; }

    // A goal far away in the direction the player looks (#thisway).
    static std::shared_ptr<GoalXZ> fromDirection(double originX, double originZ, float yawDegrees, double distance);

private:
    int x_, z_;
};

class GoalBlock : public Goal {
public:
    GoalBlock(int x, int y, int z) : pos_{x, y, z} {}
    explicit GoalBlock(const BlockPos& p) : pos_(p) {}
    bool isInGoal(int x, int y, int z) const override { return x == pos_.x && y == pos_.y && z == pos_.z; }
    double heuristic(int x, int y, int z) const override { return calculate(x - pos_.x, y - pos_.y, z - pos_.z); }
    std::string toString() const override;
    // yDiff = currentY - goalY
    static double calculate(double xDiff, int yDiff, double zDiff);
    const BlockPos& pos() const { return pos_; }

private:
    BlockPos pos_;
};

// Standing with feet or head in the block (to reach it, e.g. a chest from below).
class GoalTwoBlocks : public Goal {
public:
    explicit GoalTwoBlocks(const BlockPos& p) : pos_(p) {}
    bool isInGoal(int x, int y, int z) const override {
        return x == pos_.x && (y == pos_.y || y == pos_.y - 1) && z == pos_.z;
    }
    double heuristic(int x, int y, int z) const override;
    std::string toString() const override;
    const BlockPos& pos() const { return pos_; }

private:
    BlockPos pos_;
};

// Feet, head, or the block above the head in the target: mining an ore from below.
class GoalThreeBlocks : public Goal {
public:
    explicit GoalThreeBlocks(const BlockPos& p) : pos_(p) {}
    bool isInGoal(int x, int y, int z) const override {
        return x == pos_.x && z == pos_.z && y <= pos_.y && y >= pos_.y - 2;
    }
    double heuristic(int x, int y, int z) const override;
    std::string toString() const override;
    const BlockPos& pos() const { return pos_; }

private:
    BlockPos pos_;
};

// Standing next to a block (any side, or on top of it).
class GoalGetToBlock : public Goal {
public:
    explicit GoalGetToBlock(const BlockPos& p) : pos_(p) {}
    bool isInGoal(int x, int y, int z) const override;
    double heuristic(int x, int y, int z) const override;
    std::string toString() const override;
    const BlockPos& pos() const { return pos_; }

private:
    BlockPos pos_;
};

// Next to a block to break it, but not standing on top of it (BuilderProcess.GoalBreak).
class GoalBreak : public GoalGetToBlock {
public:
    using GoalGetToBlock::GoalGetToBlock;
    bool isInGoal(int x, int y, int z) const override { return y <= pos().y && GoalGetToBlock::isInGoal(x, y, z); }
    std::string toString() const override { return "GoalBreak" + GoalGetToBlock::toString().substr(14); }
};

// Next to a position to place a block there, without standing in it (BuilderProcess.GoalAdjacent).
class GoalAdjacent : public GoalGetToBlock {
public:
    using GoalGetToBlock::GoalGetToBlock;
    bool isInGoal(int x, int y, int z) const override {
        if (x == pos().x && z == pos().z && (y == pos().y || y == pos().y - 1)) return false;  // feet or head inside
        return GoalGetToBlock::isInGoal(x, y, z);
    }
    std::string toString() const override { return "GoalAdjacent" + GoalGetToBlock::toString().substr(14); }
};

class GoalNear : public Goal {
public:
    GoalNear(const BlockPos& p, int range) : pos_(p), rangeSq_(static_cast<int64_t>(range) * range) {}
    bool isInGoal(int x, int y, int z) const override { return BlockPos{x, y, z}.distSq(pos_) <= rangeSq_; }
    double heuristic(int x, int y, int z) const override { return GoalBlock::calculate(x - pos_.x, y - pos_.y, z - pos_.z); }
    std::string toString() const override;
    const BlockPos& pos() const { return pos_; }

private:
    BlockPos pos_;
    int64_t rangeSq_;
};

class GoalComposite : public Goal {
public:
    explicit GoalComposite(std::vector<GoalPtr> goals) : goals_(std::move(goals)) {}
    bool isInGoal(int x, int y, int z) const override;
    double heuristic(int x, int y, int z) const override;
    double heuristic() const override;
    std::string toString() const override;
    std::string key() const override;
    const std::vector<GoalPtr>& goals() const { return goals_; }

private:
    std::vector<GoalPtr> goals_;
};

// Go as far as possible from the origin goal (#invert).
class GoalInverted : public Goal {
public:
    explicit GoalInverted(GoalPtr origin) : origin_(std::move(origin)) {}
    bool isInGoal(int, int, int) const override { return false; }
    double heuristic(int x, int y, int z) const override { return -origin_->heuristic(x, y, z); }
    double heuristic() const override { return -origin_->heuristic(); }
    std::string toString() const override;
    std::string key() const override { return "Inverted(" + origin_->key() + ")"; }
    const GoalPtr& origin() const { return origin_; }

private:
    GoalPtr origin_;
};

class GoalRunAway : public Goal {
public:
    GoalRunAway(double distance, std::optional<int> maintainY, std::vector<BlockPos> from, bool never = false);
    bool isInGoal(int x, int y, int z) const override;
    double heuristic(int x, int y, int z) const override;
    double heuristic() const override;
    std::string toString() const override;
    std::string key() const override;

private:
    bool never_;  // exploration: never "arrive", keep going away
    std::vector<BlockPos> from_;
    int64_t distanceSq_;
    std::optional<int> maintainY_;
};

// Travel along a direction forever (#tunnel without target, explore helpers).
class GoalStrictDirection : public Goal {
public:
    GoalStrictDirection(const BlockPos& origin, Direction dir);
    bool isInGoal(int, int, int) const override { return false; }
    double heuristic(int x, int y, int z) const override;
    std::string toString() const override;

private:
    BlockPos origin_;
    int dx_, dz_;
};

// Reach one of the "highways" (axis or diagonal through 0,0) at a given height (#axis).
class GoalAxis : public Goal {
public:
    explicit GoalAxis(int height) : height_(height) {}
    bool isInGoal(int x, int y, int z) const override;
    double heuristic(int x, int y, int z) const override;
    std::string toString() const override;

private:
    int height_;
};

}  // namespace baritone
