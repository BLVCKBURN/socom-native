# SOCOM Native

Independent reverse-engineering tools and native-PC reimplementation research for **SOCOM: U.S. Navy SEALs** on PlayStation 2.

> **Current state:** reverse engineering and runtime-development tooling.  
> This is **not yet a playable native PC port**.

This repository contains original reverse-engineering work, parsers, converters, documentation, and validation data. It does not distribute the original game executable, disc image, game archives, textures, models, audio, or other proprietary SOCOM assets.

A legally obtained copy of the original game is required to use the extraction and conversion tools.

---

## Current Status

| Phase | Progress | Status |
|---|---:|---|
| File / data format recovery | 90% | Core archives, UI, textures, models, animation, and mission data understood |
| Retail boot / menu | 90% | Native menu, original UI assets, background video, navigation, and music working |
| Runtime / recompilation | 45% | Player, weapon, scheduler, and execution systems being recovered |
| Gameplay | 25% | Native gameplay systems still being integrated |
| Mission loading | 15% | Next major milestone is loading and running a retail mission |
| Packaging | 5% | Standalone PC build comes later |

## Next

1. Finish retail menu transitions and behavior.
2. Integrate recovered player, weapon, scheduler, and game-state systems.
3. Load a retail mission and spawn the player natively.
4. Restore gameplay, AI, animation, collision, and mission execution.
5. Package the native PC runtime.

## Goal

Run legitimately obtained SOCOM game data natively on PC without PS2 emulation.

## Legal

This repository contains reverse-engineering and reimplementation work only. Original SOCOM game assets are not distributed.
