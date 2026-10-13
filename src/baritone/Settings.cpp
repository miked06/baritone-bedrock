#include "baritone/Settings.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <fstream>
#include <ranges>
#include <stdexcept>

namespace baritone {

namespace {

Settings g_defaults;

std::vector<SettingInfo> buildTable() {
    Settings& live = Settings::get();
    std::vector<SettingInfo> table;
#define BARITONE_SETTING_ENTRY(type, name, def, desc) \
    table.push_back({#name, desc, SettingPtr{&live.name}, SettingPtr{&g_defaults.name}});
    BARITONE_SETTINGS(BARITONE_SETTING_ENTRY)
#undef BARITONE_SETTING_ENTRY
    return table;
}

bool iequals(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b, [](char x, char y) { return std::tolower(x) == std::tolower(y); });
}

std::string normalizeBlockName(std::string_view name) {
    std::string out(name);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (out.find(':') == std::string::npos) out = "minecraft:" + out;
    return out;
}

}  // namespace

namespace {
thread_local int g_uncensored = 0;
}

bool shouldCensorCoordinates() { return g_uncensored == 0 && Settings::get().censorCoordinates; }
std::string maybeCensor(int coord) { return shouldCensorCoordinates() ? "<censored>" : std::to_string(coord); }
UncensoredScope::UncensoredScope() { ++g_uncensored; }
UncensoredScope::~UncensoredScope() { --g_uncensored; }

Settings& Settings::get() {
    static Settings live;
    return live;
}

std::span<const SettingInfo> settingsTable() {
    static const std::vector<SettingInfo> table = buildTable();
    return table;
}

const SettingInfo* findSetting(std::string_view name) {
    for (const SettingInfo& info : settingsTable()) {
        if (iequals(info.name, name)) return &info;
    }
    return nullptr;
}

std::string settingValueToString(const SettingPtr& ptr) {
    return std::visit(
        [](auto* p) -> std::string {
            using T = std::remove_pointer_t<decltype(p)>;
            if constexpr (std::is_same_v<T, bool>) return *p ? "true" : "false";
            else if constexpr (std::is_same_v<T, Vec3i>) return std::format("{},{},{}", p->x, p->y, p->z);
            else if constexpr (std::is_same_v<T, Color>) return std::format("{},{},{}", p->r, p->g, p->b);
            else if constexpr (std::is_same_v<T, std::string>) return *p;
            else if constexpr (std::is_same_v<T, BlockList> || std::is_same_v<T, StringList>) {
                std::string out;
                for (const auto& b : *p) out += (out.empty() ? "" : ",") + b;
                return out;
            } else return std::format("{}", *p);
        },
        ptr);
}

void parseSettingValue(const SettingInfo& info, std::string_view text) {
    std::visit(
        [&](auto* p) {
            using T = std::remove_pointer_t<decltype(p)>;
            if constexpr (std::is_same_v<T, bool>) {
                if (iequals(text, "true") || text == "1" || iequals(text, "on")) *p = true;
                else if (iequals(text, "false") || text == "0" || iequals(text, "off")) *p = false;
                else throw std::invalid_argument(std::format("'{}' n'est pas true/false", text));
            } else if constexpr (std::is_same_v<T, std::string>) {
                *p = std::string(text);
            } else if constexpr (std::is_same_v<T, StringList>) {
                StringList list;
                for (auto part : std::views::split(text, ',')) {
                    std::string_view s(part.begin(), part.end());
                    if (!s.empty()) list.emplace_back(s);
                }
                *p = std::move(list);
            } else if constexpr (std::is_same_v<T, Vec3i> || std::is_same_v<T, Color>) {
                int v[3];
                int n = 0;
                for (auto part : std::views::split(text, ',')) {
                    std::string_view s(part.begin(), part.end());
                    if (n == 3 || std::from_chars(s.data(), s.data() + s.size(), v[n]).ptr != s.data() + s.size() || s.empty()) {
                        throw std::invalid_argument(std::format("'{}' n'est pas de la forme x,y,z", text));
                    }
                    ++n;
                }
                if (n != 3) throw std::invalid_argument(std::format("'{}' n'est pas de la forme x,y,z", text));
                if constexpr (std::is_same_v<T, Color>) {
                    for (int c : v)
                        if (c < 0 || c > 255) throw std::invalid_argument(std::format("'{}' : couleur r,g,b de 0 a 255", text));
                    *p = Color{v[0], v[1], v[2]};
                } else {
                    *p = Vec3i{v[0], v[1], v[2]};
                }
            } else if constexpr (std::is_same_v<T, BlockList>) {
                BlockList list;
                for (auto part : std::views::split(text, ',')) {
                    std::string_view s(part.begin(), part.end());
                    if (!s.empty()) list.push_back(normalizeBlockName(s));
                }
                *p = std::move(list);
            } else {
                T value{};
                auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
                if (ec != std::errc{} || ptr != text.data() + text.size()) {
                    throw std::invalid_argument(std::format("'{}' n'est pas un nombre valide", text));
                }
                *p = value;
            }
        },
        info.ptr);
}

bool isModified(const SettingInfo& info) {
    return settingValueToString(info.ptr) != settingValueToString(info.defaultValue);
}

void saveSettings(const std::string& path) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return;
    for (const SettingInfo& info : settingsTable()) {
        if (isModified(info)) out << info.name << ' ' << settingValueToString(info.ptr) << '\n';
    }
}

int loadSettings(const std::string& path) {
    std::ifstream in(path);
    if (!in) return 0;
    int count = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto space = line.find(' ');
        std::string name = line.substr(0, space);
        std::string value = space == std::string::npos ? "" : line.substr(space + 1);
        const SettingInfo* info = findSetting(name);
        if (!info) continue;
        try {
            parseSettingValue(*info, value);
            ++count;
        } catch (const std::exception&) {
        }
    }
    return count;
}

void resetSetting(const SettingInfo& info) {
    std::visit(
        [&](auto* p) {
            using T = std::remove_pointer_t<decltype(p)>;
            *p = *std::get<T*>(info.defaultValue);
        },
        info.ptr);
}

}  // namespace baritone
