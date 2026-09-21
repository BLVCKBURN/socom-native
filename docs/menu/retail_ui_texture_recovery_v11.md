# Retail UI Texture Recovery v11

The missing menu artwork has been located in the original GameZ UI texture banks.

Retail texture names:

```text
SplashLogo.tif
myriad_font.tif
arrow_top.tif
arrow_botm.tif
```

Texture banks:

```text
run/ui/assetlib/ui2d/ui2d_txr.zed
run/ui/assetlib/uibf/uibf_txr.zed
run/ui/assetlib/uimp/uimp_txr.zed
```

v11 reuses the already recovered SOCOM GameZ texture decoder used by the character/weapon pipeline.

Supported texture format:

```text
ZAR/CZAR envelope 0x00020002
PSMT8 indexed texels
linear archive texel layout
TEX0.CBP palette selection
CSM1 CLUT permutation
PSMCT16 and PSMCT32 palettes
```

At startup, the runtime looks for matching `_pal.zed` archives and decodes the menu artwork to:

```text
%TEMP%\socom-native-ui-assets
```

as RGBA PNGs.

The UI still requests the original retail names, such as `SplashLogo.tif`. The asset resolver transparently maps those requests to the decoded PNG cache.

Manual validation:

```powershell
.\runtime\retail_ui\test_retail_menu_textures.ps1 `
    -GameRoot "C:\Users\IHowa\Downloads\SOCOM"
```

Expected:

```text
READY SplashLogo.png
READY myriad_font.png
READY arrow_top.png
READY arrow_botm.png
```
