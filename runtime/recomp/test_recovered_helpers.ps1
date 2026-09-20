$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$SourceDir = Join-Path $PSScriptRoot "recovered"
$BuildDir = Join-Path $RepoRoot "build\recomp-recovery"
cmake -S $SourceDir -B $BuildDir
if ($LASTEXITCODE -ne 0) { throw "Recovered recomp helper configure failed." }
cmake --build $BuildDir --config Release --target socom_recomp_recovery_smoke
if ($LASTEXITCODE -ne 0) { throw "Recovered recomp helper build failed." }
$Exe = @(
    (Join-Path $BuildDir "Release\socom_recomp_recovery_smoke.exe"),
    (Join-Path $BuildDir "socom_recomp_recovery_smoke.exe"),
    (Join-Path $BuildDir "socom_recomp_recovery_smoke")
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $Exe) { throw "Smoke-test executable was not found under $BuildDir" }
& $Exe
if ($LASTEXITCODE -ne 0) { throw "Recovered recomp helper smoke test failed." }
Write-Host "Recovered native helper smoke test passed."
