# Retail Menu Conversion v3

This checkpoint replaces the earlier string-only menu bootstrap with a generic compiled-RDR AST parser and a data-driven main-menu model.

## Corrected retail main menu

`dlgMenu.rdr` contains five primary BUTTON controls:

| MenuSpot | Control | Caption | Source position | CROSS animation |
|---:|---|---|---|---|
| 1 | `new_game_button` | NEW GAME | 256,330 | `UiprepMission1` |
| 2 | `multiplayer_button` | ONLINE | 256,360 | `do_multi_or_medius` |
| 3 | `options_button` | OPTIONS | 256,390 | `CallOptions` |
| 4 | `tutorial_button` | DOCUMENTARY | 256,420 | `CallDocumentary` |
| 5 | `load_game_button` | LOAD GAME | 256,300 | `MainMenuOnLoad` |

The earlier four-item bootstrap omitted DOCUMENTARY. This is corrected.

## Retail carousel behavior

`OnMenuBtnDown` advances:

```text
1 -> 2 -> 3 -> 4 -> 5 -> 1
```

`OnMenuBtnUp` reverses the same loop.

The five visible carousel slots are:

```text
Y = 250, 275, 300, 325, 350
```

with the selected item centered at logical Y=300. The RDR animation definitions move each control between these slots over 0.1 seconds.

## Data-driven styles

The parser now reads each BUTTON's actual:

- caption
- XPOS / YPOS
- XSIZE / YSIZE
- NORMAL scale/color
- ACTIVE scale/color
- PRESSED scale/color
- DISABLED scale/color
- CROSS animation name

No menu captions or colors need to be synthesized by the PC runtime.

## Confirmed transitions

Retail animation data currently establishes:

```text
UiprepMission1
  first-use -> dlgControllerPresetsNewGame.rdr
  configured path -> dlgAlaskaCinematic.rdr

CallOptions
  -> dlgOptions_Menu.rdr

CallDocumentary
  -> dlg_documentary.rdr
```

The online and load-game paths remain dependent on platform services:
- `do_multi_or_medius` requires network/IOP replacement work.
- `MainMenuOnLoad` requires the PC memory-card/save shim.

## What is still not "full retail"

This checkpoint does not yet claim pixel-perfect retail UI. Remaining work:
1. decode/render original UI TIFF/image assets;
2. integrate `menuloop.pss` inside the same native window rather than external ffplay;
3. execute animation definitions generically instead of implementing only the recovered carousel behavior;
4. implement valve storage and IF/ELSE evaluation;
5. implement the UI command VM (`SWITCHMENU`, `ActivateButton`, `DisableButton`, `EnableButton`, `NODE`, etc.);
6. parse/render Options, Documentary, controller preset and mission-selection dialogs with the same generic renderer;
7. PC save backend for LOAD GAME;
8. PC network backend/stub strategy for ONLINE.

The architecture is now aimed at one reusable RDR-driven UI runtime rather than a separate hand-coded screen for every menu.
