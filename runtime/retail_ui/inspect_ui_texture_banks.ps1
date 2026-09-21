param(
    [Parameter(Mandatory=$true)][string]$GameRoot,
    [Parameter(Mandatory=$true)][string]$TextureTool
)

$ErrorActionPreference="Stop"
if(Get-Variable PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue){
    $PSNativeCommandUseErrorActionPreference=$false
}

$Banks=@(
    @{ Txr="ui2d_txr.zed"; Pal="ui2d_pal.zed" },
    @{ Txr="uibf_txr.zed"; Pal="uibf_pal.zed" },
    @{ Txr="uimp_txr.zed"; Pal="uimp_pal.zed" }
)

foreach($bank in $Banks){
    $txr=Get-ChildItem $GameRoot -Recurse -File -Filter $bank.Txr -ErrorAction SilentlyContinue | Select-Object -First 1
    $pal=Get-ChildItem $GameRoot -Recurse -File -Filter $bank.Pal -ErrorAction SilentlyContinue | Select-Object -First 1
    if(-not $txr -or -not $pal){ continue }

    Write-Host ""
    Write-Host "=== $($txr.FullName) ===" -ForegroundColor Cyan
    & $TextureTool $txr.FullName $pal.FullName --list
}
