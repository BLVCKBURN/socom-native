# Mission and runtime reader findings

This note records the current schema-level reverse engineering of mission-specific and global RDR data from the SCUS_971.34 reader archives.

## Data is split by responsibility

The mission readers do not contain one monolithic mission script. The tested multiplayer mission archives separate configuration into several small systems:

| Reader | Responsibility observed |
| --- | --- |
| `mission.rdr` | Loading-screen asset, mission UI strings, equipment-enable valves, UI vehicle/team names |
| `mp9.rdr` / `mp10.rdr` / `mp11.rdr` / `mp12.rdr` | World envelope: units, lighting, fog, camera, grid, asset libraries, terrain/world root |
| `valves.rdr` | Mission-local named state/counter slots |
| `actions.rdr` | Interactive world node -> valve/animation/action-type binding |
| `chartype.rdr` | Navy SEAL, terrorist, and escortee character definitions |
| `vehicles.rdr` | Spawnable character/actor definitions and spawn/setup/team data |
| `aimaps.rdr` | AI navigation-map file list |
| `net_*.rdr` | Named graph nodes, positions, radius/flags, and optional global IDs |
| `map.rdr` | World extents/floor metadata |
| `*_lib.rdr` | Asset-library manifests (textures, palettes, models and related asset metadata) |

This suggests a native runtime should load these as configuration into separate subsystems rather than build a single RDR scripting interpreter.

## Global valve registry

`global_valves.rdr` is a global state registry containing **107 entries** in this build; **36** are explicitly marked `PERSIST`. Observed names include mission completion/failure/timeout, persistent controller settings, multiplayer game/round state, timers, scores, team/player readiness, bomb state/owner, friendly fire, filters, and UI state.

Persistent values are explicitly tagged with `type = "PERSIST"` in the data.

Per-map `valves.rdr` adds local state. Examples:

```text
MP9 / MP12:
  mp_bomb      id 6  initial 0
  round_count  id 7  initial 0

MP10 / MP11:
  rescue       id 6  initial 0
  rescuecount  id 7  initial 0
  round_count  id 8  initial 0
  rescuecheck  id 9  initial 0
  freecount    id 10 initial 0
  killcount    id 11 initial 0
```

A small named integer/counter system is therefore a concrete early native-runtime requirement.

## Multiplayer mission descriptors

Decoded mission titles and modes from the tested archives:

| Map | Title | Mode evidence |
| --- | --- | --- |
| MP9 | BITTER JUNGLE | bomb placement/defusal text; two MPBOMB actions |
| MP10 | BLOOD LAKE | hostage rescue text; three escortee character types |
| MP11 | DEATH TRAP | hostage rescue text; three escortee character types |
| MP12 | THE RUINS | bomb placement/defusal text; two MPBOMB actions |

The bomb maps bind two world nodes through `actions.rdr`. MP9 uses `terrorist_tent` / `seal_tent` with action range 32; MP12 uses the same node names with range 34. Both use animation `BombPlace`, action type `MPBOMB`, and bitmap `action_MP_bomb.tif`.

## Multiplayer master configuration

`mpconfig.rdr` supplies the higher-level game-mode configuration. The decoded multiplayer map entries all use `game_time = 360` and `round_count = 11` in this data set.

| Map | Game type |
| --- | --- |
| MP1 | FOOTBOMB |
| MP2 | DEATHMATCH |
| MP4 | EXTRACT |
| MP5 | DEATHMATCH |
| MP6 | EXTRACT |
| MP7 | FOOTBOMB |
| MP8 | DEATHMATCH |
| MP9 | FOOTBOMB |
| MP10 | EXTRACT |
| MP11 | EXTRACT |
| MP12 | FOOTBOMB |

The MP9/MP12 `FOOTBOMB` classification matches their bomb placement/defusal `mission.rdr` text and `actions.rdr` entries. The MP10/MP11 `EXTRACT` classification matches their escortee/hostage records. This cross-file agreement is strong evidence for the intended responsibility of these schemas.

## World envelope

All four decoded multiplayer world files use `MetersPerUnit ~= 0.1`.

| Map | Default material | Night | Grid dimension | Cell dimension | World root model |
| --- | --- | ---: | --- | ---: | --- |
| MP9 | GRASS | 0 | 11 x 9 | 320 | `congo_mp9` |
| MP10 | GRASS | 0 | 11 x 11 | 256 | `mp10` |
| MP11 | none | 1 | 6 x 7 | 320 | `mp11` |
| MP12 | STONE | 0 | 15 x 14 | 180 | `mp12_terrain` |

Each tested `world_tree` contains one type-2 top-level world/terrain node in these reader files; the detailed scene content is expected to be reached through the referenced model/asset data rather than enumerated inline here.

## Character and actor configuration

`chartype.rdr` maps logical multiplayer slots (`Seal1`...`Seal4`, `Terrorist1`...`Terrorist4`) to map-specific character definitions such as `mp9_seal1` and `mp12_terror4`.

MP10 and MP11 also provide `POW1`, `POW2`, and `POW3` escortee definitions. Their `vehicles.rdr` files create three hostage actors with team data and named setup/start nodes.

The term `vehicles` is therefore not limited to drivable vehicles in these data sets; it is being used for game actor/entity setup as well.

## Navigation examples

MP12 exposes both `net_000.rdr` and `net_global.rdr` graph data. The graph records contain names, 3D positions, radii, flags, and in the global form a `gid` pair. `aimaps.rdr` separately references one or more `.map` navigation files.

This points to at least two navigation layers: reader-level graph nodes and external AI map files.

## Core reader examples

Other global readers confirm that RDR is used as a general engine configuration language:

- `ai_moves.rdr`: animation name + move type + stance records.
- `ai_turrets.rdr`: turret health, parts, yaw/pitch limits, weapon/ammo, detection range and team.
- `controller.rdr`: named controller presets and mapping records.
- `motion_range.rdr`: default and per-animation positional/rotational tolerances.
- `bmode.rdr`: compact render/blend-mode tables.

The native port should therefore keep the generic RDR tree loader separate from schema-specific adapters such as `ValveRegistry`, `MissionDescriptor`, `WorldConfig`, `CharacterTypeSet`, and `ActorSpawnConfig`.
