#include "baritone/utils/BlockSelector.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace baritone {
namespace {
std::string trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return std::string(text);
}
}
BlockSelector BlockSelector::parse(std::string_view text) {
    std::string input = trim(text);
    std::ranges::transform(input, input.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto bracket = input.find('[');
    BlockSelector selector;
    selector.name = trim(std::string_view(input).substr(0, bracket));
    if (selector.name.empty() || selector.name.find_first_of(" \t\r\n],=") != std::string::npos)
        throw std::invalid_argument("Invalid block selector: " + input);
    if (selector.name.find(':') == std::string::npos) selector.name = "minecraft:" + selector.name;
    if (bracket == std::string::npos) return selector;
    if (input.back() != ']' || input.find('[', bracket + 1) != std::string::npos || input.find(']') != input.size() - 1)
        throw std::invalid_argument("Invalid block selector: " + input);
    std::string_view properties(input.data() + bracket + 1, input.size() - bracket - 2);
    if (properties.empty()) return selector;
    while (true) {
        const auto comma = properties.find(',');
        const auto pair = properties.substr(0, comma);
        const auto equals = pair.find('=');
        if (equals == std::string::npos || pair.find('=', equals + 1) != std::string::npos)
            throw std::invalid_argument("Expected state=value in block selector: " + input);
        std::string key = trim(pair.substr(0, equals)), value = trim(pair.substr(equals + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
        if (key.empty() || value.empty()) throw std::invalid_argument("Empty block state or value: " + input);
        if (std::ranges::any_of(selector.states, [&](const auto& state) { return state.first == key; }))
            throw std::invalid_argument("Duplicate block state: " + key);
        // Bool-valued states are exposed as NBT bytes by the platform.
        if (value == "true") value = "1";
        else if (value == "false") value = "0";
        selector.states.emplace_back(std::move(key), std::move(value));
        if (comma == std::string::npos) break;
        properties.remove_prefix(comma + 1);
    }
    return selector;
}
bool BlockSelector::matches(const BlockProps& block) const {
    return name == block.name && std::ranges::all_of(states, [&](const auto& state) {
        return block.state(state.first) == state.second;
    });
}
std::string BlockSelector::toString() const {
    std::string result = name;
    if (!states.empty()) {
        result += '[';
        for (const auto& [key, value] : states) {
            if (result.back() != '[') result += ',';
            result += key + '=' + value;
        }
        result += ']';
    }
    return result;
}
}
