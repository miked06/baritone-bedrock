#include "hooks/GameHooks.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <format>
#include <string>

#include "baritone/Settings.h"
#include "core/Bot.h"
#include "core/Log.h"
#include "core/Safe.h"
#include "memory/Hooks.h"
#include "memory/Signatures.h"
#include "platform/PathRenderer.h"
#include "sdk/Game.h"
#include "sdk/Render.h"

namespace gamehooks {
namespace {

DWORD g_updateThread = 0;  // the thread running MinecraftGame::_update (logged with the mouse thread)

// ---- MultiPlayerLevel::_subTick: one call per client tick (20/s) while in a world ----
using SubTickFn = void(__fastcall*)(void* level);
SubTickFn g_subTick = nullptr;

void __fastcall subTick(void* level) {
    hooks::Guard guard;
    safe::run("tick", [level] { Bot::get().onTick(level); });
    g_subTick(level);
}

// ---- MinecraftGame::_update: every frame, even in menus or paused ----
using UpdateFn = void(__fastcall*)(void* game);
UpdateFn g_update = nullptr;

void __fastcall update(void* game) {
    hooks::Guard guard;
    g_updateThread = GetCurrentThreadId();
    g_update(game);
    safe::run("frame", [] { Bot::get().onFrame(); });
}

// ---- LevelRenderer::renderLevel: draw paths and goals over the world ----
using RenderLevelFn = void(__fastcall*)(sdk::LevelRenderer* renderer, sdk::ScreenContext* screen, void* unk);
RenderLevelFn g_renderLevel = nullptr;

void __fastcall renderLevel(sdk::LevelRenderer* renderer, sdk::ScreenContext* screen, void* unk) {
    hooks::Guard guard;
    g_renderLevel(renderer, screen, unk);
    safe::run("render", [&] { platform::PathRenderer::get().render(renderer, screen); });
}

// ---- UpdatePlayerFromCameraSystemUtil::_updatePlayer: camera -> player rotation ----
// While the bot looks somewhere, the player is rotated without touching the camera
// (Baritone's freeLook): swap the camera quaternion for the call, then restore it.
using UpdatePlayerFn = void(__fastcall*)(sdk::CameraComponent* camera, void* a, void* b);
UpdatePlayerFn g_updatePlayer = nullptr;

void __fastcall updatePlayer(sdk::CameraComponent* camera, void* a, void* b) {
    hooks::Guard guard;
    std::optional<Bot::LookTarget> target;
    safe::run("look", [&] { target = Bot::get().rotationTarget(); });
    if (!target) {
        g_updatePlayer(camera, a, b);
        return;
    }
    sdk::Vec4 saved = camera->lookAngles;
    camera->lookAngles = sdk::rotationToQuaternion(target->player.x, target->player.y);
    g_updatePlayer(camera, a, b);
    // SERVER mode: give the camera back; CLIENT mode: the camera turns too (possibly smoothed)
    camera->lookAngles = target->camera ? sdk::rotationToQuaternion(target->camera->x, target->camera->y) : saved;
}

// ---- ClientInputUpdateSystemInternal::tickUpdateClientInput ----
// The real signature has many arguments; forwarding 18 integer slots covers all of them.
using InputTickFn = void(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                                      uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                                      uintptr_t, uintptr_t, uintptr_t, uintptr_t);
InputTickFn g_inputTick = nullptr;

sdk::MoveInputComponent* localMoveInput() {
    auto* client = sdk::ClientInstance::get();
    auto* player = client ? client->localPlayer() : nullptr;
    return player ? player->moveInput() : nullptr;
}

void __fastcall inputTick(uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6,
                          uintptr_t a7, uintptr_t a8, uintptr_t a9, uintptr_t a10, uintptr_t a11, uintptr_t a12,
                          uintptr_t a13, uintptr_t a14, uintptr_t a15, uintptr_t a16, uintptr_t a17, uintptr_t a18) {
    hooks::Guard guard;
    sdk::MoveInputComponent* input = nullptr;
    safe::run("input (before)", [&] {
        input = localMoveInput();
        if (input) Bot::get().beforeInputTick(*input);
    });
    g_inputTick(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18);
    if (input) safe::run("input (after)", [&] { Bot::get().afterInputTick(*input); });
}

// ---- ClientInstanceScreenModel::sendChatMessage: returns false to drop the message ----
using SendChatFn = bool(__fastcall*)(void* model, std::string& message);
SendChatFn g_sendChat = nullptr;

bool __fastcall sendChat(void* model, std::string& message) {
    hooks::Guard guard;
    // every message goes through the bot: the prefix setting, chatControl (Java
    // ExampleBaritoneControl); a message it used is not sent
    bool used = false;
    safe::run("command", [&] { used = Bot::get().onChatSend(message); });
    if (used) return false;
    return g_sendChat(model, message);
}

// ---- ItemStackNetManagerClient::handleItemStackResponse: the server's answers to inventory
// requests (logged: the server refuses a request silently otherwise) ----
using HandleResponseFn = void(__fastcall*)(void* manager, const void* responses);
HandleResponseFn g_handleResponse = nullptr;

void __fastcall handleResponse(void* manager, const void* responses) {
    hooks::Guard guard;
    safe::run("item response", [&] {
        // std::vector<ItemStackResponseInfo>, 0x30 bytes each: result, request id at +0x10
        auto first = *reinterpret_cast<const uint8_t* const*>(responses);
        auto last = *reinterpret_cast<const uint8_t* const*>(reinterpret_cast<const uint8_t*>(responses) + 8);
        for (const uint8_t* r = first; r && r < last; r += 0x30) {
            auto containers = (*reinterpret_cast<const uintptr_t*>(r + 0x20) - *reinterpret_cast<const uintptr_t*>(r + 0x18));
            int id = *reinterpret_cast<const int32_t*>(r + 0x10);
            logger::debug("ItemStackResponse: request {} result {} ({} bytes of container info)", id, r[0], containers);
            Bot::get().player().onItemStackResponse(id, r[0]);
        }
    });
    g_handleResponse(manager, responses);
}

// ---- GameCore::handleMouseInput: the game feeds its mouse queue here; a requested click is
// fed the same way, so the game uses the held item exactly as for a real right click. The
// button stays down a bit more than one tick: using an item on a block is done by the tick
// while the button is held, not on the press itself ----
using MouseInputFn = bool(__fastcall*)(void*, void*, void*);
MouseInputFn g_mouseInput = nullptr;
using FeedFn = void(__fastcall*)(void* device, char button, char state, short x, short y, short dx, short dy,
                                 bool forceMotionless);

std::atomic<int> g_clickRequest{0};  // mouse button to click (2 = right), 0 = none
int g_pressedButton = 0;             // currently held down by us
ULONGLONG g_pressedAt = 0;
constexpr ULONGLONG kClickHoldMs = 80;  // > one tick (50 ms), < the game's repeat delay
uint64_t g_mouseCalls = 0;
std::atomic<ULONGLONG> g_autoUseUntil{0};  // right click as soon as the game targets a block

// the game's own pick (what a click would use) is on a block
bool targetingBlock() {
    sdk::Level* level = Bot::get().player().level();
    if (!level) return false;
    const sdk::HitResult* hit = level->hitResult();
    if (!mem::isReadable(hit, sizeof(sdk::HitResult)) || hit->type != 0) return false;
    // the game's pick can reach further than a placement is allowed: blockReachDistance from the eyes
    baritone::Vec3d eye = Bot::get().player().eyePosition();
    double dx = hit->pos.x - eye.x, dy = hit->pos.y - eye.y, dz = hit->pos.z - eye.z;
    double reach = baritone::Settings::get().blockReachDistance;
    return dx * dx + dy * dy + dz * dz <= reach * reach;
}

bool __fastcall mouseInput(void* a1, void* a2, void* a3) {
    hooks::Guard guard;
    bool result = g_mouseInput(a1, a2, a3);
    safe::run("mouse input", [] {
        if (++g_mouseCalls == 1)
            logger::debug("handleMouseInput on thread {} (update thread {})", GetCurrentThreadId(), g_updateThread);
        auto* device = reinterpret_cast<uint8_t*>(sigs::MouseDevice_instance.address);
        auto feed = reinterpret_cast<FeedFn>(sigs::MouseDevice_feed.address);
        if (!device || !feed) return;
        short x = *reinterpret_cast<int16_t*>(device + 4), y = *reinterpret_cast<int16_t*>(device + 6);
        if (g_pressedButton) {
            if (GetTickCount64() - g_pressedAt < kClickHoldMs) return;
            feed(device, static_cast<char>(g_pressedButton), 0, x, y, 0, 0, false);
            g_pressedButton = 0;
        } else if (int button = g_clickRequest.exchange(0)) {
            feed(device, static_cast<char>(button), 1, x, y, 0, 0, false);
            g_pressedButton = button;
            g_pressedAt = GetTickCount64();
        } else if (GetTickCount64() < g_autoUseUntil && targetingBlock()) {
            // checked every frame: the first frame a block comes within reach (water bucket clutch)
            feed(device, 2, 1, x, y, 0, 0, false);
            g_pressedButton = 2;
            g_pressedAt = GetTickCount64();
            logger::debug("auto use: right click on the targeted block");
        }
    });
    return result;
}

}  // namespace

void requestClick(int button) { g_clickRequest = button; }

void autoUseWhileTargeting(int ms) { g_autoUseUntil = GetTickCount64() + ms; }

bool install() {
    bool ok = true;
    ok &= hooks::create("GameCore::handleMouseInput", sigs::GameCore_handleMouseInput.address, &mouseInput,
                        &g_mouseInput);
    ok &= hooks::create("ItemStackNetManagerClient::handleItemStackResponse", sigs::ItemStackNet_handleResponse.address,
                        &handleResponse, &g_handleResponse);
    ok &= hooks::create("MultiPlayerLevel::_subTick", sigs::Level_subTick.address, &subTick, &g_subTick);
    ok &= hooks::create("ClientInputUpdateSystem::tickUpdateClientInput", sigs::ClientInput_tick.address, &inputTick,
                        &g_inputTick);
    ok &= hooks::create("ClientInstanceScreenModel::sendChatMessage", sigs::ChatScreen_sendChat.address, &sendChat,
                        &g_sendChat);
    ok &= hooks::create("MinecraftGame::_update", sigs::MinecraftGame_update.address, &update, &g_update);
    ok &= hooks::create("UpdatePlayerFromCameraSystemUtil::_updatePlayer", sigs::UpdatePlayerFromCamera.address,
                        &updatePlayer, &g_updatePlayer);
    ok &= hooks::create("LevelRenderer::renderLevel", sigs::LevelRenderer_renderLevel.address, &renderLevel,
                        &g_renderLevel);
    return ok;
}

}  // namespace gamehooks
