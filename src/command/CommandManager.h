#pragma once
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace command {

// Arguments after the command name, split on spaces.
using Args = std::vector<std::string>;

struct Command {
    std::vector<std::string> names;  // first is the main name, the rest are aliases
    std::string usage;               // e.g. "goto <x> <y> <z>"
    std::string description;
    std::function<void(const Args&)> run;
};

// Thrown by commands for user errors; the message is shown in chat in red.
struct CommandError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class CommandManager {
public:
    static constexpr char kPrefix = '#';

    void add(Command command);
    const std::vector<Command>& commands() const { return commands_; }
    const Command* find(std::string_view name) const;

    // A chat message (Java ExampleBaritoneControl.onSendChatMessage): with prefixControl,
    // "<prefix>command" runs the command; with chatControl(Anyway), a message that is a
    // command or a setting runs too. Returns true when the message was used (not sent).
    bool onChat(std::string_view message);
    // Java runCommand: a command line without prefix ("goto 1 2 3", "allowBreak false",
    // "allowBreak"). Returns false if it is neither a command nor a setting.
    // echo: Java logRanCommand (internal re-runs like aliases don't echo again)
    bool runCommand(std::string_view line, bool echo = true);
    // Lines from the dev pipe: "#command" (the prefix setting doesn't apply).
    bool execute(std::string_view line);

private:
    void logRanCommand(std::string_view command, std::string_view rest);
    std::vector<Command> commands_;
};

// Helpers for argument parsing; throw CommandError with a readable message.
int parseInt(const std::string& s, std::string_view what);
double parseDouble(const std::string& s, std::string_view what);
// Relative coordinates ("~", "~5") are resolved against `base`.
int parseCoord(const std::string& s, int base, std::string_view what);

}  // namespace command
