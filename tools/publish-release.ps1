#Requires -Version 5.1
<#
.SYNOPSIS
    Package the current firmware build as a GitHub Release (vX.Y.Z).

.DESCRIPTION
    - locates the build images
    - writes release/<version>/{SHA256SUMS.txt, ota_manifest.json, size-report.csv}
    - commits that metadata, creates tag vX.Y.Z, pushes
    - creates the GitHub release and uploads img / ota.cpio / checksums

.EXAMPLE
    .\tools\publish-release.ps1 -Version v1.1.0
    .\tools\publish-release.ps1 -Version v1.1.1 -Build
#>
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$RepoDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path,
    [string]$ImagesDir = '',
    [switch]$Build,
    [switch]$Draft,
    [string]$Proxy = 'http://127.0.0.1:7897'
)

$ErrorActionPreference = 'Stop'
$gh = 'C:\Program Files\GitHub CLI\gh.exe'
if (-not (Test-Path $gh)) { $gh = (Get-Command gh -ErrorAction SilentlyContinue).Source }
if (-not $gh) { throw 'GitHub CLI (gh) not found' }

if ($Build) {
    & (Join-Path $RepoDir 'setup.ps1') -SdkDir 'C:\Users\chenfukun\Desktop\D133\luban-lite' -SkipClone | Out-Null
    $tmp = Join-Path $env:TEMP 'd133-release-tmp'
    New-Item -ItemType Directory -Force $tmp | Out-Null
    $env:TMP = $tmp; $env:TEMP = $tmp
    & 'C:\Users\chenfukun\Desktop\D133\luban-lite\tools\netapp\build.ps1' -Jobs 8
    if ($LASTEXITCODE) { throw 'firmware build failed' }
}

if (-not $ImagesDir) {
    $ImagesDir = 'C:\Users\chenfukun\Desktop\D133\luban-lite\output\d13x_JLC_rt-thread_helloworld_netapp\images'
}
$img = Join-Path $ImagesDir 'd13x_JLC_v1.1.0.img'
$cpio = Join-Path $ImagesDir 'ota.cpio'
foreach ($f in $img, $cpio) { if (-not (Test-Path $f)) { throw "missing build output: $f" } }

$metaDir = Join-Path $RepoDir "release\$Version"
New-Item -ItemType Directory -Force $metaDir | Out-Null

$sums = foreach ($f in $img, $cpio, (Join-Path $ImagesDir 'd13x_os.itb')) {
    '{0}  {1}' -f (Get-FileHash $f -Algorithm SHA256).Hash.ToLower(), (Split-Path $f -Leaf)
}
$sums | Set-Content (Join-Path $metaDir 'SHA256SUMS.txt') -Encoding ASCII

$crc = (& python -c "import zlib;print('%08X' % (zlib.crc32(open(r'$cpio','rb').read()) & 0xffffffff))").Trim()
[ordered]@{
    file   = 'ota.cpio'
    bytes  = (Get-Item $cpio).Length
    crc32  = $crc
    sha256 = (Get-FileHash $cpio -Algorithm SHA256).Hash.ToLower()
} | ConvertTo-Json | Set-Content (Join-Path $metaDir 'ota_manifest.json') -Encoding UTF8
Copy-Item (Join-Path $ImagesDir 'd13x.summary.csv') (Join-Path $metaDir 'size-report.csv') -Force
Write-Host "OTA crc32 = $crc"

$env:HTTPS_PROXY = $Proxy
$env:HTTP_PROXY = $Proxy

git -C $RepoDir add "release/$Version"
git -C $RepoDir commit -m "Release $Version metadata (SHA256SUMS, OTA manifest)" | Out-Null
git -C $RepoDir tag -f $Version | Out-Null
git -C $RepoDir push | Out-Null
git -C $RepoDir push -f origin $Version | Out-Null

$notes = Join-Path $metaDir 'RELEASE_NOTES.md'
$args = @('release', 'create', $Version, '-R', 'FukunHennan/d133-netapp',
          '--title', "D133 netapp $Version", '--notes-file', $notes)
if ($Draft) { $args += '--draft' }
$args += @($img, $cpio,
           (Join-Path $metaDir 'SHA256SUMS.txt'),
           (Join-Path $metaDir 'ota_manifest.json'),
           (Join-Path $metaDir 'size-report.csv'))
& $gh @args
if ($LASTEXITCODE) { throw 'gh release create failed' }

& $gh release view $Version -R FukunHennan/d133-netapp --json tagName,name,url,assets
