param([int]$Jobs = 8)
$ErrorActionPreference = 'Stop'
$sdk = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location -LiteralPath $sdk
$env:SDK_PRJ_TOP_DIR = $sdk
$env:ENV_ROOT = Join-Path $sdk 'tools\env'
$env:PKGS_ROOT = Join-Path $env:ENV_ROOT 'packages'
$env:RTT_ROOT = Join-Path $sdk 'kernel\rt-thread'
$env:PYTHONIOENCODING = 'UTF-8'
$env:PATH = "$sdk\tools\env\tools\Python38;$sdk\tools\env\tools\Python27\Scripts;$sdk\tools\env\tools\bin;$sdk\tools\scripts\onestep;$env:PATH"
# Build both stages explicitly so a bootloader failure cannot be hidden by a pipe.
scons --apply-def=d13x_JLC_baremetal_bootloader_defconfig
if ($LASTEXITCODE) { throw 'Bootloader configuration failed' }
scons -j $Jobs
if ($LASTEXITCODE) { throw 'Bootloader build failed' }
scons --apply-def=d13x_JLC_rt-thread_helloworld_netapp_defconfig
if ($LASTEXITCODE) { throw 'Application configuration failed' }
scons -j $Jobs
if ($LASTEXITCODE) { throw 'Application build failed' }
$images = Join-Path $sdk 'output\d13x_JLC_rt-thread_helloworld_netapp\images'
$elf = Get-Item -LiteralPath (Join-Path $images 'd13x.elf')
foreach ($name in @('d13x_JLC_v1.1.0.img', 'ota.cpio')) {
    $artifact = Get-Item -LiteralPath (Join-Path $images $name)
    if ($artifact.Length -eq 0 -or $artifact.LastWriteTime -lt $elf.LastWriteTime) {
        throw "Missing or stale output: $name; inspect SDK packaging errors"
    }
}
python3 tools/netapp/ota_manifest.py (Join-Path $images 'ota.cpio')
if ($LASTEXITCODE) { throw 'OTA manifest generation failed' }
