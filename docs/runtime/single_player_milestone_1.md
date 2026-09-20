# Single-Player Milestone 1

## Goal

Boot Mission 8 in a native Windows executable, render the recovered world, create collision, spawn the player, and allow local traversal.

## Current path

The runtime can now generate its intermediate scene/collision data directly from the user's legally obtained SOCOM files instead of requiring pre-generated glTF files.

Required M8 data:

- `models.zar`
- `m8_mdl.zed`
- `m8_txr.zed`
- `m8_pal.zed`

The update includes source for the independently reverse-engineered M8 scene and collision exporters. `build_m8_from_game.ps1` locates the source files below a supplied game-data root, builds the exporters and runtime, generates the intermediate glTF data locally, builds `m8_runtime.snr`, and launches the game.

## Validated runtime dataset

- 38,220 render triangles
- 9,244 collision triangles
- initial test spawn selected from recovered collision
- fixed-step player physics
- gravity and grounded state validated against the M8 collision surface

## Next single-player work

1. Load original textures into the runtime renderer directly.
2. Load/spawn the player SEAL actor.
3. Integrate skeleton and motion clips.
4. Add weapon state and firing.
5. Spawn NPC actors.
6. Implement navigation and basic AI.
7. Implement GameZ valve/state registry.
8. Connect actions and `mzanim` mission control flow.
9. Progress M8 from mission start to completion.

Multiplayer remains intentionally deferred until the single-player local simulation works end-to-end.
