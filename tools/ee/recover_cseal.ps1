param(
 [Parameter(Mandatory=$true)][string]$ElfPath,
 [string]$OutDir=""
)
$ErrorActionPreference="Stop"
$Repo=(Resolve-Path(Join-Path $PSScriptRoot "..\..")).Path
if(-not $OutDir){$OutDir=Join-Path $Repo "generated\ee\scus_971_34\cseal"}
$Src=Join-Path $Repo "tools\ee\cseal_native"
$Build=Join-Path $Repo "build\ee_cseal"
New-Item -ItemType Directory -Force -Path $Src,$Build,$OutDir|Out-Null
Copy-Item -Force (Join-Path $PSScriptRoot "recover_cseal.cpp") (Join-Path $Src "recover_cseal.cpp")
Copy-Item -Force (Join-Path $PSScriptRoot "CMakeLists.cseal.txt") (Join-Path $Src "CMakeLists.txt")
cmake -S $Src -B $Build
if($LASTEXITCODE-ne 0){throw "CMake configure failed."}
cmake --build $Build --config Release
if($LASTEXITCODE-ne 0){throw "CSeal analyzer build failed."}
$exe=Get-ChildItem $Build -Recurse -Filter recover_cseal.exe|Select-Object -First 1
if(-not $exe){$exe=Get-ChildItem $Build -Recurse -Filter recover_cseal|Select-Object -First 1}
if(-not $exe){throw "recover_cseal executable not found."}
& $exe.FullName $ElfPath $OutDir
if($LASTEXITCODE-ne 0){throw "CSeal recovery failed."}
Write-Host "CSeal reports: $OutDir" -ForegroundColor Green
