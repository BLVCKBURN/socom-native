# Retail Main Menu Bootstrap

## Milestone

The immediate target is now:

```text
socom_menu_boot.exe
  -> open original readerc.zar
  -> extract original dlgMenu.rdr
  -> recover original main-menu controls
  -> create native Windows window
  -> display/select retail menu entries
```

This is intentionally an integration bootstrap, not a recreated replacement UI.

## Confirmed retail menu data

`dlgMenu.rdr` is leaf 21 in the supplied retail `readerc.zar`.

Archive payload details observed for this build:

```text
leaf: dlgMenu.rdr
data offset: 0x70610
data size:   0x17258
```

The compiled RDR string pool contains:

```text
LIBRARY
ui/assetlib/ui2d

BACKGROUND
TYPE
MPEG_LOOPING
FILENAME
run/movies/common/menuloop.pss

FONT
myriad
```

Main-menu controls include:

```text
NEW GAME
LOAD GAME
ONLINE
OPTIONS
```

Other original command/data strings include:

```text
UiprepMission1
MainMenuOnLoad
do_multi_or_medius
CallOptions
FlushMenuHistory
SWITCHMENU
dlgOptions_Menu.rdr
dlgMISSIONSELECTION.rdr
```

## Current bootstrap behavior

The first `socom_menu_boot.exe`:

- reads the original ZAR archive;
- extracts `dlgMenu.rdr` in memory;
- validates the compiled RDR string pool;
- refuses to synthesize the four main menu entries if the original data is absent;
- creates a native Win32 window;
- supports Up/Down and Enter/Space;
- exposes the exact retail menu labels.

For this first milestone, rendering is deliberately minimal and PC-native. The retail MPEG background, original UI images, font rasterization, sound, and command-state execution are the next integration layers.

## Why this step matters

It gives the project one executable target that joins:

```text
original SOCOM archive
+ original RDR menu data
+ native PC process/window
```

rather than keeping the reverse-engineered menu logic isolated in reports.

## Next work

1. Parse the object/control tree rather than only validating known strings.
2. Execute original `SWITCHMENU` / `SetMenuState` command flow.
3. Load `dlgOptions_Menu.rdr` and `dlgMISSIONSELECTION.rdr`.
4. Add the original UI textures.
5. Add a PC MPEG/PSS playback backend for `menuloop.pss`.
6. Bind keyboard/XInput/DirectInput to retail control events.
7. Replace memory-card calls with PC save storage.
8. Transition the NEW GAME path into the recovered mission-selection lifecycle.

The deeper CSeal/weapon recovery remains parked and resumes after this menu milestone.
