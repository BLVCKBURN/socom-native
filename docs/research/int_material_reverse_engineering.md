# SOCOM 1 `int_material.cpp` reverse engineering

Reference executable: `SCUS_971.34`

## Loader

`materials.rdr` is loaded through the generic RDR/config loader and then parsed
by the material loader at approximately `0x00222200`.

The parser searches the root for:

```text
SOILS
```

and creates one 0x3C-byte runtime material object per child entry.

Collision/intersection material IDs are direct indices into the global material
vector. The lookup path bounds-checks the incoming numeric ID against the
material count and then indexes the material pointer array directly.

For M8, the observed collision material IDs are:

```text
2, 3, 4, 5, 7, 9, 10, 11, 16
```

Therefore these map directly to the corresponding `SOILS` entries in
`materials.rdr`.

## Runtime material layout

Recovered layout:

```cpp
struct IntMaterial {
    char*    name;                  // +0x00
    char*    decal;                 // +0x04

    int32_t  stepSound;             // +0x08
    int32_t  stealthStepSound;      // +0x0C
    int32_t  crawlSound;            // +0x10
    int32_t  landSound;             // +0x14

    int32_t  weaponAnim;            // +0x18

    float    opacity;               // +0x1C
    float    penetration;           // +0x20
    float    ricochet;              // +0x24
    float    elasticityCoeff;       // +0x28
    float    impactRadiusMod;       // +0x2C
    float    stealthFactor;         // +0x30
    float    footStepOffset;        // +0x34

    uint8_t  flags;                 // +0x38
    uint8_t  unknown39;
    uint8_t  unknown3A;
    uint8_t  unknown3B;
};
static_assert(sizeof(IntMaterial) == 0x3C);
```

## Parsed fields

The executable contains and parses:

```text
NAME
OPACITY
STEPSOUND
STEALTH_STEPSOUND
CRAWLSOUND
LANDSOUND
PENETRATION
RICOCHET
ELASTICITY_COEFF
STEALTH_FACTOR
FOOT_STEP_OFFSET
VOLUMETRIC
IMPACT_RADIUS_MOD
LIQUID
UNDERWATER
PICKUP
WEAPONANIM
DECAL
```

Two additional strings near this parser are:

```text
UNKNOWN
PARTICLE_SYSTEM
```

These are special runtime material entries created by code.

## Field mapping

```text
NAME               -> +0x00 allocated string
DECAL              -> +0x04 allocated string

STEPSOUND          -> +0x08 resolved sound handle/id
STEALTH_STEPSOUND  -> +0x0C resolved sound handle/id
CRAWLSOUND         -> +0x10 resolved sound handle/id
LANDSOUND          -> +0x14 resolved sound handle/id

WEAPONANIM         -> +0x18 resolved runtime index/reference

OPACITY            -> +0x1C float
PENETRATION        -> +0x20 float
RICOCHET           -> +0x24 float
ELASTICITY_COEFF   -> +0x28 float
IMPACT_RADIUS_MOD  -> +0x2C float
STEALTH_FACTOR     -> +0x30 float
FOOT_STEP_OFFSET   -> +0x34 float
```

Packed flag bits at `+0x38`:

```text
bit 0  VOLUMETRIC
bit 1  LIQUID
bit 2  UNDERWATER
bit 4  PICKUP
```

Bit 3 is also manipulated by the runtime/default material constructors, but its
authoritative field name has not yet been tied to a parsed keyword and should
remain unnamed until further evidence is found.

## Constructor defaults

The generic constructor initializes:

```text
opacity           = 1.0
penetration       = 0.0
ricochet          = 0.0
elasticityCoeff   = 0.0
impactRadiusMod   = 1.0
stealthFactor     = 0.5
footStepOffset    = 0.0
```

The sound/reference fields are initialized to zero.

Special code-created material entries named `UNKNOWN` and `PARTICLE_SYSTEM`
override subsets of these defaults.

## Next step

Once the original `materials.rdr` is available, parse its `SOILS` entries in
file order and join them directly against the M8 collision `material_id`.
This will produce named collision surfaces with their penetration, ricochet,
footstep, stealth, liquid/underwater, and related gameplay behavior.
