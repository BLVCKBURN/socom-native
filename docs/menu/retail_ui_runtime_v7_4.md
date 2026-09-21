# Retail UI Runtime v7.4

v7.4 replaces two of the most visible approximations in the PC front end.

## Original Myriad bitmap font

The retail core `fonts.rdr` defines:

```text
face: myriad
texture: myriad_font.tif
glowtexture: myriad_font.tif
xspacing: 1.7
opacity: 0.7
```

It also defines the source rectangle of each glyph inside the 512x256 atlas.

v7.4 includes the recovered ASCII/© glyph map and renders menu captions and legal text directly from `myriad_font.tif`. Arial remains only as a fallback if the retail atlas cannot be found.

## Timed retail menu motion

Previous builds applied `OBJECT_MOTION_FROM_TO` and opacity targets immediately.

v7.4 reads:

```text
TRANSLATE_FROM
TRANSLATE_TO
OPACITY_FROM
OPACITY_TO
RUN_TIME
```

and maintains live tween state for each object/control.

The Win32 presentation timer advances those animations at approximately 60 Hz using smooth interpolation. The main menu's 0.1-second carousel transitions therefore visibly move through the original RDR coordinates instead of snapping.

## Existing v7.3 improvements retained

- real-time paced `menuloop.pss`;
- original `SplashLogo.tif` lookup;
- ZAR asset fallback extraction;
- `MUS_MENU_THEME -> SMUS021B`;
- hidden audio-only ffplay backend for menu music.

## Next fidelity work

- retail menu SFX;
- exact glow pass from `glowtexture`;
- scale animation;
- NODE mutations;
- better timer scheduling for delayed RDR sequences;
- embedded audio playback instead of ffplay.
