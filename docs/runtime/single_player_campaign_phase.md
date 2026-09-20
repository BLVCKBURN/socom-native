# Single-Player Campaign Phase

This phase expands the native runtime from the M8/POW Camp vertical slice into a campaign-wide map boot path.

## Scope of this checkpoint

The campaign builder scans a locally extracted, legally owned SOCOM disc for the single-player mission banks:

- `m1_mdl.zed` / `m1_txr.zed` / `m1_pal.zed`
- ...
- `m12_mdl.zed` / `m12_txr.zed` / `m12_pal.zed`

For each mission it:

1. Reconstructs `worldmodel` collision.
2. Probes the discovered `models*.zar` archives for the matching visual world.
3. Decodes the map texture/palette banks.
4. Builds a textured `.snr` runtime pack.
5. Recursively validates the resulting pack before exposing it in the front end.
6. Records success/failure and the matched source files in `campaign_build_report.csv`.

The shared `MESH_seal_A_des` player pack is built once through the proven M8 character path and reused across the campaign.

## Native front end

`socom_frontend.exe` is a new Windows-native mission selector. It reads `campaign_manifest.txt`, lists every mission whose runtime pack validated successfully, and launches `socom-native.exe` with the shared SEAL player pack.

This is a functional native campaign shell, not yet a pixel-perfect reconstruction of the original PS2 menu. Reversing original `zUI`/`zTwoD` menu layout, fonts, backgrounds, audio, save/profile flow, briefing/armory screens, and transition logic remains a separate front-end fidelity phase.

## Campaign missions

1. Death at Sea
2. Ghost Town
3. Oil Platform Takedown
4. Golden Triangle Holiday
5. Temple at Hohn Kaen
6. City of the Forgotten
7. Mercenary Staging Area
8. POW Camp
9. Mountain Assault
10. Prison Break
11. Mouth of the Beast
12. Deathblow

## What this does *not* mean yet

A mission being listed in the front end means its world, collision, textures, and player runtime pack successfully build and load. It does not yet mean the original mission is completable.

The remaining single-player reverse-engineering/runtime work includes:

- exact mission insertion/spawn points
- weapons and equipment in the runtime
- weapon attachment, aiming, firing, reloads, ballistics/hitscan, damage, death
- actor/NPC spawning from mission data
- AI state machines and `zGraph` navigation
- squad members, squad commands, and formations
- valve/state execution
- action bindings and trigger volumes
- `mzanim` mission/choreography execution
- objectives, success/failure, checkpoints and transitions
- HUD/TACMAP/crosshair/weapon UI
- original main menu, briefing, armory and options UI
- audio/SFX/voice/music
- save/profile/options persistence
- cutscenes/video

Multiplayer remains intentionally deferred until the single-player simulation is coherent end-to-end.
