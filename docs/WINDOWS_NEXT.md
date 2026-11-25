# Windows automation and validation

This guide covers Windows automation commands and records the gameplay checks
performed for each release. Built-in messages are in English.

## Scope and remaining work

| Requested addition | Current implementation | Remaining work |
|---|---|---|
| Production objectives | `#produce` coordinates stock acquisition, supported gathering, recursive crafting and delivery, with opt-in smelting and harvesting | Full Survival/native runs of combined jobs |
| Game recipe catalog | Local Bedrock shaped/shapeless JSON importer, explicit tag mappings, up to 2048 planning outputs | Direct native ingredient extraction, multiple recipes per output and auxiliary item variants |
| Smelting workshop | Empty-station survey, distributed input/fuel, later output collection and unused fuel return | Native multi-furnace/blast-furnace/smoker validation |
| In-game textured ghosts | Opt-in local Bedrock resource pack PNGs rendered on nearby difference boxes, visually checked on Bedrock 26.65 | Complex models, live texture atlas integration and larger preview benchmarks |
| Manual build assistant | Nearby missing-block highlight, matching hotbar selection and orientation text | Native visual/gameplay checks; complex blocks and moving main-inventory stacks are excluded |
| Construction recovery | World/dimension-bound plan checkpoints including support columns and phase; explicit recovery checks current chunks/protection/obstacles | Native crash/restart and large Survival site validation |
| Portable shulker logistics | Survival placement, observed withdrawal and matching box recovery verified on Bedrock 26.65 | Additional item metadata formats and larger transfer batches |
| Interactive map | Native control-panel map with loaded/session-visited chunks, route, local markers and protection footprints. Live selection and rejection of a request for another save also passed | Native navigation to a selected destination on this build, persistent exploration layers and terrain-height rendering |
| Roads and bridges | Bounded road preview, headroom, lighting and existing foundation/construction integration | Native construction and larger terrain validation |
| Specialized farms | Bounded single-crop jobs for sugar cane, bamboo, cactus, pumpkin and melon; base preservation and observed item targets | Native crop fixtures still required |
| Villager trading | Not implemented in this expansion | Native offer discovery, budgets, stock refresh and confirmed trade actions |
| Rail travel | Per-world station registration, rail geometry and passenger minecart control | Native route validation passed on a three-rail fixture, full in-game ride and dismount still to validate |
| Technical build validation | Bounded static audit flags technical components, unsupported states and block-entity contents | Native validation; no redstone circuit/timing simulation |
| Exportable diagnostics | Local ZIP with version/signature status, recent events, task/workflow definitions, settings and bounded log tail; `#report` produced a valid archive on Bedrock 26.65 | Panel export validation |
| Workflow library | Paused, configurable build/stock, gather/store, produce/store and loadout templates | Native end-to-end template validation |

The initial read-only check on **2026-11-07** found installed package
`1.26.6301.0` while the native profile still targeted `1.26.6005.0`.
The compatibility guard was preserved while the fixed native references were
recovered. Version 1.7.0 now targets `1.26.6301.0`; loading,
short navigation and reversible inventory transfers have been checked in the
existing world. See [the 26.63 port notes](DEVELOPMENT.md#reverse-engineering-notes-2663).
`re/windows-next-doctor.json` records the initial mismatch, while
`re/fix-2663-final-doctor.json` records the corrected runtime.

A subsequent read-only dump/signature audit found **27/27 unique signatures**,
but **0/5 exact fixed crafting/anvil/grindstone profile entries** still match at
their old locations (the baseline dump matches 5/5). This confirms that updating
only the accepted package number would be insufficient. Local evidence:
`re/windows-next-2663-signatures.json` and
`re/windows-next-native-2663-audit.json`.

## Production objectives

```text
#storage add table x y z
#storage add supplies x y z
#storage add output x y z
#produce table torch 128 supplies output
#produce table torch 16 -
#produce table iron_pickaxe 1 - output furnace
#produce table paper 12 - output - 16
#produce status
#stop
```

The quantity is **additional output**. A supply pass can retain confirmed partial
withdrawals; planning then uses actual inventory. Gathering children receive an
absolute target and are checked against the requested item, not ore blocks already
in inventory. Known policies cover ordinary coal/raw-metal/diamond/lapis/redstone
drops, cobblestone/deepslate, logs, dirt, sand, netherrack and blackstone. Other
missing inputs stop before gathering; supply those materials explicitly. Ordinary
ore drops require putting Silk Touch pickaxes into storage, because the miner's
tool selector could otherwise choose one.

When an empty furnace is registered, `#produce` can smelt iron, copper or gold
ingots, glass or stone as intermediate ingredients or final outputs. It uses coal
and keeps active loadout reserves. A farm radius of 1–64 blocks around the starting
position enables harvesting missing sugar cane, bamboo, cactus, pumpkins or melon
slices. The job replans from observed inventory after each successful stage.

Dependency calculations run in a bounded background job. Inventory changes while
planning trigger a fresh calculation. Limit: 64 planning passes, one hour and the
existing 2304-item crafting target. The job stops on missing resources, failed
children or unconfirmed output. Optional delivery deposits the requested additional quantity,
leaving existing stock and any crafting-batch surplus. Completed actions are not
rolled back on cancellation.

## Importing recipe descriptions

```powershell
python tools/import_recipes.py "C:\packs\matching-pack\recipes" "C:\exports\recipes.txt"
```

```text
#recipe import "C:\exports\recipes.txt"
#craft plan torch 128
```

Use recipe files matching the installed game/behavior pack. The importer follows
Microsoft's [shaped recipe format](https://learn.microsoft.com/en-us/minecraft/creator/reference/content/recipereference/examples/recipedefinitions/minecraftrecipe_shaped?view=minecraft-bedrock-stable)
and [shapeless recipe format](https://learn.microsoft.com/en-us/minecraft/creator/reference/content/recipereference/examples/recipedefinitions/minecraftrecipe_shapeless?view=minecraft-bedrock-stable).
It only imports crafting-table definitions with supported plain item descriptors.
An explicit `--tags mapping.json` can resolve tags to exact item alternatives.
Default mappings cover plank and stone-crafting ingredients; review them for custom
packs. It reports unsupported workstations, metadata/auxiliary variants, multiple
outputs, container-return recipes, malformed definitions and unresolved tags.

One recipe per output is chosen deterministically by priority, ingredient count
and source path. The sidecar report lists competing variants and selected files.
Dependency cycles are refused by the core planner. Unlock descriptions are
reported; availability/unlocks are checked by Minecraft when executing a real
recipe. Importing does not unlock or override a game recipe. Existing importer
output files are preserved; importing in Minecraft explicitly replaces the saved
planning catalog after validation. The default remains the small starter catalog.

## Smelting workshop

```text
#storage add furnace_a x y z
#storage add furnace_b x y z
#workshop start raw_iron iron_ingot 32 coal 4 furnace_a furnace_b
#workshop status
```

Register 1..16 distinct stations. All are surveyed empty before loading, then each
is checked again when used. Input and the explicit total fuel budget are divided
between stations, with 1..64 of each per station. Supported models include furnace,
blast-furnace and smoker inputs. Minecraft decides whether the chosen recipe/fuel
combination is valid; the pool does not estimate burn duration or refill by itself.
Stations are loaded before later collection passes, allowing normal furnace work
to overlap. Inputs may come from multiple compatible plain stacks. Keep empty
inventory slots for output and unused fuel.

Only one-to-one recipes are supported. The pool checks server receipts, output
type/quantity, unexpected leftover input and the final inventory target. It stops
on refusal, unexpected UI closure, full inventory or its one-hour/512-action limit.
It never blindly resends an unconfirmed transfer. Cancellation/failure can leave
input, fuel or output inside stations; inspect them before starting another batch.

## Manual building and technical audit

```text
#schematic load house.mcstructure x y z
#assist on
#assist status
#assist off
#technical
```

The manual assistant highlights a supported missing block in gold and selects a
matching **hotbar** item when the suggested target changes. It supplies expected
orientation states via status and the panel. The player moves and clicks. Wrong
blocks, unloaded cells, protected positions, unsupported states and technical
components are skipped. Keep materials in the hotbar and prepare suitable support
faces yourself. The hint refreshes at most twice per second within eight blocks;
it is guidance, not proof that a particular face is reachable. Other bot activity
suspends hints; stop/world changes disable assistance.

`#technical` checks at most 262144 cells and records at most 512 issue samples,
printing the first 32. It flags redstone/rail components, states outside the
placement controller's orientation support and block-entity contents/text. It is
a **static audit**: it does not simulate circuits, timings, fluid updates or NBT.
Existing schematic verification compares supported block/orientation behavior;
it must not be presented as validation of every redstone state.

## Recovering a construction job

```text
#construction start temporary cobblestone 16
#construction checkpoint
#construction recover
```

A ready, executing construction plan saves a bounded checkpoint in the world's
`construction` data category. It includes exact target descriptions, support
columns, the foundation policy and current phase. Encoding/writing happens off
the game thread; the immutable plan snapshot is copied once after planning.
Each phase waits for its checkpoint to be saved before it can modify blocks.
`#construction checkpoint` shows the recorded phase and support/target counts.
Loading a world does not automatically resume any job. Run `#construction recover`
explicitly after inspecting the site and inventory.

Recovery requires the same local save identity and dimension, and exact builder settings. It
checks all target/support cells incrementally for loaded chunks, current
protection and fluid/unbreakable obstacles before resuming. A partially completed
phase is repeated by comparing live blocks, rather than replaying inventory
transactions. Cleanup removes only cells still matching the recorded support
material. Same-material replacements by another player cannot be distinguished;
inspect an externally modified site before recovery. A checkpoint is not a world
backup or inventory rollback. Corrupt files are refused; completed phase-3 jobs
cannot be restarted through recovery. A crash before the first write finishes
can leave no checkpoint, and later recovery can repeat the last saved phase.

## Map and roads

Open **Map** in the control panel. Green cells are loaded chunks, gray cells were
loaded earlier this session, and dark cells are unknown. The map follows the
player across a 25-by-25-chunk view. It also draws a bounded path, nearby storage,
portals, waypoints, the active building origin and protected-area footprints.
Protected footprints project all heights onto the map. Click to select a
destination, then **Go to selection**; the command checks the world/dimension
again before starting navigation. Cached terrain is not read from disk for these
map updates. This view does not show surface heights or every persistent explored
chunk. Marker/zone/path counts are bounded and can omit entries in dense areas.

```text
#road 100 63 100 140 65 110 3 cobblestone 8
#schematic requirements
#construction plan permanent cobblestone 16
#construction start permanent cobblestone 16
```

Road coordinates describe **paving blocks**, not player feet. Routes go along X
then Z, up to 256 horizontal steps, odd width 1..9 and grade at most 1:4. Paving
is stone, cobblestone or dirt. Lighting is off (`0`) or every 4..64 blocks and
needs width at least three. The preview includes two blocks of walking headroom;
inspect removals before construction. Solid height changes are steps, not an
automatically generated stair/slab surface. Foundation planning needs loaded dry
ground within the chosen depth (maximum 16); it does not invent supports across
oceans or unload terrain. The bounding volume must fit 262144 cells; split larger
routes. Preview generation never starts building.

## Targeted crops

```text
#harvest sugar_cane 64 16
#harvest bamboo 64 16
#harvest cactus 32 16
#harvest pumpkin 16 16
#harvest melon 64 16
```

The quantity is additional inventory output; melon counts slices. Radius is
1..64 (default 16). Jobs wait for an observed quantity, bounded to one hour. The
bottom block of sugar cane, bamboo and cactus is preserved even if generic
`replantCrops` is disabled. A current selection always restricts specialized
harvesting. Only the selected crop and its dropped items are targeted; no bone
meal is spent. Pumpkin/melon stems are not harvesting targets. Missing ripe crops
cause a bounded wait, not a fabricated completion. The generic `#farm` remains
available for mixed crops and its existing planting behavior.

## Portable shulker withdrawal

```text
#shulker inspect 3
#shulker withdraw 3 diamond 8 ~1 ~ ~
#shulker status
```

Place the box in hotbar slot 1–9 to withdraw. Inspection can read inventory
slots 1–36. Use Survival, with two empty cells above a reachable solid floor
within three blocks. The box and its contents must use supported vanilla stack
data. The original hotbar slot is reserved for pickup. The process matches the
original container slots, checks the inventory increase after each receipt and
verifies the remaining contents in the recovered box. Interruption or a failed
check reports the last box position for manual pickup.

## Minecart routes

```text
#rail add departure x y z
#rail add arrival x y z
#rail inspect arrival
#rail route departure arrival
#rail go departure arrival
```

Register both rail blocks in the same dimension. The planner follows up to 512
loaded track blocks and checks slopes, curves, clearance and protected regions.
Use a passenger minecart at the departure station and start within eight blocks.
Provide powered rails as needed. The arrival rail must be flat and unpowered,
with solid ground and two blocks of clearance on both sides. Baritone waits for
the cart to stop on that rail before sending the normal dismount input. Use
`#stop` to release the controls and `#rail status` to inspect the current phase.
`#rail inspect x y z` checks the same stopping requirements before you register
a station. Its result covers the rail and both exits. Use `#rail route` to check
track connectivity.

On Bedrock 26.65, station commands, planning on a connected three-rail track and
rejection of an arrival without a braking rail passed in an existing save. The
temporary stations and tracks were removed. Simulated tests cover curves,
slopes, blocking changes, boarding and dismount confirmation. Complete rides
with a real cart, propulsion and a braking rail still need a gameplay test.

## Workflow templates and diagnostics

```text
#workflow templates
#workflow template prepare-build "house.mcstructure" 100 64 100 supplies stone 64
#workflow template mine-store coal_ore coal 32 output
#workflow template produce-store table torch 16 supplies output
#workflow template equip mining supplies
#workflow template harvest-store sugar_cane 64 output 16
#workflow list
#workflow run
#report "C:\exports\baritone-report.zip"
```

Templates are loaded **paused**. Quantities for gather/produce templates are
additional at creation; their nodes store absolute targets for recovery. Deposit
nodes preserve starting stock, subject to active loadout reserves. The build
template loads its own previewed placement before building and verification;
verification blocks the workflow if supported comparisons report missing, wrong,
extra or unknown blocks. The original generic build task remains available.

The **Export report** panel button creates the same local ZIP as `#report`.
Nothing is uploaded. The archive contains status/version/signature readiness,
recent diagnostics, current task/workflow definitions, bounded settings/catalog
tails and up to two MiB of logs. It excludes saves, chunk caches and credentials.
Logs and command definitions can include world names, coordinates and local paths:
inspect the archive before sharing. Limits are 16 entries, two MiB per entry and
eight MiB total; existing output files are preserved.

## Validation

### 1.9.0 on Bedrock 26.65

- The offline profile check passed all nine references for `1.26.6501.0`.
  The same check rejects `1.26.6301.0`.
- The 152 C++ regressions passed in the normal and AddressSanitizer builds.
  The five Python importer tests passed.
- Minecraft returned help for 108 commands and 27 aliases. The 28 read-only
  probes, navigation preview, schematic and texture import, road and foundation
  planning, technical audit and HTML/ZIP exports passed. The exported WebGL
  viewer ran in Chromium.
- A 12-cell test schematic produced two issue pages. The command handled an
  empty category and refused an out-of-range page and stale placement report.
  Rail inspection diagnosed a coordinate without a usable rail and refused an
  unknown station. Offline tests cover a ready platform, slope, active power,
  blocked exits, unsafe footing and entry protection.

The existing peaceful save retained its position during the command checks.
Full native rail and elytra trips and combined Survival jobs remain open.

### 1.8.0 on Bedrock 26.65

The Windows candidate was tested on Bedrock 26.65 on 2026-11-19.

- **151 C++ tests** passed in the normal and AddressSanitizer builds. The five
  Python recipe importer tests passed. Offline profile checks passed for both
  supported Windows packages.
- The game resolved 27 native signatures. It returned help for 108 primary
  commands and 27 aliases, and passed 28 live status and read-only probes
  plus the navigation preview, road and foundation planning, schematic
  import, texture import and rejection, technical audit, HTML viewer export
  and diagnostics ZIP checks. Chromium displayed the exported viewer.
- The minecart regression reproduced incorrect cancellation when boarding was
  confirmed on the second rail. It now passes, including observed dismount in
  the simulated trip. A complete in-game ride remains pending.
- The control panel showed the live world and chunk map. The selected
  destination label now survives refreshes. Closing the map clears its selection.
  An empty inventory now produces visible feedback from `#inv all`.

The read-only checks and previews left the player at the same position in the
peaceful save. Full in-game rail and elytra trips, the unimplemented villager
trading module, and combined Survival jobs remain open.

```powershell
cmake --build build --config RelWithDebInfo
ctest --test-dir build -C RelWithDebInfo --output-on-failure
python tools/test_import_recipes.py
python tools/doctor.py --report re/windows-next-doctor.json
```

Recorded on 2026-11-07:

- DLL and control panel compile in RelWithDebInfo.
- **139 C++ core tests** and **5 Python importer tests** pass, including the
  AddressSanitizer core build. Final CTest runs took 7.24 seconds normally and
  13.56 seconds with ASAN on this machine. Core cases cover gather/replan/craft, fresh chest
  acquisition/delivery, loadout-reserve refusal, paused workflow targets, archive
  limits/CRC, large catalog loading, manual hints/protection, technical audit
  bounds, roads/foundations, support-preserving checkpoint recovery, map framing,
  multi-station loading/collection, rejected/unconfirmed transfers and base-stem
  preservation with generic replanting disabled.
- An independent executable using the production archive writer produced a ZIP
  that Python's `zipfile` accepted: all entry names, CRCs, binary data and timestamps
  were checked. This validates the archive format, not the in-game export command.
- The compiled panel and map opening/disconnected state were inspected through
  Windows computer-use screenshots. A clipped legend was corrected and Go remains
  disabled until a map destination exists. No synthetic map was presented as live
  game data, and no gameplay command was submitted during this UI check.

Historical evidence from the 26.63 audit is retained locally under
`re/windows-next-*` and `re/diagnostic-check/report.zip`.
