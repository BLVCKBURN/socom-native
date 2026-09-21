# Retail Boot / Intro / Main Menu v2

## Recovered retail UI chain

The original UI archive encodes this boot path:

```text
dlgIntroScreen.rdr
  ONSTART / SWITCHMENU
  -> dlgIntroCinematic.rdr

dlgIntroCinematic.rdr
  BACKGROUND TYPE MPEG
  FILENAME run/movies/common/sony448.pss
  ONMPEGEND / switch_menu
  -> dlgSecondCinematic.rdr

dlgSecondCinematic.rdr
  BACKGROUND TYPE MPEG
  FILENAME run/movies/Intro_2.pss
  ONMPEGEND
  -> dlgMenu.rdr

dlgMenu.rdr
  BACKGROUND TYPE MPEG_LOOPING
  FILENAME run/movies/common/menuloop.pss
```

`dlgSecondCinematic.rdr` also maps START/CIRCLE/SQUARE/TRIANGLE/CROSS/directions/triggers to `goto_menu`, so the retail intro is skippable.

## Other startup movie evidence

The core reader's `splash.rdr` references:

```text
splash.pss
```

The retail executable itself contains:

```text
socom.pss
```

Their exact position before the UI chain above is not yet proven, so this checkpoint does not invent their ordering.

## PC movie backend

The PS2 MPEG backend is replaced temporarily by `ffplay.exe`.

This lets the native executable feed the original `.pss` files directly to a PC decoder while the original retail RDR files remain authoritative for:

- which movie to play;
- what screen follows it;
- when the main menu is reached.

This is an integration backend, not the final embedded movie renderer.

## Current executable behavior

`socom_retail_boot.exe`:

1. finds the UI `readerc.zar`;
2. loads `dlgIntroScreen.rdr`;
3. follows its retail `SWITCHMENU`;
4. plays `sony448.pss`;
5. follows `ONMPEGEND`;
6. plays `Intro_2.pss`;
7. follows `ONMPEGEND`;
8. loads `dlgMenu.rdr`;
9. exposes the retail NEW GAME / LOAD GAME / ONLINE / OPTIONS controls.

The menu background path `menuloop.pss` is recovered and displayed in the current bootstrap, but the looping MPEG is not yet composited behind the Win32 controls.

## Next menu integration

The next iteration should:

1. embed MPEG decoding/rendering inside the native window;
2. loop `menuloop.pss` behind the actual UI controls;
3. parse control positions/styles instead of using bootstrap positioning;
4. decode original UI textures such as `SplashLogo.tif`, arrow images and button assets;
5. execute `UiprepMission1`, `MainMenuOnLoad`, `do_multi_or_medius`, and `CallOptions` through the recovered retail UI-command dispatch;
6. transition NEW GAME into `dlgMISSIONSELECTION.rdr`.

At that point the bootstrap becomes a real native implementation of the original UI state machine rather than a validation shell.
