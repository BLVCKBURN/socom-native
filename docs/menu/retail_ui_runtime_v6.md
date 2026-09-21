# Retail UI Runtime v6

v6 advances the native PC front end from retail script transitions into live retail animation state.

## Added in v6

The RDR runtime now supports multiple `SEQUENCE_DEFINITION` blocks under one animation. This matters because the original main-menu carousel animations update several controls in separate sequence blocks.

The VM now executes:

- `OBJECT_ACTIVE_STATE`
- `OBJECT_MOTION_FROM_TO`
- `OBJECT_OPACITY_FROM_TO`

alongside the existing:

- `VALVE`
- `IF / ELSEIF / ELSE / ENDIF`
- `CALL_ANIMATION`
- `SWITCHMENU`
- `EnableButton`
- `DisableButton`
- `ActivateButton`
- `FlushMenuHistory`

## WAIT behavior

Short waits used for menu handoffs continue synchronously.

Long waits, such as the 110-second `UiStopAttract` timer, stop that delayed sequence instead of immediately executing attract-mode logic. A future timer queue will resume those continuations at their retail deadline.

## Verified retail behavior

Against `SCUS_971.34` UI `readerc.zar`:

```text
CallOptions
 -> dlgOptions_Menu.rdr

UiprepMission1, ChosePresetOnce=0
 -> dlgControllerPresetsNewGame.rdr

UiprepMission1, ChosePresetOnce=1
 -> dlgAlaskaCinematic.rdr

OnMenuBtnDown, MenuSpot=1
 -> MenuSpot=2
 -> NEW GAME y: 300 -> 275
 -> LOAD GAME y: 275 -> 250
 -> ONLINE y: 325 -> 300
```

Those positions are applied from the original `OBJECT_MOTION_FROM_TO` commands.

## Current visual limitation

Motion and opacity currently apply the retail target value immediately. The next rendering step is timed interpolation using each command's `RUN_TIME`.

Remaining high-value visual work:

1. timed motion/opacity interpolation;
2. `NODE` mutations and scale commands;
3. original UI images/TIFFs;
4. original font rendering;
5. in-window `menuloop.pss`;
6. menu sounds.
