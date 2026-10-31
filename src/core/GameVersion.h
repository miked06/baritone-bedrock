#pragma once

namespace gameversion {
inline constexpr char supported[] = "26.60 (package 1.26.6005.0)";
// Check before reading game objects or installing hooks: signatures alone do not
// establish that offsets, vtable indices, or calling conventions still match.
bool check();
}
