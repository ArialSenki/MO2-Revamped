# The bridge calls a Revamped native initializer for the DLL MO2 named by its
# real path in the mod, also when GetModuleFileNameW gives the virtual path
# in the game folder (usvfs hooks it; alpha.68 matched only that, and no
# initializer ran in the game), and when it was loaded from a path of 260
# characters or more, which the process snapshot leaves out (alpha.74 reads
# the loader's own list for those).
#   powershell -File run.ps1 -ZigPath <zig 0.13.0> -Bridge <eldenring_mo2_bridge.dll>
param(
    [Parameter(Mandatory = $true)][string]$ZigPath,
    [Parameter(Mandatory = $true)][string]$Bridge
)
$ErrorActionPreference = 'Stop'
$work = Join-Path ([System.IO.Path]::GetTempPath()) ('er-init-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $work 'tmp') -Force | Out-Null
& $ZigPath cc -target x86_64-windows-gnu -O1 -shared -o (Join-Path $work 'target.dll') (Join-Path $PSScriptRoot 'target.c')
& $ZigPath cc -target x86_64-windows-gnu -O1 -municode -o (Join-Path $work 'host.exe') (Join-Path $PSScriptRoot 'host.c')
Copy-Item -LiteralPath $Bridge -Destination $work
$env:TEMP = Join-Path $work 'tmp'; $env:TMP = $env:TEMP

$short = Join-Path $work 'mods\candidate\DLLs'
New-Item -ItemType Directory -Path $short -Force | Out-Null
Copy-Item (Join-Path $work 'target.dll') $short
$long = Join-Path $work 'mods\long'
while ($long.Length -lt 270) { $long = Join-Path $long 'a folder name to pass the old path limit' }
$long = Join-Path $long 'DLLs'
[System.IO.Directory]::CreateDirectory('\\?\' + $long) | Out-Null
[System.IO.File]::Copy((Join-Path $work 'target.dll'), '\\?\' + (Join-Path $long 'target.dll'))
$cases = @(
    @{ Name = 'real path';              Dll = (Join-Path $short 'target.dll'); Mode = '' },
    @{ Name = 'virtual path (usvfs)';   Dll = (Join-Path $short 'target.dll'); Mode = 'virtual' },
    @{ Name = 'path over 259 characters'; Dll = (Join-Path $long 'target.dll'); Mode = '' }
)
$failed = 0
Push-Location $work
try {
    foreach ($case in $cases) {
        Remove-Item initialized.txt -ErrorAction SilentlyContinue
        $env:ELDENRING_MO2_NATIVE_INITIALIZERS = "$($case.Dll)|mo2_test_init"
        $dll = if ($case.Dll.Length -ge 260) { '\\?\' + $case.Dll } else { $case.Dll }
        $arguments = @("`"$(Join-Path $work 'eldenring_mo2_bridge.dll')`"", "`"$dll`"")
        if ($case.Mode) { $arguments += $case.Mode }
        # a hang (alpha.71 under the loader lock) fails the case after 40 s
        $process = Start-Process -FilePath .\host.exe -ArgumentList $arguments -NoNewWindow -PassThru
        $null = $process.Handle
        if (-not $process.WaitForExit(40000)) { $process.Kill(); $process.WaitForExit(); $code = 'hung' }
        else { $code = $process.ExitCode }
        "{0}: {1}" -f $case.Name, $(if ($code -eq 0) { 'initializer called' } else { $failed++; 'NOT CALLED' })
    }
} finally {
    Pop-Location
}
if ($failed) {
    Get-Content (Join-Path $work 'tmp\EldenRingMO2Bridge.log') | Select-String 'initializer|MO2-managed|Mod Engine'
    exit 1
}
