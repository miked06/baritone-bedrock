#pragma once
#include <deque>
#include <cstdint>
#include <string>
#include <string_view>
namespace baritone {
struct Diagnostic {
    uint64_t sequence = 0;
    std::string code, message;
    bool error = false;
};
class Diagnostics {
public:
    void record(std::string message, bool error) {
        std::string code = "status";
        if (error) {
            code = "task_failure";
            if (message.find("Missing materials") != std::string::npos || message.find("missing item") != std::string::npos) code = "missing_materials";
            else if (message.find("No path") != std::string::npos || message.find("no path") != std::string::npos) code = "no_path";
            else if (message.find("protected") != std::string::npos) code = "protected_area";
            else if (message.find("refused") != std::string::npos) code = "request_rejected";
            else if (message.find("time") != std::string::npos) code = "timeout";
            else if (message.find("allowBreak") != std::string::npos || message.find("allowPlace") != std::string::npos) code = "permission";
        }
        events_.push_back({++sequence_, std::move(code), message.substr(0, 4096), error});
        if (events_.size() > 64) events_.pop_front();
    }
    uint64_t sequence() const { return sequence_; }
    const std::deque<Diagnostic>& events() const { return events_; }
    std::string errorsAfter(uint64_t sequence) const {
        std::string result;
        for (const auto& event : events_) if (event.error && event.sequence > sequence) {
            if (!result.empty()) result += "; ";
            result += event.code + ": " + event.message;
        }
        return result.substr(0, 4096);
    }
private:
    uint64_t sequence_ = 0;
    std::deque<Diagnostic> events_;
};
}
