# Retail UI Runtime v4

This checkpoint replaces the main-menu-only executable with a reusable compiled-RDR screen runtime.

## What now works

The runtime can load arbitrary retail UI `.rdr` leaves from `UI\readerc.zar` and extract:

- background type / filename;
- font;
- TEXT/IMAGE object metadata;
- BUTTON controls;
- retail positions and sizes;
- captions;
- NORMAL / ACTIVE / PRESSED / DISABLED styles;
- button CROSS animation;
- direct button argument;
- animation-definition index;
- nested `CALL_ANIMATION`;
- `ui::UI_COMMAND TYPE SWITCHMENU ARGUMENT ...`.

A menu-history stack now tracks screen changes.

## Tested retail screens

The data model is exercised against:

```text
dlgMenu.rdr
dlgOptions_Menu.rdr
dlgOptions_Audio.rdr
dlgOptions_Video.rdr
dlgControllerSelection.rdr
dlgControllerPresets.rdr
dlgControllerPresetsNewGame.rdr
dlg_documentary.rdr
dlgMISSIONSELECTION.rdr
```

This is the beginning of one PC implementation of the original UI engine rather than separate hand-written screens.

## Current interaction

- Arrow keys navigate the active screen's parsed BUTTON controls.
- Enter/Space invokes the retail CROSS animation.
- If the button contains a direct `.rdr` argument, it performs a native `SWITCHMENU`.
- If the CROSS animation calls another animation, the runtime recursively resolves the first retail `SWITCHMENU`.
- Escape/Q pops the PC-side representation of the original menu-history stack.

## Platform-dependent paths

Two paths remain deliberately explicit rather than faked:

```text
LOAD GAME -> MainMenuOnLoad
ONLINE    -> do_multi_or_medius
```

LOAD GAME depends on a PC replacement for memory-card/save management.
ONLINE depends on replacement networking/IOP behavior.

## Next implementation work

The remaining work to call the front end "retail-complete" is now concentrated:

1. animation interpreter:
   - VALVE assignment/comparison
   - IF / ELSE / ELSEIF / ENDIF
   - WAIT
   - OBJECT_ACTIVE_STATE
   - OBJECT_MOTION_FROM_TO
   - NODE mutations
2. UI asset renderer:
   - IMAGE objects and state images
   - original fonts
3. integrated MPEG renderer:
   - intro PSS
   - looping menuloop.pss background
4. sound/VAG menu backend
5. PC save backend replacing memory card
6. online-platform strategy
7. exact controller button mapping.

The screen parser/state transition architecture is now shared across the retail front end.
