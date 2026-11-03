# Windows automation expansion (1.6.0)

Requested: all twelve proposed additions. No adaptive performance controller.
This expansion builds on Windows release 1.5.0.

| Addition | Completion requirement | Current state |
|---|---|---|
| Recursive crafting | Bounded dependency alternatives and confirmed intermediate crafting | Core execution passed; native read-only torch plan passed. Multi-step Survival execution pending. |
| Automatic restocking | Fresh supply surveys, absolute inventory targets, return and builder-session resume | Core transfer/depletion and interrupted-builder resume tests passed. Native chest survey/withdraw/return passed; native builder resume and multi-source trips pending. |
| Inventory loadouts | Saved quantities, hotbar roles, durability checks and deposit reserves | Core persistence/error tests passed. Native main-inventory/hotbar swaps, inverse swaps and chest-based fill passed; original inventory restored. |
| Advanced storage manager | Compatible partial merges, capacity, sorting, last-seen multi-container search | Core tests and native partial/full-source merges, chest sorting with scratch space and last-seen lookup passed. Barrel/shulker and multi-container coverage pending. |
| Construction planner | Support chains, temporary columns/permanent foundations, three builder phases | Core protection/cleanup tests passed. Native Creative execution built two temporary supports and the target, then removed both supports; final structure verified. Larger/Survival plans pending. |
| Textured schematic preview | Local HTML/WebGL viewer, resource-pack textures, orientation, opacity, layers | Real native export and browser rendering passed. Complex geometry uses placeholders; in-game overlay remains colored outlines. |
| Visual workflow editor | Native editable graph, conditions, bounded loops and failure branches | Core tests passed. Native save/apply/run, false inventory branch and two-node wait/success branch verified. More action-node scenarios pending. |
| Continuous portal travel | Same-world linked-exit confirmation, cooldown and route continuation | Core expected/unexpected transition tests passed. Real multi-portal travel pending. |
| Mining planner | Selected vein/tunnel/branch layouts, objective/time estimates, hazard checks | Core layout/navigation restriction tests and native one-block Creative vein excavation/final verification passed. Larger/Survival/tunnel/branch scenarios pending. Completion does not certify drop collection. |
| Tool maintenance | Durability policy, confirmed replacement, workstation repair and Mending progress checks | Core tests passed. Native anvil preview, cost refusal and repair passed. Grindstone controller/preview/input mappings verified; complete damaged-tool combine, Survival XP and live Mending scenarios pending. See the repair guide. |
| Action history | Stable observations, bounded history and conditional undo of supported placements/removals | Core observation/change/restore tests and native conditional undo of an owned cobblestone placement passed. Session-only; no NBT restoration. |
| Compatibility doctor | Package, DLL architecture/hash, signatures, player and inventory policy | Native doctor passed 27/27 signatures; external package/DLL/world/policy checks passed. |

Core tests, successful compilation and native gameplay checks are separate milestones.
An added command or a planner alone does not establish a working native module.
No release should describe all twelve as complete until their runtime integration
and relevant validation are recorded here.

Target: Windows x64 package **1.26.6005.0** (Bedrock 26.60), release **1.6.0**.

## Production and inventory

```text
#craft plan torch 16
#storage add workbench x y z
#craft auto workbench torch 16
#craft status
#recipe list
#recipe set torch 4 1*coal|charcoal 1*stick
#loadout capture mining
#loadout set mining cobblestone 16 64 8
#loadout set mining diamond_pickaxe 1 1 0 20
#loadout check mining
#loadout use mining
#loadout fill mining [supplyLocation]
#loadout equip mining
#loadout off
```

Craft quantities are **additional output**. Restock/loadout targets are **total
inventory quantities**. The editable starter recipe catalog has 18 outputs; it is
not an extraction of every game recipe. Native recipes, unlocks, ingredients and
server results remain authoritative. Missing base resources stop production before
crafting. Custom descriptions persist in `recipes.txt` in the Baritone data folder.

Loadouts persist globally in `loadouts.txt`. Active minimums protect **storage
deposits**; they do not reserve crafting ingredients or control every other module.
Filling equips the profile; use `#loadout use` to activate its deposit policy.
Quantity counts include damaged tools, while equipping separately checks durability.
Unreadable policies block deposits until repaired/reloaded or explicitly reset.

## Supplies and storage

```text
#storage add supplies x y z
#storage scan all
#storage find cobblestone
#storage sort supplies
#restock item cobblestone 64 [supplies]
#restock auto on [supplies]
#restock auto off
#restock status
```

Surveys inspect registered containers and return. Search reports **last-seen**
counts/timestamps held in memory until a world/dimension change. Each restock job
surveys sources afresh. Missing/inaccessible supplies stop with an explanation;
confirmed partial transfers remain in inventory. Furnace/crafting slots are excluded.

Merges require supported plain-stack identity, capacity and native/server checks.
Unknown/custom metadata uses empty destinations. Sorting compacts matching piles
then orders item identifiers. Swapping occupied slots needs an empty container or
inventory scratch slot. Cancellation can leave an item in that slot; inspect before
restarting. No automatic rollback of completed inventory actions is claimed.

Automatic restocking reacts to a paused builder's material deficit, fetches small
batches and resumes only the same builder session after success. Failure leaves the
builder paused and disables the monitor. Legacy block/item aliases still need
checking. Stop/world changes disable the monitor.

## Construction and mining

```text
#schematic load structure.mcstructure x y z
#construction plan temporary cobblestone 8
#construction start temporary cobblestone 8
#construction start permanent stone 8
#mining plan vein diamond_ore
#mining plan tunnel iron_ore
#mining start branch diamond_ore
```

Construction: at most 262144 source cells, 65536 support blocks, depth 1..16;
support material cobblestone, stone or dirt. Columns must reach loaded stable ground
or a planned block. Existing plants/fluids/obstacles are not silently replaced.
Falling targets depending on columns require permanent foundations. Execution
builds columns, builds the structure, then conditionally removes temporary columns.
Permanent foundations remain; cancellation can leave temporary supports in place.
Complex attached blocks may require manually prepared access/supports.

Mining uses the newest selection, at most 128 per axis and 262144 cells. Tunnels
run on X at central Z, two blocks high; branches add Z corridors every four X
blocks. They require an existing continuous floor. Vein mode targets the chosen
known blocks. Liquid boundaries, falling roofs, unknown chunks and protected
modifications fail preflight. Navigation cannot break outside the layout or place
scaffolds. Estimates exclude walking, effects, Fortune, Unbreaking and pickup.
Completion verifies excavation, not collected item quantities.

Planned execution needs exact builder settings without layer offsets, repeats,
ignored/skipped blocks, ignored existing blocks or substitutions. Normal permissions and
protection policies remain in effect. Preflight alone is not execution validation.

## Local textured preview

```text
#schematic rotate 90
#schematic mirror x
#preview3d "C:\Users\yourname\Downloads\structure-preview.html"
```

Use **Export textured 3D** in the panel or the command after loading a schematic.
Open the HTML in a WebGL browser and select an unpacked resource-pack folder with
`blocks.json`, `textures/terrain_texture.json` and PNG textures. Files stay local;
the CSP blocks network connections. No CDN dependencies or Mojang assets are bundled.
The bot and in-game colored overlay do not require a pack.

Models support cubes, slabs, straight stairs, log axes and supported cardinal
orientations. Other/connected shapes use cube placeholders; biome tint, animation,
entities and NBT are not simulated. Missing textures use a checkerboard. Blended
transparency is approximate. Limits: 65536 non-air blocks, 255 textures and 200000
visible faces. Existing export files are preserved.

Mapping follows [Microsoft's blocks.json documentation](https://learn.microsoft.com/en-us/minecraft/creator/reference/content/blockreference/examples/blocksjsonfilestructure?view=minecraft-bedrock-stable)
and [Mojang's sample resource pack](https://github.com/Mojang/bedrock-samples/tree/main/resource_pack).

## Visual workflows

Open **Workflow editor**, create node 0, enter its action/arguments, success/failure
destinations, visits and timeout, then Save node. Append consecutive indices or
select a row to edit it. Apply loads the complete graph paused; inspect the log
before Run. Load current/import file restores definitions. Editing resets progress.

Edges: node index, `-1` finish, `-2` stop. Limits: 128 nodes, 1..100 visits per node,
512 dispatches per run, one dispatch/poll per tick. Recovery is paused and bound to
the world/dimension. Malformed checkpoints are preserved until explicitly replaced.

```text
#workflow node 0 -1 1 2 30 ifitem torch 16
#workflow node 1 0 -2 1 600 restock torch 16 supplies
#workflow run
#workflow pause
#workflow restart
```

This graph finishes if 16 torches exist, otherwise restocks and checks again.
`ifitem` branches immediately; `inventory item count` waits. Other actions: goto,
wait, mine, build, verify, withdraw, deposit. Editor coordinates are absolute and
wait durations are **ticks**. Storage nodes require location, item, quantity and a
checkpointed absolute inventory target; prefer `restock item target [source]`.
Bounds do not undo repeated nodes' completed effects.

## Portals, tools and history

`#portal go destination` retains its captured route through registered links.
An entry must point inside an active portal; continuation requires the same world,
linked dimension and arrival within eight blocks of the registered exit. There is
a cooldown and bounded deadline. Unexpected changes, reconnects or leaving the
world cancel intent. No portal creation/discovery or same-dimension gateway routing.

```text
#maintenance check
#maintenance on 10
#maintenance off
#history list
#history undo 4
#history clear
#doctor
```

Maintenance preserves worn tools and replaces them at/below the remaining-durability
threshold using suitable inventory spares. It preserves class, minimum tier, Silk
Touch mode and minimum Efficiency. Missing spares/unconfirmed moves pause the bot.
Stop/world changes disable this replacement policy. Explicit repair jobs now use
`#repair preview|start anvil|grindstone station toolSlot materialSlot count maxLevels`.
`#repair mend xpWaypoint toolSlot targetPercent timeoutSeconds` equips a Mending
item at a registered XP waypoint and waits for observed durability progress.
The [tool repair guide](TOOL_REPAIR.md) explains donors, cost ceilings, preview
inventory movements, cancellation and native validation limits. Automatic
replacement does not choose repairs or spend XP by itself.

History records stable observations following bot break/place intentions; concurrent
unrelated changes cannot be perfectly attributed. It holds 4096 session entries,
clears on world/dimension changes, and conditionally undoes at most 64 in reverse
order. Undo checks live state and protection each tick, restores supported plain
full-block placements/removals through normal builder actions and requires materials.
It skips changed/unloaded cells, arbitrary replacements, fluids, falling blocks,
block entities and unsupported states. It does not recover items, inventory or NBT.

## Validation

```powershell
cmake --build build --config RelWithDebInfo
ctest --test-dir build -C RelWithDebInfo --output-on-failure
python tools/doctor.py --report re/doctor.json
python tools/test_preview3d.py
```

The browser test uses Playwright Chromium, or `--browser` with an existing
Chrome/Chromium executable. Its generated test textures check actual rendering,
local JSONC/PNG import, layers, opacity, orbit and absence of HTTP requests.
`--html path.html` also checks a real native export.

Recorded 2026-11-02/03: **124 core tests passed** in both the normal and
AddressSanitizer builds; DLL/control panel compiled. Native checks passed for the
doctor, automatic Creative inventory opening, main/hotbar swaps and their inverses,
partial/full-source merges, chest sorting, fresh restock/loadout-fill trips,
three-phase temporary-support construction, one-block vein excavation, conditional
placement undo, visual workflow branches and real textured HTML export. The browser
test also passed against that native export using installed Google Chrome.

All native tests used the existing Creative world; no new world was created.
Owned fixture blocks were removed, original inventory names/counts/tool damage and
selected slot restored, and temporary storage/profile/selection/workflow definitions
cleared. The later anvil repair intentionally improved the wooden pickaxe from
58/59 to 59/59 durability; its original slot and all inventory quantities were
restored after repair-fixture cleanup. Tests do not establish Survival behavior, every container type or every
cross-module combination. Remaining scenarios are listed above.

An idle/native-action timing sample after the inventory-description change reported
bot tick mean 2.181 ms, p95 4.576 ms and p99 7.144 ms. This is one local sample;
the game window/render load differed from earlier samples, so it does not establish
an FPS improvement or guarantee an absence of lag. No adaptive performance
controller is included.

The final regression review also rejects block substitutions before planned
construction/mining execution: substituted supports would not match the conditional
cleanup contract. Read-only construction preflight remains available with those
normal-builder settings. The final DLL was rebuilt/reloaded and the external
architecture/package/connection/world/inventory-policy checks all passed.

Repair extension: native anvil execution confirmed the improved output and normal
server response; preview-only return and cost-ceiling refusal also passed. The
grindstone's two network input slots (16/17), controller and cached preview were
checked live. Full automated grindstone execution and live Mending repair at an
XP source remain pending; the additional core tests cover their adapter contracts.
