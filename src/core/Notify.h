#pragma once
#include <string_view>

// Windows notifications (Baritone's NotificationHelper uses the system tray on Windows too):
// a balloon from a tray icon owned by the game window.
namespace notify {

void show(std::string_view title, std::string_view message, bool error);
// Removes the tray icon (unload).
void shutdown();

}  // namespace notify
