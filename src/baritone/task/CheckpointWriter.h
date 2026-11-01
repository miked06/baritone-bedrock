#pragma once
#include <chrono>
#include <fstream>
#include <future>
#include <map>
#include <optional>
#include "baritone/task/TaskQueue.h"
namespace baritone {
// File writes happen off the game thread. Pending snapshots are coalesced per
// destination; shutdown drains them before the DLL can be unloaded.
class CheckpointWriter {
public:
    ~CheckpointWriter() { wait(); }
    void submit(std::filesystem::path path, std::string content) {
        if (!pending_.contains(path) && pending_.size() >= 32) throw std::runtime_error("Too many pending checkpoint destinations");
        pending_[std::move(path)] = std::move(content);
        pump();
    }
    std::optional<std::string> poll() {
        std::optional<std::string> error;
        if (job_.valid() && job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try { job_.get(); } catch (const std::exception& e) { error = e.what(); }
        }
        pump(); return error;
    }
    void wait() noexcept {
        while (job_.valid() || !pending_.empty()) {
            if (job_.valid()) job_.wait();
            try { poll(); } catch (...) { pending_.clear(); }
        }
    }
private:
    void pump() {
        if (job_.valid() || pending_.empty()) return;
        auto entry = pending_.begin();
        auto path = entry->first; auto content = std::move(entry->second); pending_.erase(entry);
        job_ = std::async(std::launch::async, [path = std::move(path), content = std::move(content)] {
            std::filesystem::create_directories(path.parent_path());
            auto tmp = path; tmp += ".checkpoint.tmp";
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out.write(content.data(), content.size())) throw std::runtime_error("Could not write task checkpoint");
            out.close(); if (!out) throw std::runtime_error("Could not close task checkpoint");
            std::filesystem::rename(tmp, path);
        });
    }
    std::map<std::filesystem::path, std::string> pending_;
    std::future<void> job_;
};
}
