param([Parameter(Mandatory=$true)][string]$UiReader)
$ErrorActionPreference="Stop"
$Repo=(Resolve-Path(Join-Path $PSScriptRoot "..\..")).Path
$Build=Join-Path $Repo "build\retail_ui_v6_test"
New-Item -ItemType Directory -Force -Path $Build|Out-Null
$Compiler=$null
if(Get-Command cl.exe -ErrorAction SilentlyContinue){$Compiler="cl"}
elseif(Get-Command g++.exe -ErrorAction SilentlyContinue){$Compiler="g++"}
elseif(Get-Command clang++.exe -ErrorAction SilentlyContinue){$Compiler="clang++"}
if(-not$Compiler){throw "No C++ compiler found."}
Push-Location $Build
try{
 if($Compiler-eq"cl"){
   & cl /nologo /std:c++17 /EHsc /DNOMINMAX /I $PSScriptRoot (Join-Path $PSScriptRoot "ui_runtime_test.cpp") /Fe:ui_runtime_test.exe
 }else{
   & $Compiler -std=c++17 -I $PSScriptRoot (Join-Path $PSScriptRoot "ui_runtime_test.cpp") -o ui_runtime_test.exe
 }
 if($LASTEXITCODE-ne 0){throw "Retail UI test build failed."}
 & .\ui_runtime_test.exe $UiReader
 if($LASTEXITCODE-ne 0){throw "Retail UI test failed."}
}finally{Pop-Location}
