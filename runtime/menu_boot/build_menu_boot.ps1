param(
    [string]$Configuration = "Release",
    [string]$ReaderArchive = ""
)

$ErrorActionPreference = "Stop"
$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Src = $PSScriptRoot
$Build = Join-Path $Repo "build\menu_boot"

New-Item -ItemType Directory -Force -Path $Build | Out-Null

Write-Host "Configuring SOCOM retail menu bootstrap..."
cmake -S $Src -B $Build
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

Write-Host "Building socom_menu_boot.exe..."
cmake --build $Build --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "Menu bootstrap build failed." }

$exe = Get-ChildItem -Path $Build -Filter socom_menu_boot.exe -Recurse |
    Select-Object -First 1
if (-not $exe) { throw "socom_menu_boot.exe not found after build." }

Write-Host ""
Write-Host "Built:" -ForegroundColor Green
Write-Host $exe.FullName

if ($ReaderArchive) {
    if (-not (Test-Path $ReaderArchive)) {
        throw "Reader archive not found: $ReaderArchive"
    }
    Write-Host ""
    Write-Host "Launching with retail readerc.zar..."
    & $exe.FullName $ReaderArchive
}
