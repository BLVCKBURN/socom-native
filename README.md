# SOCOM Native

Reverse-engineering tools and native-PC reimplementation research for
**SOCOM: U.S. Navy SEALs** on PlayStation 2.

This repository contains original reverse-engineering work, parsers,
converters, documentation, and validation data. It **does not include**
the original game executable, disc image, ZAR/ZED archives, textures,
models, audio, or other proprietary SOCOM assets.

A legally obtained copy of the original game is required to use the
asset-conversion tools.

## Current status

Implemented / substantially decoded:

- GameZ ZAR/CZAR archive structure
- Preorder archive node trees and serialized name relocation
- Weapon/ammunition database structure
- PS2 VIF rigid-model geometry
- Rigid-model glTF export
- Character VIF/skinning data
- Exact character skeleton hierarchy from `clib_mdl.zed`
- Q15 skin weights, including 5-influence characters
- GameZ PSMT8 textures + PSMCT16/32 palettes
- Character and weapon texture extraction
- `motion.zar` 30 Hz skeletal animation tracks
- Runtime bone-name registry
- Skinned + textured + animated character glTF export
- MP6 character-bank validation
- Weapon-bank validation

In progress:

- World/level reconstruction
- Mission scripting/state data
- Game runtime systems
- Native PC renderer/gameplay implementation

## Repository layout

```text
tools/
  archive/       ZAR/CZAR inspection and catalog tools
  models/        rigid + skinned model conversion
  textures/      ZED texture/palette extraction
  animation/     motion/bone-registry tools

docs/
  research/      recovered structures and tables
  validation/    batch-export/import validation results

research/
  hashes/        hashes/metadata for reference game files
```

## Build

The tools are portable C++17 and are intended to build on Windows and
macOS, including Apple Silicon.

### macOS

```bash
cmake -S . -B build
cmake --build build --config Release
```

### Windows

From a Developer PowerShell / Developer Command Prompt:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Executables will be placed under the CMake build directory.

## Example workflow

The repository intentionally expects game files to remain outside Git.

```text
your legally obtained SOCOM files
              |
              v
        conversion tools
              |
              v
        generated PC assets
              |
              v
        future native runtime
```

For example, the character pipeline currently combines:

```text
models.zar
clib_mdl.zed
clib_txr.zed
clib_pal.zed
motion.zar
       |
       v
textured + skinned + animated glTF
```

## Reference build

The current research is primarily based on the user's `SCUS_971.34`
build. See `research/hashes/reference_files.txt` for identifying
metadata without distributing the executable itself.

## Legal / project scope

This is an independent reverse-engineering/reimplementation project.
No original game assets are distributed by this repository.

SOCOM, PlayStation, and related trademarks/copyrights belong to their
respective owners. This project is not affiliated with or endorsed by
Sony Interactive Entertainment or the original developers.

## Contributing

At this stage, useful contributions include:

- confirming structures against additional legally obtained builds
- improving parsers and exporters
- documenting GameZ structures
- writing tests using synthetic/non-copyrighted fixtures
- implementing native runtime subsystems

Do not submit copyrighted game files or extracted proprietary assets.
