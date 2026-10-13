#pragma once
#include <array>
#include <cstddef>

namespace sdk {
struct MoveInputComponent;
struct MoveInputState;
}  // namespace sdk

// Equivalent of Baritone's InputOverrideHandler: the bot "presses" keys by forcing the raw
// key state right before the game's input system reads it, then restores the real state
// so the player's own keys are not left stuck.
enum class Key : std::size_t {
    MoveForward,
    MoveBack,
    MoveLeft,
    MoveRight,
    Jump,
    Sneak,
    Sprint,
    Count,
};

class InputOverride {
public:
    void set(Key input, bool pressed) { forced_[static_cast<std::size_t>(input)] = pressed; }
    bool get(Key input) const { return forced_[static_cast<std::size_t>(input)]; }
    void clear() { forced_.fill(false); }
    bool any() const;

    // When false, the real keyboard is ignored while the bot is in control
    // (Baritone blocks player movement keys while pathing).
    void setControlling(bool controlling) { controlling_ = controlling; }
    bool controlling() const { return controlling_; }

    // Called around ClientInputUpdateSystem::tickUpdateClientInput.
    void beforeInputTick(sdk::MoveInputComponent& input);
    void afterInputTick(sdk::MoveInputComponent& input);
    std::size_t inputTicks() const { return inputTicks_; }
    void logNextTicks(int n) { debugLog_ = n; }

private:
    int debugLog_ = 0;
    std::size_t inputTicks_ = 0;
    std::array<bool, static_cast<std::size_t>(Key::Count)> forced_{};
    bool controlling_ = false;
    bool applied_ = false;
    std::array<unsigned char, 16> savedRaw_{};
};
