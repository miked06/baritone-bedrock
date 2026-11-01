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
