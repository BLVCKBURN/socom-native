param(
 [Parameter(Mandatory=$true)][string]$UiReader,
 [string]$Configuration="Release"
)
$ErrorActionPreference="Stop"
$Repo=(Resolve-Path(Join-Path $PSScriptRoot "..\..")).Path
$Build=Join-Path $Repo "build\retail_menu_v3"
cmake -S $PSScriptRoot -B $Build
if($LASTEXITCODE-ne 0){throw "CMake configure failed"}
cmake --build $Build --config $Configuration
if($LASTEXITCODE-ne 0){throw "Build failed"}
$exe=Get-ChildItem $Build -Recurse -Filter socom_retail_menu_v3.exe|Select-Object -First 1
if(-not$exe){throw "socom_retail_menu_v3.exe not found"}
& $exe.FullName $UiReader
