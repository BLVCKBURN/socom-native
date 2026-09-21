param(
    [Parameter(Mandatory=$true)][string]$GameRoot,
    [Parameter(Mandatory=$true)][string]$TextureTool
)

$ErrorActionPreference="Stop"

$txr=Get-ChildItem $GameRoot -Recurse -File -Filter ui2d_txr.zed -ErrorAction SilentlyContinue |
     Select-Object -First 1
$pal=Get-ChildItem $GameRoot -Recurse -File -Filter ui2d_pal.zed -ErrorAction SilentlyContinue |
     Select-Object -First 1

if(-not $txr -or -not $pal){
    throw "ui2d texture/palette pair not found."
}

$Cache=Join-Path $env:TEMP "socom-native-ui2d-direct"
New-Item -ItemType Directory -Force -Path $Cache | Out-Null

$Targets=@(
    "SplashLogo.tif",
    "myriad_font.tif",
    "arrow_top.tif",
    "arrow_botm.tif"
)

foreach($target in $Targets){
    Write-Host ""
    Write-Host "Direct extract: $target" -ForegroundColor Cyan

    $old=$ErrorActionPreference
    $ErrorActionPreference="Continue"

    try{
        $output=& $TextureTool `
            $txr.FullName `
            $pal.FullName `
            --extract `
            $target `
            $Cache 2>&1

        $code=$LASTEXITCODE
        $output | ForEach-Object { Write-Host "  $_" }
        Write-Host "  exit=$code"

        $png=Join-Path $Cache ([IO.Path]::GetFileNameWithoutExtension($target)+".png")
        if(Test-Path $png){
            Write-Host "  READY: $png" -ForegroundColor Green
        }else{
            Write-Host "  no PNG created" -ForegroundColor Red
        }
    }
    finally{
        $ErrorActionPreference=$old
    }
}
