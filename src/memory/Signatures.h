#pragma once
#include <cstdint>
#include <span>
#include <string_view>

// Byte patterns of the game functions/globals we use. When Minecraft updates, this file
// and sdk/Offsets.h are the only places that should need changes (tools/sigcheck.py
// verifies every pattern against a dump of the new version).
namespace sigs {

enum class Resolve {
    Match,  // the address of the match itself (pattern at the start of a function)
    Rel32,  // a rip-relative operand inside the match (call target, global variable...)
};

struct Signature {
    std::string_view name;
    std::string_view pattern;
    Resolve mode = Resolve::Match;
    int relOffset = 0;  // Rel32: offset of the disp32 inside the match
    uintptr_t address = 0;

    template <typename T>
    T as() const { return reinterpret_cast<T>(address); }
};

// clang-format off
// mov [rip+Platform_GameCore global], r15
inline Signature PlatformGameCore         {"Platform_GameCore global", "4C 89 3D ? ? ? ? 4D 85 FF", Resolve::Rel32, 3};
inline Signature Level_subTick            {"MultiPlayerLevel::_subTick", "55 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 44 0F 29 6D ? 44 0F 29 65 ? 44 0F 29 5D ? 44 0F 29 55 ? 66 44 0F 7F 4D"};
inline Signature ClientInput_tick         {"ClientInputUpdateSystemInternal::tickUpdateClientInput", "41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC ? ? ? ? 48 8B 84 24"};
inline Signature ChatScreen_sendChat      {"ClientInstanceScreenModel::sendChatMessage", "E8 ? ? ? ? 3C ? 75 ? 48 8B 8E ? ? ? ? 48 C7 45 ? ? ? ? ? 48 8B 01 48 8B 40 ? 48 8D 55 ? FF 15 ? ? ? ? 48 8B 4D", Resolve::Rel32, 1};
inline Signature GuiData_displayMessage   {"GuiData::displayClientMessage", "55 56 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 48 C7 85 ? ? ? ? ? ? ? ? 44 88 CB"};
inline Signature LocalPlayer_applyTurnDelta{"LocalPlayer::applyTurnDelta", "55 41 56 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 44 0F 29 5D ? 44 0F 29 55 ? 44 0F 29 4D ? 44 0F 29 45 ? 0F 29 7D ? 0F 29 75 ? 48 C7 45 ? ? ? ? ? 48 89 D7 48 89 CE 48 8B 89"};
inline Signature LevelRenderer_renderLevel{"LevelRenderer::renderLevel", "E8 ? ? ? ? 45 31 E4 48 83 BE", Resolve::Rel32, 1};
// 3D rendering with the game's tessellator (from Latite)
inline Signature Tessellator_begin        {"Tessellator::begin", "56 57 55 53 48 83 EC ? 48 8B 05 ? ? ? ? 48 31 E0 48 89 44 24 ? 80 B9 ? ? ? ? ? 0F 85 ? ? ? ? 80 B9"};
inline Signature Tessellator_vertex       {"Tessellator::vertex", "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 44 0F 29 4D ? 44 0F 29 45 ? 0F 29 7D ? 0F 29 75 ? 48 C7 45 ? ? ? ? ? 0F 28 F3 0F 28 FA 44 0F 28 C1 48 89 CE 48 8B 0D"};
inline Signature Tessellator_color        {"Tessellator::color", "80 B9 ? ? ? ? ? 0F 85 ? ? ? ? F3 0F 10 05 ? ? ? ? F3 0F 10 0A"};
inline Signature MeshHelpers_renderImmediately{"MeshHelpers::renderMeshImmediately", "55 41 57 41 56 41 54 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 48 C7 85 ? ? ? ? ? ? ? ? 80 BA ? ? ? ? ? 0F 85 ? ? ? ? 4C 89 CF"};
inline Signature RenderMaterialGroup_common{"mce::RenderMaterialGroup::common", "48 8D 15 ? ? ? ? 4C 8D 45 ? E8 ? ? ? ? 48 8D 4D ? E8 ? ? ? ? 48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8D 0D ? ? ? ? E8 ? ? ? ? E9 ? ? ? ? 48 8D 0D", Resolve::Rel32, 3};
// The game's operator delete (custom global allocator). Memory allocated by the game
// must be freed with this, never with our CRT.
inline Signature GameOperatorDelete      {"operator delete (game allocator)", "55 48 83 EC 30 48 8D 6C 24 30 48 C7 45 F8 FE FF FF FF 48 89 CA 48 8B 0D ? ? ? ? 48 85 D2 74 ? 48 8B 01 48 8B 40 10 FF 15"};
// Copies the camera orientation (quaternion) into the player's rotation, every frame
inline Signature UpdatePlayerFromCamera   {"UpdatePlayerFromCameraSystemUtil::_updatePlayer", "41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC ? ? ? ? 44 0F 29 94 24 ? ? ? ? 44 0F 29 8C 24 ? ? ? ? 44 0F 29 84 24 ? ? ? ? 0F 29 BC 24 ? ? ? ? 0F 29 B4 24 ? ? ? ? 4C 89 C6"};
// Once per frame on the client thread, also while the game is paused
inline Signature MinecraftGame_update     {"MinecraftGame::_update", "E8 ? ? ? ? 48 8B 8F ? ? ? ? BA ? ? ? ? E8 ? ? ? ? 48 8B 9F", Resolve::Rel32, 1};
// Server-authoritative inventory: moving items sends an ItemStackRequest, like the
// inventory screen (ItemStackNetManagerClient begin / addRequestAction / end + queue)
inline Signature ItemStackNet_beginRequest {"ItemStackNetManagerClient::_beginRequest", "55 41 57 41 56 56 57 53 48 83 EC 68 48 8D 6C 24 60 48 C7 45 00 FE FF FF FF 48 89 CE 48 8B 41 38 48 8B 48 20 48 85 C9 0F"};
inline Signature ItemStackNet_addRequestAction{"ItemStackNetManagerClient::addRequestAction", "55 56 57 53 48 81 EC F8 00 00 00 48 8D AC 24 80 00 00 00 48 C7 45 70 FE FF FF FF 48 89 CE 48 89 55 68 48 8B 12 E8 ? ? ? ? 84 C0 0F 84"};
inline Signature ItemStackNet_endRequest   {"ItemStackNetManagerClient::_endRequest (queue)", "55 41 56 56 57 53 48 83 EC 40 48 8D 6C 24 40 48 C7 45 F8 FE FF FF FF 48 89 CE 48 8D 55 E8 E8 ? ? ? ? 4C 8B 75 E8 4D 85 F6"};
inline Signature ItemStackNet_handleResponse{"ItemStackNetManagerClient::handleItemStackResponse", "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC 98 01 00 00 48 8D AC 24 80 00 00 00 48 C7 85 10 01 00 00 FE FF FF FF 48 89 4D 48 C6 41 58 01"};
// ItemStackBase(ItemStackBase const&): copies an item stack (the ItemStack copy then sets its vtable and net id)
inline Signature ItemStackBase_copy        {"ItemStackBase::ItemStackBase(ItemStackBase const&)", "55 56 48 83 EC 58 48 8D 6C 24 50 48 C7 45 00 FE FF FF FF 48 89 D6 48 8D 05 ? ? ? ? 48 89 01 48 8D 41 08 48 89 45 D8 0F 57 C0 0F 11 41 08 0F 11 41 13 66 C7 41 23 01 01"};
// mouse input: GameCore::handleMouseInput feeds the MouseDevice queue the input handler reads;
// the right click there goes through the game's own "use" path (fireworks while gliding)
inline Signature GameCore_handleMouseInput {"GameCore::handleMouseInput", "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 44 0F 29 BD ? ? ? ? 44 0F 29 B5 ? ? ? ? 44 0F 29 AD ? ? ? ? 44 0F 29 A5 ? ? ? ? 44 0F 29 9D ? ? ? ? 44 0F 29 95 ? ? ? ? 44 0F 29 8D ? ? ? ? 44 0F 29 85 ? ? ? ? 0F 29 BD ? ? ? ? 0F 29 B5 ? ? ? ? 48 C7 85 ? ? ? ? ? ? ? ? 48 89 CE 8B 05"};
// inside handleMouseInput: lea rcx,[MouseDevice]; mov dl,1; mov r8b,1; call MouseDevice::feed
inline Signature MouseDevice_instance      {"MouseDevice instance", "48 8D 0D ? ? ? ? B2 01 41 B0 01 E8", Resolve::Rel32, 3};
inline Signature MouseDevice_feed          {"MouseDevice::feed", "48 8D 0D ? ? ? ? B2 01 41 B0 01 E8", Resolve::Rel32, 13};
// item durability and enchantments, read-only lookups in the item's user data (CompoundTag)
inline Signature ItemStackBase_getDamageValue {"ItemStackBase::getDamageValue", "56 57 48 83 EC ? 48 8B 05 ? ? ? ? 48 31 E0 48 89 44 24 ? 48 8B 41 ? 48 85 C0 74 ? 48 83 38"};
// called by getDamageValue: Tag* CompoundTag::get(std::string_view) (a map lookup)
inline Signature CompoundTag_get {"CompoundTag::get", "56 57 48 83 EC ? 48 8B 05 ? ? ? ? 48 31 E0 48 89 44 24 ? 48 8B 41 08 48 85 C0 74 ? 48 83 38 00 74 ? 48 8B 71 10 48 85 F6 74 ? 48 8D 3D ? ? ? ? 48 89 7C 24 20 48 C7 44 24 28 06 00 00 00 48 8D 54 24 20 48 89 F1 E8", Resolve::Rel32, 75};
// vtable of the take/place/swap/drop actions, from the Swap construction: mov byte [rdi+8],2; lea rax,[vtable]
inline Signature ItemStackRequestTransfer_vtable{"ItemStackRequestActionTransfer vtable", "C6 47 08 02 48 8D 05 ? ? ? ? 48 89 07 66 C7 47 10 01 00 C6 47 12 00", Resolve::Rel32, 7};
// clang-format on

std::span<Signature* const> all();

// Scans every signature in Minecraft.Windows.exe. Logs each result; false if any is missing.
bool resolveAll();

}  // namespace sigs
