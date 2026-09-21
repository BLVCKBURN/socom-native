# Retail UI Texture Recovery v11.2

The first successful rendering of the original menu textures exposed two
presentation bugs in the PC compatibility layer.

## Vertical texture origin

The original decoded assets appeared as:

```text
SplashLogo   upside down
arrow_top    pointing down
arrow_botm   pointing up
myriad atlas vertically inverted
```

That is a vertical-origin mismatch between the decoded PS2 texture and the PC
top-left image convention.

v11.2 flips decoded RGBA rows before writing the temporary PNG cache.

The old `%TEMP%\socom-native-ui-assets` PNG cache is invalidated once so the
corrected files are regenerated automatically.

## IMAGE scale

The retail `dlgMenu.rdr` object definition for the logo contains:

```text
SplashLogo
x=85
y=32
w=470
h=216
scale=0.5
```

Earlier PC builds drew the full 470x216 rectangle.

v11.2 now applies object scale to IMAGE dimensions, yielding an intended logo
extent of approximately:

```text
235 x 108 logical pixels
```

The same logic applies to the retail arrow images and other IMAGE objects.

This pass is specifically based on the first successful screenshot using the
actual PS2 menu textures rather than approximations.
