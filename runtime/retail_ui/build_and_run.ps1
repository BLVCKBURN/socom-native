param(
 [Parameter(Mandatory=$true)][string]$GameRoot,
 [string]$UiReader="",
 [string]$StartScreen="dlgMenu.rdr",
 [string]$Configuration="Release"
)
$ErrorActionPreference="Stop"
$Repo=(Resolve-Path(Join-Path $PSScriptRoot "..\..")).Path
$Build=Join-Path $Repo "build\retail_ui_v7"

cmake -S $PSScriptRoot -B $Build
if($LASTEXITCODE-ne 0){throw "CMake configure failed."}

cmake --build $Build --config $Configuration
if($LASTEXITCODE-ne 0){throw "Retail UI v11 build failed."}

$exe=Get-ChildItem $Build -Recurse -Filter socom_retail_ui.exe|Select-Object -First 1
if(-not$exe){throw "socom_retail_ui.exe not found."}

$textureTool=Get-ChildItem $Build -Recurse -Filter socom_texture_tool.exe|Select-Object -First 1
if($textureTool){
  & (Join-Path $PSScriptRoot "prepare_retail_menu_assets.ps1") `
      -GameRoot $GameRoot `
      -TextureTool $textureTool.FullName
}else{
  Write-Warning "socom_texture_tool.exe was not found; retail UI textures will remain unresolved."
}

if(-not(Get-Command ffmpeg.exe -ErrorAction SilentlyContinue)){
  Write-Warning "ffmpeg.exe is not on PATH. The menu will run, but menuloop.pss will not render."
}

$args=@($GameRoot)
if($UiReader){$args+=$UiReader}
elseif($StartScreen-ne"dlgMenu.rdr"){
  throw "When specifying StartScreen, also pass UiReader."
}
if($UiReader){$args+=$StartScreen}

& $exe.FullName @args
