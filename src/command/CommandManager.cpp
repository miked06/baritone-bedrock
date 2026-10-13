#include "command/CommandManager.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <ranges>
#include <stdexcept>

#include "baritone/Settings.h"
#include "core/Chat.h"
#include "core/Log.h"

namespace command {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
    return s;
}

std::string lower(std::string_view s) {
    std::string out(s);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}  // namespace

void CommandManager::add(Command command) {
    commands_.push_back(std::move(command));
}

const Command* CommandManager::find(std::string_view name) const {
    std::string key = lower(name);
    for (const Command& cmd : commands_) {
        if (std::ranges::find(cmd.names, key) != cmd.names.end()) return &cmd;
    }
    return nullptr;
}

bool CommandManager::execute(std::string_view line) {
    if (line.empty() || line.front() != kPrefix) return false;
    line.remove_prefix(1);
    if (!runCommand(line) && !line.empty()) {
        chat::error(std::format("Commande inconnue : {}. Tape #help", line.substr(0, line.find(' '))));
    }
    return true;
}

bool CommandManager::onChat(std::string_view msg) {
    const baritone::Settings& s = baritone::Settings::get();
    if (msg.empty() || msg.front() == '/') return false;  // vanilla commands
    if (s.prefixControl && !s.prefix.empty() && msg.starts_with(s.prefix)) {
        std::string_view commandStr = msg.substr(s.prefix.size());
        if (!runCommand(commandStr) && !trim(commandStr).empty()) {
            chat::error(std::format("Commande inconnue : {}. Tape {}help", commandStr.substr(0, commandStr.find(' ')), s.prefix));
        }
        return true;
    }
    if (s.chatControl || s.chatControlAnyway) return runCommand(msg);
    return false;
}

// Java logRanCommand: "> command args" (censorRanCommands: "> command ...")
void CommandManager::logRanCommand(std::string_view command, std::string_view rest) {
    const baritone::Settings& s = baritone::Settings::get();
    if (!s.echoCommands) return;
    chat::info(std::format("> {}{}", command, s.censorRanCommands ? std::string(" ...") : std::string(rest)));
}

bool CommandManager::runCommand(std::string_view msg, bool echo) {
    msg = trim(msg);
    if (msg.empty()) return runCommand("help", echo);
    Args words;
    for (auto part : std::views::split(msg, ' ')) {
        if (!std::ranges::empty(part)) words.emplace_back(std::ranges::begin(part), std::ranges::end(part));
    }
    const std::string& name = words.front();
    std::string_view rest = msg.substr(std::min(msg.size(), name.size()));
    Args args(words.begin() + 1, words.end());

    // a setting name alone shows it (a boolean is toggled), with one value sets it
    if (const baritone::SettingInfo* setting = baritone::findSetting(name); setting && args.size() <= 1 && !find(name)) {
        if (echo) logRanCommand(name, rest);
        if (args.empty()) {
            if (std::holds_alternative<bool*>(setting->ptr)) return runCommand(std::format("set toggle {}", setting->name), false);
            return runCommand(std::format("set {}", setting->name), false);
        }
        return runCommand(std::format("set {} {}", setting->name, args[0]), false);
    }

    const Command* cmd = find(name);
    if (!cmd) return false;
    if (echo) logRanCommand(name, rest);
    logger::info("Command: {}", msg);
    try {
        cmd->run(args);
    } catch (const CommandError& e) {
        chat::error(e.what());
    } catch (const std::exception& e) {
        logger::error("Command {} threw: {}", name, e.what());
        chat::error(baritone::Settings::get().verboseCommandExceptions ? std::format("Erreur interne : {}", e.what())
                                                                      : "Erreur interne (details dans log.txt)");
    }
    return true;
}

int parseInt(const std::string& s, std::string_view what) {
    int value = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || ptr != s.data() + s.size()) {
        throw CommandError(std::format("{} invalide : '{}'", what, s));
    }
    return value;
}

double parseDouble(const std::string& s, std::string_view what) {
    double value = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || ptr != s.data() + s.size()) {
        throw CommandError(std::format("{} invalide : '{}'", what, s));
    }
    return value;
}

int parseCoord(const std::string& s, int base, std::string_view what) {
    if (!s.empty() && s.front() == '~') {
        return s.size() == 1 ? base : base + parseInt(s.substr(1), what);
    }
    return parseInt(s, what);
}

}  // namespace command
