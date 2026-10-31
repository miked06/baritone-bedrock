# Stability and performance audit

Local validation on 2026-10-31, version **1.4.1**, Windows x64, Minecraft Bedrock **26.60 / package 1.26.6005.0**. Changes were developed on `codex/stability-performance-audit` from the published 1.4.0 source. This report records bounded tests, not a guarantee that every world, file, server or graphics configuration is bug-free.

Validated local DLL SHA-256: `09817de30e06daef1388838660ff229967555377d6e94dfa31737ad5f0734682`. This audit does not publish a GitHub release.

## Fixes reproduced by regression tests

| Area | Failure and correction |
|---|---|
| Farming | With periodic rescans disabled, farming never performed its initial scan. It now starts once regardless of that interval. Harvesting/planting respects permissions, replant settings and selections, including dropped items. Selection changes no longer permanently erase cached scan candidates. |
| Builder permissions | Nearby wrong blocks could receive a break click with `allowBreak=false`. Placement cost overrides and direct placement clicks could bypass placement/fluid restrictions. These permission checks now precede actions and overrides. Exact `allowBreakAnyway` exceptions remain supported. |
| Large builds | A large matching placement was scanned completely in one tick. Full scans now retain a cursor, yield after at most 4096 positions or a soft two-millisecond budget, and share that budget across recursive layers/repeats. A partial scan is not completion. Completed positions use section bitmaps rather than one hash node per block. |
| Sparse exploration | Distant allowlisted chunks could require huge ring enumeration or be falsely considered finished beyond the old search limit. Allowlist candidates are now searched directly. The regression targets chunk `(12000,-12000)`. |
| Cache | Periodic save/prune requests could join an unfinished writer on the game thread. They now defer while it runs; dirty updates survive for the next save. Explicit save/shutdown remains a synchronization point. Zero-result searches do not load disk regions. |
| Mining | Restricting targets to permitted names discarded state predicates and expanded ore families again. Restricted filters now retain the original selectors and exact allowed names. |
| Schematic loading | File I/O, decompression and conversion blocked the game thread. A worker now returns a result for the game thread to apply. Pending edits/builds are rejected; stop/clear/world transitions discard results; shutdown waits before unloading DLL code. Failed imports preserve the previous placement. |
| Compressed imports | Corrupted compressed checksums/sizes were accepted. Gzip/zlib integrity and header validation now rejects those inputs. |
| Diagnostic worker | `#calc` detached a worker that could outlive DLL code. It now uses a managed future, rejects concurrent calculations and waits during shutdown. |

## Automated coverage

**75 tests pass** in the normal RelWithDebInfo build and in a separate MSVC AddressSanitizer build. No AddressSanitizer error was reported. The suite includes 2000 deterministic malformed NBT inputs in both byte orders. These are focused tests, not exhaustive fuzzing or thread/leak-sanitizer coverage.

One ASan run hit the elytra route test's former three-second timeout after 119808 nodes. This functional test now allows ten seconds while retaining goal, obstacle-route and collision assertions. The final ASan run reached the goal after 122434 nodes in 2995 ms. Production search deadlines were not changed; this test is not a flight-latency guarantee.

| Feature group | Automated verification | Native gameplay scope |
|---|---|---|
| Navigation and movement costs | Straight/diagonal walking, obstacles, breaking, ascending/descending, falls, water/bucket fall, pillars, bridges, parkour, ladders, lava avoidance, segmented/unloaded paths, goals and cutoffs. | Native world reads and read-only path calculations; no comprehensive traversal campaign in this audit. |
| Mining and tools | Ore families, state filters, branch-mining goal, permission restrictions, break speeds, enchantments, durability and inventory tool retrieval. | Native diagnostic/settings paths; no new complete ore-mining campaign. |
| Farming and follow/pickup | Initial scan, harvesting permissions, selection/drop filters, item range and pickup goals. | Native farming with no eligible crops stopped without actions; mining with breaking disabled was rejected. No complete growth/harvest/replant cycle or extended moving-target chase in this audit. |
| Explore and cache | Distant allowlist goal, cache roundtrip, updated chunks during background save, cached searches, culling, chunk queue deduplication and mesh budgets. | Existing-world cache operates during profiling; no multi-hour exploration/disk-failure campaign. |
| Schematics and selections | Bedrock/Sponge/Litematica/legacy readers, transforms, orientations, masks, layers/material counts, preview limits/live states, malformed dimensions/palettes/NBT, compressed integrity, async cancellation/errors, incremental completion and layer/repeat budgets. | Large imports, cancellation, stale-build guard, invalid-file preservation, capped preview and a large ignored-cell scan. The previous 1.4.0 validation also built and removed a single stone block. Complex survival buildings remain unverified end to end. |
| Elytra | Physics, obstacle-avoiding flight paths, allocator modes and collision diagnostics. | No long-distance flight, rocket depletion, emergency landing or dimension-transition campaign in this audit. |
| Controls and integration | Quoted command arguments, profiles, click selection and one-shot disconnect behavior. | Pause/resume/stop, process status, version and diagnostics checked. A three-position native path was calculated without movement. A concurrent `#calc` was rejected; unloading during a two-second search waited for its result before logging `Unloaded`, without crashing Minecraft. Unloading during a large schematic load also completed cleanly, followed by reinjection. World/dimension load invalidation was reviewed in source; an actual transition during parsing was not exercised. |

The structural parity tool reports **63 Java command names/aliases and 96 Bedrock names/aliases**, with no missing Java command names or user setting declarations. It explicitly identifies the unimplemented `elytraNetherSeed` and `elytraPredictTerrain` behavior. Names do not establish behavioral parity. See [FEATURE_COMPARISON.md](FEATURE_COMPARISON.md) for other platform differences.

## Native performance samples

Samples used the existing world **Mon monde**, in the same stationary third-person view. No new world was created and no terrain was modified by this audit. Schematic fixtures were generated for testing and removed afterward. `allowBreak` and `allowPlace` were disabled during builder scans. Original settings were restored afterward.

`#perf` is disabled by default and keeps the last 4096 samples per metric. Reported percentiles/maxima describe that rolling window; `n` is the total sample count. Frame interval includes Minecraft, system scheduling and frame limiting; overlay time measures the CPU render hook, not GPU completion. Captures are short measurements on this PC, not guaranteed FPS or universal upper bounds. Some runs overlapped compilation/tests and are not controlled hardware benchmarks.

| Scenario | Result |
|---|---|
| Initial stationary baseline, about 44 seconds | Frame interval mean 5.281 ms, p99 7.766 ms, max 11.009 ms. Bot tick mean 1.076 ms; overlay mean 0.002 ms. |
| Synchronous import before the fix, 256x64x256 / 4,194,304 cells | Frame max **94.223 ms**; bot frame max **92.058 ms**. Import log reported 86 ms. |
| Same import after moving parsing to a worker | Worker 90.015 ms; frame max **10.018 ms**, bot frame max **3.106 ms** in the 109-frame capture. Building while pending was rejected. |
| Short large ignored-cell builder scan, 12 seconds / 241 ticks | Core tick mean 1.138 ms, p99 1.752 ms, max 2.028 ms. Frame p99 7.574 ms, max 23.443 ms. Cancellation left the bot idle. This demonstrates that occasional frame spikes can still occur. |
| Full ignored-cell scan in 1.4.1 | Completed 4,194,304 cells in 56 seconds including a five-second pause. A 112-second capture spanning scanning and subsequent idle time recorded 2250 ticks: core mean 0.827 ms, p99 1.173 ms, max 2.148 ms. The final rolling frame window p99 was 7.244 ms, max 9.500 ms; it does not describe the earlier scan frames. |
| 512-cell stone preview, 47 seconds | Preview calculation mean 0.035 ms, max 0.044 ms. Overlay CPU mean 1.002 ms, p99 1.474 ms, max 1.846 ms. Rolling frame p99 9.098 ms, max 12.371 ms. The cap and visible colors were inspected in-game. |

The large schematic contains only ignored `structure_void` cells: it exercises loading/scanning without placing or breaking anything. It is not evidence that a four-million-block survival construction was completed. Short process-memory snapshots also showed no obvious growth, but do not establish the absence of memory leaks.

## Reproduce

```powershell
cmake --build build --config RelWithDebInfo
ctest --test-dir build -C RelWithDebInfo --output-on-failure
build/bin/RelWithDebInfo/baritone_tests.exe --filter builder
python tools/paritycheck.py re/baritone-java
```

For a separate optimized AddressSanitizer test build (MSVC x64, ASan components installed):

```powershell
cmake -S . -B build/asan -A x64 '-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /fsanitize=address' '-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS /fsanitize=address' '-DCMAKE_EXE_LINKER_FLAGS=/INCREMENTAL:NO'
cmake --build build/asan --config RelWithDebInfo --target baritone_tests
build/asan/bin/RelWithDebInfo/baritone_tests.exe
```

Run from an x64 developer shell, or add that toolset's `bin/Hostx64/x64` directory to `PATH` for the ASan runtime. Run normal and sanitizer suites sequentially: some fixtures use fixed temporary filenames. Only the standalone tests were run with ASan; the injected game DLL was the normal build.

In Minecraft, start a capture with `#perf on`, exercise one feature, then use `#perf off`. Save `log.txt` before reinjecting, because startup replaces it. Wait for **Loaded** after `#schematic load` before issuing placement edits or build commands. The [schematic guide](SCHEMATICS.md) describes format and cancellation limits.
