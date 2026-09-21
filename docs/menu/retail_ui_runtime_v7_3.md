# Retail UI Runtime v7.3

This pass focuses on visible retail fidelity.

## Water/menu-loop pacing

v7 decoded `menuloop.pss` as fast as FFmpeg could produce frames. The window sampled the newest frame, which made the water appear too fast and jittery.

v7.3 launches the decoder with:

```text
-re -stream_loop -1
```

so FFmpeg respects input timestamps. Presentation invalidation is also increased to approximately 60 Hz while the source keeps its own native cadence.

## SOCOM logo

The asset resolver now:

1. searches loose files case-insensitively;
2. searches the entire extracted game tree;
3. scans compatible `.zar` archives;
4. extracts a matching retail leaf into a temporary cache.

This is used for `SplashLogo.tif`, `arrow_top.tif`, `arrow_botm.tif`, and later UI imagery.

If `SplashLogo.tif` still cannot be located, the runtime draws a temporary SOCOM text fallback in the correct logo region. The fallback automatically disappears when the real TIFF resolves.

## Retail menu music

The core retail `sounds.rdr` mapping establishes:

```text
MUS_MENU_THEME -> SMUS021B
```

v7.3 searches the extracted game data for an asset whose stem is `SMUS021B`, including compatible ZAR archives, and launches it through an audio-only hidden `ffplay` process.

The theme remains active through ordinary menu screens and is stopped when entering a full MPEG cinematic.

## Still pending

- exact Myriad font rasterization;
- timed interpolation for button/object motion;
- retail menu SFX (`SND_BACK`, `SND_THUNK`, etc.);
- exact PS2 image blending/color handling;
- embedded audio decode rather than temporary ffplay backend.
