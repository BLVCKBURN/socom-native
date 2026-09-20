# Native conversion plan: original PS2 game logic on PC

## Definition of "native conversion"

The target is not an emulator and not a new game that merely loads SOCOM assets. The target is a PC executable whose game behavior is derived from the original SCUS_971.34 code and original disc data.

The conversion will use three layers:

1. **Recovered original game/engine logic** — application state, mission logic, valves, AI, squad behavior, weapons, animation control, UI script commands, save-state behavior, etc.
2. **EE native-recomp bridge** — preserves original 32-bit EE virtual addresses and calling-state shape while functions are migrated incrementally to native C++.
3. **PC platform backends** — replacements for hardware/OS-specific PS2 services: GS/VIF/VU rendering, PAD input, SPU2 audio, MPEG playback, CD/DVD paths, memory-card storage, timing/threading and later networking.

A PC graphics API is still required, but it is a backend for the recovered GameZ renderer; it is not a substitute game/runtime design.

## Why preserve the original EE address space initially

The retail executable contains many raw 32-bit object pointers, globals, vtables, command tables and data references. Trying to redesign all of those simultaneously would destroy our ability to compare behavior with the PS2 build.

During migration, the PC process will model the original `0x00100000`-based guest data/BSS space and bind original function addresses to native functions. That gives us a stable compatibility layer while class layouts and functions are understood. Once a subsystem is fully native, the bridge can be reduced or removed for that subsystem.

## Migration order

### Phase A — executable map and deterministic tooling

- identify binary/version/hash;
- enumerate source-module anchors and strings;
- recover function/call candidates;
- recover static dispatch/vtable tables;
- establish known function addresses and original globals;
- keep generated manifests reproducible from the user's own `SCUS_971.34`.

Status: underway; this update establishes the first reproducible analyzer and retail address manifest.

### Phase B — application/state framework

Rebuild the original boot and state flow around the `0x00141630` application loop, `COurGame`/`CGameState`, `MenuState`, and the recovered UI command dispatch. Replace our temporary front end with the original state machine and RDR-driven screens.

### Phase C — GameZ core services

Prioritize code that is platform-neutral and already supported by recovered data formats:

- zArchive / asset lookup
- zReader / compiled RDR
- zValve
- zNode / entity ownership
- scheduler/tick registration
- zMath
- zAnim command execution
- mission/action/objective state

### Phase D — player, weapons and AI

Port the original `zSeal`, `zWeapon`, `zCharacter`, `zAI`, `zGraph`, `zGrid`, and zIntersect logic. Use the original CoreState tick ordering recovered from the retail executable.

### Phase E — PC hardware backends

- **GS/VIF/VU -> PC renderer:** translate GameZ draw state/packets/material state to a modern host API. Existing geometry/texture reverse engineering becomes validation data, not the game logic.
- **PAD -> host input:** keyboard/mouse + controllers while preserving original logical controls.
- **SPU2 -> host audio:** original banks/events/voice routing on a PC mixer.
- **MPEG -> host video:** play original movie assets through a host decoder.
- **memory card -> filesystem:** preserve original save semantics in a normal PC save directory.
- **CD/DVD -> filesystem/archive mount:** preserve original `/run/...` resource paths while sourcing files from a legal game extraction.

### Phase F — campaign parity

Boot -> intro -> original menu -> briefing/armory -> every single-player mission -> debrief -> save/progression, with mission objectives, teammates, enemies, weapons, audio, cinematics and HUD driven by recovered original code/data.

### Phase G — multiplayer

Only after single-player parity: original network object/state behavior mapped to a maintainable PC networking backend. Legacy Medius-specific service behavior should be isolated from local gameplay simulation.

## Short-term decompilation targets

The next high-value functions are:

- application game-state construction around `0x001426C0`;
- `SetMission` (`0x001D61A0`) and the mission-state transition it invokes;
- `SwitchMenu` (`0x001D5DB0`) and `SetMenuState` (`0x001D49B0`);
- `CMission` initialization at `0x001FA610` split into named helper functions;
- CoreState setup/teardown around `st_core.cpp`;
- `zrdr_parse.cpp` routines, so our RDR implementation can be compared function-for-function;
- `zanim_cmd_parse.cpp` / `zanim_cmd_tick.cpp`, allowing mzanim behavior to come from the original interpreter semantics;
- `seal_create.cpp` / `seal.cpp` for player and NPC creation;
- `zwep_weapon.cpp` for original weapon firing/ammo/damage behavior.

## Repository policy

Do not commit the retail ELF, ISO data, models, textures, sound or other copyrighted game assets. Commit only original reverse-engineering code, analysis metadata, documentation, tests, and generated address/symbol manifests.
