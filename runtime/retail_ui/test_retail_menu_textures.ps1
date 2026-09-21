param(
    [Parameter(Mandatory=$true)][string]$GameRoot,
    [string]$Configuration="Release"
)
$ErrorActionPreference="Stop"

$Repo=(Resolve-Path(Join-Path $PSScriptRoot "..\..")).Path
$Build=Join-Path $Repo "build\retail_ui_v7"

cmake -S $PSScriptRoot -B $Build
if($LASTEXITCODE-ne 0){throw "CMake configure failed."}

cmake --build $Build --config $Configuration --target socom_texture_tool
if($LASTEXITCODE-ne 0){throw "Texture tool build failed."}

$tool=Get-ChildItem $Build -Recurse -Filter socom_texture_tool.exe | Select-Object -First 1
if(-not $tool){throw "socom_texture_tool.exe not found."}

& (Join-Path $PSScriptRoot "prepare_retail_menu_assets.ps1") `
    -GameRoot $GameRoot `
    -TextureTool $tool.FullName
