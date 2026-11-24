# MO2 Revamped: line coverage of the standalone C++ test suites (uibase, bsatk, organizer).
#
#   powershell -ExecutionPolicy Bypass -File coverage.ps1 [-Out <folder>] [-Only uibase,bsatk,organizer]
#
# Makes separate coverage builds (/Od /Zi, linked /DEBUG /PROFILE) of the test exes and of a
# copy of uibase.dll compiled from source into -Out (nothing in the CMake build is touched),
# instruments them with Microsoft.CodeCoverage.Console from VS 2022 BuildTools, runs every test
# under `collect`, merges the results and exports Cobertura (-Out\coverage.cobertura.xml). Then
# coverage_report.py prints the per-library figures and an estimate over all of
# modorganizer/src + uibase/src.
#
# uibase tests run on the private desktop (D:\mo2build\scripts\vis\desk.ps1) as run.ps1 does;
# the moc output comes from the uibase CMake build (-UibaseBuild), read only.
# Before each build and run, memguard.ps1 waits until RAM is under 70%; cl uses /MP4 at most.
param(
  [string]$Out = (Join-Path $env:TEMP 'mo2r-cppcov'),
  [string[]]$Only,
  [string]$Qt = 'D:\mo2build\Qt\6.12.0\msvc2022_64',
  [string]$Deps = 'D:\mo2build\mo\build',
  [string]$UibaseBuild = 'D:\mo2build\wt-pyperf-upstream\build\uibase\src',
  [string]$Python = 'C:\Program Files\Python312\python.exe',
  [string]$VsDevCmd = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat',
  [string]$MemGuard = 'D:\mo2build\scripts\memguard.ps1',
  [string]$Desk = 'D:\mo2build\scripts\vis\desk.ps1',
  [switch]$MergeOnly   # only merge -Out\runs\*.coverage again and report
)
$ErrorActionPreference = 'Stop'

$here = $PSScriptRoot
$source = (Resolve-Path (Join-Path $here '..\..\..')).Path
$uibaseSrc = Join-Path $source 'upstream\modorganizer_super\uibase\src'
$bsatkSrc = Join-Path $source 'upstream\modorganizer_super\bsatk\src'
$orgSrc = Join-Path $source 'modorganizer\src'
$cov = Join-Path (Split-Path (Split-Path $VsDevCmd)) 'IDE\Extensions\Microsoft\CodeCoverage.Console\Microsoft.CodeCoverage.Console.exe'
if (-not (Test-Path $cov)) { throw "Microsoft.CodeCoverage.Console not found: $cov" }
if ($Only) { $suites = @($Only | ForEach-Object { $_ -split ',' }) } else { $suites = @('uibase', 'bsatk', 'organizer') }

$bin = Join-Path $Out 'bin'
$objRoot = Join-Path $Out 'obj'
$runs = Join-Path $Out 'runs'
foreach ($d in $bin, $objRoot, $runs) { New-Item -ItemType Directory -Force $d | Out-Null }
$compiledList = Join-Path $Out 'compiled.txt'
if ($MergeOnly) { $suites = @() }
else {
  Get-ChildItem $runs -Filter *.coverage -ErrorAction SilentlyContinue | Remove-Item -Force
  Remove-Item $compiledList -ErrorAction SilentlyContinue
}

function Wait-Memory {
  & powershell -ExecutionPolicy Bypass -File $MemGuard | Out-Host
  if ($LASTEXITCODE -ne 0) { throw 'memguard: RAM stayed high, not starting' }
}

# the compiler's environment, once
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
  $envText = & cmd.exe /c "set `"PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%`" && `"$VsDevCmd`" -no_logo -arch=amd64 -host_arch=amd64 >nul && set"
  foreach ($line in $envText) {
    if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
  }
}

# cl and link through response files: the paths have spaces
function Invoke-Tool([string]$tool, [string]$name, [string[]]$arguments) {
  Wait-Memory
  $rsp = Join-Path $objRoot "$name.$tool.rsp"
  ($arguments | ForEach-Object { if ($_ -match '\s' -and $_ -notmatch '^"') { "`"$($_ -replace '\\$', '\\')`"" } else { $_ } }) -join "`r`n" |
    Set-Content -Encoding utf8 $rsp
  $log = & "$tool.exe" "@$rsp" 2>&1
  $code = $LASTEXITCODE
  $log | Set-Content -Encoding utf8 (Join-Path $objRoot "$name.$tool.log")
  if ($code -ne 0) {
    $log | Where-Object { "$_" -match '\b(error|fatal)\b' } | Select-Object -First 30 | Out-Host
    throw "$name`: $tool failed ($code)"
  }
}

$covCl = @('/nologo', '/MP4', '/c', '/std:c++20', '/EHsc', '/MD', '/Od', '/Ob0', '/Zi', '/W3', '/utf-8',
  '/Zc:__cplusplus', '/DWIN32', '/D_WINDOWS', '/DNDEBUG', '/DUNICODE', '/D_UNICODE', '/DNOMINMAX')
$covLink = @('/nologo', '/DEBUG', '/PROFILE', '/OPT:NOREF', '/OPT:NOICF', '/INCREMENTAL:NO', '/MACHINE:X64')

function Build-Binary([string]$name, [string[]]$sources, [string[]]$clFlags, [string[]]$linkFlags, [string]$output) {
  $obj = Join-Path $objRoot $name
  New-Item -ItemType Directory -Force $obj | Out-Null
  Get-ChildItem $obj -Filter *.obj -ErrorAction SilentlyContinue | Remove-Item -Force
  # the report doesn't count a compiled source as "never compiled" even when it has no code
  $sources | Add-Content -Encoding utf8 $compiledList
  Invoke-Tool 'cl' $name ($covCl + $clFlags + @("/Fo$obj\", "/Fd$obj\$name.cl.pdb") + $sources)
  $objs = Get-ChildItem $obj -Filter *.obj | ForEach-Object FullName
  Invoke-Tool 'link' $name ($covLink + $linkFlags + @("/OUT:$output", "/PDB:$([IO.Path]::ChangeExtension($output, '.pdb'))") + $objs)
  Write-Host "built $output"
}

function Instrument([string]$file) {
  & $cov instrument --nologo $file | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "instrument failed: $file" }
}

$qtInc = @('include', 'include\QtCore', 'include\QtGui', 'include\QtWidgets', 'include\QtNetwork', 'include\QtQml',
  'include\QtQmlIntegration', 'include\QtQuick', 'include\QtQuickWidgets', 'include\QtOpenGL', 'mkspecs\win32-msvc') |
  ForEach-Object { "/I$Qt\$_" }
$qtLib = @('Qt6Core', 'Qt6Gui', 'Qt6Widgets', 'Qt6Network', 'Qt6Qml', 'Qt6Quick', 'Qt6QuickWidgets', 'Qt6OpenGL') |
  ForEach-Object { "$Qt\lib\$_.lib" }
$boost = Join-Path $Deps 'boost_1_85_0'
$needUibase = ($suites -contains 'uibase') -or ($suites -contains 'organizer')

# ---- uibase.dll, coverage copy (sources + moc from the CMake build, flags of uibase.vcxproj)
if ($needUibase) {
  $autogen = Join-Path $UibaseBuild 'uibase_autogen'
  $mocs = Join-Path $autogen 'mocs_compilation_RelWithDebInfo.cpp'
  if (-not (Test-Path $mocs)) { throw "no moc output in ${autogen}: configure/build uibase with CMake first" }
  $uiSources = @(Get-ChildItem $uibaseSrc -Filter *.cpp | Where-Object Name -ne 'pch.cpp' | ForEach-Object FullName) + $mocs
  $uiFlags = @('/permissive-', '/wd4996', '/FIpch.h',
    "/I$autogen\include_RelWithDebInfo", "/I$(Join-Path $source 'upstream\deps\sip')", "/I$uibaseSrc",
    "/I$uibaseSrc\game_features", "/I$(Join-Path $source 'upstream\modorganizer_super\revamped_fluent')", "/I$boost",
    "/I$(Join-Path $Deps 'spdlog-v1.14.1\include')",
    '/D_CRT_SECURE_NO_WARNINGS', '/DBOOST_CONFIG_SUPPRESS_OUTDATED_MESSAGE', '/D_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING',
    '/DQT_MESSAGELOGCONTEXT', '/DGITID=\"coverage\"', '/DUIBASE_EXPORT', '/Duibase_EXPORTS', '/DQT_NO_DEBUG',
    '/D_ENABLE_EXTENDED_ALIGNED_STORAGE', '/DWIN64', '/D_WIN64', '/DSPDLOG_USE_STD_FORMAT',
    '/DQT_CORE_LIB', '/DQT_GUI_LIB', '/DQT_WIDGETS_LIB', '/DQT_NETWORK_LIB', '/DQT_QML_LIB', '/DQT_QUICK_LIB',
    '/DQT_QUICKWIDGETS_LIB', '/DQT_OPENGL_LIB', '/DQT_QMLINTEGRATION_LIB') + $qtInc
  $blib = "$boost\lib64-msvc-14.3\lib"
  $uiLink = @('/DLL', "/IMPLIB:$bin\uibase.lib", "/LIBPATH:$blib") + $qtLib + @(
    'version.lib', 'dbghelp.lib', 'user32.lib', 'ws2_32.lib', 'shell32.lib', 'mpr.lib', 'userenv.lib', 'd3d11.lib',
    'dxgi.lib', 'dxguid.lib', 'd3d12.lib', 'gdi32.lib', 'ole32.lib', 'oleaut32.lib', 'uuid.lib', 'comdlg32.lib', 'advapi32.lib')
  Build-Binary 'uibase' $uiSources $uiFlags $uiLink "$bin\uibase.dll"
  Instrument "$bin\uibase.dll"
}
$uibaseLib = "$bin\uibase.lib"

# test name, exe, arguments, on the private desktop?
$tests = @()

# ---- uibase tests (test_lines without AddressSanitizer here: it doesn't mix with the instrumentation)
if ($suites -contains 'uibase') {
  $dir = Join-Path $here 'uibase'
  foreach ($t in 'versioninfo', 'utility', 'fileversion', 'log', 'misc', 'lines') {
    $exe = "$bin\test_$t.exe"
    Build-Binary "test_$t" @("$dir\test_$t.cpp") (@('/permissive-', '/wd4996', '/DQT_NO_DEBUG', "/I$dir", "/I$uibaseSrc") + $qtInc) `
      (@($uibaseLib) + $qtLib[0..2] + @('version.lib', 'shell32.lib', 'ole32.lib', 'user32.lib')) $exe
    Instrument $exe
    $tests += @{ Name = "test_$t"; Exe = $exe; Args = @('--out', "$runs\test_$t.log"); Desk = $true }
  }
}

# ---- bsatk: its sources compiled in
if ($suites -contains 'bsatk') {
  $zlib = Join-Path $Deps 'zlib-v1.3.1'
  $lz4 = Join-Path $Deps 'lz4-v1.9.4'
  $exe = "$bin\bsatk_reader_test.exe"
  $srcs = @(Join-Path $here 'bsatk\bsatk_reader_test.cpp') + (Get-ChildItem $bsatkSrc -Filter *.cpp | ForEach-Object FullName)
  Build-Binary 'bsatk_reader_test' $srcs @('/permissive', '/D_CRT_SECURE_NO_WARNINGS', '/DBOOST_CONFIG_SUPPRESS_OUTDATED_MESSAGE',
    "/I$bsatkSrc", "/I$boost", "/I$Deps\DirectXTex\Include", "/I$zlib\include", "/I$lz4\lib") `
    @("/LIBPATH:$boost\lib64-msvc-14.3\lib", "$zlib\lib\zlibstatic.lib", "$lz4\bin\liblz4.lib") $exe
  Copy-Item "$lz4\bin\liblz4.dll" $bin -Force
  Instrument $exe
  $work = Join-Path $runs 'work-bsatk'
  New-Item -ItemType Directory -Force $work | Out-Null
  $tests += @{ Name = 'bsatk_reader_test'; Exe = $exe; Args = @($work); Desk = $false }
}

# ---- organizer: only the sources each test needs, as organizer\run.ps1 does
if ($suites -contains 'organizer') {
  $dir = Join-Path $here 'organizer'
  $fixtures = Join-Path $runs 'fixtures'
  New-Item -ItemType Directory -Force $fixtures | Out-Null
  Wait-Memory
  Push-Location $fixtures
  try {
    & cl.exe /nologo /LD /MD "$dir\fixtures\fake_proxy.cpp" /Fe:d3d11.dll | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'fake_proxy didn''t build' }
    & cl.exe /nologo /MD "$dir\fixtures\fake_game.cpp" /Fe:game.exe d3d11.lib | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'fake_game didn''t build' }
  } finally { Pop-Location }
  $orgFlags = @('/permissive-', '/DWIN32_LEAN_AND_MEAN', "/I$Qt\include", "/I$Qt\include\QtCore", "/I$orgSrc", "/I$uibaseSrc")
  $org = @(
    @{ Name = 'collection_names_test'; Src = @('collectionnames.cpp', 'collectionfiles.cpp', 'collectionmanifest.cpp');
       Libs = @("$Qt\lib\Qt6Core.lib", $uibaseLib); Args = @((Join-Path $runs 'work-names')) },
    @{ Name = 'download_url_test'; Src = @('downloadurl.cpp'); Libs = @("$Qt\lib\Qt6Core.lib", $uibaseLib); Args = @() },
    @{ Name = 'rootlaunch_test'; Src = @('rootlaunch.cpp', 'rootcopy.cpp', 'importpatch.cpp');
       Libs = @("$Qt\lib\Qt6Core.lib", 'advapi32.lib', 'user32.lib'); Args = @($fixtures, (Join-Path $runs 'work-rootlaunch')) }
  )
  foreach ($t in $org) {
    $exe = "$bin\$($t.Name).exe"
    Build-Binary $t.Name (@("$dir\$($t.Name).cpp") + ($t.Src | ForEach-Object { "$orgSrc\$_" })) $orgFlags $t.Libs $exe
    Instrument $exe
    $tests += @{ Name = $t.Name; Exe = $exe; Args = $t.Args; Desk = $false }
  }
}

# ---- run every test under collect (the instrumented uibase.dll sits next to the exes)
$env:PATH = "$bin;$Qt\bin;$env:PATH"
$failed = @()
foreach ($t in $tests) {
  Wait-Memory
  $covFile = "$runs\$($t.Name).coverage"
  if ($t.Desk) {
    $quoted = ($t.Args | ForEach-Object { "`"$_`"" }) -join ' '
    $line = "`"$cov`" collect --nologo -f coverage -o `"$covFile`" `"$($t.Exe)`" $quoted"
    & $Desk -CommandLine $line -WorkDir $bin -Wait -TimeoutMs 600000 | Out-Null
    $log = $t.Args[1]
    $text = if (Test-Path $log) { [IO.File]::ReadAllText($log) } else { '' }
    $summary = ($text -split "`n" | Where-Object { $_ -match "^$($t.Name): \d+ passed" } | Select-Object -First 1)
    if (-not $summary -or $summary -notmatch ' 0 failed') { $failed += $t.Name }
    Write-Host "$($t.Name): $summary"
  } else {
    Push-Location $bin
    try { & $cov collect --nologo -f coverage -o $covFile $t.Exe @($t.Args) | Select-Object -Last 3 | Out-Host } finally { Pop-Location }
    # collect returns the test's exit code
    if ($LASTEXITCODE -ne 0) { $failed += $t.Name }
  }
  if (-not (Test-Path $covFile)) { Write-Host "$($t.Name): no coverage file"; $failed += $t.Name }
}

# ---- merge and export
$files = Get-ChildItem $runs -Filter *.coverage | ForEach-Object FullName
if (-not $files) { throw 'no coverage collected' }$cobertura = Join-Path $Out 'coverage.cobertura.xml'
$ErrorActionPreference = 'Continue'
$mergeLog = & $cov merge --nologo @files -f cobertura -o $cobertura 2>&1
$code = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($code -ne 0) { $mergeLog | Select-Object -Last 10 | Out-Host; throw 'merge failed' }
Write-Host "cobertura: $cobertura"

& $Python -I (Join-Path $here 'coverage_report.py') $cobertura --source $source --compiled $compiledList --json (Join-Path $Out 'coverage-summary.json')
if ($failed) { Write-Host "tests that failed or gave no coverage: $($failed -join ', ')" }
exit $failed.Count
