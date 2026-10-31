#pragma once
#include <cctype>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace baritone {
// Quotes at the beginning of an argument group filenames with spaces. Embedded
// block-state quotes and Windows path separators are preserved verbatim.
inline std::vector<std::string> commandArguments(std::string_view line, bool incomplete = false) {
    std::vector<std::string> result;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i == line.size()) break;
        std::string word;
        if (line[i] == '"') {
            ++i;
            bool closed = false;
            while (i < line.size()) {
                const char c = line[i++];
                if (c == '"') { closed = true; break; }
                word += c;
            }
            if (!closed && !incomplete) throw std::invalid_argument("Unclosed quoted argument");
            if (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i])))
                throw std::invalid_argument("Expected a space after a quoted argument");
        } else {
            while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) word += line[i++];
        }
        result.push_back(std::move(word));
    }
    return result;
}
} // namespace baritone
