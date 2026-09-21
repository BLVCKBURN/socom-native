# Retail UI Runtime v7.6 — Intro decode reliability

v7.5 could display a featureless black window indefinitely if the intro PSS failed to resolve or FFmpeg launched without producing frames.

v7.6 makes that failure observable and recoverable.

## FFmpeg changes

The retail PSS decoder now explicitly uses:

```text
-map 0:v:0
-an
-sn
-dn
-fflags +genpts
-probesize 50M
-analyzeduration 50M
-vf scale=640:448:flags=lanczos,format=bgra
-vsync 0
```

The runtime still uses `-re` so video presentation follows source timing.

## No more silent black screen

The runtime now distinguishes:

- movie path not found;
- ffmpeg.exe missing;
- FFmpeg process launch failure;
- decoder process exit;
- no first frame after 5 seconds.

Before the first decoded frame, the native window displays the file it is trying to decode.

If an intro cannot decode, v7.6 reports the exact resolved path and advances to the next retail boot stage instead of hanging forever on black.

This means a problem with `sony448.pss` can no longer prevent reaching `Intro_2.pss`, and a problem with `Intro_2.pss` can no longer prevent reaching `dlgMenu.rdr`.
