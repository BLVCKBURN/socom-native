# Retail UI Runtime v5

v5 fixes the critical animation-definition indexing bug from v4 and begins executing the original retail menu VM.

## Major fix

`ANIMATION_DEFINITIONS` is a sibling of `SCREENS` in compiled UI RDR files. v4 indexed only `SCREENS`, so functions such as `CallOptions` and `UiprepMission1` appeared unresolved even though their retail definitions were already present in `dlgMenu.rdr`.

v5 indexes the entire RDR AST.

## VM operations implemented

- `VALVE name = value`
- `IF VALVE`
- `ELSEIF VALVE`
- `ELSE`
- `ENDIF`
- `CALL_ANIMATION`
- `ui::UI_COMMAND SWITCHMENU`
- `DisableButton`
- `EnableButton`
- `ActivateButton`
- `FlushMenuHistory`

`WAIT`, `SOUND`, opacity, and object-motion commands currently preserve sequencing but are visual/audio no-ops.

## Retail main-menu behavior now executed

Arrow keys no longer manually increment a PC selection index. The runtime dispatches the retail button animations:

- DOWN/RIGHT -> `OnMenuBtnDown`
- UP/LEFT -> `OnMenuBtnUp`
- CROSS -> button-specific retail animation

The VM changes the original `MenuSpot` valve and runs the original button enable/disable/activate commands.

## Proven transitions

The VM test against the retail `dlgMenu.rdr` proves:

- `CallOptions` -> `dlgOptions_Menu.rdr`
- `UiprepMission1` with `ChosePresetOnce == 0` -> `dlgControllerPresetsNewGame.rdr`
- `UiprepMission1` with `ChosePresetOnce == 1` -> `dlgAlaskaCinematic.rdr`
- `OnMenuBtnDown` changes `MenuSpot` 1 -> 2
- `OnMenuBtnUp` changes `MenuSpot` 2 -> 1

This is now executing the retail script logic instead of reproducing those transitions manually.

## Still required for visual retail fidelity

- `OBJECT_MOTION_FROM_TO`
- `OBJECT_OPACITY_FROM_TO`
- `OBJECT_ACTIVE_STATE`
- `NODE`
- external animation libraries (`uianim.rdr`, `UIMemCardAnim.rdr`) when a screen references definitions not embedded locally
- UI images/textures and original font rendering
- embedded PSS video background
- menu sounds
- PC save/memory-card backend
