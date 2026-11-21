# The bridge's guard against a thread whose DLL was unloaded before it ran
# (Nightreign's start crash with Damage Competition, whose load fails now and
# then after starting a thread). victim.dll starts a thread in DllMain and
# fails its load, so the thread starts in freed memory: host.exe crashes
# without the bridge and goes on with it, and the bridge loads the DLL once
# more, which works when only the first load fails.
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
$bridgeArgument = Join-Path $work 'eldenring_mo2_bridge.dll'
function Invoke-Host([string[]]$Arguments) {
    Remove-Item victim-*.txt -ErrorAction SilentlyContinue
    $output = Join-Path $work 'out.txt'
    $start = @{ FilePath = '.\host.exe'; NoNewWindow = $true; Wait = $true; PassThru = $true; RedirectStandardOutput = $output }
    if ($Arguments) { $start.ArgumentList = $Arguments }
    $p = Start-Process @start
    @{ Code = $p.ExitCode; Text = (Get-Content $output -Raw) }
}
Push-Location $work
try {
    $env:VICTIM_ALWAYS_FAILS = '1'
    $without = Invoke-Host @()
    $always = Invoke-Host @($bridgeArgument)
    Remove-Item Env:VICTIM_ALWAYS_FAILS
    $once = Invoke-Host @($bridgeArgument)
} finally {
    Pop-Location
}
"without the bridge:          exit 0x{0:X} (a crash expected)" -f $without.Code
"bridge, DLL always fails:    exit 0x{0:X} (0 expected); {1}" -f $always.Code, ($always.Text -split "`n" | Select-String survived)
"bridge, DLL fails only once: exit 0x{0:X} (0 expected); {1}" -f $once.Code, ($once.Text -split "`n" | Select-String survived)
Get-Content (Join-Path $work 'tmp\EldenRingMO2Bridge.log') | Select-String 'unloaded|Stopped|again|second'
if ($without.Code -eq 0 -or $always.Code -ne 0 -or $once.Code -ne 0 -or $once.Text -notmatch 'second load worked') { exit 1 }
