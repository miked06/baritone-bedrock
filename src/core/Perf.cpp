#include "core/Perf.h"

#include <Windows.h>
#include <Psapi.h>

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
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), memory.cb)) {
        constexpr double mib = 1024.0 * 1024.0;
        lines.push_back(std::format("Minecraft process: {:.1f} MiB working set, {:.1f} MiB private, {:.1f} MiB peak working set",
                                    memory.WorkingSetSize / mib, memory.PrivateUsage / mib, memory.PeakWorkingSetSize / mib));
    }
    // A full machine makes Windows page the game out: frames slow down whatever the bot does.
    MEMORYSTATUSEX system{};
    system.dwLength = sizeof(system);
    if (GlobalMemoryStatusEx(&system)) {
        constexpr double gib = 1024.0 * 1024.0 * 1024.0;
        const double freeRam = system.ullAvailPhys / gib, totalRam = system.ullTotalPhys / gib;
        const double commitUsed = (system.ullTotalPageFile - system.ullAvailPageFile) / gib, commitLimit = system.ullTotalPageFile / gib;
        lines.push_back(std::format("System: {:.1f} GiB of {:.1f} GiB RAM free, commit {:.1f}/{:.1f} GiB", freeRam, totalRam,
                                    commitUsed, commitLimit));
        if (system.ullAvailPhys < system.ullTotalPhys / 10 || system.ullAvailPageFile < system.ullTotalPageFile / 20)
            lines.push_back("Warning: Windows is low on memory. The game slows down while it pages to disk; close other "
                            "programs or lower the render distance.");
    }

    for (std::size_t i = 0; i < samples.size(); ++i) {
        std::vector<double> sorted;
        sorted.reserve(capacity);
        uint64_t total = 0;
        {
            std::lock_guard lock(mutex);
            const auto& bucket = samples[i];
            sorted.assign(bucket.values.begin(), bucket.values.begin() + bucket.size);
            total = bucket.total;
        }
        if (sorted.empty()) continue;
        std::ranges::sort(sorted);
        double sum = 0;
        for (double value : sorted) sum += value;
        auto percentile = [&](double q) { return sorted[static_cast<std::size_t>(std::ceil(q * sorted.size())) - 1]; };
        lines.push_back(std::format("{}: n={} mean={:.3f} p95={:.3f} p99={:.3f} max={:.3f}", names[i], total,
            sum / sorted.size(), percentile(0.95), percentile(0.99), sorted.back()));
    }
    bool none;
    {
        std::lock_guard lock(mutex);
        none = std::ranges::all_of(samples, [](const Samples& bucket) { return bucket.size == 0; });
    }
    if (!enabled() && none)
        lines.push_back("No timing samples: type #perf on, play until it slows down, then #perf");
    return lines;
}
}
