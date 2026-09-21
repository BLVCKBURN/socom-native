param([string]$OutDir="")
$ErrorActionPreference="Stop"
$Repo=(Resolve-Path(Join-Path $PSScriptRoot "..\..")).Path
if(-not $OutDir){$OutDir=Join-Path $Repo "generated\ee\scus_971_34\cseal_weapons"}
$Src=Join-Path $Repo "tools\ee\cseal_weapons_native"
$Build=Join-Path $Repo "build\ee_cseal_weapons"
New-Item -ItemType Directory -Force -Path $Src,$Build,$OutDir|Out-Null
Copy-Item -Force (Join-Path $PSScriptRoot "recover_cseal_weapons.cpp") (Join-Path $Src "recover_cseal_weapons.cpp")
Copy-Item -Force (Join-Path $PSScriptRoot "CMakeLists.cseal_weapons.txt") (Join-Path $Src "CMakeLists.txt")
cmake -S $Src -B $Build
if($LASTEXITCODE-ne 0){throw "CMake configure failed."}
cmake --build $Build --config Release
if($LASTEXITCODE-ne 0){throw "Weapon recovery analyzer build failed."}
$exe=Get-ChildItem $Build -Recurse -Filter recover_cseal_weapons.exe|Select-Object -First 1
if(-not $exe){$exe=Get-ChildItem $Build -Recurse -Filter recover_cseal_weapons|Select-Object -First 1}
if(-not $exe){throw "recover_cseal_weapons executable not found."}
& $exe.FullName $OutDir
if($LASTEXITCODE-ne 0){throw "Weapon recovery report generation failed."}
Write-Host "Weapon recovery reports: $OutDir" -ForegroundColor Green
