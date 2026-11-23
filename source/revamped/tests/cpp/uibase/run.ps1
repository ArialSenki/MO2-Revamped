# MO2 Revamped: builds and runs the uibase unit tests (not shipped).
#
#   powershell -ExecutionPolicy Bypass -File run.ps1 [-Only <name>[,<name>]] [-Out <folder>]
#                                                    [-Build <uibase build>] [-Direct]
#
# Builds each test_*.cpp here with cl (VS 2022 BuildTools) against the built
# uibase.lib/uibase.dll and Qt 6.12, into -Out, then runs them. Tests run on the
# private desktop (D:\mo2build\scripts\vis\desk.ps1) so nothing can show on the
# user's screen; -Direct runs them in this console instead. Exit code: the
# number of tests that failed or didn't build.
#
# uibase itself is built by CMake:
#   cmake --build D:\mo2build\wt-pyperf-upstream\build --config RelWithDebInfo --target uibase
param(
  [string[]]$Only,
  [string]$Out   = (Join-Path $env:TEMP 'mo2r-cpp-uibase'),
  [string]$Build = 'D:\mo2build\wt-pyperf-upstream\build',
  [string]$Qt    = 'D:\mo2build\Qt\6.12.0\msvc2022_64',
  [switch]$Direct
)
$ErrorActionPreference = 'Continue'

$here   = $PSScriptRoot
$uibase = (Resolve-Path "$here\..\..\..\..\upstream\modorganizer_super\uibase\src").Path
$libDir = "$Build\uibase\src\RelWithDebInfo"
$vsDev  = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'
$desk   = 'D:\mo2build\scripts\vis\desk.ps1'

if (-not (Test-Path "$libDir\uibase.lib")) { throw "no uibase.lib in ${libDir}: build uibase first" }
New-Item -ItemType Directory -Force $Out | Out-Null

# test name -> extra cl options (test_lines checks a header-only reader with AddressSanitizer)
$tests = [ordered]@{
  'versioninfo' = ''
  'utility'     = ''
  'fileversion' = ''
  'log'         = ''
  'misc'        = ''
  'lines'       = '/fsanitize=address'
}
if ($Only) { $names = @($Only | ForEach-Object { $_ -split ',' }) } else { $names = @($tests.Keys) }

$common = @(
  '/nologo', '/std:c++20', '/EHsc', '/permissive-', '/Zc:__cplusplus', '/utf-8', '/MD', '/Zi', '/O1',
  '/W3', '/wd4996',
  '/DUNICODE', '/D_UNICODE', '/DNOMINMAX', '/DWIN32', '/D_WINDOWS', '/DNDEBUG', '/DQT_NO_DEBUG',
  "/I`"$here`"", "/I`"$uibase`"",
  "/I`"$Qt\include`"", "/I`"$Qt\include\QtCore`"", "/I`"$Qt\include\QtGui`"", "/I`"$Qt\include\QtWidgets`""
) -join ' '
$libs = "`"$libDir\uibase.lib`" `"$Qt\lib\Qt6Core.lib`" `"$Qt\lib\Qt6Gui.lib`" `"$Qt\lib\Qt6Widgets.lib`" version.lib shell32.lib ole32.lib user32.lib"

# cl's folder holds the AddressSanitizer runtime
$clDir = Split-Path ((& cmd /c "call `"$vsDev`" -no_logo -arch=amd64 -host_arch=amd64 >nul 2>&1 && where cl.exe") | Select-Object -First 1)
$env:PATH = "$libDir;$Qt\bin;$clDir;$env:PATH"

$failed = 0
$results = @()
foreach ($name in $names) {
  if (-not $tests.Contains($name)) { throw "unknown test '$name'" }
  $src = "$here\test_$name.cpp"
  $exe = "$Out\test_$name.exe"

  # build
  $cmdFile = "$Out\build_$name.cmd"
  @(
    '@echo off'
    "call `"$vsDev`" -no_logo -arch=amd64 -host_arch=amd64 >nul 2>&1 || exit /b 1"
    "cd /d `"$Out`""
    "cl $common $($tests[$name]) /Fe:`"$exe`" /Fo:`"$Out\test_$name.obj`" /Fd:`"$Out\test_$name.pdb`" `"$src`" /link $libs"
  ) | Set-Content -Encoding ascii $cmdFile
  $buildLog = & cmd /c "`"$cmdFile`"" 2>&1
  if ($LASTEXITCODE -ne 0 -or -not (Test-Path $exe)) {
    $buildLog | Select-Object -Last 30 | Write-Host
    Write-Host "test_${name}: BUILD FAILED"
    $results += "test_${name}: build failed"
    $failed++
    continue
  }

  # run (PATH has the built uibase.dll, Qt and the AddressSanitizer runtime)
  $log = "$Out\test_$name.log"
  Remove-Item $log -ErrorAction SilentlyContinue
  if ($Direct) {
    & $exe --out $log
  } else {
    & $desk -CommandLine "`"$exe`" --out `"$log`"" -WorkDir $Out -Wait -TimeoutMs 300000 | Out-Null
  }
  # a crash can keep the log open a little longer (error reporting); wait for it
  $text = ''
  for ($i = 0; $i -lt 120; $i++) {
    $running = Get-Process "test_$name" -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }
    if (-not $running) {
      try { $text = [IO.File]::ReadAllText($log); break } catch { }
      if (-not (Test-Path $log)) { break }
    }
    Start-Sleep -Milliseconds 500
  }
  if ($null -eq $text) { $text = '' }
  Write-Host $text.TrimEnd()
  if ($text -match "(?m)^test_${name}: (\d+) passed, (\d+) failed") {
    $results += $Matches[0]
    if ([int]$Matches[2] -ne 0) { $failed++ }
  } else {
    $results += "test_${name}: crashed or timed out (no summary line)"
    $failed++
  }
}

Write-Host ''
$results | ForEach-Object { Write-Host $_ }
exit $failed
