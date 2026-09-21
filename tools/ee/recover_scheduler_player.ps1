param(
    [Parameter(Mandatory=$true)][string]$ElfPath,
    [string]$OutDir = ""
)
$ErrorActionPreference = "Stop"
$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $OutDir) { $OutDir = Join-Path $Repo "generated\ee\scus_971_34\scheduler_player" }

$Build = Join-Path $Repo "build\ee_scheduler_player"
$Src = Join-Path $Repo "tools\ee\scheduler_player_native"
New-Item -ItemType Directory -Force -Path $Src,$Build,$OutDir | Out-Null

Copy-Item -Force (Join-Path $PSScriptRoot "recover_scheduler_player.cpp") (Join-Path $Src "recover_scheduler_player.cpp")
Copy-Item -Force (Join-Path $PSScriptRoot "CMakeLists.scheduler_player.txt") (Join-Path $Src "CMakeLists.txt")

Write-Host "Configuring scheduler/player recovery analyzer..."
cmake -S $Src -B $Build
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

Write-Host "Building scheduler/player recovery analyzer..."
cmake --build $Build --config Release
if ($LASTEXITCODE -ne 0) { throw "Native analyzer build failed." }

$exe = Get-ChildItem -Path $Build -Filter recover_scheduler_player.exe -Recurse | Select-Object -First 1
if (-not $exe) {
    $exe = Get-ChildItem -Path $Build -Filter recover_scheduler_player -Recurse | Select-Object -First 1
}
if (-not $exe) { throw "recover_scheduler_player executable not found after build." }

Write-Host "Running scheduler/player recovery..."
& $exe.FullName $ElfPath $OutDir
if ($LASTEXITCODE -ne 0) { throw "Scheduler/player recovery failed." }

Write-Host ""
Write-Host "Outputs: $OutDir" -ForegroundColor Green
