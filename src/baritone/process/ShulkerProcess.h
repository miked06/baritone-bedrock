#pragma once

#include "baritone/api/PlayerContext.h"
#include "baritone/process/Process.h"

namespace baritone {

class ShulkerProcess : public Process {
public:
    using Process::Process;
    void start(int slot, std::string item, int count, BlockPos position);
    bool isActive() override;
    void onLostControl() override;
    double priority() const override { return 3.9; }
    void customizeContext(CalculationContext& context) override;
    std::optional<PathingCommand> onTick(bool failed, bool safe) override;
    std::string displayName() const override;

private:
    enum class Phase { Idle, Place, Confirm, Open, Transfer, Receipt, Close, Break, Pickup };
    Phase phase_ = Phase::Idle;
    PortableShulker expected_;
    PortableShulker afterTransfer_;
    BlockPos position_{};
    std::string item_;
    int slot_ = 0, previousSlot_ = 0, remaining_ = 0, ticks_ = 0, sent_ = 0, inventoryBefore_ = 0;
    bool leftInWorld_ = false;
    void advance(Phase next) { phase_ = next; ticks_ = 0; }
};

}
