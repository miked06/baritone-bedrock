#pragma once

#include <vector>

#include "baritone/process/Process.h"

namespace baritone {

class RailProcess : public Process {
public:
    using Process::Process;
    void start(BlockPos from, BlockPos to);
    bool isActive() override;
    void onLostControl() override;
    double priority() const override { return 3.9; }
    void customizeContext(CalculationContext& context) override;
    std::optional<PathingCommand> onTick(bool failed, bool safe) override;
    std::string displayName() const override;

private:
    enum class Phase { Idle, Board, Ride, Exit };
    Phase phase_ = Phase::Idle;
    std::vector<BlockPos> path_;
    std::size_t current_ = 0;
    uint64_t target_ = 0;
    Vec3d previous_;
    int ticks_ = 0, stationary_ = 0, offRoute_ = 0, exitTicks_ = 0;
    bool interacted_ = false;
};

}  // namespace baritone
