#include "memory/Signatures.h"

#include <libhat/process.hpp>
#include <libhat/scanner.hpp>
#include <libhat/signature.hpp>

#include <array>
#include <chrono>

#include "core/Log.h"

namespace sigs {

std::span<Signature* const> all() {
    static constexpr std::array list{
        &PlatformGameCore,       &Level_subTick,
        &ClientInput_tick,       &ChatScreen_sendChat,
        &GuiData_displayMessage, &LocalPlayer_applyTurnDelta,
        &LevelRenderer_renderLevel, &MinecraftGame_update,
        &UpdatePlayerFromCamera, &GameOperatorDelete,
        &Tessellator_begin, &Tessellator_vertex, &Tessellator_color,
        &MeshHelpers_renderImmediately, &RenderMaterialGroup_common,
        &ItemStackNet_beginRequest, &ItemStackNet_addRequestAction, &ItemStackNet_endRequest,
        &ItemStackRequestTransfer_vtable, &ItemStackNet_handleResponse, &ItemStackBase_copy,
        &GameCore_handleMouseInput, &MouseDevice_instance, &MouseDevice_feed,
        &ItemStackBase_getDamageValue, &CompoundTag_get,
    };
    return list;
}

bool resolveAll() {
    auto module = hat::process::get_module("Minecraft.Windows.exe");
    if (!module) {
        logger::error("Minecraft.Windows.exe module not found");
        return false;
    }
    auto text = module->get_section_data(".text");
    auto start = std::chrono::steady_clock::now();

    bool ok = true;
    for (Signature* sig : all()) {
        auto parsed = hat::parse_signature(sig->pattern);
        if (!parsed.has_value()) {
            logger::error("Invalid pattern for {}", sig->name);
            ok = false;
            continue;
        }
        auto result = hat::find_pattern(text.begin(), text.end(), parsed.value());
        if (!result.has_result()) {
            logger::error("Signature not found: {}", sig->name);
            ok = false;
            continue;
        }
        sig->address = sig->mode == Resolve::Rel32
                           ? reinterpret_cast<uintptr_t>(result.rel(sig->relOffset))
                           : reinterpret_cast<uintptr_t>(result.get());
        logger::debug("  {:<55} Minecraft.Windows.exe+0x{:X}", sig->name,
                      sig->address - module->address());
    }

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    logger::info("Signatures: {} scanned in {} ms", all().size(), ms.count());
    return ok;
}

}  // namespace sigs
