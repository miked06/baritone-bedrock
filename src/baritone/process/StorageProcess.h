#pragma once
#include <filesystem>
#include <map>
#include "baritone/process/Process.h"
namespace baritone {
class StorageProcess : public Process {
public:
    using Process::Process;
    void addLocation(std::string name, BlockPos position);
    void removeLocation(const std::string& name);
    const std::map<std::string, BlockPos>& locations() const { return locations_; }
    void load(const std::filesystem::path& file);
    void save(const std::filesystem::path& file) const;
    void start(const std::string& location, std::string item, int count, bool withdraw, bool returnToStart = true);
    bool isActive() override;
    bool isTemporary() const override { return true; }
    double priority() const override { return 2; }
    std::optional<PathingCommand> onTick(bool failed, bool safe) override;
    void onLostControl() override;
    std::string displayName() const override;
    bool succeeded() const { return succeeded_; }
    const std::string& failure() const { return failure_; }
private:
    std::map<std::string, BlockPos> locations_;
    bool active_ = false, withdraw_ = false, return_ = true, waiting_ = false, closing_ = false, succeeded_ = false;
    std::string item_, location_, failure_;
    BlockPos target_{}, origin_{};
    int remaining_ = 0, requested_ = 0, sent_ = 0, timeout_ = 0;
};
}
