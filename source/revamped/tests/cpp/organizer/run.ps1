# MO2 Revamped: builds and runs the organizer's C++ unit tests (not shipped).
# Each test compiles only the organizer sources it tests, against Qt Core and
# the uibase build; nothing of the organizer is linked whole.
#   powershell -ExecutionPolicy Bypass -File run.ps1 [-Out <folder>] [-Asan] [-Qt <msvc2022_64 folder>] [-UibaseBuild <folder with uibase.lib>]
# Exit code: 0 when every test passed.
param(
  [string]$Out = (Join-Path $env:TEMP 'mo2r-cpp-organizer'),
  [string]$Qt = 'D:\mo2build\Qt\6.12.0\msvc2022_64',
  [string]$UibaseBuild = 'D:\mo2build\wt-pyperf-upstream\build\uibase\src\RelWithDebInfo',
  [switch]$Asan,
  [string]$VsDevCmd = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$source = (Resolve-Path (Join-Path $here '..\..\..\..')).Path
$src = Join-Path $source 'modorganizer\src'
$uibaseSrc = Join-Path $source 'upstream\modorganizer_super\uibase\src'

# the compiler's environment, once per shell
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
  $envText = & cmd.exe /c "set `"PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%`" && `"$VsDevCmd`" -no_logo -arch=amd64 -host_arch=amd64 >nul && set"
  foreach ($line in $envText) {
    if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
  }
}

New-Item -ItemType Directory -Force $Out | Out-Null
$fixtures = Join-Path $Out 'fixtures'
New-Item -ItemType Directory -Force $fixtures | Out-Null

$common = @('/nologo', '/std:c++20', '/EHsc', '/permissive-', '/utf-8', '/MD', '/Zi', '/W3', '/Zc:__cplusplus',
  '/DUNICODE', '/D_UNICODE', '/DNOMINMAX', '/DWIN32_LEAN_AND_MEAN',
  "/I$Qt\include", "/I$Qt\include\QtCore", "/I$src", "/I$uibaseSrc")
if ($Asan) { $common += '/fsanitize=address' }
$qtCore = "$Qt\lib\Qt6Core.lib"
$uibase = "$UibaseBuild\uibase.lib"

function Build-Test([string]$name, [string[]]$sources, [string[]]$libs) {
  $obj = Join-Path $Out "obj-$name"
  New-Item -ItemType Directory -Force $obj | Out-Null
  & cl.exe @common "/Fo$obj\\" "/Fd$obj\\" "/Fe$Out\$name.exe" (Join-Path $here "$name.cpp") @sources /link @libs | Out-Host
  if ($LASTEXITCODE -ne 0) { throw "$name didn't build" }
}

# the stand-in game exe and proxy DLL of rootlaunch_test
Push-Location $fixtures
try {
  & cl.exe /nologo /LD /MD "$here\fixtures\fake_proxy.cpp" /Fe:d3d11.dll | Out-Host
  if ($LASTEXITCODE -ne 0) { throw 'fake_proxy didn''t build' }
  & cl.exe /nologo /MD "$here\fixtures\fake_game.cpp" /Fe:game.exe d3d11.lib | Out-Host
  if ($LASTEXITCODE -ne 0) { throw 'fake_game didn''t build' }
} finally { Pop-Location }

Build-Test 'collection_names_test' @("$src\collectionnames.cpp", "$src\collectionfiles.cpp", "$src\collectionmanifest.cpp") @($qtCore, $uibase)
Build-Test 'download_url_test' @("$src\downloadurl.cpp") @($qtCore, $uibase)
Build-Test 'nexus_oauth_test' @("$src\nexusoauthcore.cpp") @($qtCore)
Build-Test 'wabbajack_merge_test' @("$src\wabbajackfiles.cpp", "$src\wabbajackhash.cpp", "$src\collectionfiles.cpp") @($qtCore, $uibase)
Build-Test 'rootlaunch_test' @("$src\rootlaunch.cpp", "$src\rootcopy.cpp", "$src\importpatch.cpp") @($qtCore, 'advapi32.lib', 'user32.lib')

# Qt and uibase from their own folders
$env:PATH = "$Qt\bin;$UibaseBuild;$env:PATH"
$failed = 0
$runs = @(
  @{ Name = 'collection_names_test'; Args = @((Join-Path $Out 'work-names')) },
  @{ Name = 'download_url_test'; Args = @() },
  @{ Name = 'nexus_oauth_test'; Args = @() },
  @{ Name = 'wabbajack_merge_test'; Args = @((Join-Path $Out 'work-wabbajack-merge')) },
  @{ Name = 'rootlaunch_test'; Args = @($fixtures, (Join-Path $Out 'work-rootlaunch')) }
)
foreach ($t in $runs) {
  & (Join-Path $Out "$($t.Name).exe") @($t.Args) | Out-Host
  if ($LASTEXITCODE -ne 0) { $failed++; Write-Host "$($t.Name) FAILED ($LASTEXITCODE)" }
}
if ($failed -eq 0) { Write-Host 'all organizer tests passed' } else { Write-Host "$failed test program(s) failed" }
exit $failed
