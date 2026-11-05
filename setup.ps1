<#
.SYNOPSIS
    Overlay this project onto a luban-lite V1.0 checkout and optionally build it.

.DESCRIPTION
    1. clone https://gitee.com/lcsc/luban-lite.git (once, ~1.9 GB) into .\luban-lite
    2. apply firmware\patches\0001-d133-netapp-sdk-changes.patch (8 SDK files)
    3. copy the netapp application, defconfig, tools, docs and fonts into the SDK
    4. with -Build, run the SDK's tools\netapp\build.ps1

    Re-running is safe: an already-applied patch is detected and skipped.

.EXAMPLE
    .\setup.ps1
    .\setup.ps1 -Build
    .\setup.ps1 -SdkDir D:\sdk\luban-lite -SkipClone -Build
#>

param(
    [string]$SdkDir = (Join-Path $PSScriptRoot 'luban-lite'),
    [string]$SdkUrl = 'https://gitee.com/lcsc/luban-lite.git',
    [string]$SdkCommit = 'f5be1e4c0c1415431c2841a2994c8cdbd23aafa2',
    [switch]$SkipClone,
    [switch]$Build,
    [int]$Jobs = 8
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

function Write-Step([string]$text) {
    Write-Host "`n=== $text ===" -ForegroundColor Cyan
}

Write-Step 'SDK'
if (-not (Test-Path (Join-Path $SdkDir '.git'))) {
    if ($SkipClone) { throw "SDK not found at $SdkDir and -SkipClone was given." }
    Write-Host "cloning $SdkUrl -> $SdkDir  (~1.9 GB, 4.2 万文件, 请耐心等待)"
    git clone $SdkUrl $SdkDir
    if ($LASTEXITCODE) { throw 'git clone failed' }
}

$head = (git -C $SdkDir rev-parse HEAD)
if ($head -ne $SdkCommit) {
    Write-Host "checkout $SdkCommit (current $head)"
    git -C $SdkDir checkout --quiet $SdkCommit
    if ($LASTEXITCODE) { throw "cannot checkout $SdkCommit; the SDK working tree may be dirty." }
} else {
    Write-Host "SDK already at $SdkCommit"
}

Write-Step 'Apply SDK patch'
$patch = Join-Path $root 'firmware\patches\0001-d133-netapp-sdk-changes.patch'
if (-not (Test-Path $patch)) { throw "patch not found: $patch" }

git -C $SdkDir apply --check --reverse $patch 2>$null
if ($LASTEXITCODE -eq 0) {
    Write-Host 'patch already applied, skipping'
} else {
    git -C $SdkDir apply --check $patch
    if ($LASTEXITCODE) { throw 'patch does not apply: SDK must be V1.0 (f5be1e4c) and unmodified.' }
    git -C $SdkDir apply $patch
    if ($LASTEXITCODE) { throw 'git apply failed' }
    Write-Host 'patch applied'
}

Write-Step 'Install project files'
$appSrc  = Join-Path $root 'firmware\application\rt-thread\helloworld\netapp'
$appDst  = Join-Path $SdkDir 'application\rt-thread\helloworld'
Copy-Item $appSrc $appDst -Recurse -Force

Copy-Item (Join-Path $root 'firmware\target\configs\d13x_JLC_rt-thread_helloworld_netapp_defconfig') `
          (Join-Path $SdkDir 'target\configs') -Force

Copy-Item (Join-Path $root 'firmware\tools\netapp') (Join-Path $SdkDir 'tools') -Recurse -Force

New-Item -ItemType Directory -Force -Path (Join-Path $SdkDir 'docs\netapp') | Out-Null
Copy-Item (Join-Path $root 'firmware\docs\netapp-design.md') `
          (Join-Path $SdkDir 'docs\netapp\README.md') -Force

Copy-Item (Join-Path $root 'fonts_src') (Join-Path $SdkDir 'fonts_src') -Recurse -Force

Write-Host "application -> $appDst\netapp"
Write-Host 'config      -> target\configs\d13x_JLC_rt-thread_helloworld_netapp_defconfig'
Write-Host 'tools       -> tools\netapp'
Write-Host 'design doc  -> docs\netapp\README.md'
Write-Host 'fonts       -> fonts_src'

if ($Build) {
    Write-Step 'Build firmware'
    & (Join-Path $SdkDir 'tools\netapp\build.ps1') -Jobs $Jobs
    if ($LASTEXITCODE) { throw 'build failed' }
    $images = Join-Path $SdkDir 'output\d13x_JLC_rt-thread_helloworld_netapp\images'
    Write-Host "`nimages: $images" -ForegroundColor Green
    Get-ChildItem $images -Filter 'd13x_JLC_v1.1.0.img', 'ota.cpio' -ErrorAction SilentlyContinue |
        Select-Object Name, Length, LastWriteTime | Format-Table -AutoSize
}

Write-Step 'Done'
Write-Host 'build:  .\luban-lite\tools\netapp\build.ps1'
Write-Host 'flash:  see docs\flashing.md (use AiBurn GUI "Burn online")'
Write-Host 'sim:    cd simulator; cmake -S . -B build -G Ninja ...'
