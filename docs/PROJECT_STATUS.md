# SOCOM Native â€” Reverse Engineering Status

> Target retail build: **SOCOM: U.S. Navy SEALs â€” SCUS_971.34**
>
> This repository contains reverse-engineering research and clean native implementation work. It does **not** distribute original copyrighted game assets. A legally owned game disc/extracted data set is required.

## Project Direction

The project has moved beyond the original M8 rendering proof-of-concept.

The long-term target is a **native PC conversion driven by the original SCUS_971.34 game logic**, with PS2-specific platform services replaced by PC backends. The retail EE/MIPS executable is treated as the behavioral source of truth.

The earlier OpenGL runtime remains useful as a validation harness for decoded geometry, textures, collision, characters, animation, and mission data, but it is no longer the intended source of gameplay behavior.

## Retail Executable

Current analyzed executable:

- File: `SCUS_971.34`
- ELF: 32-bit little-endian MIPS / PlayStation 2 EE
- Entry point: `0x00140008`
- Load base: `0x00100000`
- Retail ELF files are intentionally not committed to this repository.

Recovered analysis currently includes:

- 6,693 distinct direct JAL targets
- 49,785 direct JAL call sites
- 94 embedded source/header module anchors
- 162 recovered UI/script command handlers
- boot/application-state anchors
- mission lifecycle anchors
- initial `CMission` object layout
- UI/menu runtime structures
- address-compatible native recompilation dispatch

## Recovered Mission Lifecycle

Current high-confidence retail path:

```text
Application startup
    |
    v
0x001FB4F0
CMission global construction
singleton: 0x004D4880
    |
    v
Original UI / RDR menu flow
    |
    v
0x001D61A0
SetMission
    |
    v
0x001FAE10
Load mission descriptor / mission.rdr state
    |
    v
0x00172EC0
game-state transition
    |
    v
0x001FA610
activate selected mission
    |
    +--> 0x001F9D70
    |    animation/archive setup
    |
    v
0x001F9FA0
enter mission runtime
    |
    v
0x001F8EB0
actual mission frame/update
    |
    v
0x001F8580
mission result transition
    |
    v
0x001F92A0
runtime cleanup
    |
    v
0x001F9780
state shutdown
```

Known mission result values:

- Complete = `2`
- Failure = `3`
- Abort = `4`
- Timeout = `5`

## Native Recompilation Layer

`runtime/recomp/` is the foundation for incremental conversion of retail EE functions into native host C++.

The design preserves:

- original SCUS_971.34 addresses
- guest EE memory/address semantics
- R5900 register representation where needed
- original function identity
- address-to-native dispatch

Several small retail UI/container helpers have already been translated to native C++ and bound to their original EE addresses. This is the beginning of replacing understood PS2 functions with native equivalents instead of rewriting gameplay from scratch.

## Asset / Data Reverse Engineering

Substantial GameZ data support already exists for:

- ZAR/CZAR archives
- compiled RDR data
- world models
- collision
- PS2 texture/palette data
- character meshes
- skeletons and skin weights
- `motion.zar` animation clips
- `mzanim.zar` mission animation/control commands
- mission valves/actions/configuration
- AI/navigation data investigation

The M8 validation path has demonstrated:

- 38,220 rendered world triangles
- 9,244 collision triangles
- decoded world textures/material assignments
- native player rendering
- recovered SEAL skeleton/skin
- original stand/walk/run animation playback

## Current Reverse-Engineering Priorities

1. Recover boot and application-state transitions from the retail ELF.
2. Finish the original RDR-driven menu command interpreter.
3. Recover `CMission` lifecycle and scheduler functions function-by-function.
4. Trace the real `zSeal` player controller from `CMission::Tick`.
5. Recover entity, AI, graph/navigation, weapon, damage, and objective execution.
6. Replace PS2-only services with PC implementations:
   - GS/VIF/VU rendering
   - PAD input
   - SPU2/audio
   - MPEG playback
   - CD/DVD file access
   - memory-card/save services
7. Drive all single-player missions through the recovered original game-state logic.
8. Restore original front-end, briefing, armory, HUD, TACMAP, debriefing, save/options, and audio behavior.
9. Address multiplayer only after the single-player runtime is coherent.

## Repository Policy

Commit:

- reverse-engineered source
- native replacement code
- tools
- address maps
- structural analysis
- documentation
- small non-copyright test fixtures created specifically for this project

Do not commit:

- `SCUS_971.34`
- original `.zar`, `.zed`, `.pss`, ISO, or other retail assets
- generated textures/models
- `.snr` / `.spr` runtime packs
- CMake/Visual Studio build outputs
- generated analysis directories that can be reproduced locally

## Reference

The project may use public reverse-engineering work such as reCOM as a naming/structural cross-reference. Addresses and behavior for this repository remain grounded in the project's own **SCUS_971.34** analysis because other SOCOM builds differ.
