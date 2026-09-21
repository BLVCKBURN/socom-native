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
    throw "ui2d texture/palette bank pair not found."
}

$Targets=@(
    "SplashLogo.tif",
    "myriad_font.tif",
    "arrow_top.tif",
    "arrow_botm.tif"
)

$Cache=Join-Path $env:TEMP "socom-native-ui-assets-direct"
New-Item -ItemType Directory -Force -Path $Cache | Out-Null

foreach($target in $Targets){
    Write-Host ""
    Write-Host "Testing $target" -ForegroundColor Cyan

    $stdout=[IO.Path]::GetTempFileName()
    $stderr=[IO.Path]::GetTempFileName()

    try{
        $command = '""{0}" "{1}" "{2}" --extract "{3}" "{4}" 1>"{5}" 2>"{6}""' -f `
            $TextureTool,$txr.FullName,$pal.FullName,$target,$Cache,$stdout,$stderr

        & $env:ComSpec /d /s /c $command
        $exitCode=$LASTEXITCODE

        if(Test-Path $stdout){ Get-Content $stdout -ErrorAction SilentlyContinue }
        if(Test-Path $stderr){ Get-Content $stderr -ErrorAction SilentlyContinue }

        Write-Host "Exit code: $exitCode"
    }
    finally{
        Remove-Item $stdout,$stderr -Force -ErrorAction SilentlyContinue
    }
}
