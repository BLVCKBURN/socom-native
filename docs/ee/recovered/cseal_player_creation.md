# CSeal / Player Creation Recovery — SCUS_971.34

This checkpoint follows the retail current-player pointer into the actual SEAL allocation, construction, registration, and teardown path.

## Player creation chain

The application-side player spawn routine begins at:

```text
0x00251F10
```

It builds/loads the player identity (`PLAYERNAMEVAR`, default format `Player%d`), determines the player allocation path, and reaches:

```text
0x00251FF4 -> 0x002955A0
```

`0x002955A0` is the high-confidence CSeal factory.

The factory:

1. resolves the requested player/character configuration,
2. allocates exactly `0x1180` bytes,
3. calls `0x0025D5E0`,
4. registers the object with the entity system,
5. initializes player-specific attached objects,
6. returns the final `CSeal*`.

The application wrapper then calls:

```text
0x00252000 -> 0x00200020
```

which is the already recovered current-player setter.

Therefore the player singleton at `0x0048D548` holds a `CSeal*`.

## CSeal object size

```text
sizeof(CSeal) = 0x1180 = 4480 bytes
```

This comes directly from the allocation size immediately before the constructor call.

## Constructor / destructor

```text
CSeal constructor:  0x0025D5E0
CSeal destructor:   0x0025CD50
```

The constructor installs:

```text
primary vtable:    0x004899F0
secondary vtable:  0x00489A4C
```

The destructor writes the same vtables before tearing the object down, giving strong constructor/destructor symmetry.

## High-confidence layout

Selected fields:

```text
+0x000  primary vtable
+0x028  entity/model/node pointer
+0x0C0  owned controller/interface pointer
+0x110  secondary/base-interface vtable
+0x160  state/mode byte
+0x161  secondary state byte
+0x162  state/flag byte
+0x164  float state parameter

+0x478  creation descriptor/source pointer
+0x530  major embedded gameplay/task subsystem

+0xDC8  attached runtime/controller pointer
+0xDCC  peer/backlink pointer
+0xDD8  owned/reference pointer
+0xDDC  owned/reference pointer
+0xDE0  seal-adjacent dynamic record

+0xE90  embedded container
+0xFAC  embedded container
+0x1020 embedded owned object
```

The complete current map is emitted by `recover_cseal` as `cseal_layout.csv`.

## State byte at +0x160

Two adjacent functions beginning at:

```text
0x00252050
0x00252150
```

dispatch through jump tables based on the signed byte at `CSeal + 0x160`.

This is high-confidence evidence that `+0x160` is a compact player state/mode enum.

The destructor resets:

```text
+0x160 = 0
+0x161 = -1
+0x162 = 0
+0x164 = 1.0f
```

## Embedded subsystem at +0x530

`CSeal + 0x530` is repeatedly passed into a family of functions during state changes, construction, and destruction. It is one of the highest-value next subobjects to identify because it participates directly in player runtime transitions.

## Seal-adjacent record at +0xDE0

The earlier `seal.cpp` function `0x002517A0` stores its resulting record pointer into:

```text
CSeal + 0xDE0
```

`0x00251770` destroys the current record.

Related routines:

```text
0x002526C0  list removal / destruction path
0x002527D0  allocate and initialize a 0x2C-byte record
```

These records carry type/flag bits, a timer, a pointer at `+0x10`, and list ownership. They are used by the mission-frame path previously recovered.

## Creation source module

The separate source anchor:

```text
seal_create.cpp
```

begins around `0x002956F0`, directly after the factory. This confirms the factory/player setup neighborhood is original SEAL creation code rather than a generic entity allocator.

Embedded retail strings in this module include:

```text
CSealEx
zdb::CNodeEx
netchar%d
seal_create.cpp
grenade
rifle
pistol
clib
gravity
```

## Next reverse-engineering targets

The strongest path from here is:

1. type the state enum at `+0x160`,
2. identify the `+0x530` embedded gameplay/task subsystem,
3. resolve the vtable at `0x004899F0`,
4. map the important virtual slots used during spawn and destruction,
5. continue through `seal_create.cpp`,
6. recover input/controller ownership at `+0x0C0` and `+0xDC8`,
7. trace weapon slots (`rifle`, `pistol`, `grenade`) from the creation path,
8. connect those fields to `weapon_pre_tick` / `weapon_post_tick`,
9. begin native implementations for the simplest recovered CSeal accessors/state helpers.

This moves the PC conversion from "current player is an opaque pointer" to a concrete retail CSeal object with lifecycle, size, vtables, state bytes, and major subobjects.
