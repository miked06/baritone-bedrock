#pragma once
#include <chrono>
#include <future>
#include <optional>
#include "baritone/schematic/Schematic.h"
namespace baritone {
// File I/O, decompression and palette conversion never touch live game objects.
// Cancellation discards the result; explicit shutdown joins before DLL unload.
class SchematicLoadJob {
public:
    struct Result { SchematicPtr schematic; std::string error; double milliseconds = 0; bool cancelled = false; };
    ~SchematicLoadJob() { wait(); }
    bool running() const { return future_.valid(); }
    bool start(std::string path) {
        if (running()) return false;
        cancelled_ = false;
        future_ = std::async(std::launch::async, [path = std::move(path)] {
            Result result;
            const auto start = std::chrono::steady_clock::now();
            try { result.schematic = loadSchematic(path); }
            catch (const std::exception& error) { result.error = error.what(); }
            result.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            return result;
        });
        return true;
    }
    void cancel() { cancelled_ = true; }
    std::optional<Result> poll() {
        if (!running() || future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return std::nullopt;
        auto result = future_.get();
        result.cancelled = cancelled_;
        return result;
    }
    void wait() { if (running()) future_.wait(); future_ = {}; }
private:
    std::future<Result> future_;
    bool cancelled_ = false;
};
}
