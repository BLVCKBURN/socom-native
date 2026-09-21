$ErrorActionPreference="Stop"
$Here=Split-Path -Parent $MyInvocation.MyCommand.Path
$Repo=(Resolve-Path(Join-Path $Here "..\..\..")).Path
$Build=Join-Path $Repo "build\recomp_cseal_weapon_test"
New-Item -ItemType Directory -Force -Path $Build|Out-Null
$Compiler=$null
if(Get-Command cl.exe -ErrorAction SilentlyContinue){$Compiler="cl"}
elseif(Get-Command g++.exe -ErrorAction SilentlyContinue){$Compiler="g++"}
elseif(Get-Command clang++.exe -ErrorAction SilentlyContinue){$Compiler="clang++"}
if(-not $Compiler){throw "No C++ compiler found in PATH. Run from a Visual Studio Developer PowerShell or install a C++ toolchain."}
Push-Location $Build
try{
 if($Compiler -eq "cl"){
   & cl /nologo /std:c++17 /EHsc /I (Join-Path $Repo "runtime\recomp\native") /I (Join-Path $Repo "runtime\recomp\recovered") (Join-Path $Repo "runtime\recomp\native\cseal_weapon_accessors_test.cpp") /Fe:cseal_weapon_test.exe
 } else {
   & $Compiler -std=c++17 -I (Join-Path $Repo "runtime\recomp\native") -I (Join-Path $Repo "runtime\recomp\recovered") (Join-Path $Repo "runtime\recomp\native\cseal_weapon_accessors_test.cpp") -o cseal_weapon_test.exe
 }
 if($LASTEXITCODE-ne 0){throw "Native accessor test build failed."}
 & .\cseal_weapon_test.exe
 if($LASTEXITCODE-ne 0){throw "Native accessor smoke test failed."}
} finally {Pop-Location}
