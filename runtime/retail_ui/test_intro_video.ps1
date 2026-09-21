param(
    [Parameter(Mandatory=$true)][string]$GameRoot
)

$ErrorActionPreference="Stop"
$ffmpeg=Get-Command ffmpeg.exe -ErrorAction Stop

$sony=Get-ChildItem $GameRoot -Recurse -File -Filter sony448.pss -ErrorAction SilentlyContinue |
      Select-Object -First 1
$intro=Get-ChildItem $GameRoot -Recurse -File -Filter Intro_2.pss -ErrorAction SilentlyContinue |
       Select-Object -First 1

if(-not $sony){ Write-Warning "sony448.pss was not found." }
if(-not $intro){ Write-Warning "Intro_2.pss was not found." }

@($sony,$intro) | Where-Object { $_ } | ForEach-Object {
    Write-Host ""
    Write-Host "Testing $($_.FullName)" -ForegroundColor Cyan

    & $ffmpeg.Source `
      -hide_banner `
      -loglevel info `
      -probesize 100M `
      -analyzeduration 100M `
      -i $_.FullName `
      -map 0:v:0 `
      -frames:v 1 `
      -f null NUL

    if($LASTEXITCODE -eq 0){
        Write-Host "First video frame decoded successfully." -ForegroundColor Green
    }else{
        Write-Warning "FFmpeg could not decode the first video frame."
    }
}
