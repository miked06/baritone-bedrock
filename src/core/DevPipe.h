#pragma once
#include <optional>
#include <string>

// Development channel: tools/cmd.py writes "#command" lines to \\.\pipe\BaritoneBedrock_<pid>
// and they run as if typed in the chat. Lets the bot be tested from a terminal.
namespace devpipe {

void start();
void stop();

// Next pending command, consumed on the game thread.
std::optional<std::string> poll();

}  // namespace devpipe
