param(
    [Parameter(Mandatory=$true)][string]$ReaderArchive
)
$ErrorActionPreference="Stop"
$Repo=(Resolve-Path(Join-Path $PSScriptRoot "..\..")).Path
$Build=Join-Path $Repo "build\menu_data_test"
New-Item -ItemType Directory -Force -Path $Build|Out-Null

$Compiler=$null
if(Get-Command cl.exe -ErrorAction SilentlyContinue){$Compiler="cl"}
elseif(Get-Command g++.exe -ErrorAction SilentlyContinue){$Compiler="g++"}
elseif(Get-Command clang++.exe -ErrorAction SilentlyContinue){$Compiler="clang++"}
if(-not $Compiler){throw "No C++ compiler found in PATH."}

Push-Location $Build
try {
    if($Compiler -eq "cl") {
        & cl /nologo /std:c++17 /EHsc /I $PSScriptRoot `
          (Join-Path $PSScriptRoot "menu_data_test.cpp") /Fe:menu_data_test.exe
    } else {
        & $Compiler -std=c++17 -I $PSScriptRoot `
          (Join-Path $PSScriptRoot "menu_data_test.cpp") -o menu_data_test.exe
    }
    if($LASTEXITCODE-ne 0){throw "menu data test build failed."}
    & .\menu_data_test.exe $ReaderArchive
    if($LASTEXITCODE-ne 0){throw "menu data test failed."}
} finally { Pop-Location }
