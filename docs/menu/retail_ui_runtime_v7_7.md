# Retail UI Runtime v7.7

The direct PS2 PSS-to-rawvideo pipe used by v7.6 can stall with some Windows FFmpeg builds.

v7.7 changes boot video playback to:

```text
original .pss
  -> one-time temporary FFmpeg transcode
  -> %TEMP%\socom-native-video-cache\<movie>_pc.mpg
  -> real-time BGRA decode
  -> native SOCOM window
```

The original PSS remains the source asset. The temporary MPEG is only a runtime compatibility cache and never belongs in the repository.

A new test helper is installed:

```powershell
.\runtime\retail_ui\test_intro_video.ps1 -GameRoot "D:\SOCOM"
```

It tests `sony448.pss` and `Intro_2.pss` independently and prints FFmpeg's probe/decode result.
