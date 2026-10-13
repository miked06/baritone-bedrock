#pragma once
#include <functional>
#include <optional>
#include <vector>

#include "baritone/api/PlayerContext.h"
#include "baritone/process/Process.h"

namespace baritone {

// #follow players | player <names> | entities | entity <types> (Baritone's FollowProcess).
class FollowProcess : public Process {
public:
    using Filter = std::function<bool(const EntityInfo&)>;
    using Process::Process;

    void follow(Filter filter, std::string description);
    void pickup(std::function<bool(const std::string& item)> items, std::string description);
    bool isActive() override;
    std::optional<PathingCommand> onTick(bool calcFailed, bool isSafeToCancel) override;
    void onLostControl() override {
        filter_ = nullptr;
        cache_.clear();
    }
    std::string displayName() const override { return "Suivre " + description_; }
    const std::vector<EntityInfo>& following() const { return cache_; }

private:
    void scanWorld();
    GoalPtr towards(const EntityInfo& e) const;

    Filter filter_;
    bool into_ = false;  // walk straight into the target (pickup)
    std::string description_;
    std::vector<EntityInfo> cache_;
};

}  // namespace baritone
