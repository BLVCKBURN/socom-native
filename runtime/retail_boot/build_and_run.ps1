param(
    [Parameter(Mandatory=$true)][string]$GameRoot,
    [string]$UiReader = "",
    [string]$Configuration = "Release",
    [switch]$BuildOnly
)
$ErrorActionPreference="Stop"
$Repo=(Resolve-Path(Join-Path $PSScriptRoot "..\..")).Path
$Build=Join-Path $Repo "build\retail_boot"
New-Item -ItemType Directory -Force -Path $Build|Out-Null

Write-Host "Configuring retail boot executable..."
cmake -S $PSScriptRoot -B $Build
if($LASTEXITCODE-ne 0){throw "CMake configure failed."}

Write-Host "Building socom_retail_boot.exe..."
cmake --build $Build --config $Configuration
if($LASTEXITCODE-ne 0){throw "Retail boot build failed."}

$exe=Get-ChildItem $Build -Recurse -Filter socom_retail_boot.exe|Select-Object -First 1
if(-not $exe){throw "socom_retail_boot.exe not found."}

Write-Host ""
Write-Host "Built: $($exe.FullName)" -ForegroundColor Green

if(-not $BuildOnly){
    if(-not(Get-Command ffplay.exe -ErrorAction SilentlyContinue)){
        Write-Warning "ffplay.exe is not currently on PATH. The EXE will stop before movie playback until FFmpeg/ffplay is available."
    }
    $args=@($GameRoot)
    if($UiReader){$args+=$UiReader}
    & $exe.FullName @args
}
