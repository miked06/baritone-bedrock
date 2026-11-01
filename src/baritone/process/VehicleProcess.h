#pragma once
#include <future>
#include "baritone/process/Process.h"
#include "baritone/pathing/VehiclePath.h"
namespace baritone {
class VehicleProcess : public Process {
public:
    using Process::Process;
    ~VehicleProcess() { shutdown(); }
    void board(VehicleKind kind);
    void navigate(VehicleKind kind,BlockPos destination);
    void dismount();
    void shutdown();
    bool isActive() override { return mode_!=Mode::Idle; }
    void onLostControl() override { cancel_=true; mode_=Mode::Idle; path_.clear(); }
    std::optional<PathingCommand> onTick(bool failed,bool safe) override;
    std::string displayName() const override;
private:
    enum class Mode { Idle, Board, Navigate, Dismount } mode_=Mode::Idle;
    VehicleKind kind_=VehicleKind::Boat;
    std::future<VehiclePath> calculation_;
    std::atomic<bool> cancel_{false};
    std::vector<BlockPos> path_;
    std::size_t next_=0;
    uint64_t target_=0;
    int ticks_=0, stuck_=0;
    Vec3d last_;
};
}
