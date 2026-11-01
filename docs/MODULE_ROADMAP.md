# Module expansion

Requested scope: all proposed modules except the adaptive performance controller.
Existing opt-in performance diagnostics remain available; no adaptive controller is planned.

Each module must have executable behavior, regression coverage and documented native validation before being called complete. Native actions must use the game's normal requests and verify results.

| Module | Implementation / validation |
|---|---|
| Schematic verifier and repair | Implemented; core tests and live matching/unknown comparisons checked; broader repair fixtures pending |
| Build material planner | Implemented; core allocation/multi-block tests and live exported-placement comparison checked |
| Chest and storage logistics | Implemented; native whole-stack deposit/withdrawal accepted and restored; independent native model updates reconciled from original stack snapshots |
| Automatic crafting and smelting | Implemented; core acceptance/refusal/output tests pass; native Creative recipe discovery/actions/workstation completion checked; furnace input/fuel/output accepted and complete iron-smelting cycle collected; screen-close lifetime crash fixed by queued native UI pop; Survival crafting pending |
| Persistent tasks and recovery | Implemented; bounded checkpoints, paused recovery and world/dimension checks covered in core tests |
| Survival assistant | Implemented; eating/retreat/slot-restoration core tests pass; sustained native eating pending |
| Task queue and workflows | Implemented; live wait/goto sequence checked; storage actions use checkpointed inventory targets |
| Visual control panel | Implemented; live status and task-list button checked; concurrent pipe clients fixed |
| Advanced schematic workspace | Implemented; named resident snapshots/persisted file presets, transform independence and shared preview limits covered in tests; native switch and restart/reload checked |
| Protected areas | Implemented; core path/edit-override tests, native registration/persistence checked |
| Task failure diagnostics | Implemented; bounded diagnostics and task stop reasons; more native scenarios pending |
| Tree and animal farming | Implemented; complete-tree/natural-leaf detection and adult population core tests pass; native chicken feeding observed in-love state, completed once and refused immediate repeat; native harvesting/replanting pending |
| Quarry manager | Implemented; bounded preflight, top-down layers, preserved ramp, optional torches/unloading; core ramp/liquid/path-protection tests pass; live one-layer fixture cleared 24 blocks, preserved ramp, then restored original 25-position schematic with all matches |
| Portal route planner | Implemented; directed cross-dimension graph, shortest distance route, scaling and persistence covered in core tests; route legs require repeating go after each dimension transition |
| Boat and mount navigation | Implemented; separate wide-corridor planner, boarding/steering/dismount states; core corridor/hazard/unloaded tests pass; native driving pending |
| Schematic export | Implemented; core round-trip/state/overwrite/unknown tests and native four-position export/reimport checked |
| Automated gameplay validation | Implemented; default read-only checks passed in the existing world; custom scenarios support command/log/status assertions and JSON evidence reports |

## Development validation on 2026-11-01

The RelWithDebInfo regression executable passes **100 tests**, also passing in a
separate MSVC AddressSanitizer build with no sanitizer errors reported. The expansion was
also exercised in the existing Creative world **Mon monde**, without creating a
new world. Successful native checks include storage transfers and inventory
restoration, recipe/workstation actions, a complete furnace iron-smelting cycle,
schematic export/reload/comparison, placement persistence, task sequencing,
protection persistence, a removable quarry fixture and one confirmed adult
chicken feeding. The quarry fixture was compared against its original exported
air placement and restored with all 25 positions matching.

Testing exposed and fixed a furnace screen-lifetime crash and a misleading entity
interaction return value. These observations do not establish that every feature
works in Survival, on servers or during long sessions. Native tree replanting,
animal harvesting, vehicle steering/dismount, sustained Survival assistance,
complex multi-layer quarry excavation and actual portal transitions still need
dedicated gameplay runs. The older [stability audit](STABILITY_AUDIT.md) describes
the published 1.4.1 build, not this development build.
