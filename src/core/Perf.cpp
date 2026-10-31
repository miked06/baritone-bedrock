#include "core/Perf.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <format>
#include <mutex>

namespace perf {
namespace {
constexpr std::size_t capacity = 4096;
struct Samples {
    std::array<double, capacity> values{};
    std::size_t size = 0, next = 0;
    uint64_t total = 0;
};
std::atomic<bool> active{false};
std::mutex mutex;
std::array<Samples, static_cast<std::size_t>(Metric::Count)> samples;
constexpr const char* names[] = {"Frame interval", "Game update", "Bot frame", "Bot tick", "World scan", "Local refresh", "Core tick", "Schematic preview", "Overlay render", "Background schematic load"};
}
bool enabled() { return active.load(std::memory_order_relaxed); }
void setEnabled(bool value) { active.store(value, std::memory_order_relaxed); }
void reset() { std::lock_guard lock(mutex); samples = {}; }
void record(Metric metric, double milliseconds) {
    if (!std::isfinite(milliseconds) || milliseconds < 0) return;
    std::lock_guard lock(mutex);
    auto& bucket = samples.at(static_cast<std::size_t>(metric));
    bucket.values[bucket.next] = milliseconds;
    bucket.next = (bucket.next + 1) % capacity;
    bucket.size = std::min(capacity, bucket.size + 1);
    ++bucket.total;
}
void markFrame() {
    thread_local std::chrono::steady_clock::time_point last{};
    if (!enabled()) { last = {}; return; }
    const auto now = std::chrono::steady_clock::now();
    if (last != std::chrono::steady_clock::time_point{}) record(Metric::FrameInterval, std::chrono::duration<double, std::milli>(now - last).count());
    last = now;
}
std::vector<std::string> report() {
    std::vector<std::string> lines;
    lines.push_back(std::format("Performance capture {} (last {} samples per metric, milliseconds; frame interval includes Minecraft)", enabled() ? "on" : "off", capacity));
    std::lock_guard lock(mutex);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto& bucket = samples[i];
        if (!bucket.size) continue;
        std::vector<double> sorted(bucket.values.begin(), bucket.values.begin() + bucket.size);
        std::ranges::sort(sorted);
        double sum = 0;
        for (double value : sorted) sum += value;
        auto percentile = [&](double q) { return sorted[static_cast<std::size_t>(std::ceil(q * sorted.size())) - 1]; };
        lines.push_back(std::format("{}: n={} mean={:.3f} p95={:.3f} p99={:.3f} max={:.3f}", names[i], bucket.total,
            sum / sorted.size(), percentile(0.95), percentile(0.99), sorted.back()));
    }
    return lines;
}
}
