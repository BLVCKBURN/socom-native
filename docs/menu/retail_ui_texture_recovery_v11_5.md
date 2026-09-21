# Retail UI Texture Recovery v11.5

v11.4 introduced a `cmd.exe` compatibility wrapper around
`socom_texture_tool.exe`. The resulting blank diagnostics for the `ui2d` bank
show that this wrapper was interfering with the command line.

This is distinct from a texture-format failure.

v11.1 had already successfully extracted all four required main-menu textures
from `ui2d_txr.zed + ui2d_pal.zed` using direct PowerShell native invocation.

v11.5 therefore restores that proven invocation path:

```powershell
& $TextureTool $txr $pal --extract $target $Cache 2>&1
```

For each native call only, `$ErrorActionPreference` is temporarily changed to
`Continue`. This prevents Windows PowerShell from terminating the scan when an
expected probe miss writes to stderr.

`$LASTEXITCODE` and the existence of the output PNG determine success.

The v11.2 fixes remain in place:

- decoded UI textures are vertically corrected;
- RDR IMAGE scale is applied;
- stale UI PNGs are regenerated;
- the retail menu lifecycle/layout fixes remain active.

A direct `ui2d` diagnostic is also included:

```powershell
.\runtime\retail_ui\test_ui2d_direct.ps1 `
    -GameRoot "C:\Users\IHowa\Downloads\SOCOM" `
    -TextureTool "<path-to-socom_texture_tool.exe>"
```
