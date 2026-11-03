# Native module integration notes

Target: Windows package 1.26.6005.0. Generated headers are references; live layouts and virtual functions must be checked for this binary.

- GameMode vtable RVA `0xF1A8E90`: slot 14 `interact(Actor&, Vec3 const&, HandSlot)` at RVA `0x37685D0`, slot 15 attack at `0x3769340`. Register use was checked in disassembly. Calls require live entities, reach/line-of-sight checks and permitted modification at their position.
- The native interaction return value is not an outcome receipt: chicken feeding returned false while the animal entered in-love state. The adapter reports successful dispatch after its checks, and processes separately require observed breeding/death/riding metadata. One feeding and immediate-repeat refusal were checked live.
- ContainerManagerModel: container id `+0x50`, type `+0x51`, MSVC unordered-map object `+0xC8`, linked-list head `+0xD0`. Inventory screen exposes fourteen models in the observed session. Model-name and collection bounds are checked before access.
- ContainerModel: FullContainerName at `+0x30`, item vector at `+0x100`; virtual slot 4 returns size, slot 8 returns ItemStack by model index, slot 13 sets an item. Native ItemStack stride is `0x98`.
- The client inventory model has 27 slots, hotbar nine, combined inventory 36. Network inventory addressing uses the existing verified inventory adapter; UI model indices must not be confused with player inventory indices.
- FullContainerName is 12 bytes: byte enum, optional uint value at `+4`, presence byte `+8`. Native transfer slot info is 40 bytes, slot at `+0xC`, net-id variant at `+0x10`.
- ActorDataFlagComponent stores the actor bitset. Baby is bit 11, in-love 7, saddled 8, tamed 28, riding 2. Missing metadata is treated as unknown, never assumed adult or mountable.
- Crafting controller callback map at manager `+0x60`; observed CraftingContainerManagerController vtable `0xF1B4690`, slot 16 at `0x3A4C510` is the normal auto-place-stack recipe path. SlotData and AutoPlaceItem are 40 bytes. Recipe model virtual slot 11 returns ItemStackBase via ItemInstance (slot 8 is the empty ItemStack for these models). Native Creative actions and observed inventory output were confirmed live. These interfaces are guarded to this exact profile.
- ItemStackResponseInfo uses a 40-byte container-info vector at `+0x18`; each slot receipt is 88 bytes, count `+2`, net id `+4`. Native container models update independently of the player's inventory. Reconciliation retains original game-allocated item data, respects the server receipt and updates only the omitted player prediction; furnace models are left to normal server updates.
- Furnace UI model indices are local (zero for each one-slot model), while network slot numbers are input 0, fuel 1 and output 2. Sending fuel as slot 0 was rejected with ItemStackNetResult 50 (FailedToValidateDstSlot). The corrected input/fuel/output sequence was accepted in a live smelting cycle.
- Container closing must queue the UI pop. Immediate deleteContainerManager leaves furnace UI binding callbacks holding expired weak references (observed native crash at `0xCC50ED8`). ClientInstance's scene pointer `+0x5E8` and SceneStack virtual slot 26 (`0x56C490`) schedule one normal pop; the screen's exit path owns container cleanup. Inventory swaps, crafting and furnace completion were checked with this sequence.

Primary reference declarations/implementations:

- [GameMode](https://github.com/LiteLDev/LeviLamina/blob/main/src/mc/world/gamemode/GameMode.h)
- [Container manager](https://github.com/LiteLDev/LeviLamina/blob/main/src/mc/world/containers/managers/models/ContainerManagerModel.h)
- [Container model](https://github.com/LiteLDev/LeviLamina/blob/main/src/mc/world/containers/models/ContainerModel.h)
- [Actor flag access](https://github.com/LiteLDev/LeviLamina/blob/main/src/mc/world/actor/provider/SynchedActorDataAccess.cpp)
- [Intrinsic block-state types/directions](https://learn.microsoft.com/en-us/minecraft/creator/reference/content/blockreference/examples/intrinsicblockstateslist)

Declarations and disassembly checks do not establish end-to-end gameplay support. Individual modules still require tests of request acceptance, refusal, interruption and restored state.

## Windows expansion checks (2026-11-02)

- The inventory-key handler at RVA `0x657D810` takes `ClientInstance*` and the default layout (`0`). Creative inventory opening must follow this complete handler on the frame thread. Sending only InteractPacket action 6 timed out in Creative. Calling LocalPlayer virtual `0xC3` (`0x6DA8900`) directly omitted controller preparation, hit an expired weak-reference assertion and was removed. The same world was saved/reopened to clear the failed UI session. The corrected full-handler path subsequently opened, transferred and closed inventory normally.
- Native main-inventory swaps (slots 18/0 and inverse) and quantity transfers (slots 9/10 and inverse) were accepted after automatic Creative UI opening. Original names, counts and pickaxe damage were verified restored.
- Item `maxStackSize` is byte `+0xA8` in this binary (getter `+0x176E30`, virtual slot 100); do not copy the newer reference header's slot number. Plain compatible stacks require the same Item/Block identity and aux value with no user-data pointer. Unknown metadata never merges.
- The guarded canonical empty ItemStack is at RVA `0x1293AE90`, vtable `0xEFF9E80`, null item/data and count zero. A live fully consumed merge returned source count/net ID zero and restored the original target count correctly.
- A temporary chest at `1164 67 -8` was used for accepted partial deposits, full-source merging into an occupied pile, three-step sorting with an inventory scratch slot, fresh surveys, `#restock item` and `#loadout fill`. Contents were returned, the empty test chest removed, its Creative test item stack discarded through an accepted normal request, and original inventory slots/counts restored. The temporary location/profile/selection were removed.
- Conditional history undo removed an owned cobblestone fixture and confirmed air. Placement history now records the actual replaced/adjacent target rather than the clicked support, avoiding attribution of natural changes in the support block.
- Three-phase Creative construction placed two supports at `1164 67 -8` and `1164 68 -8`, placed the target stone at `1164 69 -8`, then removed both temporary supports. This exposed a builder mismatch-cache bug: a conditional cell becoming ignored after removal remained cached and paused completion. Removing ignored cells from that cache fixed the regression, covered by an offline test. Repeating the native job completed all three phases and final schematic verification reported one matching block, zero differences. Native selected-vein mining then removed the owned target and verified completion; all three fixture cells were checked as air.

## Repair ABI and gameplay checks (2026-11-02/03)

- Active repair controllers are found in the manager's callback map at `+0x60`.
  This binary's anvil controller vtable is RVA `0xF499460`, grindstone
  `0xF497C50`. Both profiles require exact vtable/method matches before calls.
- Virtual slot 8 returns the takeable `ItemStackBase` from the output `SlotData`:
  anvil RVA `0xCC9C7B0`, grindstone `0xCC57970`. The returned preview is an
  `ItemInstance` (`0x80` bytes), not a full `ItemStack` (`0x98` bytes). Native
  preview reads were verified in both interfaces. Anvil cost is `+0xC8`, material
  count `+0x1F4`; these offsets were checked in preview-update disassembly and a
  live one-plank/one-level preview.
- Virtual slot 15 is the normal `handleAutoPlace(SlotData const&, int,
  vector<AutoPlaceItem> const&, vector<AutoPlaceResult>&)` path: anvil
  `0xCC9A9D0`, grindstone `0xCC57F10`. The amount is one. Anvil execution generated
  action 15 (optional recipe), two consume actions and an output transfer in the
  same accepted request. Capturing every request emitted inside this call, then
  observing the improved output, avoids treating dispatch as a completed repair.
  The grindstone output call has not yet been exercised with two damaged tools.
- One-slot UI models do not supply usable network indices by themselves.
  Anvil network container 0 uses slot **1**, material container 1 slot **2**.
  Sending input to slot 0 was refused with result 55 (`CannotPlaceItem`), without
  changing inventory. Normal UI deposit/return traces and corrected automatic
  transfers confirmed the 1/2 mapping. Grindstone containers 50/51 use shared-UI
  slots **16/17**, confirmed by normal UI deposits and accepted returns. Never
  take from anvil/grindstone result-preview containers with a raw transfer.
- Native Creative anvil tests confirmed input transfers, preview-only recovery,
  zero-level cost-ceiling refusal and accepted execution. The wooden pickaxe's
  damage changed from 1 to 0 of 59, one Creative test plank was consumed, and the
  repaired pickaxe was returned to original slot 18. Temporary station blocks,
  Creative item piles and registration/selection were removed; original item
  quantities/slots and selected slot were restored. No new world or game-mode
  change was used.
- Mending's identifier is 26 in the reference `Enchant::Type`. The process waits
  for actual durability changes rather than writing damage or XP. Offline tests
  cover confirmed swaps, real-progress observation, no-progress timeout, health
  loss and slot restoration. A real XP-source repair and Survival XP debit/anvil
  wear still require gameplay validation.

References: [AnvilContainerManagerController](https://github.com/LiteLDev/LeviLamina/blob/main/src/mc/world/containers/managers/controllers/AnvilContainerManagerController.h),
[GrindstoneContainerManagerController](https://github.com/LiteLDev/LeviLamina/blob/main/src/mc/world/containers/managers/controllers/GrindstoneContainerManagerController.h),
[ItemStackNetResult](https://github.com/LiteLDev/LeviLamina/blob/main/src/mc/world/inventory/network/ItemStackNetResult.h),
[Enchant](https://github.com/LiteLDev/LeviLamina/blob/main/src/mc/world/item/enchanting/Enchant.h).
