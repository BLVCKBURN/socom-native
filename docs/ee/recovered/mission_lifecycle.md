# SCUS_971.34 Mission Lifecycle Recovery

This document records behavior recovered directly from the retail `SCUS_971.34` EE executable. Addresses are version-locked to SHA-256:

`5111e776f64611ae1dcdc725f0ce9db660e90e1763e897e8f5b90f7402ce30d1`

## Corrected lifecycle

The earlier working name `mission_construct_init` for `0x001FA610` was too broad. Direct callers now separate construction, selection, activation, frame execution and shutdown:

| Phase | Retail address | Evidence |
|---|---:|---|
| Global `CMission` construction / one-time init | `0x001FB4F0` | Called on singleton `0x004D4880` from startup at `0x00140658`; initializes object fields, common readers and subsystems. |
| Mission descriptor selection | `0x001FAE10` | Called by UI `SetMission` at `0x001D61A0`; reads `mission.rdr`, `UiVars`, `Valves`, `LoadingScreenAssets`, `WEAPON_MODEL_POSTFIX`, `TacMapZoom`, reverb data, etc. |
| Selected mission activation | `0x001FA610` | Called from state-loading path `0x00172EC0` with the selected mission string. Mounts `readerm.zar`, mission sound banks, objective/camera/action data, animation data and mission nodes. |
| Animation data mount | `0x001F9D70` | Builds `/run/sp/%s/zanim.zar`, `/run/sp/%s/mzanim.zar` (plus MP/general variants) and registers `common` / `mission` animation sets. |
| Runtime/state entry | `0x001F9FA0` | Called at `0x0017CC58`; creates mission result valves and initializes mission runtime systems. |
| Per-frame mission routine | `0x001F8EB0` | Actual frame routine. `0x001F94A0` is only a scheduler callback wrapper. |
| Objective maintenance | `0x001F8900` | Runs from the frame path and references `PRIMARY OBJ` / `SECONDARY OBJ`. |
| Terminal result state | `0x001F8580` | Receives result values 2/3/4/5 for complete/failure/abort/timeout. |
| Runtime cleanup | `0x001F92A0` | Removes `common` / `mission` animation sets and tears down runtime systems. |
| State shutdown | `0x001F9780` | Called from state shutdown callback at `0x00172E3C`; clears active mission state and scheduler/runtime registrations. |

## Mission result enum

The frame code checks named valve handles and invokes `0x001F8580` with the following observed values:

```cpp
enum class MissionResult : uint8_t {
    Complete = 2,
    Failure  = 3,
    Abort    = 4,
    Timeout  = 5,
};
```

`0x001F8580` stores the state byte at `CMission + 0x18`. It uses `CMission + 0x64` as a transition deadline: a `-1.0f` sentinel is replaced with approximately `elapsed_time + 3.0f` when a result transition begins.

## AI / mission parameter block

`0x001F9710` initializes a 0x30-byte defaults block at `CMission + 0xB4`. `0x001F94C0` then reads `ai_params` and the following named keys:

- `weather_factor`
- `respawn_time`
- `respawn_fade`
- `respawn_range`
- `light_scale`
- `light_inside`
- `shadow_scale`
- `active_limit`
- `nosnooze_limit`
- `fireteam_command`
- `SATCHEL_TIMER`
- `SEEFARTHER_BINOCS`
- `SEEFARTHER_WEAPZOOM`

The direct ordered field mapping for the scalar block is preserved in `mission_runtime_layout.hpp`. `SATCHEL_TIMER` is stored separately at `CMission + 0x5E4`, with a retail default of `10.0f` before override.

## What this means for the native port

The PC conversion can now preserve the original application semantics instead of inventing a mission manager. The native side should maintain the original lifecycle boundaries and progressively replace each retail address with an address-compatible host implementation:

`construct -> select/descriptor -> activate -> state enter -> frame -> result -> cleanup -> shutdown`

Platform-specific PS2 work (GS/VIF/VU, SPU2, PAD, CD/DVD, memory card, MPEG) can be redirected to PC backends while the mission state machine and gameplay ordering remain derived from the retail code.
