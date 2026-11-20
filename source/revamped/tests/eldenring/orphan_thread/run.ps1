# The bridge's guard against a thread whose DLL was unloaded before it ran
# (Nightreign's start crash with Damage Competition). victim.dll starts a
# thread in DllMain and fails its load, so the thread starts in freed memory:
# host.exe crashes without the bridge and goes on with it.
#   powershell -File run.ps1 -ZigPath <zig 0.13.0> -Bridge <eldenring_mo2_bridge.dll>
param(
    [Parameter(Mandatory = $true)][string]$ZigPath,
    [Parameter(Mandatory = $true)][string]$Bridge
)
$ErrorActionPreference = 'Stop'
$work = Join-Path ([System.IO.Path]::GetTempPath()) ('er-orphan-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $work 'tmp') -Force | Out-Null
& $ZigPath cc -target x86_64-windows-gnu -O1 -shared -o (Join-Path $work 'victim.dll') (Join-Path $PSScriptRoot 'victim.c')
& $ZigPath cc -target x86_64-windows-gnu -O1 -o (Join-Path $work 'host.exe') (Join-Path $PSScriptRoot 'host.c')
Copy-Item -LiteralPath $Bridge -Destination $work
# the bridge writes its log to TEMP: not the real one
$env:TEMP = Join-Path $work 'tmp'; $env:TMP = $env:TEMP
Push-Location $work
try {
    $without = (Start-Process -FilePath .\host.exe -NoNewWindow -Wait -PassThru).ExitCode
    $with = (Start-Process -FilePath .\host.exe -ArgumentList (Join-Path $work 'eldenring_mo2_bridge.dll') -NoNewWindow -Wait -PassThru).ExitCode
} finally {
    Pop-Location
}
"without the bridge: exit 0x{0:X} (a crash expected)" -f $without
"with the bridge:    exit 0x{0:X} (0 expected)" -f $with
Get-Content (Join-Path $work 'tmp\EldenRingMO2Bridge.log') | Select-String 'unloaded|Stopped'
if ($with -ne 0 -or $without -eq 0) { exit 1 }
