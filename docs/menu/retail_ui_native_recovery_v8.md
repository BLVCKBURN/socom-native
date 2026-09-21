# Retail UI Native Recovery v8

The project is no longer treating the retail menu as something to recreate by hand.

The intended architecture is:

```text
SCUS_971.34
  -> recover/recompile original UI engine behavior

retail RDR/ZAR/TIF/PSS/VAG data
  -> consume original assets and declarative scripts

PS2 hardware services
  -> replace only the platform backend
```

## Recovered retail command queue

Disassembly of `SCUS_971.34` establishes the following handlers:

```text
0x001D40F0 DisableButton
0x001D41F0 EnableButton
0x001D42F0 ActivateButton
0x001D4750 FlushMenuHistory
0x001D5DB0 SwitchMenu
```

`DisableButton`, `EnableButton`, and `ActivateButton` resolve their argument and append a record to the menu storage at `0x004B3450`.

Recovered record:

```cpp
struct RetailMenuCommandRecord {
    uint32_t type;
    char text[48];
}; // 0x34
```

The retail storage contains 40 records.

Recovered command values:

```text
5 SwitchMenu
6 ActivateButton
7 EnableButton
8 DisableButton
```

v8 implements this 40-record native queue and routes those RDR operations through it. This preserves the retail command/event ordering rather than immediately mutating a hand-authored screen.

## Why the original PS2 menu cannot simply be called from Windows

The functions in `SCUS_971.34` are Emotion Engine MIPS machine code. Windows x64 cannot directly execute them.

For a native port, the choices are:

- emulate the PS2 CPU and hardware;
- dynamically/static-recompile the MIPS;
- recover the functions into equivalent native C++.

`socom-native` is incrementally doing the third, with the retail ELF as the behavioral source of truth.

Only these boundaries need actual PC replacements:

```text
GS/VIF/VU graphics
MPEG/PSS playback
SPU2/VAG audio
PAD input
memory card/save storage
```

The menu design, state flow, command IDs, scripts, strings and assets do not need to be reinvented.

## v8 startup

v8 returns to direct `dlgMenu.rdr` startup by default. The intro-video experiment remains available, but it no longer blocks front-end recovery.

## Menu music

The retail menu requests `MUS_MENU_THEME`, mapped to `SMUS021B`.

The retail executable references `RUN\SOUNDS\VAGSTORE.ZAR` and `.VAG`/`.VPK` lookup. v8 retains VAGSTORE extraction and adds a temporary FFmpeg-to-WAV compatibility cache before playback.

If the theme still does not play, the next step is targeted reversal of the VAG/VPK sound bank rather than more UI reconstruction.
