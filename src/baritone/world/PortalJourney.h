#pragma once
#include <optional>
#include "baritone/world/PortalGraph.h"
#include "baritone/task/TaskQueue.h"
namespace baritone {
// Route intent survives only a confirmed link in the same world. No reconnect
// resume: a lost LocalPlayer or a manual stop cancels the intent.
class PortalJourney {
public:
    void start(const PortalGraph& graph, WorldIdentity identity, BlockPos start, const std::string& destination);
    std::optional<BlockPos> step(const WorldIdentity& identity, BlockPos feet, bool portalPresent);
    bool transition(const WorldIdentity& identity, BlockPos feet);
    void cancel() { active_ = armed_ = false; }
    bool active() const { return active_; }
    bool awaitingTransition() const { return armed_; }
    const std::string& failure() const { return failure_; }
private:
    PortalGraph graph_;
    WorldIdentity identity_;
    PortalGraph::Route route_;
    std::size_t next_ = 0;
    bool active_ = false, armed_ = false;
    unsigned ticks_ = 0, legTicks_ = 0, cooldown_ = 0;
    std::string failure_;
    void fail(std::string reason) { failure_ = std::move(reason); cancel(); }
};
}
