#pragma once
#include <chrono>
#include <future>
#include <optional>
#include <vector>

#include "baritone/cache/WorldScanner.h"

namespace baritone {

// A block search running on a worker thread (Baritone runs its rescans on its executor).
class AsyncScan {
public:
    ~AsyncScan() { wait(); }

    bool running() const { return future_.valid(); }

    void start(std::shared_ptr<const WorldSnapshot> world, BlockFilter filter, BlockPos center, std::size_t max,
               int chunkRadius) {
        if (running() || !world) return;
        future_ = std::async(std::launch::async, [world = std::move(world), filter = std::move(filter), center, max,
                                                  chunkRadius] {
            return searchWorld(*world, filter, center, max, chunkRadius);
        });
    }

    // The result once the scan is done.
    std::optional<std::vector<BlockPos>> poll() {
        if (!running() || future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return std::nullopt;
        return future_.get();
    }

    void wait() {
        if (running()) future_.wait();
        future_ = {};
    }

private:
    std::future<std::vector<BlockPos>> future_;
};

}  // namespace baritone
