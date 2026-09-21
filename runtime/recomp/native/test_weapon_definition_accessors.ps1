$ErrorActionPreference="Stop"
$Here=Split-Path -Parent $MyInvocation.MyCommand.Path
$Repo=(Resolve-Path(Join-Path $Here "..\..\..")).Path
$Build=Join-Path $Repo "build\recomp_weapon_definition_test"
New-Item -ItemType Directory -Force -Path $Build|Out-Null
$Compiler=$null
if(Get-Command cl.exe -ErrorAction SilentlyContinue){$Compiler="cl"}
elseif(Get-Command g++.exe -ErrorAction SilentlyContinue){$Compiler="g++"}
elseif(Get-Command clang++.exe -ErrorAction SilentlyContinue){$Compiler="clang++"}
if(-not $Compiler){throw "No C++ compiler found in PATH."}
Push-Location $Build
try{
 if($Compiler -eq "cl"){
  & cl /nologo /std:c++17 /EHsc /I (Join-Path $Repo "runtime\recomp\native") /I (Join-Path $Repo "runtime\recomp\recovered") (Join-Path $Repo "runtime\recomp\native\weapon_definition_accessors_test.cpp") /Fe:weapon_definition_test.exe
 } else {
  & $Compiler -std=c++17 -I (Join-Path $Repo "runtime\recomp\native") -I (Join-Path $Repo "runtime\recomp\recovered") (Join-Path $Repo "runtime\recomp\native\weapon_definition_accessors_test.cpp") -o weapon_definition_test.exe
 }
 if($LASTEXITCODE-ne 0){throw "Weapon definition accessor test build failed."}
 & .\weapon_definition_test.exe
 if($LASTEXITCODE-ne 0){throw "Weapon definition accessor smoke test failed."}
} finally {Pop-Location}
