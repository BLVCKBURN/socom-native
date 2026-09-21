# Retail UI Runtime v7 — Visual Fidelity Pass

v7 changes priority from diagnostic rendering to the actual retail visual composition.

## Original retail main-menu visual data

`dlgMenu.rdr` defines:

```text
BACKGROUND:
  TYPE MPEG_LOOPING
  FILENAME run/movies/common/menuloop.pss

OBJECTS:
  SplashLogo   IMAGE SplashLogo.tif  x=85  y=32   size=470x216
  LEGALEZE     TEXT  Developed by Zipper Interactive, Inc.
  LEGALEZE1    TEXT  ©2002 Sony Computer Entertainment America Inc.
  arrow_up     IMAGE arrow_top.tif
  arrow_dwn    IMAGE arrow_botm.tif
```

The button/control layout remains driven by the original RDR.

## v7 rendering

- `menuloop.pss` is decoded by the installed `ffmpeg.exe`.
- FFmpeg raw BGRA frames are piped into the native process.
- Frames are rendered directly into the same Win32 window.
- Original TIFF assets are loaded with Windows GDI+.
- IMAGE objects use their original RDR coordinates and sizes.
- TEXT and BUTTON objects remain RDR-driven.
- The runtime remains 640x448 logical coordinates and scales to the window.

This is intentionally different from launching a separate ffplay window: the original movie is now the menu background surface of the native PC front end.

## Game root

v7 needs the root containing the extracted retail RUN/UI data so it can resolve:

```text
run/movies/common/menuloop.pss
SplashLogo.tif
arrow_top.tif
arrow_botm.tif
data/common/dialog/uianim.rdr
data/common/dialog/UIMemCardAnim.rdr
```

Run:

```powershell
.\runtime\retail_ui\build_and_run.ps1 -GameRoot "D:\SOCOM"
```

Optionally specify the UI archive explicitly:

```powershell
.\runtime\retail_ui\build_and_run.ps1 `
  -GameRoot "D:\SOCOM" `
  -UiReader "D:\SOCOM\UI\readerc.zar"
```

## Remaining fidelity work

v7 is the first visual-retail pass, not the final renderer.

Still to recover/replace:
- exact Myriad font asset rendering;
- timed interpolation rather than target snapping;
- arrow/object animation timing;
- NODE mutations and scale animation;
- menu sound playback;
- external `uianim.rdr` and `UIMemCardAnim.rdr` filesystem loading;
- exact PS2 video color/aspect behavior;
- screen-specific TIFF backgrounds beyond the main menu.
