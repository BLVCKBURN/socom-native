# Retail UI Runtime v7.8

The direct FFmpeg diagnostic proved both retail intro PSS files decode successfully.

Observed retail stream properties:

```text
sony448.pss
  MPEG-2 Main
  640x368
  SAR 46:45
  DAR 16:9
  29.97 fps
  duration 8.24 s

Intro_2.pss
  MPEG-2 Main
  640x368
  SAR 46:45
  DAR 16:9
  29.97 fps
  duration 1:57.78
```

This means further PSS video-format reverse engineering is not required for the boot path.

## Critical v7.8 correction

Earlier builds forced the movie to 640x448 and defined the raw-frame pipe as 640x448 BGRA.

v7.8 keeps the retail video picture at 640x368 and reads exactly:

```text
640 * 368 * 4 bytes
```

per BGRA frame.

The 640x368 image is centered vertically in the game's 640x448 UI coordinate surface, giving 40 logical pixels above and below the video region.

The temporary compatibility cache is also versioned:

```text
%TEMP%\socom-native-video-cache\sony448_pc_640x368.mpg
%TEMP%\socom-native-video-cache\Intro_2_pc_640x368.mpg
```

so stale stretched caches from older builds are not reused.

## Timing

The source is 29.97 fps. FFmpeg is still run with real-time pacing and the native window redraws frequently enough to present each source frame without accelerating the movie.

UI animation delta time is now measured from the actual Windows timer instead of assuming an exact 16 ms every callback.
