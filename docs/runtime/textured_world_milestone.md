# SOCOM Native - Textured M8 Runtime Milestone

This update extends the Milestone 1 runtime pack from untextured vertex-color geometry to material-aware rendering using the original SOCOM M8 texture data.

## Runtime pack v2

The runtime pack now preserves:

- world positions and normals
- vertex colors
- UV coordinates
- render/material batches
- decoded RGBA texture data
- per-texture transparency flags
- collision geometry and spawn data

Validated M8 counts:

- 38,220 render triangles
- 114,660 expanded render vertices
- 1,890 render/material batches
- 79 decoded textures
- 2,266,416 bytes of raw RGBA texture data
- 9,244 collision triangles

The resulting M8 runtime pack is approximately 6.7 MB.

## Texture sidecars

`socom_scene_gltf` still emits PNG files for inspection, but now also emits a matching `.srtx` sidecar for each texture. These sidecars are a simple internal RGBA transport format used only by the native runtime packer. They are generated locally from the user's original game data and should not be committed.

## Runtime rendering

The Windows OpenGL prototype uploads the embedded SOCOM textures directly from the runtime pack and draws the world by material batch.

Controls added for bring-up/debugging:

- F6: toggle textures on/off
- F7: toggle texture V-axis orientation

The renderer currently uses a simple fixed-function lighting/blending model. Accurate PS2 material blend equations, mipmapping, texture filtering behavior, environment mapping, and additional render-state fidelity remain later milestones.
