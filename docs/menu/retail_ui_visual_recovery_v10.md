# Retail UI Visual Recovery v10

v10 uses the user's current runtime screenshot as the baseline and fixes several incorrect assumptions in the PC renderer.

## 1. Retail ONSTART lifecycle

`dlgMenu.rdr` declares six ONSTART animations:

```text
ShowCompletionLevel
UiStopAttract
SetMenuValve
fadelogo
has_memcard_changed
CleanupMissionMemory
```

Previous runtime versions parsed these declarations but never executed them.

This was a major architectural error.

`SetMenuValve` sets `MenuSpot = 1` and calls `NewSetDown`.

The retail `NewSetDown` animation initializes the visible carousel as:

```text
LOAD GAME  -> (256,275) active
NEW GAME   -> (256,300) active
ONLINE     -> (256,325) active
OPTIONS    -> (256,250) inactive
DOCUMENTARY-> (256,350) inactive
```

v10 executes all locally available ONSTART animations before the first screen frame.

## 2. Child/parent transforms

The retail RDR uses parent-relative coordinates.

Examples from `dlgMenu.rdr`:

```text
LEGALEZE
  position (320,395)

LEGALEZE1
  CHILDOF LEGALEZE
  position (0,17)
```

Therefore the copyright line belongs at approximately:

```text
(320,412)
```

not `(0,17)`.

Similarly:

```text
arrow_up  = (283,233)
arrow_dwn CHILDOF arrow_up
arrow_dwn local = (0,112)
```

so the lower arrow resolves to `(283,345)`.

v10 parses `CHILDOF` from both object and SPEC nodes and recursively resolves parent-relative coordinates.

## 3. Text alignment and color

`LEGALEZE` and `LEGALEZE1` both contain the `HCENTERED` flag.

They also specify:

```text
COLOR 30 60 90
```

Earlier runtimes ignored both fields and forced all object text to centered light gray.

v10 parses and uses the retail `HCENTERED` flag and object color.

## 4. Menu background geometry

The 640x368 geometry discovered from `sony448.pss` and `Intro_2.pss` applies to those cinematics, not to the whole UI.

v10 uses:

```text
cinematic MPEG -> 640x368, centered vertically
MPEG_LOOPING menu background -> 640x448 full UI surface
```

This removes the artificial black bars around the water background.

## 5. No fake SOCOM logo

The temporary Arial "SOCOM / U.S. NAVY SEALs" fallback has been removed.

If `SplashLogo.tif` is unresolved, v10 leaves it unresolved rather than pretending an approximation is the retail asset.

A helper is included to locate the real menu textures:

```powershell
.\runtime\retail_ui\probe_menu_assets.ps1 -GameRoot "C:\Users\...\SOCOM"
```

It checks for loose copies and searches ZAR/ZED/RDR/data containers for embedded references to:

```text
SplashLogo.tif
myriad_font.tif
arrow_top.tif
arrow_botm.tif
```

The next texture milestone is to decode the actual container containing these images rather than reproducing them manually.
