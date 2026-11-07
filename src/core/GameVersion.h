#pragma once

namespace gameversion {
inline constexpr char supported[] = "26.63 (package 1.26.6301.0)";
bool validateNativeProfile();
// Check before reading game objects or installing hooks: signatures alone do not
// establish that offsets, vtable indices, or calling conventions still match.
bool check();
}
