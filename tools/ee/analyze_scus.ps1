param(
    [Parameter(Mandatory=$true)]
    [string]$ElfPath,
    [string]$OutputDir = ""
)
$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $OutputDir) { $OutputDir = Join-Path $RepoRoot "generated\ee\scus_971_34" }
$BuildDir = Join-Path $RepoRoot "build\ee"
if (-not (Test-Path $ElfPath)) { throw "ELF not found: $ElfPath" }

cmake -S $PSScriptRoot -B $BuildDir
if ($LASTEXITCODE -ne 0) { throw "EE tools CMake configure failed." }
cmake --build $BuildDir --config Release --target socom_ee_analyze socom_ee_focus socom_ee_deep
if ($LASTEXITCODE -ne 0) { throw "EE tools build failed." }

function Find-Tool([string]$Name) {
    $Candidates = @(
        (Join-Path $BuildDir "Release\$Name.exe"),
        (Join-Path $BuildDir "$Name.exe"),
        (Join-Path $BuildDir "bin\Release\$Name.exe"),
        (Join-Path $BuildDir "bin\$Name.exe")
    )
    return $Candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}

$Analyzer = Find-Tool "socom_ee_analyze"
$Focus = Find-Tool "socom_ee_focus"
$Deep = Find-Tool "socom_ee_deep"
if (-not $Analyzer -or -not $Focus -or -not $Deep) { throw "Built EE tools could not be located under $BuildDir" }
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$ResolvedElf = (Resolve-Path $ElfPath).Path
& $Analyzer $ResolvedElf $OutputDir
if ($LASTEXITCODE -ne 0) { throw "SCUS analysis failed." }
& $Focus $ResolvedElf $OutputDir
if ($LASTEXITCODE -ne 0) { throw "SCUS focus recovery failed." }
& $Deep $ResolvedElf $OutputDir
if ($LASTEXITCODE -ne 0) { throw "SCUS deep recovery failed." }
Write-Host ""
Write-Host "Analysis + deep function recovery reports: $OutputDir"
Write-Host "  deep_functions.csv"
Write-Host "  mission_lifecycle.csv"
Write-Host "  mission_object_offsets.csv"
Write-Host "  ui_runtime_layout.csv"
