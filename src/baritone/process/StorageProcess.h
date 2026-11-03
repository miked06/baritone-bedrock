#pragma once
#include <filesystem>
#include <map>
#include <deque>
#include "baritone/api/PlayerContext.h"
#include "baritone/process/Process.h"
namespace baritone {
struct StorageObservation {
    std::map<std::string, int> items;
    int64_t observedAt = 0;
    std::size_t slots = 0;
};
class StorageProcess : public Process {
public:
    using Process::Process;
    void addLocation(std::string name, BlockPos position);
    void removeLocation(const std::string& name);
    const std::map<std::string, BlockPos>& locations() const { return locations_; }
    void load(const std::filesystem::path& file);
    void save(const std::filesystem::path& file) const;
    void start(const std::string& location, std::string item, int count, bool withdraw, bool returnToStart = true);
    void scan(const std::string& location, bool returnToStart = true);
    void sort(const std::string& location, bool returnToStart = true);
    const std::map<std::string, StorageObservation>& observations() const { return observations_; }
    bool isActive() override;
    bool isTemporary() const override { return true; }
    double priority() const override { return 2; }
    std::optional<PathingCommand> onTick(bool failed, bool safe) override;
    void onLostControl() override;
    std::string displayName() const override;
    bool succeeded() const { return succeeded_; }
    const std::string& failure() const { return failure_; }
private:
    enum class Mode { Transfer, Scan, Sort } mode_ = Mode::Transfer;
    struct PlannedMove { ContainerSlot source, target; int count = 0; };
    void begin(const std::string& location, bool returnToStart);
    bool observe(const std::vector<ContainerSlot>& slots);
    bool issue(const ContainerSlot& source, const ContainerSlot& target, int count);
    bool observedTransfer(const std::vector<ContainerSlot>& slots) const;
    std::optional<PlannedMove> sortMove(const std::vector<ContainerSlot>& slots);
    std::map<std::string, StorageObservation> observations_;
    std::deque<PlannedMove> sortMoves_;
    ContainerSlot sentSource_, sentTarget_;
    int sourceAfter_ = 0, targetAfter_ = 0, operations_ = 0, warmup_ = 0;
    bool refreshAttempted_ = false, refreshClosing_ = false;
    std::map<std::string, BlockPos> locations_;
    bool active_ = false, withdraw_ = false, return_ = true, waiting_ = false, closing_ = false, succeeded_ = false;
    std::string item_, location_, failure_;
    BlockPos target_{}, origin_{};
    int remaining_ = 0, requested_ = 0, sent_ = 0, timeout_ = 0;
};
}
