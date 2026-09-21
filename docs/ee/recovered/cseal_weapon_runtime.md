# CSeal Weapon / Equipment Runtime — SCUS_971.34

This checkpoint corrects the earlier interpretation of `CSeal + 0x160` and recovers the first concrete retail weapon inventory layout.

## Zoom-control correction

`CSeal + 0x160` is not the master player locomotion state.

The transition code references the literal:

```text
zoom_control
```

and writes zoom-scale values through `CSeal + 0x164`.

Current layout:

```text
+0x160  zoom state
+0x161  previous zoom state
+0x162  state-changed/dirty flag
+0x164  zoom scale/parameter
```

State-transition cases include fixed values:

```text
1.0
1.5
2.0
9.0
```

and weapon-derived dynamic values.

## Named attachment handles

The CSeal constructor resolves:

```text
weapon
rifle
pistol
grenade
```

and stores the resulting handles at:

```text
+0x2D0  weapon
+0x2D4  rifle
+0x2D8  pistol
+0x2DC  grenade
```

These are attachment/name/runtime handles, not the actual weapon object pointers.

## Equipment subsystem

The large subobject at:

```text
CSeal + 0x530
```

is now high-confidence weapon/equipment runtime.

Recovered fields relative to that subobject:

```text
+0x0DC  weapon instance pointer array (30 slots)
+0x154  per-slot companion pointer/data
+0x814  current selected slot
+0x81C  slot count
+0x820  owning CSeal*
+0x828  selectable/order count
+0x82C  selectable/order index array
```

`0x002B6DF0` bounds-checks slots against `0x1E` (30), then stores the weapon pointer into `+0x0DC + slot*4`.

## Retail weapon accessors translated natively

Two original EE functions now have direct host C++ equivalents.

### 0x002BB680

Retail behavior:

```cpp
return equipment->weaponSlots[equipment->currentSlot];
```

Native implementation:

```text
runtime/recomp/native/cseal_weapon_accessors.hpp
GetCurrentWeaponGuestPtr()
```

### 0x002BB5E0

Retail behavior:

- uses `+0x81C` as slot count,
- accepts positive and negative indexes,
- wraps indexes into the available range,
- treats `index == count` as a no-weapon sentinel,
- returns the pointer from `+0x0DC + index*4`.

Native implementation:

```text
GetWeaponWrappedGuestPtr()
```

A standalone smoke test covers current selection, forward wrapping, negative wrapping, sentinel behavior, and empty inventory.

## Character equipment creation

During `seal_create.cpp`, the retail game reads a list from the player creation descriptor around:

```text
descriptor +0x258 / +0x260
```

Each entry is 12 bytes.

For each entry, the equipment runtime is populated through the `0x002B6Dxx` family and calls into the actual weapon subsystem.

Concrete cross-subsystem calls include:

```text
0x00295BA0 -> 0x00339760
0x00295C24 -> 0x0033A610
0x00295C50 -> 0x0033A610
```

The weapon category byte at:

```text
weapon + 0x1D4
```

selects category labels including:

```text
rifle
pistol
grenade
```

This is the direct bridge between the CSeal creation descriptor and `zwep_weapon`.

## Next targets

The next retail conversion work should follow:

1. current weapon pointer from `0x002BB680`,
2. weapon instance methods in `zwep_weapon.cpp`,
3. identify fire/reload/equip state functions,
4. map ammo ownership through `zwep_ammo.cpp`,
5. connect those calls to:
   - `weapon_pre_tick  0x003490E0`
   - `weapon_post_tick 0x003490A0`
6. recover input/controller calls that cause the equip/fire transitions,
7. translate the simplest weapon getters/state functions to native C++.

That is the shortest path from recovered CSeal creation to original retail aiming and gunfire.
