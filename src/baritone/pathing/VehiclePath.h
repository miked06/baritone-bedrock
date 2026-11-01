#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "baritone/world/WorldSnapshot.h"
namespace baritone {
enum class VehicleKind { Boat, Horse };
struct VehiclePath { std::vector<BlockPos> points; std::string error; };
bool vehicleNodeClear(const WorldSnapshot& world,BlockPos p,VehicleKind kind);
VehiclePath findVehiclePath(const WorldSnapshot& world,BlockPos from,BlockPos to,VehicleKind kind,const std::atomic<bool>* cancel=nullptr);
}
