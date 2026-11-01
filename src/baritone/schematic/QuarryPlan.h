#pragma once
#include "baritone/schematic/Schematic.h"
#include "baritone/selection/SelectionManager.h"
#include "baritone/world/WorldSnapshot.h"
namespace baritone {
// Leaves a solid descending perimeter ramp. Existing terrain below each step
// remains as support; the inner volume is excavated to the selected depth.
class QuarryPlan : public Schematic {
public:
    QuarryPlan(Selection area, bool lighting);
    BlockPos size() const override { return size_; }
    DesiredBlock desired(int x,int y,int z,const BlockProps& current) const override;
    int rampFloor(int x,int z) const;
    bool mayBreak(int x,int y,int z) const override { return y>rampFloor(x,z); }
    BlockPos origin() const { return origin_; }
private:
    BlockPos origin_, size_;
    bool lighting_;
};
class QuarryPreflight {
public:
    void start(std::shared_ptr<const QuarryPlan> plan);
    bool step(const WorldSnapshot& world, std::size_t budget = 1024);
    bool ready() const { return ready_; }
    const std::string& failure() const { return failure_; }
    uint64_t blocks() const { return blocks_; }
    const auto& plan() const { return plan_; }
private:
    std::shared_ptr<const QuarryPlan> plan_;
    uint64_t cursor_ = 0, blocks_ = 0;
    bool ready_ = false;
    std::string failure_;
};
}
