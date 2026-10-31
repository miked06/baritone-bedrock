# Development

Notes for people working on the code: the dev loop, the tools, how to update the bot to a new game version, and what was learned while reverse engineering Bedrock.

## Dev loop

The `tools/` scripts let you rebuild and test without leaving the terminal (Python 3.10+).

```powershell
cmake --build build --config RelWithDebInfo
python tools/inject.py                      # unload the running copy, inject the new build
python tools/cmd.py "#debug" --wait 2       # run commands as if typed in the chat, print the log they wrote
python tools/cmd.py "#goto ~10 ~ ~" --wait 10
```

- `inject.py` copies the DLL to a unique name in `%LOCALAPPDATA%\BaritoneBedrock\dev\` (so the build output is never locked), asks the injected copy to unload itself, then injects the new one.
- `cmd.py` sends commands through the bot's named pipe `\\.\pipe\BaritoneBedrock_<pid>`; they run on the next game tick. The pipe is open to the authenticated users of the machine (so a terminal can reach a game started as administrator); it is local only.
- `#set logMovement true` writes one line per tick to `log.txt`: movement, inputs, target rotation, position, velocity, ground state, targeted block, held item. It is the best tool to understand what a movement does.

## Code layout and rules

See [ARCHITECTURE.md](ARCHITECTURE.md). A few rules keep the bot stable:

- **The core (`src/baritone/`) never includes game headers.** It talks to the game through `PlayerContext` and the world snapshot, so it can be tested offline.
- **Follow Baritone.** When porting or fixing logic, read the matching Java class and keep the same structure, names and constants; comment where Bedrock forces a difference (`// Bedrock: ...`) and list user-visible differences in [PARITY.md](PARITY.md).
- **Touch the game only during its tick**, from the game thread. Path calculations read the snapshot only.
- **Prefer emulating the player** (keys, rotation, mouse clicks) over calling internal functions. When a new internal call is needed, first check how and from where the game calls it itself, with the same arguments.
- Build `RelWithDebInfo`; add a test in `tests/` for new core logic.

## Updating to a new game version

Each Minecraft update moves functions and may change class layouts.

1. **Dump the game** (the executable in `WindowsApps` cannot be read, the running image can):

   ```powershell
   python tools/dump_module.py re
   ```

   This writes an unmapped `Minecraft.Windows_<version>.exe` (file offsets = RVAs) that IDA, Ghidra or `tools/relib.py` can open. Never commit it.
2. **Check signatures offline first** (`numpy` is required):

   ```powershell
   python tools/sigcheck.py re/Minecraft.Windows_1.26.6005.0.exe --json re/signatures.json
   ```

   Fix patterns in `src/memory/Signatures.h` by comparing functions with the previous dump (strings, callers and matching instruction sequences). Every pattern must match exactly once. `Rel32` resolves a relative call or address at the specified displacement offset. The injected scanner also rejects ambiguous patterns.
3. **Check layouts and calling conventions before injecting.** These include `src/sdk/Offsets.h`, the client map value in `Game.cpp`, render context fields in `Render.cpp`, and function argument types in the SDK/hooks. Update the exact supported package version in `core/GameVersion.cpp` only after verifying these against the new binary. Then use `#debug` to check position, rotation, held item, dimension, armor, hunger and game target. `#vt`, `#hex`, `#hexat`, `#ecsdump` and `#attrs` dump memory read-only to find the new layout.
4. **EnTT**: if components are no longer found, the game may use another EnTT revision; update the pinned commit in `cmake/Dependencies.cmake`.
5. Run the tests and a few in-game checks (`#goto`, `#mine`, `#build`, `#elytra`), then update the version in the README.

## Reverse engineering notes (26.60)

Target: Microsoft Store package `1.26.6005.0`, compared with the previous `1.26.5203.0` memory dump. Dumps and exploratory scripts stay in the ignored `re/` directory.

- 13 of the 26 previous signatures stopped matching. The 26 updated patterns plus one new inventory-open pattern all match once in the new `.text` section; their resolved addresses were also checked during injection.
- `MinecraftGame`'s client map moved to `+0x9D0`. Its value now contains two interface pointers before the `shared_ptr<ClientInstance>` (value size `0x20`, previously `0x18`). Updating just the map offset returns an interface subobject instead of the client.
- `ClientInstance`: game `+0x1C8`, GUI `+0x668`, region/player vtable indices `0x1D`/`0x1E`. The cursor-grabbed flag moved to `MinecraftGame+0x1F8`.
- Actor data pointers and the player inventory/game-mode/name fields moved by `+0x28`; the ECS entity context and EnTT revision remain unchanged. The active inventory request moved to manager `+0x68`.
- Main inventory moves now need the server's inventory screen context. A request addressed to `InventoryContainer` while the screen is closed is rejected with status 49/50; using the hotbar or combined container as an alias does not fix it. Mirror the inventory-key handler (RVA `0x657DA52`): send `InteractPacket` action 6 with the player's runtime id. The server then opens the client inventory. Calling `LocalPlayer::openInventory` directly only opens the client UI and still produces rejected moves.
- `InteractPacket`: size `0x58`, action `+0x30`, target runtime id `+0x38`, optional position `+0x40`, serialization mode `+0x50` (5). The native handler uses priority 2/reliability 1. `LocalPlayer::sendNetworkPacket` is vtable `0xE4`.
- Pause bot actions while its temporary inventory screen opens and the server answers. Close only that screen, using `LocalPlayer+0xEEC` (`{-1, true}`) and vtable `0xDC` (`deleteContainerManager`); normalTick consumes the close flag. Do not close an inventory the user opened. The class declarations and close sequence were cross-checked with [LeviLamina's client SDK](https://github.com/LiteLDev/LeviLamina/blob/main/src-client/mc/client/player/LocalPlayer.cpp); packet action/status names are in [Mojang's protocol reference](https://mojang.github.io/bedrock-protocol-docs/1.26.60/packets/item-stack-response-packet/).
- World name: `LevelData+0x2B0`; hit result pointer: `Level+0x1F0`. BlockSource, block-state NBT and attribute lookups were checked in game.
- Rendering requires more than new signatures: `ScreenContext` tessellator is `+0xB0`. The 1.4 correction identifies current shader color at `+0x30` and dark shader color at `+0x38`; see the shader-color section below. The former tessellator offset `+0xB8` now points at a lifetime control block; passing it to Tessellator functions corrupts that block and crashes a later frame.
- `Tessellator::color` now uses `(this, float r, float g, float b, float a)`. The render player/origin fields moved to `LevelRenderer+0x480` and `LevelRendererPlayer+0x658`.

Validation: 39 offline core tests, package detection, all eight hooks, player/world diagnostics, short navigation over uneven terrain, goal rendering, survival block placement/breaking with tool selection and durability, entity listing, empty/nonempty inventory transfers in both directions, automatic pickaxe retrieval with `allowInventory`, preservation of a manually opened inventory, a one-block selection build with automatic material retrieval, and unload/reinjection. A complete farming, schematic-building, elytra and water-clutch campaign has not been repeated on 26.60.

## Java feature audit (1.3.0)

The pinned Java reference and reproduction commands are in [JAVA_PARITY_AUDIT.md](JAVA_PARITY_AUDIT.md). `tools/paritycheck.py` checks command registrations/settings and reports settings without executable references; behavioral regressions live in the offline suite.

Native additions verified against package `1.26.6005.0`:

- `ClientInstance::requestLeaveGameAsync`: primary vtable `0xD`, RVA `0x6593740`, confirmed by the native string `virtual ClientInstance::requestLeaveGameAsync` and its scheduler/weak-reference callback. Call it from the frame after the level tick; do not destroy the world from `CustomGoalProcess::onTick`.
- `ui_fill_color`: position-only, blending enabled, depth testing disabled in the shipped `data/resource_packs/vanilla/materials/ui.material`. The position transform works in the level-render context. Batch by uniform shader color, preserve/restore the previous shader color, and keep normal-depth batches on `selection_box`.
- The existing `MouseDevice::feed` signature is also hooked for physical `#click` input. Synthetic bot clicks call its trampoline directly. Captured events are queued; ray tracing, selection changes and goals run on the frame thread. Opening a menu/focus loss restores ordinary clicks. There are now nine hooks.
- Cached terrain geometry uses immutable cached chunks on a worker, with bounded output. Explicitly join it before DLL unload; do not wait for a worker from a static destructor during Windows process termination.
- Bedrock enchantment ids: Depth Strider 7, Efficiency 15, Silk Touch 16, Frost Walker 25; cross-checked with [LeviLamina's Enchant::Type](https://github.com/LiteLDev/LeviLamina/blob/main/src/mc/world/item/enchanting/Enchant.h).

## Earlier reverse engineering notes (26.52)

Things that are not obvious and cost time to find:

**Inventory and items**

- The inventory is server-authoritative. A move is an `ItemStackRequest` sent like the inventory screen does (`ItemStackNetManagerClient`); apply it to the client only after the server accepts it (`handleItemStackResponse` hook). Changing the client first makes the server refuse the request.
- A swap into an empty slot is refused: use a place action instead.
- The item registry is thread-local and only exists during the tick. Copying or setting an `ItemStack` during a frame triggers a deliberate crash of the game, and catching it with SEH freezes the game (a lock is never released).
- After changing the held slot, wait about 3 ticks before clicking: the equipment packet is sent on the player's next tick.
- `ItemStackBase::getDamageValue` and `CompoundTag::get` are pure lookups and safe to call. `Item+0x150` is the max damage; enchantments are in the `ench` list tag (Bedrock ids: 15 efficiency, 16 silk touch, 25 frost walker).

**Using items**

- `GameMode::buildBlock` only places blocks. For other items (fireworks, bone meal, buckets) the bot feeds a real right click into the game's mouse queue (`MouseDevice::feed` from the `GameCore::handleMouseInput` hook), held for 80 ms. The game then uses its own target (`Level+0x1E8`, a `HitResult`), so aim at the real shape of the block.
- Calling the use-item functions directly from the tick froze the game.

**Breaking and placing**

- `GameMode::buildBlock` takes `(pos, face, bool, bool isSimTick)`; `continueDestroyBlock` takes `(pos, face, const Vec3& playerPos, bool& destroyed)`. A wrong argument count crashes the integrated server or writes random memory.
- `continueDestroyBlock`'s `destroyed` flag stays false when hits finish a block; read the block back to know it broke.

**Player state (EnTT components)**

- Component type id = FNV-1a 32 of the bare type name.
- `ActorGameTypeComponent` +0: game mode (0 survival, 1 creative, 2 adventure, 6 spectator).
- `MovementAbilitiesComponent` +4 bit 0: creative flying.
- `FallFlyTicksComponent`: ticks of elytra gliding; it can remain a moment after landing.
- `ActorEquipmentComponent`: `unique_ptr<SimpleContainer>` hand (container type 19) then armor (18); armor slots 0 head, 1 chest, 2 legs, 3 feet. `Container::getItem` has the same vtable index as the inventory's.
- `AttributesComponent`: a flat map — `vector<uint32>` ids, then `vector<AttributeInstance>` (0x88 bytes). An instance points to its `Attribute` (name in a `HashedString` at +8) and holds `{min, max, value}` defaults at +0x68 and current values at +0x74.
- `MobEffectsComponent`: `vector<MobEffectInstance>` indexed by effect id (0x90 bytes): +0 id, +4 duration, +0x20 amplifier.

**Other**

- The game uses its own allocator: never free game memory with the DLL's CRT.
- In creative, two jump presses within ~10 ticks toggle flight.
- A falling block entity only shows for about one tick in the entity list.
- The Windows console blocks writes while text is selected (QuickEdit); the bot turns QuickEdit off so a click in its console cannot freeze the game.

## Schematic workspace and shader color (1.4)

The 1.4 schematic/render changes are described in [SCHEMATICS.md](SCHEMATICS.md) and [FEATURE_COMPARISON.md](FEATURE_COMPARISON.md). The preview reads exact live snapshot chunks and produces a bounded difference set; the renderer consumes a copied frame. The workspace passes an already transformed schematic to `BuilderProcess::build(..., false, false)` so file rotations/orientation offsets are not applied twice.

For Bedrock 26.60, `ScreenContext` embeds `mce::MeshContext` at `+0x10`. `currentShaderColor` is the pointer at `ScreenContext+0x30`; `+0x38` points to **currentShaderDarkColor**, not the current color. A native `ShaderColor` contains RGBA at `+0` and its upload-dirty byte at `+0x10`. Drawing/restoring a color must set that byte. This layout is corroborated by [MeshContext](https://github.com/LiteLDev/LeviLamina/blob/main/src-client/mc/deps/minecraft_renderer/renderer/MeshContext.h) and [ShaderColor](https://github.com/LiteLDev/LeviLamina/blob/main/src-client/mc/deps/renderer/ShaderColor.h), and by native color-store/dirty instruction pairs in the 26.60 dump (for example RVA `0xA0ADA4`). Native `MeshHelpers::renderMeshImmediately` at `0x5AD5F90` advances the ScreenContext argument by `0x10` before the mesh render call. Prior notes that call `+0x30` fog color or use `+0x38` as the draw color are superseded by this correction.

## Releasing

1. Update `VERSION` in `CMakeLists.txt` (shown by `#version`), and the README if the supported game version changed.
2. Tag: `git tag v1.0.0 && git push origin v1.0.0`.
3. The CI builds the DLL and attaches `BaritoneBedrock.dll` and `BaritoneBedrock.pdb` to a GitHub release.
