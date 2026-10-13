#include "core/InputOverride.h"

#include <algorithm>
#include <cstring>

#include "core/Log.h"
#include "sdk/Components.h"

static_assert(sizeof(sdk::MoveInputState) == 16);

bool InputOverride::any() const {
    return std::ranges::any_of(forced_, [](bool b) { return b; });
}

void InputOverride::beforeInputTick(sdk::MoveInputComponent& input) {
    applied_ = controlling_ || any();
    if (!applied_) return;

    sdk::MoveInputState& raw = input.rawInputState;
    std::memcpy(savedRaw_.data(), &raw, sizeof(raw));

    if (controlling_) {
        raw.up = raw.down = raw.left = raw.right = false;
        raw.upLeft = raw.upRight = raw.downLeft = raw.downRight = false;
        raw.jumpDown = raw.sneakDown = raw.sprintDown = false;
        raw.analogMoveVector = {};
    }
    if (get(Key::MoveForward)) raw.up = true;
    if (get(Key::MoveBack)) raw.down = true;
    if (get(Key::MoveLeft)) raw.left = true;
    if (get(Key::MoveRight)) raw.right = true;
    if (get(Key::Jump)) raw.jumpDown = true;
    if (get(Key::Sneak)) raw.sneakDown = true;
    if (get(Key::Sprint)) raw.sprintDown = true;
    bool fwd = get(Key::MoveForward), back = get(Key::MoveBack);
    bool left = get(Key::MoveLeft), right = get(Key::MoveRight);
    if (fwd || back || left || right) {
        raw.analogMoveVector = {static_cast<float>(left) - static_cast<float>(right),
                                static_cast<float>(fwd) - static_cast<float>(back)};
    }
}

void InputOverride::afterInputTick(sdk::MoveInputComponent& input) {
    ++inputTicks_;
    if (!applied_) return;
    std::memcpy(&input.rawInputState, savedRaw_.data(), sizeof(input.rawInputState));
    applied_ = false;

    // Also write the processed state the movement system reads. When the window has no
    // focus the game ignores the raw key state, so this is what makes the bot move then.
    sdk::MoveInputState& st = input.inputState;
    bool fwd = get(Key::MoveForward), back = get(Key::MoveBack);
    bool left = get(Key::MoveLeft), right = get(Key::MoveRight);
    if (controlling_) {
        st.up = st.down = st.left = st.right = false;
        st.jumpDown = st.sneakDown = st.sprintDown = false;
    }
    if (fwd) st.up = true;
    if (back) st.down = true;
    if (left) st.left = true;
    if (right) st.right = true;
    if (get(Key::Jump)) st.jumpDown = true;
    if (get(Key::Sneak)) st.sneakDown = true;
    if (get(Key::Sprint)) st.sprintDown = true;
    if (controlling_ || fwd || back || left || right) {
        input.move = {static_cast<float>(left) - static_cast<float>(right),
                      static_cast<float>(fwd) - static_cast<float>(back)};
        st.analogMoveVector = input.move;
    }
    if (controlling_ || get(Key::Jump)) input.jumping = get(Key::Jump);
    if (controlling_ || get(Key::Sneak)) input.sneaking = get(Key::Sneak);
    if (controlling_ || get(Key::Sprint)) input.sprinting = get(Key::Sprint);
    if (debugLog_ > 0) {
        --debugLog_;
        logger::debug("input after: move {:.2f},{:.2f} up {} raw.up {} locked {} disp {:.3f},{:.3f},{:.3f}", input.move.x,
                      input.move.y, static_cast<bool>(st.up), static_cast<bool>(input.rawInputState.up),
                      static_cast<bool>(input.moveInputStateLocked), input.displacement.x, input.displacement.y,
                      input.displacement.z);
    }
}
