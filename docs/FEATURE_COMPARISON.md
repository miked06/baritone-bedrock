# Baritone Java and Bedrock feature inventory

Compared on 2026-10-31 against Java **26.3**, commit [`25111daedf1d59e6a8dfb5a3e61885cdb8d953df`](https://github.com/cabaletta/baritone/tree/25111daedf1d59e6a8dfb5a3e61885cdb8d953df). The upstream 26.3 branch was fetched again for this comparison. The default GitHub branch is a different Minecraft target, so it is not used as an interchangeable version number.

Sources are Java's [FEATURES.md](https://github.com/cabaletta/baritone/blob/25111daedf1d59e6a8dfb5a3e61885cdb8d953df/FEATURES.md), [USAGE.md](https://github.com/cabaletta/baritone/blob/25111daedf1d59e6a8dfb5a3e61885cdb8d953df/USAGE.md), registered commands, process/movement implementations, schematic interfaces and settings. Bedrock is this source tree, targeting Windows package `1.26.6005.0` (26.60). The two complete command-name lists are in [COMMAND_INVENTORY.md](COMMAND_INVENTORY.md); user settings are in [SETTINGS.md](SETTINGS.md).

**Implemented** below means there is executable source implementing the feature. **Adapted** means the user workflow or native mechanics differ. It does not mean every scenario has been tested live. A registered command name or a declared setting is not proof of behavioral parity.

## Baritone Java features

1. A* navigation with configurable movement costs, segmented searches, next-segment preparation, path splicing, timeout/render-distance cutoffs, incremental cost backoff, minimum improvements and backtrack handling.
2. Walking, sprinting, diagonal walking, stairs/slabs, climbing and descending, ladders/vines, falling into water, water-bucket falls, pillaring, bridging, block breaking/placing, parkour and parkour placement; danger/liquid/falling-block avoidance and opening doors/fence gates.
3. Geometric goals: block, XZ, Y, two-block, get-to-block, near, axis, composite, inverted, runaway and strict-direction goals. Relative-coordinate commands, directional goals, surface, tunnel and camera goals.
4. Automated mining, ore searching in loaded chunks/cache, branch/legit mining, drop pickup, target blacklisting and tool selection with enchantments, durability and potion effects.
5. Following players/entities, exploration with chunk filters, crop harvesting/replanting/bone meal, and backfilling.
6. Schematic construction, layered/repeated builds, replacements/substitutes, masks, transformations, map art, selection operations and copy/paste. MCEdit/Sponge/Litematica file readers. Commands that consume placements opened in the separately installed Schematica/Litematica Java mods.
7. Elytra trajectory planning/steering, fireworks, equipment handling, landing, flight diagnostics and Nether terrain prediction through its Java generator/native pathfinder.
8. Hotbar/inventory management; competing processes with priorities, safe cancellation, forced cancellation, pause/resume and ETA/status reporting.
9. Persistent world cache, cached special-block searches, cache save/reload/repack, waypoints and automatic bed/death waypoints.
10. Path, goal, selection, build-action and cache rendering; click-goal/drag selection screen; command completion, clickable/hoverable Java chat, profiles, notifications and configurable chat controls.
11. Java client integration/API surfaces: goal/process/behavior/schematic interfaces, events and callbacks, and supported Java mod-loader distribution targets.

Java's feature page also describes **experimental pig control**. Boats and horses are listed as future/non-supported work, not finished Java features to port. Schematica/Litematica's textured hologram UI and their complete editing/verifier functionality belong to those external mods, rather than all being implemented inside Baritone itself.

## Bedrock features and comparison

| Java feature | Bedrock implementation / difference | State |
|---|---|---|
| A*, movement costs, incremental backoff and minimum improvement | `pathing/calc/AStarPathFinder`, `ActionCosts`, `CalculationContext` | Implemented |
| Segment preparation, cutoff, splicing and backtrack handling | `behavior/PathingBehavior`, `pathing/path/PathExecutor` | Implemented |
| Walk/sprint, diagonal, ascend/descend/downward, stairs/slabs | `pathing/movement/Moves`, `Movement`, `MovementHelper` | Implemented |
| Pillar, bridge, break/place during movement | Native player actions and inventory selection | Adapted |
| Parkour and parkour placement | Cardinal jumps plus the port's diagonal jumps | Implemented; physics needs Bedrock-specific validation |
| Water, bucket falls, ladders/vines | Native movement/click timing; water costs include Depth Strider | Adapted |
| Doors/gates and danger/falling-block handling | Movement helper/executor rules and native interactions | Implemented; not every configuration tested |
| Goal types and navigation commands | `pathing/goals/Goal.h`, `#goto`, `#goal`, `#path`, `#axis`, `#invert`, `#surface`, `#thisway`, `#tunnel` | Implemented |
| Camera goal / `#come` | Uses the local player camera; no separate Java freecam entity | Adapted |
| Mining, branch/legit mining, pickup and blacklisting | `MineProcess`, `GetToBlockProcess`, `BlockFilter` | Implemented |
| Exact block-state filters | `BlockSelector`, using Bedrock property names | Adapted |
| Tools, efficiency, durability, haste/fatigue and held-tool planning | `InventoryBehavior`, `ToolSet`, native enchantment decoding | Implemented |
| Player/entity follow | `FollowProcess`, player/entity selectors | Implemented |
| Explore and chunk filters | `ExploreProcess`, `#explorefilter` | Implemented |
| Farming and backfill | `FarmProcess`, `BackfillProcess` | Implemented; full farm campaigns not repeated |
| Build, layers, repeats, masks and substitutions | `BuilderProcess`, `schematic/Schematic` | Adapted: native placement cannot set arbitrary states |
| Selection shapes, transformations, copy/paste | `SelectionManager`, fill/walls/shell/sphere/cylinder/replace/composite schematics | Implemented |
| Map art / selection-only builds | Wrappers on the direct `#build` file workflow | Implemented |
| Sponge .schem v1/v2/v3 | `JavaSchematics`; v3 nested blocks, palette and varints validated | Implemented; v3 regression added in 1.4 |
| Litematica .litematic files | Multi-region/negative dimensions and cross-long bit packing | Implemented; not all Java states translate |
| Legacy MCEdit .schematic files | Numeric ID/metadata conversion; unknown IDs are reported/ignored | Adapted |
| Build an open Schematica/Litematica placement | In 1.3 these two commands only returned errors. In 1.4 both use the Bedrock workspace | Added in 1.4 |
| Schematic hologram placement workflow (external Java mods) | Built-in `#schematic load/move/rotate/mirror/layer/preview/build`; one placement, no Java mod dependency | Added Bedrock alternative in 1.4 |
| Material list (external Java mods) | `#schematic materials`: active placement block-position counts | Added in 1.4; not remaining inventory requirements |
| Bedrock .mcstructure | Native first block layer and states | Bedrock addition |
| Inventory and hotbar management | Server-authorized temporary inventory screen with response handling | Adapted |
| Equipment and elytra swapping | Native use/armor swap operations | Adapted |
| Process priorities, cancellation, pause/resume and status | `PathingControlManager`, `PauseProcess`, status commands | Implemented |
| Chunk cache, save/reload/repack and tracked-block searches | Two-bit disk/RAM cache and live validation of search results | Implemented |
| Bounded chunk packer | Deduplicated jobs packed within the game-thread budget | Adapted |
| Elytra planning, physics, rockets, landing, diagnostics | C++ flight planner with recorded swept hitboxes/rays | Adapted; full long-distance campaigns not repeated |
| Flight allocation/culling settings | C++ arena option and clean-region eviction | Adapted |
| Seed-based Nether terrain prediction | No Bedrock generator; observed/cache-based flight only | Missing |
| Persistent/automatic waypoints | `WaypointStore`, bed/death callbacks and `#wp` | Implemented |
| Path/goal/action/selection rendering | Native tessellator, color/depth batches; ShaderColor upload fixed in 1.4 | Adapted |
| Cached terrain rendering / goal beacon | Bounded category mesh and crossed filled beam, without Java textures | Adapted |
| Click goals and drag selection | Crosshair/mouse mode in place of Java `GuiClick` | Adapted; multi-frame native drag still not validated |
| Completion | `#complete` service, including blocks/states/files | Adapted; native Tab insertion missing |
| Clickable/hoverable chat and message tags | Text-only chat; prefix adaptation | Missing rich UI |
| Settings/profiles and notifications | English output, named profiles, Windows notification bridge | Adapted |
| Disconnect on arrival | Normal asynchronous native leave operation | Implemented and checked live |
| Java APIs and loader integrations | C++ goal/process/behavior/schematic interfaces and callbacks | Adapted; no Java binary compatibility |
| Experimental pig riding | No Bedrock-specific steering validation/integration | Unverified / not claimed supported |

All source paths in this table are below `src/baritone/` unless a native platform component is named. See [JAVA_PARITY_AUDIT.md](JAVA_PARITY_AUDIT.md) for the 1.3 changes and [SCHEMATICS.md](SCHEMATICS.md) for the new workflow.

## Features specific to this Bedrock port

- Bedrock `.mcstructure` import and Java-to-Bedrock block/orientation translation.
- Built-in schematic ghost, placement editing and material-position counts without an external schematic viewer mod.
- Diagonal parkour, ore-family/item aliases and extra diagnostics (`#debug`, `#nbt`, `#world`, `#ism`, memory/signature tools).
- Exact Minecraft package guard, ambiguous-signature rejection, caught-fault diagnostics, a game-thread watchdog, and clean DLL unload/reinjection.
- A C++ core with offline worlds/tests independent of Minecraft, and a Windows x64 injected DLL adapter.

## Gaps that are not solved by adding command names

The Bedrock Nether generator, native chat Tab/hover/click integration, Java mod-loader binaries, full textured block-model holograms and experimental vehicle steering are not implemented by this change. The surviving `elytraPredictTerrain`/`elytraNetherSeed` settings do not enable a generator. No substitute uses Java terrain as if it predicted Bedrock obstacles.

Schematic building remains normal player construction: block entities/entities, fluid secondary layers and every arbitrary Java block state cannot simply be pasted by a survival player. Discarded data and conversion limits are reported/documented; the feature inventory must not be read as a promise of a perfect WorldEdit-style world copy.

## Reproduce the comparison

```powershell
python tools/paritycheck.py re/baritone-java --json re/java-parity.json --markdown docs/COMMAND_INVENTORY.md
cmake --build build --config RelWithDebInfo
ctest --test-dir build -C RelWithDebInfo --output-on-failure
```

The structural audit currently finds 63 Java names/aliases, 95 Bedrock names/aliases and all 245 user-configurable Java setting names. Only the two terrain-prediction settings lack executable references. Behavioral qualifications above take precedence over those counts.

The final local 1.4.0 RelWithDebInfo build succeeded and the regression executable passed **59 tests, 0 failed**; CTest and `git diff --check` passed. The native preview and single-block build validation is detailed in [SCHEMATICS.md](SCHEMATICS.md#validation-on-bedrock-2660). These are targeted checks, not an exhaustive validation of every Java/Bedrock feature in this inventory.
