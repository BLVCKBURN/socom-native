# Original SOCOM Front-End Reverse Engineering

This note records front-end findings recovered directly from the SCUS_971.34 GameZ reader data. It is intended to guide replacement of the temporary native front end with a faithful implementation of the PS2 state/UI flow.

## Reader archives

`readerc.zar` contains the primary dialog/UI scripts, including:

- `dlgMenu.rdr`
- `dlgMISSIONSELECTION.rdr`
- `dlgOptions_Menu.rdr`
- `dlgDebriefing.rdr`
- mission briefing/map/cinematic dialogs
- `uisounds.rdr`

The core/global reader archive contains:

- `fonts.rdr`
- `hud.rdr`
- `splash.rdr`
- `sounds.rdr`
- `controller.rdr`
- `orders.rdr`

## dlgMenu.rdr

The compiled RDR uses the same recovered GameZ cell grammar as the rest of the corpus. The main menu data recursively decodes to depth 10.

### Screen setup

Recovered screen-level configuration includes:

- asset library: `ui/assetlib/ui2d`
- background type: `MPEG_LOOPING`
- background movie: `run/movies/common/menuloop.pss`
- primary font: `myriad`
- script library: `dlgMenu.rdr`
- memory-card-aware front-end flow (`USES_MEMCARD`)

### Main menu buttons

The retail menu defines these primary controls:

1. `new_game_button` — `NEW GAME`
2. `load_game_button` — `LOAD GAME`
3. `multiplayer_button` — `ONLINE`
4. `options_button` — `OPTIONS`

Associated callbacks include:

- `UiprepMission1`
- `LoadGameOnStart`
- `do_multi_or_medius`
- `CallOptions`
- `MainMenuOnLoad`
- `OnMenuBtnDown`
- `OnMenuBtnUp`

### Common menu commands/state

Observed commands and state include:

- `SWITCHMENU`
- `SETMISSION`
- `POPUPDIALOG`
- `BackgroundMPEG`
- `ActivateButton`
- `EnableButton`
- `DisableButton`
- `AutoAim`
- `MC_UnInit`

Important valves/state names include:

- `CurrentMission`
- `GameComplete`
- `NoAutoAim`
- `CameFromDebriefing`
- `SitHere`

The menu script contains branching that selects mission/dialog flow based on `CurrentMission` and `GameComplete`, demonstrating that front-end progression is driven by the same GameZ valve/state abstraction recovered elsewhere.

### Recovered menu assets

Examples include:

- `SplashLogo.tif`
- `arrow_top.tif`
- `arrow_botm.tif`
- `blue.tif`
- `popup_load.tif`
- `popupOp.tif`
- `Pop_butt.tif`

## Mission selection

`dlgMISSIONSELECTION.rdr` defines the campaign area-of-operation screen.

Recovered configuration includes:

- libraries: `ui/assetlib/ui2d`, `ui/assetlib/uibf`
- background: `multplay_back.tif`
- font: `myriad`
- title: `AREA OF OPERATION`
- regional buttons: `ALASKA`, `THAILAND`, `CONGO`, `TURKMENISTAN`

Recovered regional artwork includes:

- `aop_alaska_picture.tif`
- `aop_thailand_picture.tif`
- `aop_congo_picture.tif`
- `aop_turkmenistan_picture.tif`
- `dogtags.tif`
- `blank_top_bar.tif`
- `common_bar_bottom.tif`

The temporary PC front end added in the campaign phase intentionally follows the recovered `NEW GAME -> AREA OF OPERATION / MISSION SELECTION` structure, while deferring original image/video/font rendering until those UI asset libraries are decoded into the native renderer.

## Options

`dlgOptions_Menu.rdr` confirms the top-level options screen and routes to subdialogs such as:

- `dlgOptions_Audio.rdr`
- `dlgOptions_Video.rdr`
- controller options

It uses the same common bar/dogtag presentation as mission selection and includes memory-card handling callbacks.

## Splash / boot media

`splash.rdr` references:

- `splash.TIF`
- `splashload.tif`
- `splash.pss`
- `winner.TIF`
- `loser.TIF`

These are the initial targets for reproducing boot/loading presentation after mission runtime stability.

## Fonts

`fonts.rdr` contains concrete glyph metrics and texture references. Recovered faces include `Gothic_13` and character-level offsets/baselines. This means the original font system is data-driven rather than requiring us to approximate the UI with system fonts permanently.

## HUD / in-game UI

`hud.rdr` contains TacMap, popup definitions, action icons, mission-specific instruction text, window geometry, colors, opacity, sound triggers and other HUD presentation data. This should become the basis of the native HUD/TACMAP rather than hard-coded replacement layouts.

## UI sound mapping

`uisounds.rdr` maps UI events such as:

- `TELETYPE`
- `BACK` -> `SND_BACK`
- `SELECTION` -> `SND_THUNK`

The broader `sounds.rdr` includes menu and mission music identifiers such as `MUS_MENU_THEME`, mission music, success/failure stingers, objective-complete/failure cues, and mission-region music sets.

## Runtime implication

The recovered front-end architecture is consistent with the rest of GameZ:

```text
RDR screen definition
        |
        +-- objects / text / images
        +-- controls
        +-- animations
        +-- UI_COMMAND
        +-- sounds
        +-- valves
        v
Native UI state machine
```

The next fidelity phase should implement a generic RDR-driven `UiScreen` interpreter rather than hand-writing each screen. The campaign front end added in this checkpoint is a bridge: it makes all successfully reconstructed missions accessible now while the original UI renderer/interpreter is brought online.
