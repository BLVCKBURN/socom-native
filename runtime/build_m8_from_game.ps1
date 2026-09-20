param(
    [Parameter(Mandatory=$true)]
    [string]$GameDataDir,

    [string]$BuildDir = "",
    [string]$GeneratedDir = "",
    [switch]$NoLaunch
)

$ErrorActionPreference = "Stop"

$RuntimeRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $RuntimeRoot

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $RepoRoot "build\runtime"
}
if ([string]::IsNullOrWhiteSpace($GeneratedDir)) {
    $GeneratedDir = Join-Path $RepoRoot "generated\m8-runtime"
}

if (!(Test-Path $GameDataDir)) {
    throw "Game data directory does not exist: $GameDataDir"
}
$GameDataDir = (Resolve-Path $GameDataDir).Path

function Find-GameFile {
    param([Parameter(Mandatory=$true)][string]$Name)
    $matches = @(Get-ChildItem -LiteralPath $GameDataDir -Recurse -File -Filter $Name -ErrorAction SilentlyContinue)
    if ($matches.Count -eq 0) {
        throw "Could not find '$Name' anywhere below: $GameDataDir"
    }
    if ($matches.Count -gt 1) {
        Write-Host "Found $($matches.Count) copies of $Name; using: $($matches[0].FullName)" -ForegroundColor Yellow
    }
    return $matches[0].FullName
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

Write-Host "=== SOCOM Native - M8 Single Player + SEAL Runtime ===" -ForegroundColor Cyan
Write-Host "Game data root: $GameDataDir"
Write-Host ""

Write-Host "Locating M8 world files..." -ForegroundColor Cyan
$M8Mdl = Find-GameFile "m8_mdl.zed"
$M8Txr = Find-GameFile "m8_txr.zed"
$M8Pal = Find-GameFile "m8_pal.zed"

Write-Host "Locating character/animation file candidates..." -ForegroundColor Cyan
$ClibMdlCandidates = @(Get-ChildItem -LiteralPath $GameDataDir -Recurse -File -Filter "clib_mdl.zed" -ErrorAction SilentlyContinue)
$ClibTxrCandidates = @(Get-ChildItem -LiteralPath $GameDataDir -Recurse -File -Filter "clib_txr.zed" -ErrorAction SilentlyContinue)
$ClibPalCandidates = @(Get-ChildItem -LiteralPath $GameDataDir -Recurse -File -Filter "clib_pal.zed" -ErrorAction SilentlyContinue)
$MotionCandidates = @(Get-ChildItem -LiteralPath $GameDataDir -Recurse -File -Filter "motion.zar" -ErrorAction SilentlyContinue)
foreach ($pair in @(
    @{ Name = "clib_mdl.zed"; Items = $ClibMdlCandidates },
    @{ Name = "clib_txr.zed"; Items = $ClibTxrCandidates },
    @{ Name = "clib_pal.zed"; Items = $ClibPalCandidates },
    @{ Name = "motion.zar"; Items = $MotionCandidates }
)) {
    if ($pair.Items.Count -eq 0) { throw "Could not find '$($pair.Name)' anywhere below: $GameDataDir" }
}

$ModelsCandidates = @(
    Get-ChildItem -LiteralPath $GameDataDir -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like "models*.zar" }
)
if ($ModelsCandidates.Count -eq 0) {
    throw "Could not find any models*.zar below: $GameDataDir"
}

Write-Host "  m8_mdl.zed:  $M8Mdl"
Write-Host "  m8_txr.zed:  $M8Txr"
Write-Host "  m8_pal.zed:  $M8Pal"
Write-Host "  clib_mdl candidates: $($ClibMdlCandidates.Count)"
Write-Host "  clib_txr candidates: $($ClibTxrCandidates.Count)"
Write-Host "  clib_pal candidates: $($ClibPalCandidates.Count)"
Write-Host "  motion.zar candidates: $($MotionCandidates.Count)"
Write-Host "  models candidates: $($ModelsCandidates.Count)"
Write-Host ""

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $GeneratedDir | Out-Null

$IntermediateDir = Join-Path $GeneratedDir "intermediate"
$SceneDir = Join-Path $IntermediateDir "scene"
$CollisionDir = Join-Path $IntermediateDir "collision"
$PlayerRoot = Join-Path $IntermediateDir "player"
$PlayerTextures = Join-Path $PlayerRoot "textures"
$StandDir = Join-Path $PlayerRoot "stand"
$WalkDir = Join-Path $PlayerRoot "walk"
$RunDir = Join-Path $PlayerRoot "run"
New-Item -ItemType Directory -Force -Path $IntermediateDir | Out-Null

$BuildLog = Join-Path $GeneratedDir "native_build.log"
if (Test-Path $BuildLog) { Remove-Item $BuildLog -Force }

Write-Host "Configuring native runtime/tools..." -ForegroundColor Cyan
& cmake -S $RuntimeRoot -B $BuildDir -A x64 2>&1 | Tee-Object -FilePath $BuildLog
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed. Full log: $BuildLog" }

Write-Host ""
Write-Host "Building world, character, animation, packer, validator, and runtime targets..." -ForegroundColor Cyan
& cmake --build $BuildDir --config Release --target `
    socom_scene_gltf `
    socom_collision_gltf `
    build_m8_runtime_pack `
    runtime_pack_check `
    socom_texture_tool `
    socom_character_gltf `
    build_player_runtime_pack `
    player_pack_check `
    socom-native `
    --parallel 2 2>&1 | Tee-Object -FilePath $BuildLog -Append
if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "Last 100 build-log lines:" -ForegroundColor Yellow
    Get-Content $BuildLog -Tail 100
    throw "Native build failed. Full log: $BuildLog"
}

$SceneExporter = Resolve-BuiltExe "socom_scene_gltf"
$CollisionExporter = Resolve-BuiltExe "socom_collision_gltf"
$NativePacker = Resolve-BuiltExe "build_m8_runtime_pack"
$PackChecker = Resolve-BuiltExe "runtime_pack_check"
$TextureTool = Resolve-BuiltExe "socom_texture_tool"
$CharacterExporter = Resolve-BuiltExe "socom_character_gltf"
$PlayerPacker = Resolve-BuiltExe "build_player_runtime_pack"
$PlayerChecker = Resolve-BuiltExe "player_pack_check"
$RuntimeExe = Resolve-BuiltExe "socom-native"

# ---------------------------------------------------------------------------
# World + collision
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "Generating M8 collision directly from m8_mdl.zed..." -ForegroundColor Cyan
if (Test-Path $CollisionDir) { Remove-Item $CollisionDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $CollisionDir | Out-Null
& $CollisionExporter $M8Mdl "worldmodel" $CollisionDir
if ($LASTEXITCODE -ne 0) { throw "M8 collision export failed." }
$CollisionGltf = Join-Path $CollisionDir "worldmodel_collision.gltf"
$CollisionBin = Join-Path $CollisionDir "worldmodel_collision.bin"
if (!(Test-Path $CollisionGltf) -or !(Test-Path $CollisionBin)) {
    throw "Collision exporter did not produce the expected glTF/bin pair."
}

Write-Host ""
Write-Host "Finding the models archive that matches the M8 world..." -ForegroundColor Cyan
$SceneGltf = $null
$SelectedWorldModels = $null
foreach ($candidate in $ModelsCandidates) {
    Write-Host "  Trying world candidate: $($candidate.FullName)"
    if (Test-Path $SceneDir) { Remove-Item $SceneDir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $SceneDir | Out-Null
    & $SceneExporter $candidate.FullName $M8Mdl $M8Txr $M8Pal $SceneDir
    $result = $LASTEXITCODE
    $candidateScene = Join-Path $SceneDir "m8_scene.gltf"
    $candidateBin = Join-Path $SceneDir "m8_scene.bin"
    if ($result -eq 0 -and (Test-Path $candidateScene) -and (Test-Path $candidateBin)) {
        $SceneGltf = $candidateScene
        $SelectedWorldModels = $candidate.FullName
        break
    }
}
if (-not $SceneGltf) {
    throw "None of the discovered models archives could generate the M8 world."
}
Write-Host "Matched M8 world archive: $SelectedWorldModels" -ForegroundColor Green

$WorldPack = Join-Path $GeneratedDir "m8_runtime.snr"
$WorldReport = Join-Path $GeneratedDir "m8_runtime_report.json"
$WorldPackLog = Join-Path $GeneratedDir "runtime_pack_build.log"
foreach ($x in @($WorldPack,$WorldReport,$WorldPackLog)) { if (Test-Path $x) { Remove-Item $x -Force } }

Write-Host ""
Write-Host "Generating textured M8 runtime pack..." -ForegroundColor Cyan
& $NativePacker --scene $SceneGltf --collision $CollisionGltf --out $WorldPack --report $WorldReport 2>&1 | Tee-Object -FilePath $WorldPackLog
if ($LASTEXITCODE -ne 0 -or !(Test-Path $WorldPack)) {
    if (Test-Path $WorldPackLog) { Get-Content $WorldPackLog -Tail 100 }
    throw "M8 runtime pack generation failed. Full log: $WorldPackLog"
}
& $PackChecker $WorldPack
if ($LASTEXITCODE -ne 0) { throw "Generated M8 runtime pack failed validation." }

# ---------------------------------------------------------------------------
# Player textures + animated SEAL
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "Selecting the character texture bank that contains the SEAL textures..." -ForegroundColor Cyan

$RequiredSealTextureBases = @(
    "seal_desert_shirt",
    "seal_desert_pants",
    "seal_desert_sleave",
    "seal_desert_boot",
    "seal_glove",
    "seal01_des_facemap",
    "mouth",
    "seal_scarf",
    "teeth",
    "gear_buckle",
    "gear_buckle_black",
    "gear_loop"
)

function Test-SealTextureSet {
    param([Parameter(Mandatory=$true)][string]$Directory)
    foreach ($base in $RequiredSealTextureBases) {
        if (!(Test-Path (Join-Path $Directory ($base + ".png")))) { return $false }
        if (!(Test-Path (Join-Path $Directory ($base + ".srtx")))) { return $false }
    }
    return $true
}

$CharacterTextureProbeLog = Join-Path $GeneratedDir "character_texture_probe.log"
if (Test-Path $CharacterTextureProbeLog) { Remove-Item $CharacterTextureProbeLog -Force }
$SelectedClibTxr = $null
$SelectedClibPal = $null

# Prefer TXR/PAL pairs that live in the same directory, then try every remaining combination.
$TexturePairs = @()
foreach ($txr in $ClibTxrCandidates) {
    foreach ($pal in $ClibPalCandidates) {
        $sameDir = ((Split-Path -Parent $txr.FullName) -eq (Split-Path -Parent $pal.FullName))
        $TexturePairs += [PSCustomObject]@{ Txr = $txr; Pal = $pal; SameDir = $sameDir }
    }
}
$TexturePairs = @($TexturePairs | Sort-Object @{Expression={$_.SameDir};Descending=$true}, @{Expression={$_.Txr.FullName}}, @{Expression={$_.Pal.FullName}})

foreach ($pair in $TexturePairs) {
    Write-Host "  Trying texture pair:" -ForegroundColor DarkCyan
    Write-Host "    TXR: $($pair.Txr.FullName)"
    Write-Host "    PAL: $($pair.Pal.FullName)"

    if (Test-Path $PlayerTextures) { Remove-Item $PlayerTextures -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $PlayerTextures | Out-Null

    "TRY TXR=$($pair.Txr.FullName) PAL=$($pair.Pal.FullName)" | Out-File -FilePath $CharacterTextureProbeLog -Append -Encoding utf8
    & $TextureTool $pair.Txr.FullName $pair.Pal.FullName --extract-all $PlayerTextures 2>&1 | Tee-Object -FilePath $CharacterTextureProbeLog -Append | Out-Host
    $textureResult = $LASTEXITCODE

    if ($textureResult -eq 0 -and (Test-SealTextureSet $PlayerTextures)) {
        $SelectedClibTxr = $pair.Txr.FullName
        $SelectedClibPal = $pair.Pal.FullName
        break
    }
}

if (-not $SelectedClibTxr -or -not $SelectedClibPal) {
    Write-Host ""
    Write-Host "SEAL texture bank probe failed. Texture-like files produced by the last candidate:" -ForegroundColor Yellow
    if (Test-Path $PlayerTextures) {
        Get-ChildItem -LiteralPath $PlayerTextures -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match 'seal|gear|mouth|teeth' } |
            Select-Object -First 80 -ExpandProperty Name | ForEach-Object { Write-Host "  $_" }
    }
    throw "Could not find a matching clib_txr.zed/clib_pal.zed pair containing the 12 required MESH_seal_A_des textures. Full probe log: $CharacterTextureProbeLog"
}

$ClibTxr = $SelectedClibTxr
$ClibPal = $SelectedClibPal
Write-Host "Matched SEAL character texture bank:" -ForegroundColor Green
Write-Host "  TXR: $ClibTxr"
Write-Host "  PAL: $ClibPal"

Write-Host ""
Write-Host "Finding MESH_seal_A_des plus a compatible clib_mdl.zed and motion.zar..." -ForegroundColor Cyan
$SelectedCharacterModels = $null
$SelectedClibMdl = $null
$SelectedMotionZar = $null
$StandGltf = $null

# Prefer clib/motion files located near the successful texture bank, but probe all copies.
$CharacterAssetRoot = Split-Path -Parent $ClibTxr
$OrderedClibMdls = @(
    $ClibMdlCandidates | Sort-Object @{Expression={ if ((Split-Path -Parent $_.FullName) -eq $CharacterAssetRoot) {0} else {1} }}, FullName
)
$OrderedMotions = @(
    $MotionCandidates | Sort-Object @{Expression={ if ((Split-Path -Parent $_.FullName) -eq $CharacterAssetRoot) {0} else {1} }}, FullName
)

:CharacterProbe foreach ($clibCandidate in $OrderedClibMdls) {
    foreach ($motionCandidate in $OrderedMotions) {
        foreach ($candidate in $ModelsCandidates) {
            Write-Host "  Trying character inputs:"
            Write-Host "    models: $($candidate.FullName)"
            Write-Host "    clib:   $($clibCandidate.FullName)"
            Write-Host "    motion: $($motionCandidate.FullName)"

            if (Test-Path $StandDir) { Remove-Item $StandDir -Recurse -Force }
            New-Item -ItemType Directory -Force -Path $StandDir | Out-Null

            & $CharacterExporter `
                $candidate.FullName `
                $clibCandidate.FullName `
                $PlayerTextures `
                "MESH_seal_A_des" `
                $StandDir `
                $motionCandidate.FullName `
                "seal_stand"
            $result = $LASTEXITCODE
            $candidateGltf = Join-Path $StandDir "seal_A_des\seal_A_des.gltf"
            $candidateBin = Join-Path $StandDir "seal_A_des\seal_A_des.bin"
            if ($result -eq 0 -and (Test-Path $candidateGltf) -and (Test-Path $candidateBin)) {
                $SelectedCharacterModels = $candidate.FullName
                $SelectedClibMdl = $clibCandidate.FullName
                $SelectedMotionZar = $motionCandidate.FullName
                $StandGltf = $candidateGltf
                break CharacterProbe
            }
        }
    }
}

if (-not $SelectedCharacterModels) {
    throw "Could not find a compatible MESH_seal_A_des / clib_mdl.zed / motion.zar combination."
}

$ClibMdl = $SelectedClibMdl
$MotionZar = $SelectedMotionZar
Write-Host "Matched SEAL character inputs:" -ForegroundColor Green
Write-Host "  models: $SelectedCharacterModels"
Write-Host "  clib:   $ClibMdl"
Write-Host "  motion: $MotionZar"

Write-Host ""
Write-Host "Exporting original SOCOM locomotion clips..." -ForegroundColor Cyan
foreach ($dir in @($WalkDir,$RunDir)) {
    if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
}
& $CharacterExporter $SelectedCharacterModels $ClibMdl $PlayerTextures "MESH_seal_A_des" $WalkDir $MotionZar "seal_walk"
if ($LASTEXITCODE -ne 0) { throw "seal_walk character export failed." }
& $CharacterExporter $SelectedCharacterModels $ClibMdl $PlayerTextures "MESH_seal_A_des" $RunDir $MotionZar "seal_run"
if ($LASTEXITCODE -ne 0) { throw "seal_run character export failed." }

$WalkGltf = Join-Path $WalkDir "seal_A_des\seal_A_des.gltf"
$RunGltf = Join-Path $RunDir "seal_A_des\seal_A_des.gltf"
foreach ($gltf in @($StandGltf,$WalkGltf,$RunGltf)) {
    if (!(Test-Path $gltf)) { throw "Expected character glTF missing: $gltf" }
}

$PlayerPack = Join-Path $GeneratedDir "seal_A_des.spr"
$PlayerReport = Join-Path $GeneratedDir "seal_A_des_report.json"
$PlayerLog = Join-Path $GeneratedDir "player_pack_build.log"
foreach ($x in @($PlayerPack,$PlayerReport,$PlayerLog)) { if (Test-Path $x) { Remove-Item $x -Force } }

Write-Host ""
Write-Host "Building animated SEAL player pack..." -ForegroundColor Cyan
& $PlayerPacker `
    --stand $StandGltf `
    --walk $WalkGltf `
    --run $RunGltf `
    --textures $PlayerTextures `
    --out $PlayerPack `
    --report $PlayerReport 2>&1 | Tee-Object -FilePath $PlayerLog
if ($LASTEXITCODE -ne 0 -or !(Test-Path $PlayerPack)) {
    if (Test-Path $PlayerLog) { Get-Content $PlayerLog -Tail 100 }
    throw "SEAL player pack generation failed. Full log: $PlayerLog"
}

Write-Host ""
Write-Host "Validating animated SEAL player pack..." -ForegroundColor Cyan
& $PlayerChecker $PlayerPack
if ($LASTEXITCODE -ne 0) { throw "Generated SEAL player pack failed validation." }

Write-Host ""
Write-Host "=== M8 Single-Player Character Runtime Ready ===" -ForegroundColor Green
Write-Host "Executable:  $RuntimeExe"
Write-Host "World pack:  $WorldPack"
Write-Host "Player pack: $PlayerPack"
Write-Host "World models archive:  $SelectedWorldModels"
Write-Host "Player models archive: $SelectedCharacterModels"
Write-Host "Player texture TXR:     $ClibTxr"
Write-Host "Player texture PAL:     $ClibPal"
Write-Host "Player clib model:      $ClibMdl"
Write-Host "Player motion archive:  $MotionZar"
Write-Host ""
Write-Host "Controls added in this phase:" -ForegroundColor Cyan
Write-Host "  F8  Toggle first-person / third-person camera"
Write-Host "  F9  Toggle player model"
Write-Host ""

if (-not $NoLaunch) {
    Write-Host "Launching textured M8 with animated SEAL player..." -ForegroundColor Cyan
    & $RuntimeExe --pack $WorldPack --player $PlayerPack
}
