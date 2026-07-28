param(
    [string]$BuildDirectory = "build",
    [string]$OutputDirectory = "dist"
)

$ErrorActionPreference = "Stop"
$loaderVersion = "v9.7.1"
$loaderUrl = "https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/$loaderVersion/Ultimate-ASI-Loader_x64.zip"
$loaderSha256 = "77da5b4c3ab4552b3ba605667961c9a46f1b6c78c80667d572d1e811e9670306"
$staging = Join-Path $OutputDirectory "Nioh1Fix"
$loaderZip = Join-Path $OutputDirectory "Ultimate-ASI-Loader_x64.zip"

Remove-Item $staging -Recurse -Force -ErrorAction SilentlyContinue
New-Item $staging -ItemType Directory -Force | Out-Null

$plugin = Join-Path $BuildDirectory "Release/Nioh1Fix.asi"
if (-not (Test-Path $plugin)) {
    $plugin = Join-Path $BuildDirectory "Nioh1Fix.asi"
}
if (-not (Test-Path $plugin)) {
    throw "Nioh1Fix.asi was not found under $BuildDirectory"
}

Copy-Item $plugin $staging
Copy-Item "Nioh1Fix.ini" $staging
Copy-Item "README.md" $staging
Copy-Item "LICENSE" $staging
Copy-Item "THIRD_PARTY.md" $staging

Invoke-WebRequest $loaderUrl -OutFile $loaderZip
if ((Get-FileHash $loaderZip -Algorithm SHA256).Hash.ToLowerInvariant() -ne $loaderSha256) {
    throw "Ultimate ASI Loader archive checksum mismatch"
}
Expand-Archive $loaderZip -DestinationPath $staging -Force
Rename-Item (Join-Path $staging "dinput8.dll") "version.dll"

$archive = Join-Path $OutputDirectory "Nioh1Fix-$((Get-Content CMakeLists.txt | Select-String 'project\(Nioh1Fix VERSION').Line.Split()[2]).zip"
Remove-Item $archive -Force -ErrorAction SilentlyContinue
Compress-Archive "$staging/*" $archive
Write-Host "Created $archive"
