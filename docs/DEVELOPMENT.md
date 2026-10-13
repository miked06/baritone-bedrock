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
2. **Inject and read the console**: every signature not found is listed (`Signature not found: ...`) and the bot does not start. Fix them in `src/memory/Signatures.h`: find the function again (strings, callers, the previous version's bytes), write a new pattern, check it is unique. Patterns marked `Rel32` resolve a relative call or address at an offset.
3. **Check the offsets** in `src/sdk/Offsets.h` with `#debug`: position, rotation, held item, dimension, armor, hunger, game target. Wrong values point to the offset to fix. `#vt`, `#hex`, `#hexat`, `#ecsdump` and `#attrs` dump memory read-only to find the new layout.
4. **EnTT**: if components are no longer found, the game may use another EnTT revision; update the pinned commit in `cmake/Dependencies.cmake`.
5. Run the tests and a few in-game checks (`#goto`, `#mine`, `#build`, `#elytra`), then update the version in the README.

## Reverse engineering notes (26.52)

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

## Releasing

1. Update `VERSION` in `CMakeLists.txt` (shown by `#version`), and the README if the supported game version changed.
2. Tag: `git tag v1.0.0 && git push origin v1.0.0`.
3. The CI builds the DLL and attaches `BaritoneBedrock.dll` and `BaritoneBedrock.pdb` to a GitHub release.
