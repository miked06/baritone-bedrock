#pragma once
#include <vector>
#include "baritone/process/Process.h"
#include "baritone/selection/SelectionManager.h"
#include "baritone/world/WorldSnapshot.h"
namespace baritone {
struct TreePlan {
    std::string log, sapling;
    std::vector<BlockPos> logs, roots;
};
std::optional<TreePlan> identifyTree(const WorldSnapshot& world, const Selection& selection, BlockPos root);
class TreeProcess : public Process {
public:
    using Process::Process;
    void start(Selection selection, int count);
    bool isActive() override { return active_; }
    void onLostControl() override { active_=false; plan_.reset(); }
    std::optional<PathingCommand> onTick(bool failed, bool safe) override;
    std::string displayName() const override;
private:
    Selection selection_;
    uint64_t cursor_=0;
    std::optional<TreePlan> plan_;
    int requested_=0, completed_=0, waiting_=0, timeout_=0;
    bool active_=false;
};
}
