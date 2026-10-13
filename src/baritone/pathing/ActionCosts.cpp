#include "baritone/pathing/ActionCosts.h"

namespace baritone::costs {

namespace {

double velocity(int ticks) {
    return (std::pow(0.98, ticks) - 1) * -3.92;
}

}  // namespace

double distanceToTicks(double distance) {
    if (distance == 0) return 0;
    double remaining = distance;
    int ticks = 0;
    while (true) {
        double fall = velocity(ticks);
        if (remaining <= fall) return ticks + remaining / fall;
        remaining -= fall;
        ++ticks;
    }
}

const std::array<double, 4097>& fallNBlocksCost() {
    static const std::array<double, 4097> table = [] {
        std::array<double, 4097> t{};
        for (int i = 0; i < 4097; ++i) t[i] = distanceToTicks(i);
        return t;
    }();
    return table;
}

const double FALL_1_25_BLOCKS_COST = distanceToTicks(1.25);
const double FALL_0_25_BLOCKS_COST = distanceToTicks(0.25);
const double JUMP_ONE_BLOCK_COST = FALL_1_25_BLOCKS_COST - FALL_0_25_BLOCKS_COST;

}  // namespace baritone::costs
