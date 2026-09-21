# Weapon Runtime Recovery v2 — SCUS_971.34

## Scheduler correction

The previous scheduler checkpoint contained a transcription error:

```text
wrong: weapon_post_tick 0x003490A0
wrong: weapon_pre_tick  0x003490E0
```

The actual function pointers loaded into register `a2` at the scheduler registration sites are:

```text
weapon_post_tick 0x003390A0
weapon_pre_tick  0x003390E0
```

Both are clean function prologues in `zwep_weapon.cpp`.

## What the global weapon scheduler actually ticks

The scheduler wrappers do not directly update the player's selected `CZWeapon`.

They drive a global projectile/effect manager:

```text
manager global: 0x0048E568
disable flag:   0x0048E574
```

`weapon_pre_tick` calls `0x00335110`, which iterates active objects and calls virtual slot `+0x08`.

`weapon_post_tick` calls `0x00334F90`, which iterates them through virtual slot `+0x0C`, and removes completed entries.

## CZProjectile vtable

RTTI evidence in the retail binary identifies:

```text
type info: 0x00470A50 -> "CZProjectile"
vtable:    0x0048A9B0
```

Layout:

```text
+0x00 type-info pointer
+0x04 adjustment/metadata
+0x08 0x00333540
+0x0C 0x00333070
```

Those two virtuals match the manager's pre/post update slots.

## CZWeapon vtable

Retail RTTI identifies:

```text
type info: 0x00470EF0 -> "CZWeapon"
vtable:    0x0048A9C0
```

Layout:

```text
+0x00 type-info pointer
+0x04 adjustment/metadata
+0x08 0x0033A1C0
+0x0C 0x00339E30
```

`0x0033A1C0` restores the `CZWeapon` vtable and tears down owned resources, so it is the destruction path.

`0x00339E30` is the only other `CZWeapon` virtual. It calls into the projectile construction/setup path around `0x00333E70`, canonicalizes the weapon type, and interacts with the global projectile manager. It is therefore the current high-confidence candidate for the original fire/projectile-spawn action, though the exact original symbol name is not yet proven.

## Weapon-definition parsing

The retail weapon reader directly exposes:

```text
FireWait
Type
NumZoomModes
ZoomMode%d
AccBurstCnt_Min
AccBurstCnt_Max
AccScalar_Min
AccScalar_Max
```

Confirmed offsets include:

```text
CZWeapon +0x48   FireWait
CZWeapon +0x1D4  Type
```

`Type` is the same byte already used by the CSeal equipment path for rifle/pistol/grenade categorization.

## Small retail accessors

The following original functions are now trivial enough to translate directly:

```text
0x003397D0 -> float +0x40
0x003397E0 -> float +0x44
0x003397F0 -> float +0x3C
0x00339800 -> float +0x38
0x00339810 -> signed byte +0x34
0x00339820 -> uint32 +0x28
0x00339830 -> uint32 +0x24
0x00339840 -> uint32 +0x08
0x00339850 -> uint32 +0x04
```

Native equivalents are supplied in:

```text
runtime/recomp/native/weapon_definition_accessors.hpp
```

## Next path

The next reversing pass should trace callers that invoke the `CZWeapon +0x0C` virtual and determine which SEAL task/input path supplies its projectile/fire arguments. In parallel, the `seal_tasks` configuration strings `reloading`, `trigger`, and related fire-mode bits should be connected to those runtime callers.

Once that is proven, the native recomp layer can add:

1. retail fire eligibility / cooldown checks,
2. ammo ownership,
3. reload state,
4. trigger/fire-mode state,
5. projectile creation,
6. projectile pre/post tick.
