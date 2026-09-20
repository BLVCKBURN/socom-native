param(
    [Parameter(Mandatory=$true)]
    [string]$GameDataDir,

    [string]$BuildDir = "",
    [string]$GeneratedDir = "",
    [switch]$NoLaunch,
    [switch]$ForceRebuild
)

$ErrorActionPreference = "Stop"

$RuntimeRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $RuntimeRoot
if ([string]::IsNullOrWhiteSpace($BuildDir)) { $BuildDir = Join-Path $RepoRoot "build\runtime" }
if ([string]::IsNullOrWhiteSpace($GeneratedDir)) { $GeneratedDir = Join-Path $RepoRoot "generated\campaign" }
if (!(Test-Path $GameDataDir)) { throw "Game data directory does not exist: $GameDataDir" }
$GameDataDir = (Resolve-Path $GameDataDir).Path

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
    return (Resolve-Path $found).Path
}

function Find-AllGameFiles {
    param([Parameter(Mandatory=$true)][string]$Name)
    return @(Get-ChildItem -LiteralPath $GameDataDir -Recurse -File -Filter $Name -ErrorAction SilentlyContinue)
}


function Invoke-NativeProbe {
    param(
        [Parameter(Mandatory=$true)][string]$Exe,
        [Parameter(Mandatory=$true)][object[]]$Arguments,
        [Parameter(Mandatory=$true)][string]$Log
    )
    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $Exe @Arguments 2>&1 | Out-File -FilePath $Log -Append -Encoding utf8
        return $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $oldPreference
    }
}

function CandidateDistance {
    param([System.IO.FileInfo]$Candidate, [string]$AssetDir)
    $parent = Split-Path -Parent $Candidate.FullName
    if ($parent -eq $AssetDir) { return 0 }
    if ($Candidate.FullName.StartsWith($AssetDir, [System.StringComparison]::OrdinalIgnoreCase)) { return 1 }
    return 2
}

$MissionNames = @{
    1  = "Death at Sea"
    2  = "Ghost Town"
    3  = "Oil Platform Takedown"
    4  = "Golden Triangle Holiday"
    5  = "Temple at Hohn Kaen"
    6  = "City of the Forgotten"
    7  = "Mercenary Staging Area"
    8  = "POW Camp"
    9  = "Mountain Assault"
    10 = "Prison Break"
    11 = "Mouth of the Beast"
    12 = "Deathblow"
}

Write-Host "=== SOCOM Native - Full Single-Player Campaign Builder ===" -ForegroundColor Cyan
Write-Host "Game data root: $GameDataDir"
Write-Host "Campaign output: $GeneratedDir"
Write-Host ""

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $GeneratedDir | Out-Null

# Build the proven M8 + SEAL path first. Besides validating the character bank,
# this creates seal_A_des.spr and configures all runtime targets.
$M8Builder = Join-Path $RuntimeRoot "build_m8_from_game.ps1"
if (!(Test-Path $M8Builder)) { throw "Missing required builder: $M8Builder" }

Write-Host "Preparing shared SEAL player runtime using the proven M8 path..." -ForegroundColor Cyan
& $M8Builder -GameDataDir $GameDataDir -BuildDir $BuildDir -NoLaunch

$PlayerPack = Join-Path $RepoRoot "generated\m8-runtime\seal_A_des.spr"
$M8Pack = Join-Path $RepoRoot "generated\m8-runtime\m8_runtime.snr"
if (!(Test-Path $PlayerPack)) { throw "Player pack was not generated: $PlayerPack" }

Write-Host ""
Write-Host "Building campaign front end..." -ForegroundColor Cyan
$FrontendBuildLog = Join-Path $GeneratedDir "frontend_build.log"
& cmake --build $BuildDir --config Release --target socom_frontend --parallel 2 2>&1 | Tee-Object -FilePath $FrontendBuildLog
if ($LASTEXITCODE -ne 0) { throw "socom_frontend build failed. Full log: $FrontendBuildLog" }

$SceneExporter = Resolve-BuiltExe "socom_scene_gltf"
$CollisionExporter = Resolve-BuiltExe "socom_collision_gltf"
$NativePacker = Resolve-BuiltExe "build_m8_runtime_pack"
$PackChecker = Resolve-BuiltExe "runtime_pack_check"
$RuntimeExe = Resolve-BuiltExe "socom-native"
$FrontendExe = Resolve-BuiltExe "socom_frontend"

$ModelsCandidates = @(
    Get-ChildItem -LiteralPath $GameDataDir -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like "models*.zar" }
)
if ($ModelsCandidates.Count -eq 0) { throw "Could not find any models*.zar below: $GameDataDir" }

$ManifestPath = Join-Path $GeneratedDir "campaign_manifest.txt"
$ReportPath = Join-Path $GeneratedDir "campaign_build_report.csv"
$ManifestLines = @("# mission|title|runtime_pack")
$Report = @()

for ($mission = 1; $mission -le 12; $mission++) {
    $title = $MissionNames[$mission]
    $prefix = "m$mission"
    Write-Host ""
    Write-Host "------------------------------------------------------------" -ForegroundColor DarkGray
    Write-Host ("MISSION {0:D2}: {1}" -f $mission, $title) -ForegroundColor Cyan

    $MdlCandidates = Find-AllGameFiles "${prefix}_mdl.zed"
    $TxrCandidates = Find-AllGameFiles "${prefix}_txr.zed"
    $PalCandidates = Find-AllGameFiles "${prefix}_pal.zed"

    if ($MdlCandidates.Count -eq 0 -or $TxrCandidates.Count -eq 0 -or $PalCandidates.Count -eq 0) {
        Write-Host "  Missing one or more mission asset files; skipping." -ForegroundColor Yellow
        $Report += [PSCustomObject]@{Mission=$mission;Title=$title;Status="MISSING_ASSETS";Mdl="";Txr="";Pal="";Models="";Pack=""}
        continue
    }

    $MissionDir = Join-Path $GeneratedDir ("m{0:D2}" -f $mission)
    $Intermediate = Join-Path $MissionDir "intermediate"
    $SceneDir = Join-Path $Intermediate "scene"
    $CollisionDir = Join-Path $Intermediate "collision"
    New-Item -ItemType Directory -Force -Path $MissionDir | Out-Null

    $WorldPack = Join-Path $MissionDir "${prefix}_runtime.snr"
    $WorldReport = Join-Path $MissionDir "${prefix}_runtime_report.json"
    $ProbeLog = Join-Path $MissionDir "world_probe.log"

    if ((Test-Path $WorldPack) -and -not $ForceRebuild) {
        Write-Host "  Existing runtime pack found; validating..." -ForegroundColor DarkCyan
        & $PackChecker $WorldPack
        if ($LASTEXITCODE -eq 0) {
            $ManifestLines += "$mission|$title|$WorldPack"
            $Report += [PSCustomObject]@{Mission=$mission;Title=$title;Status="CACHED";Mdl="";Txr="";Pal="";Models="";Pack=$WorldPack}
            continue
        }
    }

    if ($mission -eq 8 -and (Test-Path $M8Pack) -and -not $ForceRebuild) {
        Copy-Item -LiteralPath $M8Pack -Destination $WorldPack -Force
        & $PackChecker $WorldPack
        if ($LASTEXITCODE -eq 0) {
            Write-Host "  Reused validated M8 pack." -ForegroundColor Green
            $ManifestLines += "$mission|$title|$WorldPack"
            $Report += [PSCustomObject]@{Mission=$mission;Title=$title;Status="BUILT";Mdl="m8_mdl.zed";Txr="m8_txr.zed";Pal="m8_pal.zed";Models="cached M8";Pack=$WorldPack}
            continue
        }
    }

    if (Test-Path $ProbeLog) { Remove-Item $ProbeLog -Force }
    $Matched = $false
    $MatchedMdl = $null
    $MatchedTxr = $null
    $MatchedPal = $null
    $MatchedModels = $null

    # Prefer mdl/txr/pal triples from the same directory. If extraction created
    # duplicate banks, fall back to cross-directory combinations.
    $AssetSets = @()
    foreach ($mdl in $MdlCandidates) {
        foreach ($txr in $TxrCandidates) {
            foreach ($pal in $PalCandidates) {
                $md = Split-Path -Parent $mdl.FullName
                $td = Split-Path -Parent $txr.FullName
                $pd = Split-Path -Parent $pal.FullName
                $score = 3
                if ($md -eq $td -and $md -eq $pd) { $score = 0 }
                elseif ($md -eq $td -or $md -eq $pd) { $score = 1 }
                elseif ($td -eq $pd) { $score = 2 }
                $AssetSets += [PSCustomObject]@{Mdl=$mdl;Txr=$txr;Pal=$pal;Score=$score}
            }
        }
    }
    $AssetSets = @($AssetSets | Sort-Object Score, @{Expression={$_.Mdl.FullName}}, @{Expression={$_.Txr.FullName}}, @{Expression={$_.Pal.FullName}})

    :AssetProbe foreach ($set in $AssetSets) {
        $assetDir = Split-Path -Parent $set.Mdl.FullName
        $OrderedModels = @($ModelsCandidates | Sort-Object @{Expression={ CandidateDistance $_ $assetDir }}, FullName)

        if (Test-Path $CollisionDir) { Remove-Item $CollisionDir -Recurse -Force }
        New-Item -ItemType Directory -Force -Path $CollisionDir | Out-Null
        "ASSETSET MDL=$($set.Mdl.FullName) TXR=$($set.Txr.FullName) PAL=$($set.Pal.FullName)" | Out-File $ProbeLog -Append -Encoding utf8
        $collisionResult = Invoke-NativeProbe -Exe $CollisionExporter -Arguments @($set.Mdl.FullName, "worldmodel", $CollisionDir) -Log $ProbeLog
        if ($collisionResult -ne 0) { continue }
        $CollisionGltf = Join-Path $CollisionDir "worldmodel_collision.gltf"
        $CollisionBin = Join-Path $CollisionDir "worldmodel_collision.bin"
        if (!(Test-Path $CollisionGltf) -or !(Test-Path $CollisionBin)) { continue }

        foreach ($model in $OrderedModels) {
            if (Test-Path $SceneDir) { Remove-Item $SceneDir -Recurse -Force }
            New-Item -ItemType Directory -Force -Path $SceneDir | Out-Null
            "MODEL=$($model.FullName)" | Out-File $ProbeLog -Append -Encoding utf8
            $sceneResult = Invoke-NativeProbe -Exe $SceneExporter -Arguments @($model.FullName, $set.Mdl.FullName, $set.Txr.FullName, $set.Pal.FullName, $SceneDir) -Log $ProbeLog
            if ($sceneResult -ne 0) { continue }

            # The current generic world exporter retains its historical M8 output
            # basename even when fed another mission. Each mission has its own
            # intermediate directory so this is harmless.
            $SceneGltf = Join-Path $SceneDir "m8_scene.gltf"
            $SceneBin = Join-Path $SceneDir "m8_scene.bin"
            if (!(Test-Path $SceneGltf) -or !(Test-Path $SceneBin)) { continue }

            Write-Host "  Matched asset set:" -ForegroundColor Green
            Write-Host "    MDL:    $($set.Mdl.FullName)"
            Write-Host "    TXR:    $($set.Txr.FullName)"
            Write-Host "    PAL:    $($set.Pal.FullName)"
            Write-Host "    MODELS: $($model.FullName)"

            $packResult = Invoke-NativeProbe -Exe $NativePacker -Arguments @("--scene", $SceneGltf, "--collision", $CollisionGltf, "--out", $WorldPack, "--report", $WorldReport) -Log $ProbeLog
            if ($packResult -ne 0 -or !(Test-Path $WorldPack)) { continue }
            $checkResult = Invoke-NativeProbe -Exe $PackChecker -Arguments @($WorldPack) -Log $ProbeLog
            if ($checkResult -ne 0) { continue }

            $Matched = $true
            $MatchedMdl = $set.Mdl.FullName
            $MatchedTxr = $set.Txr.FullName
            $MatchedPal = $set.Pal.FullName
            $MatchedModels = $model.FullName
            break AssetProbe
        }
    }

    if ($Matched) {
        Write-Host "  Mission runtime pack validated: $WorldPack" -ForegroundColor Green
        $ManifestLines += "$mission|$title|$WorldPack"
        $Report += [PSCustomObject]@{Mission=$mission;Title=$title;Status="BUILT";Mdl=$MatchedMdl;Txr=$MatchedTxr;Pal=$MatchedPal;Models=$MatchedModels;Pack=$WorldPack}
    } else {
        Write-Host "  Could not produce a validated world pack. See $ProbeLog" -ForegroundColor Yellow
        $Report += [PSCustomObject]@{Mission=$mission;Title=$title;Status="FAILED";Mdl="";Txr="";Pal="";Models="";Pack=""}
    }
}

$ManifestLines | Set-Content -LiteralPath $ManifestPath -Encoding ascii
$Report | Export-Csv -LiteralPath $ReportPath -NoTypeInformation -Encoding utf8

# Inventory likely front-end and mission-logic assets for the next reverse-engineering phases.
$FrontendInventory = Join-Path $GeneratedDir "frontend_asset_candidates.csv"
$GameplayInventory = Join-Path $GeneratedDir "gameplay_asset_candidates.csv"
$AllGameFiles = @(Get-ChildItem -LiteralPath $GameDataDir -Recurse -File -ErrorAction SilentlyContinue)
$AllGameFiles |
    Where-Object { $_.Name -match '(?i)(menu|frontend|front|hud|ui|twod|brief|armory|font|screen|movie|mpeg|intro|logo|save|profile|option)' } |
    Select-Object Name, DirectoryName, Length |
    Export-Csv -LiteralPath $FrontendInventory -NoTypeInformation -Encoding utf8
$AllGameFiles |
    Where-Object { $_.Name -match '(?i)(reader|rdr|mzanim|zanim|aimap|nav|graph|action|valve|vehicle|chartype|weapon|sound|snd|mission)' } |
    Select-Object Name, DirectoryName, Length |
    Export-Csv -LiteralPath $GameplayInventory -NoTypeInformation -Encoding utf8

$BuiltCount = @($Report | Where-Object { $_.Status -eq "BUILT" -or $_.Status -eq "CACHED" }).Count
Write-Host ""
Write-Host "============================================================" -ForegroundColor Green
Write-Host "SOCOM single-player campaign build complete" -ForegroundColor Green
Write-Host "Validated missions: $BuiltCount / 12"
Write-Host "Manifest: $ManifestPath"
Write-Host "Report:   $ReportPath"
Write-Host "Front-end asset inventory: $FrontendInventory"
Write-Host "Gameplay asset inventory:  $GameplayInventory"
Write-Host "Frontend: $FrontendExe"
Write-Host "Runtime:  $RuntimeExe"
Write-Host "Player:   $PlayerPack"
Write-Host "============================================================" -ForegroundColor Green

if ($BuiltCount -eq 0) { throw "No single-player mission packs were successfully built." }

if (-not $NoLaunch) {
    Write-Host ""
    Write-Host "Launching SOCOM Native single-player front end..." -ForegroundColor Cyan
    & $FrontendExe --manifest $ManifestPath --runtime $RuntimeExe --player $PlayerPack
}
