# Single-Player Runtime: Player Character Phase

This phase moves `socom-native` from a free camera/collision prototype to an embodied SOCOM player character.

## Added

- `MESH_seal_A_des` as the initial local player body.
- 2,046 character triangles in 86 original material batches.
- 12 original SEAL textures decoded from `clib_txr.zed` / `clib_pal.zed`.
- 25-joint runtime skeleton and inverse-bind matrices.
- CPU skeletal skinning in the native Win32/OpenGL runtime.
- Original 30 Hz locomotion clips from `motion.zar`:
  - `seal_stand`
  - `seal_walk`
  - `seal_run`
- Automatic animation switching from player movement state.
- Horizontal root-motion neutralization so the physics/collision controller remains authoritative for world position.
- Third-person follow camera by default when the player pack is loaded.
- First-person debug camera remains available.

## Controls

- `WASD` - move
- `Mouse` - camera/look
- `Shift` - run
- `Space` - jump
- `F2` - world wireframe
- `F3` - collision debug
- `F4` - noclip
- `F5` - respawn
- `F6` - toggle textures
- `F7` - flip texture V orientation
- `F8` - toggle first/third-person camera
- `F9` - toggle player model
- `Tab` - release/capture mouse
- `Esc` - quit

## Player pack

The generated `seal_A_des.spr` is derived locally from the user's SOCOM data and is intentionally excluded from source control.

Validated player-pack contents:

- 6,138 expanded render vertices
- 2,046 triangles
- 86 render batches
- 12 textures
- 839,680 RGBA texture bytes
- 25 joints
- 3 animation clips

The generated file is approximately 1.2 MB.

## Runtime data path

```text
clib_txr.zed + clib_pal.zed
        -> character texture decode

character models.zar + clib_mdl.zed + motion.zar
        -> seal_A_des glTF intermediates
        -> stand / walk / run animation data
        -> seal_A_des.spr

m8_runtime.snr + seal_A_des.spr
        -> socom-native.exe
```

No original SOCOM data is included in the repository or update package.

## Next runtime target

The next single-player phase is weapon embodiment and combat:

1. attach an original SOCOM weapon model to the right-hand joint,
2. load the weapon/ammunition RDR definitions already decoded,
3. implement aiming and firing,
4. ray/world collision and hit detection,
5. damage/death state on a spawned target actor,
6. then begin AI/navigation integration.
