# SOCOM Native

Independent reverse-engineering tools and native-PC reimplementation research for **SOCOM: U.S. Navy SEALs** on PlayStation 2.

> **Current state:** reverse engineering and runtime-development tooling.  
> This is **not yet a playable native PC port**.

This repository contains original reverse-engineering work, parsers, converters, documentation, and validation data. It does not distribute the original game executable, disc image, game archives, textures, models, audio, or other proprietary SOCOM assets.

A legally obtained copy of the original game is required to use the extraction and conversion tools.

---

## Current Status

### Substantially Decoded

- GameZ ZAR/CZAR archive structure
- Archive node trees and serialized name relocation
- Compiled GameZ RDR data format
- Generic recursive RDR validator/decompiler
- Weapon and ammunition databases
- Mission descriptors and multiplayer configuration
- World/environment configuration
- Global and mission-local valve/state definitions
- Action bindings and interactive world objects
- Character-type definitions
- Actor/hostage configuration
- AI/navigation data structures
- PS2 VIF rigid-model geometry
- Rigid-model glTF export
- Character VIF/skinning data
- Exact character skeleton hierarchy
- Q15 skin weights, including 5-influence characters
- GameZ PSMT8 textures and PSMCT16/32 palettes
- Character and weapon texture extraction
- `motion.zar` 30 Hz skeletal animation tracks
- Runtime bone-name registry
- Skinned, textured, animated character glTF export
- `mzanim.zar` sequence and command structure
- Animation control-flow decoding
- MP6 character-bank validation
- Weapon-bank validation

### In Progress

- Typed runtime representations for decoded RDR data
- `ValveRegistry` / game-state system
- World and level reconstruction
- Actor spawning
- Mission/action execution
- Navigation runtime
- Native renderer
- Input, audio, and gameplay systems
- Native PC game loop

---

## RDR Reverse Engineering

The compiled GameZ RDR format is now structurally decoded for the current reference build.

Across the eight recovered `reader*.zar` archives:

**224 / 224 RDR files recursively validate.**

Compiled RDR files contain:

```text
+0x00  uint32  string pool size
+0x04  uint32  data offset
+0x08  string pool
...
data section
```

The data section is composed of 8-byte cells:

```text
uint32 tag
uint32 value
```

Observed tags:

| Tag | Meaning |
|---:|---|
| `1` | list header or signed int32 atom |
| `2` | IEEE float32 |
| `3` | string / symbol |
| `4` | relative pointer to another list |

A list begins with:

```text
[1, cell_count]
```

`cell_count` includes the list header itself, allowing list boundaries to be decoded deterministically.

The maximum recursive depth observed in the current corpus is **13**.

### `zweapon.rdr`

`zweapon.rdr` is structurally valid but contains ten invalid `Description` string references in explosive/grenade ammunition records.

The malformed value is consistently:

```text
0xFB95FFC0
```

The parser preserves these values as invalid/raw references instead of aborting the rest of the file.

See:

```text
docs/readers/rdr_format.md
docs/readers/rdr_corpus_validation.txt
docs/readers/mission_runtime_findings.md
```

---

## Recovered Runtime Model

The reverse engineering is beginning to expose the original GameZ runtime architecture.

```text
Game data
    |
    +-- WorldConfig
    |
    +-- MissionDescriptor
    |
    +-- ValveRegistry
    |
    +-- CharacterTypeSet
    |
    +-- ActorSpawnConfig
    |
    +-- ActionBinding
    |
    +-- Navigation
    |
    +-- Animation / Sequence State
    |
    v
Native PC Runtime
```

### Valves

`global_valves.rdr` acts as a global game-state registry containing state such as:

```text
mission success / failure
multiplayer round state
scores
timers
game type
bomb state
friendly fire
player readiness
persistent controller/options settings
```

Individual maps then define additional mission-local valves.

This is expected to become one of the first native runtime subsystems.

---

## Multiplayer Data Layout

The multiplayer reader archives separate responsibilities across several RDR files.

```text
mission.rdr
    mission metadata
    UI strings
    loading assets
    equipment settings
    team/spawn metadata

mpX.rdr
    environment
    fog
    lighting
    camera
    world grid
    terrain/world root

valves.rdr
    mission-local state variables

actions.rdr
    world interaction bindings

chartype.rdr
    character classes / teams

vehicles.rdr
    actor and hostage configuration

aimaps.rdr
net_*.rdr
    navigation / AI graph data
```

The decoded multiplayer configurations currently include both `FOOTBOMB` and `EXTRACT` game modes.

---

## Repository Layout

```text
tools/
  archive/       ZAR/CZAR inspection and catalog tools
  models/        rigid and skinned model conversion
  textures/      texture and palette extraction
  animation/     motion and animation tools
  readers/       GameZ RDR inspection/decompilation

docs/
  research/      recovered structures and tables
  validation/    validation results
  readers/       compiled RDR format and runtime research

research/
  hashes/        identifying metadata for reference game files
```

---

## Build

The tools are written in portable C++17 and are intended to build on Windows and macOS, including Apple Silicon.

### Repository

```bash
cmake -S . -B build
cmake --build build --config Release
```

### Standalone RDR Tool — Windows

From a Visual Studio Developer PowerShell:

```powershell
cl /std:c++17 /EHsc /O2 tools\readers\socom_rdr_dump.cpp /Fe:socom_rdr_dump.exe
```

### Standalone RDR Tool — macOS / Linux

```bash
c++ -std=c++17 -O2 tools/readers/socom_rdr_dump.cpp -o socom_rdr_dump
```

---

## RDR Tool Usage

List RDR files inside an archive:

```text
socom_rdr_dump readerx.zar --list
```

Decode a single RDR:

```text
socom_rdr_dump readerx.zar zweapon.rdr zweapon.json
```

Decode and validate every RDR in an archive:

```text
socom_rdr_dump readerx.zar --all output
```

Decode an extracted standalone RDR:

```text
socom_rdr_dump mission.rdr mission.json
```

---

## Project Goal

The long-term goal is not emulation.

The project is working toward reconstructing enough of the original GameZ data formats and runtime behavior to load legitimately obtained SOCOM game data into a **native PC implementation**.

```text
Original SOCOM data
        |
        v
Reverse-engineered parsers
        |
        v
Typed native game data
        |
        v
PC runtime systems
        |
        v
Native renderer + gameplay
```

---

## Reference Build

Current research is primarily based on:

```text
SCUS_971.34
```

See:

```text
research/hashes/reference_files.txt
```

for identifying metadata without distributing the original executable.

---

## Legal / Project Scope

This is an independent reverse-engineering and reimplementation project.

No original SOCOM game assets are distributed by this repository.

SOCOM, PlayStation, and related trademarks and copyrights belong to their respective owners. This project is not affiliated with or endorsed by Sony Interactive Entertainment or the original developers.

---

## Contributing

Useful contributions include improving parsers, validating structures against additional legally obtained builds, documenting GameZ formats, creating synthetic test fixtures, and implementing native runtime subsystems.

Do not submit copyrighted game files or extracted proprietary assets.
<!-- socom-native-project-status -->
## Development Status

The project is currently transitioning from format/renderer reconstruction to **incremental native recompilation of the retail SCUS_971.34 EE executable**. See [docs/PROJECT_STATUS.md](docs/PROJECT_STATUS.md) for the current reverse-engineering status, recovered mission lifecycle, and roadmap.
