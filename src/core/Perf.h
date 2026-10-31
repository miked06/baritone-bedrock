#pragma once
#include <chrono>
#include <string>
#include <vector>

namespace perf {
enum class Metric { FrameInterval, GameUpdate, BotFrame, BotTick, WorldScan, LocalRefresh, CoreTick, Preview, Overlay, BackgroundLoad, Count };
bool enabled();
void setEnabled(bool value);
void reset();
void record(Metric metric, double milliseconds);
void markFrame();
std::vector<std::string> report();
class Scope {
public:
    explicit Scope(Metric metric) : metric_(metric), active_(enabled()) {
        if (active_) start_ = std::chrono::steady_clock::now();
    }
    ~Scope() {
        if (active_) record(metric_, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count());
    }
private:
    Metric metric_;
    bool active_;
    std::chrono::steady_clock::time_point start_;
};
}
