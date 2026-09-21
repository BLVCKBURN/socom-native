param(
    [Parameter(Mandatory=$true)][string]$GameRoot,
    [Parameter(Mandatory=$true)][string]$TextureTool
)

$ErrorActionPreference="Stop"

if(-not(Test-Path $GameRoot)){
    throw "Game root not found: $GameRoot"
}
if(-not(Test-Path $TextureTool)){
    throw "Texture extractor not found: $TextureTool"
}

$Cache=Join-Path $env:TEMP "socom-native-ui-assets"
New-Item -ItemType Directory -Force -Path $Cache | Out-Null

$Targets=@(
    "SplashLogo.tif",
    "myriad_font.tif",
    "arrow_top.tif",
    "arrow_botm.tif"
)

$Banks=@(
    @{ Txr="ui2d_txr.zed"; Pal="ui2d_pal.zed" },
    @{ Txr="uibf_txr.zed"; Pal="uibf_pal.zed" },
    @{ Txr="uimp_txr.zed"; Pal="uimp_pal.zed" }
)

# Always regenerate these four small UI assets. This avoids stale PNGs from
# older orientation/scale experiments and makes each run deterministic.
foreach($target in $Targets){
    $png=[IO.Path]::GetFileNameWithoutExtension($target)+".png"
    Remove-Item (Join-Path $Cache $png) -Force -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "Preparing original SOCOM retail UI textures..." -ForegroundColor Cyan

$Failures=@{}

foreach($bank in $Banks){
    $txr=Get-ChildItem $GameRoot -Recurse -File -Filter $bank.Txr -ErrorAction SilentlyContinue |
         Select-Object -First 1
    if(-not $txr){ continue }

    $pal=Get-ChildItem $GameRoot -Recurse -File -Filter $bank.Pal -ErrorAction SilentlyContinue |
         Select-Object -First 1

    if(-not $pal){
        Write-Warning "Found $($txr.FullName), but matching $($bank.Pal) was not found."
        continue
    }

    Write-Host "  Bank: $($txr.Name) + $($pal.Name)"

    foreach($target in $Targets){
        $png=[IO.Path]::GetFileNameWithoutExtension($target)+".png"
        $out=Join-Path $Cache $png

        # Once a target is recovered from an earlier bank, do not overwrite it
        # with a duplicate copy from another UI library.
        if(Test-Path $out){ continue }

        $previousPreference=$ErrorActionPreference
        $ErrorActionPreference="Continue"

        try{
            # This direct invocation is the path that successfully extracted
            # all four retail assets in v11.1.  Windows PowerShell may emit a
            # NativeCommandError object when the tool writes stderr, but with
            # ErrorActionPreference=Continue that does not terminate the bank
            # scan.  We capture both streams and use LASTEXITCODE as the source
            # of truth.
            $toolOutput = & $TextureTool `
                $txr.FullName `
                $pal.FullName `
                --extract `
                $target `
                $Cache 2>&1

            $exitCode=$LASTEXITCODE

            if($exitCode -eq 0 -and (Test-Path $out)){
                Write-Host "    extracted $target" -ForegroundColor Green
            }else{
                if(-not $Failures.ContainsKey($target)){
                    $Failures[$target]=@()
                }

                $rendered = ($toolOutput | Out-String).Trim()
                $Failures[$target] += "$($txr.Name) [exit $exitCode]: $rendered"
            }
        }
        finally{
            $ErrorActionPreference=$previousPreference
        }
    }
}

Write-Host ""
$missing=@()

foreach($target in $Targets){
    $png=[IO.Path]::GetFileNameWithoutExtension($target)+".png"
    $out=Join-Path $Cache $png

    if(Test-Path $out){
        Write-Host "  READY  $png" -ForegroundColor Green
    }else{
        Write-Host "  MISSING $png" -ForegroundColor Red
        $missing += $target
    }
}

if($missing.Count -gt 0){
    Write-Host ""
    Write-Host "Extractor diagnostics for unresolved assets:" -ForegroundColor Yellow
    foreach($target in $missing){
        Write-Host "  $target" -ForegroundColor Yellow
        if($Failures.ContainsKey($target)){
            foreach($failure in $Failures[$target]){
                if($failure.Trim()){
                    Write-Host "    $failure"
                }
            }
        }
    }
}

Write-Host ""
Write-Host "Cache: $Cache"
