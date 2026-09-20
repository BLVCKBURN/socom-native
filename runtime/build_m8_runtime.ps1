param(
    [Parameter(Mandatory=$true)]
    [string]$SceneGltf,

    [Parameter(Mandatory=$true)]
    [string]$CollisionGltf,

    [string]$BuildDir = "",
    [string]$OutputDir = "",
    [switch]$NoLaunch
)

$ErrorActionPreference = "Stop"

$RuntimeRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $RuntimeRoot

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $RepoRoot "build\runtime"
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $RepoRoot "generated\m8-runtime"
}

if (!(Test-Path $SceneGltf)) { throw "Scene glTF not found: $SceneGltf" }
if (!(Test-Path $CollisionGltf)) { throw "Collision glTF not found: $CollisionGltf" }
$SceneGltf = (Resolve-Path $SceneGltf).Path
$CollisionGltf = (Resolve-Path $CollisionGltf).Path

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$Pack = Join-Path $OutputDir "m8_runtime.snr"
$Report = Join-Path $OutputDir "m8_runtime_report.json"
$BuildLog = Join-Path $OutputDir "native_build.log"
$PackLog = Join-Path $OutputDir "runtime_pack_build.log"

Write-Host "=== SOCOM Native - M8 Runtime Build ===" -ForegroundColor Cyan
Write-Host "Scene:     $SceneGltf"
Write-Host "Collision: $CollisionGltf"
Write-Host "Pack:      $Pack"
Write-Host ""

if (Test-Path $BuildLog) { Remove-Item $BuildLog -Force }
Write-Host "Configuring native runtime/tools..." -ForegroundColor Cyan
& cmake -S $RuntimeRoot -B $BuildDir -A x64 2>&1 | Tee-Object -FilePath $BuildLog
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed. Full log: $BuildLog" }

Write-Host ""
Write-Host "Building native packer, validator, and runtime..." -ForegroundColor Cyan
& cmake --build $BuildDir --config Release --target build_m8_runtime_pack runtime_pack_check socom-native --parallel 2 2>&1 | Tee-Object -FilePath $BuildLog -Append
if ($LASTEXITCODE -ne 0) {
    Get-Content $BuildLog -Tail 80
    throw "Runtime build failed. Full log: $BuildLog"
}

function Resolve-BuiltExe {
    param([Parameter(Mandatory=$true)][string]$Name)
    $candidates = @(
        (Join-Path $BuildDir "bin\Release\$Name.exe"),
        (Join-Path $BuildDir "bin\$Name.exe"),
        (Join-Path $BuildDir "Release\$Name.exe"),
        (Join-Path $BuildDir "$Name.exe")
    )
    $found = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $found) { throw "Build completed but $Name.exe was not found." }
    return $found
}

$Packer = Resolve-BuiltExe "build_m8_runtime_pack"
$Checker = Resolve-BuiltExe "runtime_pack_check"
$Exe = Resolve-BuiltExe "socom-native"

if (Test-Path $Pack) { Remove-Item $Pack -Force }
if (Test-Path $Report) { Remove-Item $Report -Force }
if (Test-Path $PackLog) { Remove-Item $PackLog -Force }

Write-Host ""
Write-Host "Generating M8 runtime pack natively (no Python required)..." -ForegroundColor Cyan
& $Packer --scene $SceneGltf --collision $CollisionGltf --out $Pack --report $Report 2>&1 | Tee-Object -FilePath $PackLog
if ($LASTEXITCODE -ne 0 -or !(Test-Path $Pack)) {
    if (Test-Path $PackLog) { Get-Content $PackLog -Tail 100 }
    throw "Native runtime pack generation failed. Full log: $PackLog"
}

Write-Host ""
Write-Host "Validating runtime pack..." -ForegroundColor Cyan
& $Checker $Pack
if ($LASTEXITCODE -ne 0) { throw "Runtime pack validation failed." }

Write-Host ""
Write-Host "Build complete." -ForegroundColor Green
Write-Host "Executable: $Exe"
Write-Host "Runtime pack: $Pack"

if (-not $NoLaunch) {
    Write-Host ""
    Write-Host "Launching M8..." -ForegroundColor Cyan
    & $Exe --pack $Pack
}
