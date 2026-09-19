# Reverse-engineering status

## ZAR / CZAR

Confirmed disk layout:

```text
0x00  0x64-byte header
      string block
      16-byte preorder node table
      alignment padding
      payload
```

Node record:

```cpp
struct ZarNodeDisk {
    uint32_t nameAddress;
    uint32_t dataOffset;
    uint32_t dataSize;
    uint32_t childCount;
};
```

The serialized `nameAddress` is relocated relative to the archive
header's preferred string-block base.

## Rigid models

Rigid model geometry is carried in PS2 VIF streams. Recovered data
includes positions, normals, UVs, vertex colors, triangle indices,
materials, textures, and LOD grouping.

The current exporter produces glTF 2.0.

## Character models

Character geometry uses a different VIF path with bone-local
contributions and Q15 skin weights.

Observed:

- exact GameZ bind hierarchy from `clib_mdl.zed`
- up to five bone influences on some high-detail meshes
- character packed-position scale: `10 / 32768`
- glTF export uses `JOINTS_0/WEIGHTS_0` and, when needed,
  `JOINTS_1/WEIGHTS_1`

## Textures

Confirmed GameZ texture path:

```text
*_txr.zed + *_pal.zed
```

Observed weapon/character banks use indexed PS2 textures, with
TEX0 selecting the palette.

Supported in current tooling:

- PSMT8 source texels
- CSM1 palette ordering
- PSMCT16 palettes
- PSMCT32 palettes where applicable

## Motion

`motion.zar` contains named per-bone animation tracks sampled at 30 Hz.

Per-track channels contain:

- Vec3 translation
- quaternion rotation
- constant-channel flags

The runtime bone-name registry is recreated from first-seen motion bone
names.

## Body system

The executable builds runtime body parts recursively from the GameZ
CNode hierarchy. Recovered body-part state includes parent links,
numeric bone IDs, and backing nodes.

The authoritative character skeleton comes from the model-library node
graph (`*_mdl.zed`), not from `motion.zar`.
