# Tool repair (Windows expansion, 1.6.0)

Target: Bedrock 26.60, Windows x64 package `1.26.6005.0`. Repair jobs use normal
workstation/controller actions and server responses. They do not edit durability,
create XP or manufacture inventory items directly.

## Anvil and grindstone

Register the workstation's block position and inspect inventory slot numbers:

```text
#storage add workshop x y z
#inv all
#repair preview anvil workshop 0 12 2 5
#repair start anvil workshop 0 12 2 5
#repair status
```

Arguments after the station are **tool slot**, **material/donor slot**, **material
quantity**, **maximum XP-level cost**. Slots are 0..35; hotbar slots are 0..8.
The example offers up to two items from slot 12 to repair the damaged tool in slot
0, accepting a native cost of at most five levels. Actual cost and recipes come
from Minecraft. A configured ceiling is not a claim that the player has those XP
levels; the server can refuse the action.

`preview` walks to the station, temporarily puts the specified ingredients into
its initially empty inputs, reads the native preview, closes the interface and
checks their return. It does **not** take the output. It changes inventory slots
temporarily and is not a purely read-only command. `start` additionally takes one
verified output, confirms the server responses, observed inventory improvement
and consumed-input state, closes the interface and returns to the starting point.

Each job needs two empty inventory slots, one damaged durable item, a distinct
material slot, quantity 1..64 and a level ceiling 0..39. Anvil materials are a
matching donor item or supported base materials: planks, cobblestone, relevant
metal/diamond/leather, phantom membrane or turtle scute. The native preview must
improve durability while keeping the original item type, name and enchantment
list exactly. Enchantment-book jobs, renaming and enchantment upgrades are excluded.
Custom/add-on recipes are not inferred from these base-material rules.

For a grindstone, supply one matching, unnamed, unenchanted donor tool:

```text
#storage add bench x y z
#repair preview grindstone bench 0 12 1 0
#repair start grindstone bench 0 12 1 0
```

Both items must be unnamed and unenchanted. The chosen donor is consumed when
execution succeeds. This mode does not automate disenchanting valuable equipment.
Ordinary game behavior, including anvil wear and repair-history cost, still applies.

`#stop` cancels the job and queues normal closure of its owned workstation.
`#repair close` stops repair/Mending and closes only the bot-owned interface.
Minecraft normally returns input items when closing; if the inventory/world changes
during the job, inspect the returned slots and surroundings. There is no blind
rollback or repeated repair submission after a refusal/uncertain response.
Completed repair outputs are not included in block-history undo.

Advanced inspection: `#repair open workshop` interacts with a nearby registered
station without walking; `#repair status` reports its models and cached preview.
Opening a station alone does not consume inputs or take a result.

## Mending at an existing XP source

Stand at the intended XP collection point and save a uniquely named waypoint:

```text
#wp save user xp_farm
#repair mend xp_farm 20 95 300
#repair status
```

Arguments are **XP waypoint**, **tool slot**, **target remaining-durability percent**,
**total timeout in seconds**. Targets are 1..100%, timeouts 30..1800 seconds. The
waypoint must be unique and in the current dimension; no portal trip is implied.

The bot walks to that point, equips the damaged Mending item, waits for actual
durability changes, restores the borrowed hotbar slot after success and returns
to its starting point. A main-inventory item temporarily exchanges places with
the selected hotbar stack. Mending can share available XP with other eligible
equipment; this process does not remove the player's armor to influence selection.

The XP source must already function independently. This job does not build an XP
farm, attack mobs, mine XP ores or throw experience bottles. It stops on health
loss, a changed/disappearing tool, additional tool damage, leaving the source,
an unrelated open interface, sixty seconds without durability progress or its
overall deadline. Cancelling/failing while a swap is pending can leave the tool
in the borrowed hotbar slot; the log identifies the slots to inspect. It never
reports success merely because time elapsed.

Automatic replacement (`#maintenance on`) remains a separate policy. It does not
choose a repair recipe, spend XP or select a donor tool on the player's behalf.
Repair jobs are explicitly started with the commands above.

## Recorded validation, 2026-11-02/03

- **124 core tests passed**, both normal and AddressSanitizer builds. Repair
  coverage includes preview/input recovery, cost ceilings, refusal without retries,
  confirmed improved output, name/enchantment preservation, explicit grindstone
  donor consumption, Mending progress, borrowed-slot restoration, no-XP timeout
  and stopping on health loss. Core tests use a simulated player adapter.
- Native Creative anvil execution passed: automatic input transfers, preview of
  damage 1 -> 0 with cost one level/one plank, preview-only return of all inputs,
  rejection with a zero-level ceiling, then accepted execution and observed output.
  The existing wooden pickaxe was repaired from 58/59 to 59/59 durability and
  returned to its original inventory slot 18.
- Native grindstone opening, controller recognition, preview reading and manual
  deposit/return in both input slots passed. A full automated combine of two
  damaged tools is **still pending**. Live Mending progress at an XP source is
  **still pending**, as are Survival XP debit/anvil-wear and enchanted/named-item
  gameplay scenarios. Offline coverage is not substituted for those checks.
- The temporary anvil/grindstone block, their Creative item piles, remaining test
  planks, station registration and selection were removed. Original inventory
  quantities/slots and selected slot were restored; the wooden pickaxe retains
  its intentional durability improvement. The final compatibility doctor passed.

Native ABI findings are recorded in [NATIVE_MODULE_NOTES.md](NATIVE_MODULE_NOTES.md).
Reference declarations: [anvil controller](https://github.com/LiteLDev/LeviLamina/blob/main/src/mc/world/containers/managers/controllers/AnvilContainerManagerController.h),
[grindstone controller](https://github.com/LiteLDev/LeviLamina/blob/main/src/mc/world/containers/managers/controllers/GrindstoneContainerManagerController.h),
[enchantment identifiers](https://github.com/LiteLDev/LeviLamina/blob/main/src/mc/world/item/enchanting/Enchant.h).
These declarations are checked against this client binary rather than assumed to
provide compatible offsets. The official [enchantment guide](https://learn.microsoft.com/en-us/minecraft/creator/documents/introtoenchantments?view=minecraft-bedrock-stable)
describes Mending's XP-based repair behavior.
