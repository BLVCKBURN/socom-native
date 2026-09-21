# Scheduler and Player Runtime Recovery — SCUS_971.34

This checkpoint follows the corrected ELF mapping and recovers the original retail scheduler/player bridge used by the mission frame.

## zsys scheduler

The callback registration function is:

```text
0x0030B4F0  zsys_sched.cpp
```

The global scheduler object used throughout the retail executable is:

```text
0x00527130
```

A registered callback node is `0x1C` bytes. High-confidence fields are:

```cpp
struct SchedulerNode {
    // +0x00 name/storage metadata (partially typed)
    uint8_t enabled;        // +0x04 (surrounded by unknown bytes)
    float priority;         // +0x08
    Callback callback;      // +0x0C
    void* context;          // +0x10
    // list/container links follow
}; // 0x1C bytes
```

The scheduler calls callbacks as:

```cpp
bool callback(void* context, float dt);
```

The mission callback is registered during `CMission` activation:

```text
registration callsite: 0x001FA79C
name:                  "Mission"
priority:              0.98
callback:              0x001F94A0
context:               CMission*
```

`0x001F94A0` is the thin wrapper that calls `0x001F8EB0`.

## Recovered core scheduler order

The retail application registers these callbacks through the same API:

| Priority | Name | Callback |
|---:|---|---:|
| 0.80 | UnitTick | `0x002E1A20` |
| 0.90 | ai_pre_tick | `0x002C4910` |
| 0.98 | Mission | `0x001F94A0` |
| 1.00 | entity_pre_tick | `0x001F08F0` |
| 3.00 | weapon_pre_tick | `0x003490E0` |
| 4.50 | CAiEventTick | `0x0017E900` |
| 4.60 | entity_tick | `0x001F07D0` |
| 4.70 | ai_direg_tick | `0x002C4690` |
| 5.00 | diTick | `0x00231DC0` |
| 6.00 | entity_post_tick | `0x001F05C0` |
| 8.00 | weapon_post_tick | `0x003490A0` |
| 10.50 | ai_route_planner | `0x0017FDB0` |
| 10.60 | ai_post_tick | `0x002C4340` |
| 11.00 | sound_post_tick | `0x00302DF0` |

Other recovered registrations include `MissionFadeToEnd`, `Update visual effects`, `CClutterAnimManager`, `ParticleTick`, and dynamically named zAnim callbacks.

This gives the native conversion the original engine ordering rather than an invented update loop.

## Current-player global

The ELF `.reginfo` section gives:

```text
GP = 0x00494670
```

`0x00200010` is a one-load getter:

```text
return *(gp - 0x7128);
```

Therefore the original current-player global is:

```text
0x0048D548
```

The paired setter is:

```text
0x00200020
```

Known setter call sites include:

```text
0x0017BFAC  application/state path
0x00252000  seal/player creation path
```

The second site occurs immediately after the player/seal object is created and before it is registered with the entity system, giving high confidence that this is the retail current-player pointer.

## CMission frame → player/seal

`CMission::Tick` at `0x001F8EB0` calls the current-player getter at:

```text
0x001F8EF8
0x001F91A4
```

The later path takes:

```text
current_player + 0x1C
```

and passes it to:

```text
0x00250C90
0x00250E30
```

Both routines are in the code immediately preceding the embedded `seal.cpp` source anchor.

### 0x00250C90

This routine:

- iterates a global list,
- processes entries whose low type bits equal 8 or 9,
- decrements a floating timer unless it is the sentinel `9,999,999.0`,
- measures distance relative to the object passed from `current_player + 0x1C`,
- performs cleanup/action when the timer expires and the distance condition is satisfied,
- otherwise extends the timer by 60 seconds.

The exact original semantic name is not yet proven, so the recomp layer intentionally keeps the neutral name `kSealNeighborhoodTick`.

### 0x00250E30

This is a small global/deferred cleanup routine in the same neighborhood. It does not materially use its incoming `a0` value, despite the mission frame passing `current_player + 0x1C`.

Its exact class/method name remains unresolved, so it is kept as `kSealNeighborhoodCleanup`.

## Next recovery step

The next high-value work is:

1. recover the player creation function containing callsite `0x00252000`;
2. type the player/seal object around offsets `+0x1C`, `+0x28`, `+0xA0`, `+0x160`;
3. identify the virtual method tables reached from the seal object;
4. map the nearby `seal.cpp` functions at:
   - `0x002517A0`
   - `0x002526C0`
   - `0x002527D0`
   - `0x0025CD50`
5. connect those to the already recovered scheduler phases;
6. then move into weapon ownership/fire paths and AI/entity interaction.

The important result of this pass is that the PC port can now reproduce the original retail **scheduler order and current-player ownership model** rather than substituting a custom game loop.
