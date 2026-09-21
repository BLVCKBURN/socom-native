[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ElfPath,
    [string]$OutDir = "",
    [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
if (Test-Path variable:PSNativeCommandUseErrorActionPreference) {
    $PSNativeCommandUseErrorActionPreference = $false
}

$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$NativeDir = Join-Path $PSScriptRoot "native"
if (-not $OutDir) {
    $OutDir = Join-Path $Repo "generated\ee\scus_971_34\retail_chain_corrected"
}
if (-not $BuildDir) {
    $BuildDir = Join-Path $Repo "build\ee-retail-chain"
}

if (-not (Test-Path -LiteralPath $ElfPath -PathType Leaf)) {
    throw "ELF not found: $ElfPath"
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "CMake is required. It should already be available from the Visual Studio/C++ setup used by the SOCOM project."
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

Write-Host "Configuring native retail-chain analyzer..." -ForegroundColor Cyan
& cmake -S $NativeDir -B $BuildDir
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed for native retail-chain analyzer." }

Write-Host "Building native retail-chain analyzer..." -ForegroundColor Cyan
& cmake --build $BuildDir --config Release
if ($LASTEXITCODE -ne 0) { throw "Native retail-chain analyzer build failed." }

$candidates = @(
    (Join-Path $BuildDir "Release\socom_retail_chain_analyzer.exe"),
    (Join-Path $BuildDir "socom_retail_chain_analyzer.exe"),
    (Join-Path $BuildDir "socom_retail_chain_analyzer")
)
$Analyzer = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $Analyzer) {
    $Analyzer = Get-ChildItem -LiteralPath $BuildDir -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -in @("socom_retail_chain_analyzer.exe", "socom_retail_chain_analyzer") } |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $Analyzer) { throw "Native analyzer executable was not produced under $BuildDir" }

Write-Host "Running native retail-chain analyzer..." -ForegroundColor Cyan
& $Analyzer $ElfPath $OutDir
if ($LASTEXITCODE -ne 0) { throw "Corrected retail-chain analysis failed." }

Write-Host ""
Write-Host "Corrected retail-chain analysis complete:" -ForegroundColor Green
Write-Host $OutDir
Write-Host ""
Write-Host "Python is not required." -ForegroundColor Green
