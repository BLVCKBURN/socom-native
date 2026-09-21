param(
    [Parameter(Mandatory=$true)][string]$GameRoot
)

$ErrorActionPreference="Stop"

$targets=@(
    "SplashLogo.tif",
    "myriad_font.tif",
    "arrow_top.tif",
    "arrow_botm.tif"
)

Write-Host "Loose retail assets:" -ForegroundColor Cyan
foreach($target in $targets){
    $hits=Get-ChildItem $GameRoot -Recurse -File -ErrorAction SilentlyContinue |
          Where-Object { $_.Name -ieq $target }
    if($hits){
        $hits | ForEach-Object { Write-Host "  $target -> $($_.FullName)" -ForegroundColor Green }
    }else{
        Write-Host "  $target -> not loose" -ForegroundColor DarkYellow
    }
}

Write-Host ""
Write-Host "Containers containing the asset names as embedded ASCII:" -ForegroundColor Cyan

$candidates=Get-ChildItem $GameRoot -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Extension -in ".zar",".zed",".rdr",".bin",".dat" }

foreach($file in $candidates){
    try{
        $bytes=[IO.File]::ReadAllBytes($file.FullName)
        $text=[Text.Encoding]::ASCII.GetString($bytes)
        $matched=@()
        foreach($target in $targets){
            if($text.IndexOf($target,[StringComparison]::OrdinalIgnoreCase) -ge 0){
                $matched+=$target
            }
        }
        if($matched.Count){
            Write-Host "  $($file.FullName)"
            Write-Host "    $($matched -join ', ')"
        }
    }catch{}
}
