# Retail UI Texture Recovery v11.4

v11.1 successfully demonstrated that all four required main-menu textures can
be extracted from the retail `ui2d` bank.

Later PowerShell wrappers reintroduced a failure mode by letting native stderr
or process invocation behavior interfere with bank probing.

v11.4 makes the extraction path deterministic:

- the four temporary PNGs are regenerated on every launch;
- the extractor is invoked through `cmd.exe`;
- stdout/stderr are redirected inside `cmd.exe`, so Windows PowerShell cannot
  promote an expected native error to `NativeCommandError`;
- each target continues through all three UI banks until recovered;
- unresolved targets print the extractor result for every attempted bank.

The four expected results remain:

```text
READY SplashLogo.png
READY myriad_font.png
READY arrow_top.png
READY arrow_botm.png
```

A focused helper is also included to test the known retail `ui2d` pair directly:

```powershell
.\runtime\retail_ui\test_ui2d_targets.ps1 `
    -GameRoot "C:\Users\IHowa\Downloads\SOCOM" `
    -TextureTool "<path-to-socom_texture_tool.exe>"
```
