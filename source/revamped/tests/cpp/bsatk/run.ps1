# Builds bsatk_reader_test.cpp with cl and runs it.
#
#   run.ps1                       links bsatk.lib from an existing CMake build
#   run.ps1 -Asan                 compiles bsatk's sources in, with AddressSanitizer
#                                 (catches out-of-bounds reads/writes the plain build
#                                 can't see)
#   run.ps1 -BsatkLib <lib>       another bsatk.lib (build it first, for example
#                                 cmake --build <build> --config RelWithDebInfo --target bsatk)
#
# zlib, lz4 and boost come from the dependencies of the MO2 build (-Deps);
# nothing is installed or downloaded. Output and test files go to -OutDir.
param(
  [string]$BsatkLib = 'D:\mo2build\wt-pyperf-upstream\build\bsatk\src\RelWithDebInfo\bsatk.lib',
  [string]$Deps = 'D:\mo2build\mo\build',
  [string]$OutDir = '',
  [switch]$Asan
)
$ErrorActionPreference = 'Continue'

$here = $PSScriptRoot
$repo = (Resolve-Path (Join-Path $here '..\..\..\..\..')).Path
$bsatkSrc = Join-Path $repo 'source\upstream\modorganizer_super\bsatk\src'
if (-not $OutDir) {
  $OutDir = Join-Path $env:TEMP 'mo2r-bsatk-reader-test'
}
$build = Join-Path $OutDir ($(if ($Asan) { 'build-asan' } else { 'build' }))
$work = Join-Path $OutDir ($(if ($Asan) { 'work-asan' } else { 'work' }))
New-Item -ItemType Directory -Force $build, $work | Out-Null

$vsdev = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path $vsdev)) {
  $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  $vs = & $vswhere -latest -products * -property installationPath
  $vsdev = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'
}

$boost = Join-Path $Deps 'boost_1_85_0'
$zlib = Join-Path $Deps 'zlib-v1.3.1'
$lz4 = Join-Path $Deps 'lz4-v1.9.4'
$inc = @($bsatkSrc, $boost, (Join-Path $Deps 'DirectXTex\Include'), (Join-Path $zlib 'include'),
  (Join-Path $lz4 'lib')) | ForEach-Object { "/I`"$_`"" }

$sources = @("`"$(Join-Path $here 'bsatk_reader_test.cpp')`"")
$libs = @("`"$(Join-Path $zlib 'lib\zlibstatic.lib')`"", "`"$(Join-Path $lz4 'bin\liblz4.lib')`"")
$flags = '/nologo /EHsc /std:c++20 /MD /O2 /Zi /W3 /permissive /DWIN32 /DNOMINMAX /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /DBOOST_CONFIG_SUPPRESS_OUTDATED_MESSAGE'
if ($Asan) {
  # boost_thread is built without ASan: the container annotations must match it
  $flags += ' /fsanitize=address /D_DISABLE_VECTOR_ANNOTATION /D_DISABLE_STRING_ANNOTATION'
  $sources += Get-ChildItem $bsatkSrc -Filter *.cpp | ForEach-Object { "`"$($_.FullName)`"" }
} else {
  if (-not (Test-Path $BsatkLib)) { throw "bsatk.lib not found: $BsatkLib" }
  $libs = @("`"$BsatkLib`"") + $libs
}

$exe = Join-Path $build 'bsatk_reader_test.exe'
# through a .cmd file: cmd's quoting of a long /c line is too fragile
$script = Join-Path $build 'build.cmd'
@(
  '@echo off',
  "call `"$vsdev`" -no_logo -arch=amd64 -host_arch=amd64 >nul 2>nul",
  "cd /d `"$build`"",
  "cl $flags $($inc -join ' ') $($sources -join ' ') /Fe`"$exe`" /link /LIBPATH:`"$boost\lib64-msvc-14.3\lib`" $($libs -join ' ')",
  'exit /b %errorlevel%'
) | Set-Content -Encoding ascii $script
cmd /c "`"$script`"" 2>&1 | Write-Host
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }

Copy-Item (Join-Path $lz4 'bin\liblz4.dll') $build -Force
if ($Asan) {
  $asanDll = Get-ChildItem (Join-Path (Split-Path (Split-Path (Split-Path $vsdev))) 'VC\Tools\MSVC') -Recurse -Filter 'clang_rt.asan_dynamic-x86_64.dll' |
    Where-Object { $_.FullName -like '*\Hostx64\x64\*' } | Sort-Object FullName -Descending | Select-Object -First 1
  Copy-Item $asanDll.FullName $build -Force
  # a huge allocation is a failure too, as the guard in the plain build makes it
  $env:ASAN_OPTIONS = 'max_allocation_size_mb=256:allocator_may_return_null=0:detect_leaks=0'
}

& $exe $work
$code = $LASTEXITCODE
Write-Host "exit code $code"
exit $code
