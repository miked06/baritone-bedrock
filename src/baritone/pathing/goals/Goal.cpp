#include "baritone/pathing/goals/Goal.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>

#include "baritone/Settings.h"
#include "baritone/pathing/ActionCosts.h"

namespace baritone {

// ---- GoalYLevel ----
double GoalYLevel::calculate(int goalY, int currentY) {
    if (currentY > goalY) return costs::fallCost(2) / 2 * (currentY - goalY);  // need to descend
    if (goalY > currentY) return (goalY - currentY) * costs::JUMP_ONE_BLOCK_COST;  // need to ascend
    return 0;
}

std::string GoalYLevel::toString() const {
    return std::format("GoalYLevel{{y={}}}", maybeCensor(level_));
}

// ---- GoalXZ ----
double GoalXZ::calculate(double xDiff, double zDiff) {
    // Mix of manhattan and euclidean: the path walks either straight or diagonally.
    double x = std::abs(xDiff);
    double z = std::abs(zDiff);
    double straight, diagonal;
    if (x < z) {
        straight = z - x;
        diagonal = x;
    } else {
        straight = x - z;
        diagonal = z;
    }
    diagonal *= costs::SQRT_2;
    return (diagonal + straight) * Settings::get().costHeuristic;
}

std::shared_ptr<GoalXZ> GoalXZ::fromDirection(double originX, double originZ, float yawDegrees, double distance) {
    // Bedrock yaw: 0 = +Z (south), 90 = -X (west), same as Java.
    double theta = yawDegrees * std::numbers::pi / 180.0;
    double x = originX - std::sin(theta) * distance;
    double z = originZ + std::cos(theta) * distance;
    return std::make_shared<GoalXZ>(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(z)));
}

std::string GoalXZ::toString() const {
    return std::format("GoalXZ{{x={},z={}}}", maybeCensor(x_), maybeCensor(z_));
}

// ---- GoalBlock ----
double GoalBlock::calculate(double xDiff, int yDiff, double zDiff) {
    // yDiff of 1 means one block above the goal: going from 0,yDiff,0 to GoalYLevel 0 is exact.
    return GoalYLevel::calculate(0, yDiff) + GoalXZ::calculate(xDiff, zDiff);
}

std::string GoalBlock::toString() const {
    return std::format("GoalBlock{{x={},y={},z={}}}", maybeCensor(pos_.x), maybeCensor(pos_.y), maybeCensor(pos_.z));
}

// ---- GoalTwoBlocks ----
double GoalTwoBlocks::heuristic(int x, int y, int z) const {
    int yDiff = y - pos_.y;
    if (yDiff < 0) yDiff++;
    return GoalBlock::calculate(x - pos_.x, yDiff, z - pos_.z);
}

std::string GoalTwoBlocks::toString() const {
    return std::format("GoalTwoBlocks{{x={},y={},z={}}}", maybeCensor(pos_.x), maybeCensor(pos_.y), maybeCensor(pos_.z));
}

// ---- GoalGetToBlock ----
bool GoalGetToBlock::isInGoal(int x, int y, int z) const {
    int xDiff = x - pos_.x, yDiff = y - pos_.y, zDiff = z - pos_.z;
    return std::abs(xDiff) + std::abs(yDiff < 0 ? yDiff + 1 : yDiff) + std::abs(zDiff) <= 1;
}

double GoalGetToBlock::heuristic(int x, int y, int z) const {
    int yDiff = y - pos_.y;
    return GoalBlock::calculate(x - pos_.x, yDiff < 0 ? yDiff + 1 : yDiff, z - pos_.z);
}

std::string GoalGetToBlock::toString() const {
    return std::format("GoalGetToBlock{{x={},y={},z={}}}", maybeCensor(pos_.x), maybeCensor(pos_.y), maybeCensor(pos_.z));
}

// ---- GoalNear ----
std::string GoalNear::toString() const {
    return std::format("GoalNear{{x={},y={},z={},rangeSq={}}}", maybeCensor(pos_.x), maybeCensor(pos_.y), maybeCensor(pos_.z), rangeSq_);
}

// ---- GoalComposite ----
bool GoalComposite::isInGoal(int x, int y, int z) const {
    return std::ranges::any_of(goals_, [&](const GoalPtr& g) { return g->isInGoal(x, y, z); });
}

double GoalComposite::heuristic(int x, int y, int z) const {
    double min = std::numeric_limits<double>::max();
    for (const GoalPtr& g : goals_) min = std::min(min, g->heuristic(x, y, z));
    return min;
}

double GoalComposite::heuristic() const {
    double min = std::numeric_limits<double>::max();
    for (const GoalPtr& g : goals_) min = std::min(min, g->heuristic());
    return min;
}

std::string GoalComposite::toString() const {
    return std::format("GoalComposite{{{} goals}}", goals_.size());
}

std::string GoalComposite::key() const {
    UncensoredScope raw;
    std::string k = "Composite[";
    for (const GoalPtr& g : goals_) k += g->key() + ";";
    return k + "]";
}

std::string GoalInverted::toString() const {
    return std::format("GoalInverted{{{}}}", origin_->toString());
}

// ---- GoalThreeBlocks ----
double GoalThreeBlocks::heuristic(int x, int y, int z) const {
    int yDiff = y - pos_.y;
    if (yDiff < 0) yDiff = std::min(0, yDiff + 2);
    return GoalBlock::calculate(x - pos_.x, yDiff, z - pos_.z);
}

std::string GoalThreeBlocks::toString() const {
    return std::format("GoalThreeBlocks{{x={},y={},z={}}}", maybeCensor(pos_.x), maybeCensor(pos_.y), maybeCensor(pos_.z));
}

// ---- GoalRunAway ----
GoalRunAway::GoalRunAway(double distance, std::optional<int> maintainY, std::vector<BlockPos> from, bool never)
    : never_(never), from_(std::move(from)), distanceSq_(static_cast<int64_t>(distance * distance)), maintainY_(maintainY) {}

double GoalRunAway::heuristic() const {
    return never_ ? -std::numeric_limits<double>::infinity() : 0;
}

bool GoalRunAway::isInGoal(int x, int y, int z) const {
    if (never_) return false;
    if (maintainY_ && *maintainY_ != y) return false;
    for (const BlockPos& p : from_) {
        int64_t dx = x - p.x, dz = z - p.z;
        if (dx * dx + dz * dz < distanceSq_) return false;
    }
    return true;
}

double GoalRunAway::heuristic(int x, int y, int z) const {
    double min = std::numeric_limits<double>::max();
    for (const BlockPos& p : from_) min = std::min(min, GoalXZ::calculate(p.x - x, p.z - z));
    min = -min;
    if (maintainY_) min = min * 0.6 + GoalYLevel::calculate(*maintainY_, y) * 1.5;
    return min;
}

std::string GoalRunAway::toString() const {
    return std::format("GoalRunAway{{{} positions}}", from_.size());
}

std::string GoalRunAway::key() const {
    UncensoredScope raw;
    std::string k = std::format("RunAway{{{},{},{}", distanceSq_, maintainY_.value_or(-99999), never_);
    for (const BlockPos& p : from_) k += std::format("|{}", p);
    return k + "}";
}

// ---- GoalStrictDirection ----
GoalStrictDirection::GoalStrictDirection(const BlockPos& origin, Direction dir) : origin_(origin) {
    DirOffset o = offsetOf(dir);
    dx_ = o.x;
    dz_ = o.z;
}

double GoalStrictDirection::heuristic(int x, int y, int z) const {
    int along = (x - origin_.x) * dx_ + (z - origin_.z) * dz_;
    int across = std::abs((x - origin_.x) * dz_) + std::abs((z - origin_.z) * dx_);
    int vertical = std::abs(y - origin_.y);
    return -along * 100.0 + across * 1000.0 + vertical * 1000.0;
}

std::string GoalStrictDirection::toString() const {
    return std::format("GoalStrictDirection{{{},dx={},dz={}}}", origin_, dx_, dz_);
}

// ---- GoalAxis ----
bool GoalAxis::isInGoal(int x, int y, int z) const {
    return y == height_ && (x == 0 || z == 0 || std::abs(x) == std::abs(z));
}

double GoalAxis::heuristic(int x0, int y, int z0) const {
    int x = std::abs(x0);
    int z = std::abs(z0);
    int shrt = std::min(x, z);
    int lng = std::max(x, z);
    int diff = lng - shrt;
    double flatAxisDistance = std::min({static_cast<double>(x), static_cast<double>(z), diff * costs::SQRT_2});
    return flatAxisDistance * Settings::get().costHeuristic + GoalYLevel::calculate(height_, y);
}

std::string GoalAxis::toString() const {
    return std::format("GoalAxis{{y={}}}", height_);
}

}  // namespace baritone
