# SOCOM Native Runtime - Single Player Milestone 1

The first runtime milestone boots a reconstructed M8 world in a native Windows executable and provides world rendering, collision, gravity, jumping, mouse look, and player movement.

## Recommended build path: use your extracted game data directly

You no longer need to manually create `m8_scene.gltf` or `worldmodel_collision.gltf`.

The runtime update includes the existing reverse-engineered M8 scene and collision exporters. Give the script the root of your legally extracted or mounted SOCOM game data:

```powershell
.\runtime\build_m8_from_game.ps1 -GameDataDir "D:\SOCOM_EXTRACTED"
```

The script recursively locates:

```text
m8_mdl.zed
m8_txr.zed
m8_pal.zed
models.zar
```

If several `models.zar` files exist, it tries them until it finds the one containing the M8 `worldmodel` data.

It then automatically:

1. Builds the M8 scene exporter.
2. Builds the M8 collision exporter.
3. Builds `socom-native.exe`.
4. Generates `m8_scene.gltf` locally.
5. Generates `worldmodel_collision.gltf` locally.
6. Converts both to the local `.snr` runtime pack.
7. Launches M8.

Generated game-derived files are written below `generated/` and must not be committed to GitHub.

## Required original files

The current M8 path uses the following files from the user's own SOCOM data:

```text
models.zar
m8_mdl.zed
m8_txr.zed
m8_pal.zed
```

No original game files are included in this repository or update package.

## Controls

```text
WASD     Move
Mouse    Look
Shift    Sprint
Space    Jump
F2       World wireframe
F3       Collision debug
F4       Noclip
F5       Respawn
Tab      Release/capture mouse
F1       Controls
Esc      Quit
```

## Legacy path

If you already have the two generated glTF files, the older script still works:

```powershell
.\runtime\build_m8_runtime.ps1 `
    -SceneGltf "C:\path\to\m8_scene.gltf" `
    -CollisionGltf "C:\path\to\worldmodel_collision.gltf"
```
