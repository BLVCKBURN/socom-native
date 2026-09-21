# Retail UI Texture Recovery v11.3

v11.3 fixes Windows PowerShell native-process error handling while probing
multiple retail UI texture banks.

An expected extractor miss such as:

```text
texture is absent or uses a texture layout not yet supported: myriad_font.tif
```

must not stop the search. The same texture name may exist in another bank
(`uimp_txr.zed`, for example).

The asset preparation script now launches `socom_texture_tool.exe` using
`Start-Process` with redirected stdout/stderr and checks only the process exit
code. Non-zero exits are treated as probe misses and the script continues to
the next texture bank.

This is especially important on Windows PowerShell 5.x, where stderr from a
native executable can become a terminating `NativeCommandError` when
`$ErrorActionPreference = "Stop"`.
