# Unit test of when the bridge's "start minimized" guard ends by itself
# (under Wine and Proton, where the user's click never reaches it).
#   powershell -File run.ps1 -ZigPath <zig 0.13.0>
param([Parameter(Mandatory = $true)][string]$ZigPath)
$ErrorActionPreference = 'Stop'
$work = Join-Path ([System.IO.Path]::GetTempPath()) ('er-focus-guard-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work -Force | Out-Null
try {
    $exe = Join-Path $work 'test_focus_guard.exe'
    & $ZigPath c++ -target x86_64-windows-gnu -O1 -o $exe (Join-Path $PSScriptRoot 'test_focus_guard.cpp')
    if ($LASTEXITCODE -ne 0) { throw "zig exited with code $LASTEXITCODE" }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "focus guard checks failed" }
} finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
