#pragma once
#include <functional>
#include <string>

namespace baritone {

// Where the core writes developer traces: the DLL sends them to log.txt, the tests nowhere.
inline std::function<void(const std::string&)>& debugSink() {
    static std::function<void(const std::string&)> sink;
    return sink;
}

inline void debugLog(const std::string& line) {
    if (auto& sink = debugSink()) sink(line);
}

}  // namespace baritone
