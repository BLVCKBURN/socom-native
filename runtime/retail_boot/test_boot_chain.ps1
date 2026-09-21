param([Parameter(Mandatory=$true)][string]$UiReader)
$ErrorActionPreference="Stop"
$Repo=(Resolve-Path(Join-Path $PSScriptRoot "..\..")).Path
$Build=Join-Path $Repo "build\retail_boot_data_test"
New-Item -ItemType Directory -Force -Path $Build|Out-Null
$Compiler=$null
if(Get-Command cl.exe -ErrorAction SilentlyContinue){$Compiler="cl"}
elseif(Get-Command g++.exe -ErrorAction SilentlyContinue){$Compiler="g++"}
elseif(Get-Command clang++.exe -ErrorAction SilentlyContinue){$Compiler="clang++"}
if(-not $Compiler){throw "No C++ compiler found in PATH."}
Push-Location $Build
try{
 if($Compiler-eq"cl"){
  & cl /nologo /std:c++17 /EHsc /I $PSScriptRoot (Join-Path $PSScriptRoot "retail_boot_data_test.cpp") /Fe:retail_boot_data_test.exe
 }else{
  & $Compiler -std=c++17 -I $PSScriptRoot (Join-Path $PSScriptRoot "retail_boot_data_test.cpp") -o retail_boot_data_test.exe
 }
 if($LASTEXITCODE-ne 0){throw "Boot-chain test build failed."}
 & .\retail_boot_data_test.exe $UiReader
 if($LASTEXITCODE-ne 0){throw "Boot-chain data test failed."}
}finally{Pop-Location}
