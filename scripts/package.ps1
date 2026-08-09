param(
    [string]$BuildDirectory = "build",
    [string]$OutputDirectory = "dist"
)

$ErrorActionPreference = "Stop"
$loaderVersion = "v9.7.2"
$loaderUrl = "https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/$loaderVersion/Ultimate-ASI-Loader_x64.zip"
$loaderSha256 = "1c1f9ebf3996df4a4bcf9be0785b9de8f934ef6497f73145602488fc7d8c2909"
$version = (Get-Content "VERSION" -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') {
    throw "VERSION must contain a semantic version in X.Y.Z form"
}
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

$archive = Join-Path $OutputDirectory "Nioh1Fix-$version.zip"
Remove-Item $archive -Force -ErrorAction SilentlyContinue
Compress-Archive "$staging/*" $archive
Write-Host "Created $archive"
