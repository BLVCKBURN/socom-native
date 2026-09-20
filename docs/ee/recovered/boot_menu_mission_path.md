# SCUS_971.34 recovered boot/menu/mission path

This document is version-locked to the retail `SCUS_971.34` executable whose SHA-256 is:

`5111e776f64611ae1dcdc725f0ce9db660e90e1763e897e8f5b90f7402ce30d1`

It records facts recovered from the executable itself. Names not proven to be original source identifiers are deliberately descriptive.

## Core path

- ELF entry: `0x00140008`
- application main/update loop: `0x00141630`
- UI command `SetMission`: `0x001D61A0`
- retail global mission object: `0x004D4880`
- mission construction/runtime initialization: `0x001FA610`
- mission RDR/configuration loader: `0x001FAE10`
- actual mission per-frame routine: `0x001F8EB0`
- registered mission tick wrapper: `0x001F94A0`

`0x001F94A0` is not the body of the mission update. It is a 32-byte wrapper which calls `0x001F8EB0` and returns zero.

## SetMission

`0x001D61A0` retrieves the UI command argument. It recognizes the literal `INPUT_STRING` and can resolve that through the active UI context. Once it has the selected mission name, it copies/records that value and calls:

`0x001FAE10(0x004D4880, selectedMission)`

This provides a direct bridge from the original RDR-driven menu command system into the original mission object.

## Mission object initialization

`0x001FA610` is called with `a0 = 0x004D4880`. Among other initialization work it:

- records the selected mission string at object offset `+0x1C`, backed by storage beginning at `+0x20`;
- sets up mission/world runtime systems;
- opens/initializes sound groups;
- resolves the mission valves `mission_complete`, `mission_failure`, `mission_abort`, `mission_timeout`, and `mission_replay` and stores their handles at offsets `+0xF4` through `+0x104`;
- selects `/run/mp/%s/`, `/run/sp/%s/`, or `/run/%s/` path forms;
- opens mission-local resources including `hudobjectives.rdr`, `missioncams.rdr`, and `actions.rdr`;
- resolves world nodes including `seal_tent` and `terrorist_tent`.

## Mission RDR/configuration loader

`0x001FAE10` loads `mission.rdr` and consumes named entries including:

- `LoadingScreenAssets`
- `UiVars`
- `Valves`
- `NAME`
- `PERM`
- `PERSIST`
- `VALUE`
- `WEAPON_MODEL_POSTFIX`
- `TacMapZoom`
- `Reverb`
- `Depth`
- `Seconds`
- `DBNAME`

The loader loops over the RDR lists directly, which is strong confirmation that our previously recovered compiled-RDR grammar is part of the actual retail execution path rather than merely an offline data interpretation.

## Mission frame

`0x001F8EB0` receives the mission object in `a0` and frame delta in `f12`. It increments the float at `+0x60` every frame and conditionally decrements the float at `+0x7C`. It checks the five mission-state valves initialized above and dispatches mission state changes through an internal helper at `0x001F8580`.

The routine also calls deeper gameplay paths and finishes by updating two alternate player/gameplay paths depending on retail mode flags. Those callees are the next reversing targets; they should not be assigned speculative names until their behavior is established.

## Why this matters for the PC conversion

The project can now preserve the original control flow instead of implementing a parallel game:

`original menu RDR -> original command ID -> native/recompiled SetMission -> retail CMission state -> mission.rdr -> original mission update semantics`

The PC-specific work should sit below or beside this path (rendering, input, audio, file I/O), while the retail state machine and gameplay data remain the source of truth.
