param([string]$OutDir="")
$ErrorActionPreference="Stop"
$Repo=(Resolve-Path(Join-Path $PSScriptRoot "..\..")).Path
if(-not $OutDir){$OutDir=Join-Path $Repo "generated\ee\scus_971_34\weapon_runtime"}
$Src=Join-Path $Repo "tools\ee\weapon_runtime_native"
$Build=Join-Path $Repo "build\ee_weapon_runtime"
New-Item -ItemType Directory -Force -Path $Src,$Build,$OutDir|Out-Null
Copy-Item -Force (Join-Path $PSScriptRoot "recover_weapon_runtime.cpp") (Join-Path $Src "recover_weapon_runtime.cpp")
Copy-Item -Force (Join-Path $PSScriptRoot "CMakeLists.weapon_runtime.txt") (Join-Path $Src "CMakeLists.txt")
cmake -S $Src -B $Build
if($LASTEXITCODE-ne 0){throw "CMake configure failed."}
cmake --build $Build --config Release
if($LASTEXITCODE-ne 0){throw "Weapon runtime analyzer build failed."}
$exe=Get-ChildItem $Build -Recurse -Filter recover_weapon_runtime.exe|Select-Object -First 1
if(-not $exe){$exe=Get-ChildItem $Build -Recurse -Filter recover_weapon_runtime|Select-Object -First 1}
if(-not $exe){throw "recover_weapon_runtime executable not found."}
& $exe.FullName $OutDir
if($LASTEXITCODE-ne 0){throw "Weapon runtime report generation failed."}
Write-Host "Weapon runtime reports: $OutDir" -ForegroundColor Green
