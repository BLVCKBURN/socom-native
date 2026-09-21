# Retail UI Texture Recovery v11.1

The first v11 extraction attempt revealed that SOCOM's UI GameZ banks contain
mixed texture formats.

`blue.tif` is not in the PSMT8 indexed layout used by the character/weapon
texture banks. The original extractor scanned every texture before processing
the requested target, so hitting `blue.tif` aborted the entire archive.

v11.1 changes the scanner to skip unrelated unsupported texture records and
continue searching for supported PSMT8 targets.

This allows assets such as:

```text
SplashLogo.tif
myriad_font.tif
arrow_top.tif
arrow_botm.tif
```

to be decoded if they use the already-supported PSMT8 layout even when the
same ZED contains other PSM types.

If a requested retail asset itself uses an unsupported format, the extractor
now reports:

```text
texture is absent or uses a texture layout not yet supported
```

and the asset-preparation script continues trying the other UI texture banks.

PowerShell native-command errors are also made non-terminating while probing
multiple banks.

A new diagnostic helper can list all currently supported PSMT8 textures from
the UI banks:

```powershell
.\runtime\retail_ui\inspect_ui_texture_banks.ps1 `
    -GameRoot "C:\Users\IHowa\Downloads\SOCOM" `
    -TextureTool "<path to socom_texture_tool.exe>"
```
