# Retail UI Runtime v7.5

This pass restores the retail boot-to-menu sequence and fixes the menu music lookup path.

## Retail boot sequence

Recovered directly from the original UI RDR files:

```text
dlgIntroScreen.rdr
  ONSTART
  -> dlgIntroCinematic.rdr

dlgIntroCinematic.rdr
  MPEG run/movies/common/sony448.pss
  ONSTART -> VagStreaming OFF
  ONMPEGEND
  -> dlgSecondCinematic.rdr

dlgSecondCinematic.rdr
  MPEG run/movies/Intro_2.pss
  ONMPEGEND
  -> dlgMenu.rdr
```

The second cinematic explicitly maps:

```text
START
CIRCLE
SQUARE
TRIANGLE
CROSS
UP
DOWN
LEFT
RIGHT
R1
R2
L1
L2
```

to `goto_menu`.

`dlgIntroCinematic.rdr` does not contain the same skip mapping, so v7.5 preserves that difference: Sony intro completes normally; `Intro_2.pss` is skippable.

## In-window cinematic playback

`sony448.pss` and `Intro_2.pss` are decoded to raw 640x448 BGRA frames by FFmpeg and displayed inside the same native Win32 surface used by the menu.

For cinematic audio, a hidden ffplay audio process reads the same PSS file while the native window displays the decoded video.

When the PSS reaches EOF the runtime follows the retail `ONMPEGEND` transition.

## Menu music lookup fix

`sounds.rdr` identifies:

```text
MUS_MENU_THEME -> SMUS021B
```

The retail executable also contains:

```text
RUN\SOUNDS\VAGSTORE.ZAR
%s.VPK
%s.VAG
%s.vpk
%s.vag
```

v7.5 therefore looks inside `VAGSTORE.ZAR` specifically for:

```text
SMUS021B.VAG
SMUS021B.vag
SMUS021B.VPK
SMUS021B.vpk
```

before falling back to a general filesystem/ZAR search.

The extracted original sound is then sent to the current PC audio backend (`ffplay`) in looping audio-only mode.

## Visual improvements retained

- real-time paced `menuloop.pss`;
- original SplashLogo lookup;
- generic ZAR asset extraction;
- original `myriad_font.tif` glyph rendering;
- RDR-driven carousel state;
- timed motion and opacity interpolation.

## Remaining fidelity work

Targeted reverse engineering is still useful for:

- exact VAG/VPK streaming semantics if FFmpeg cannot decode the retail asset;
- original menu sound effects;
- font glow pass;
- exact PS2 blending;
- delayed animation/event scheduler;
- scale and NODE animation commands.

But those are focused subsystems. The main boot/menu path can continue being converted without first reverse engineering every remaining game asset.
