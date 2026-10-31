# Java feature audit

Reference: [cabaletta/baritone, branch 26.3, commit 25111daedf1d59e6a8dfb5a3e61885cdb8d953df](https://github.com/cabaletta/baritone/tree/25111daedf1d59e6a8dfb5a3e61885cdb8d953df), inspected on 2026-10-31. Bedrock target: Windows package `1.26.6005.0` (26.60).

The original port followed Java 1.21.11. This audit compares the current reference's command registrations, settings, inventory/tool logic, movement context, block selectors, schematic masks, cache behavior, rendering, click interactions and elytra diagnostics. It records source comparison and runtime checks separately. It is not a claim that every combination of movements, processes and settings has been exhaustively tested.

## Reproduce the structural comparison

```powershell
git clone --depth 1 --branch 26.3 https://github.com/cabaletta/baritone.git re/baritone-java
python tools/paritycheck.py re/baritone-java --json re/java-parity.json
cmake --build build --config RelWithDebInfo
ctest --test-dir build -C RelWithDebInfo --output-on-failure
```

Pin the checkout to the reference commit when comparing results later. The checker reports the actual checkout revision. `re/` is ignored and is not part of the distribution.

- All 63 Java command names/aliases are registered. The missing `c` alias was added.
- All 245 user-configurable Java setting names are declared. Bedrock also has its platform additions.
- Java's three callback settings (`logger`, `notifier`, `toaster`) are API callbacks. This port uses its log callback, notifier callback and Windows notification bridge.
- The checker lists settings without executable references. The remaining pair is `elytraNetherSeed` / `elytraPredictTerrain`, described below. A source reference alone does not prove correct behavior.

## Gaps addressed in 1.3.0

| Area | Previous behavior | Updated behavior |
|---|---|---|
| Cancel alias | `#c` was missing. | Cancels all bot activity, like `#cancel` / `#stop`. |
| Tool planning | With `autoTool=false`, paths still assumed the fastest hotbar tool. | Costs use a snapshot of the held item, including potion effects. |
| Inventory tool retrieval | Pickaxes were ranked by base material speed. | Efficiency and `itemSaver` durability restrictions affect retrieval. |
| Water movement | Depth Strider was ignored. | Boots' level interpolates water/ground walking costs. Bedrock enchantment id 7 is decoded. |
| Block selectors | Commands matched block names only. | `block[state=value,...]` filters exact Bedrock properties for searches and mining. Selections can request placement orientation states. |
| Replacement builds | A position stopped matching after the old block was broken. | The initial replacement mask is cached, including state predicates, so the replacement is still placed. Reset propagates through masks, composites, rotations and mirrors; normal repeats reset it. |
| Cache search | Loaded chunks were always scanned. | `extendCacheOnThreshold` controls extension of tracked cached locations; loaded locations are checked against current states and stale entries removed. Untracked blocks are scanned directly. |
| Settings profiles | `#set load` ignored a filename. | `#set load [file]` loads the named file after resetting defaults. Missing files preserve current values. `#set save [file]` also supports named profiles. |
| Arrival | `disconnectOnArrival` only notified the player. | Queues the game's normal asynchronous leave operation after the level tick. |
| Depth rendering | Four ignore-depth settings were unused. | Goals, paths, action boxes and selections choose the requested depth mode. Colors and alpha are batched correctly for position-only materials. |
| Cached terrain | `renderCachedChunks` / `cachedChunksOpacity` were unused. | A bounded worker builds exposed faces of remembered terrain outside loaded chunks. Gray, blue and red ghosts represent solid, water and avoid cells; opacity controls transparency. |
| Goal beacon | Only an outlined column was drawn. | A filled beam uses translucent crossed surfaces and a bright core. |
| Flight diagnostics | Raytrace flags were unused. | Steering checks and actual tested swept flight hitboxes are recorded and drawn, including blocked checks. Recording is bounded and disabled when its setting is off. |
| Chunk packing | Queue capacity had no effect. | A bounded, deduplicated queue retains the latest snapshot per chunk and packs within the frame budget. Explicit repack/save drains pending work. `#world` reports queue size. |
| Flight cache memory | Cull distance/interval were unused. | Saved regions outside the configured block radius can be evicted at the configured flight interval. Files remain available for reload; dirty regions remain in memory. Failed writes are retried and cannot be pruned as saved data. |
| Flight allocator | The allocator toggle had no effect. | `elytraCustomAllocator` selects arena-backed search storage or ordinary allocation. Returned paths own their storage. |
| Click interaction | `#click` immediately walked to the aimed block. | Aim, then left click for a goal, right click above it, or hold left and change aim to select a box. `Escape`, opening a menu, `#stop`, or `#click cancel` cancels capture. `#click here` retains immediate targeting. |
| Completion | No completion service was provided. | `CommandManager::complete` and `#complete` suggest commands, settings, common arguments, known blocks/states, nearby entities, waypoints and schematic/profile files. |

## Bedrock adaptations

- `#click` uses the normal crosshair and camera. Java's free-cursor `GuiClick` screen is replaced by this aiming mode; goal/above/drag-selection actions are available.
- `#complete` provides text suggestions. Native chat Tab insertion, Java's hover tooltips and clickable chat actions are not integrated into the Bedrock text-only chat bridge.
- Cached terrain is an untextured ghost of the cache's four pathing categories. It does not reconstruct original block types or textures from the two-bit cache. Geometry is capped at 8192 exposed faces near the player and refreshed on a worker about once per second.
- Cache culling works at the existing cache region granularity (512 blocks), retaining any region intersecting the requested radius. The normal world-cache pruning policy also applies.
- Chunk packing is scheduled on the game thread within its time budget. It does not run Java's background packer against live game objects.
- The allocator toggle selects a C++ memory resource, rather than Java's native pathfinder allocator.
- The beacon has no Java animated texture. It uses filled, translucent world geometry.
- Java mod integrations (`#litematica`, `#schematica`) are replaced by direct schematic file loading. Those mods cannot be loaded into Bedrock as Java mods.

## Remaining substantive gap

`elytraPredictTerrain` and `elytraNetherSeed` are compatibility settings without a Bedrock terrain generator implementation. Java's native Nether generator cannot predict Bedrock terrain from the same seed. The current flight solver uses observed/cached chunks and replans as chunks arrive. Porting an actual Bedrock generator is separate engine work; substituting Java terrain would produce incorrect obstacle predictions.

The complete set of Java client/plugin APIs is not a binary-compatible DLL API. C++ behaviors, processes, goals, schematics and callbacks are available in the core, but a Java client mod cannot link against them.

## Verification

The `RelWithDebInfo` DLL and test executable build successfully on the target PC. The final regression run passed **53 tests, 0 failed**; CTest passed, and `git diff --check` reported no whitespace errors. The structural comparison reports 63/63 Java command names and 245/245 user setting names, with the two terrain-prediction settings still unimplemented as described above.

The regression suite covers held-tool planning, enchanted/worn tool retrieval, Depth Strider costs, exact state searches, replacement after breaking and reset propagation, cache thresholds/stale locations, bounded mesh faces, profile errors, swept flight collision diagnostics, one-shot arrival requests, click interaction outcomes, packing queue limits, cache eviction/write failures and allocator equivalence. Existing core tests continue to run.

In-game validation uses the existing world. The depth modes and cached-terrain opacity were inspected visually. Arrival triggered `ClientInstance::requestLeaveGameAsync` (vtable `0xD`, RVA `0x6593740`); the game left normally, saved its cache, and the same world was subsequently re-entered. This entry point was identified from its native diagnostic string and scheduler call sequence, cross-checked with [LeviLamina's client declarations](https://github.com/LiteLDev/LeviLamina/blob/main/src-client/mc/client/game/ClientInstance.h).

The 1.3.0 DLL was injected and identified itself correctly. `#complete` returned command names, boolean values, toggleable settings and the observed `oak_log[pillar_axis=x|y|z]` variants. `#world` reported the bounded packer queue. Click capture showed the cyan target outline; a right click created an above-block goal, a left click created a reachable goal and the bot arrived. Escape cancelled capture and opened the normal pause menu. The automated drag lasted within one frame and resolved as a click, so multi-frame native drag selection remains unverified; its state transitions are covered by the regression suite. Test settings were restored from a temporary named profile and saved, and the bot was stopped. No world blocks were modified during these parity checks.

Full long-distance elytra, farming and large schematic campaigns have not been repeated for every new setting combination. See [PARITY.md](PARITY.md) for the broader platform differences and [DEVELOPMENT.md](DEVELOPMENT.md) for reverse-engineering details.
