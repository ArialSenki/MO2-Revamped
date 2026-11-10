<#
Builds what MO2 Revamped used to ship precompiled from Mod Organizer 2.5.2
(uibase, the plugins, plugin_python, archive, lootcli, helper) from
source\upstream, and USVFS (x64 and x86) from source\usvfs, and stages the
files the release takes from them.

  -OutputDirectory\build          CMake build tree of source\upstream
  -OutputDirectory\usvfs-x64|x86  CMake build trees of source\usvfs (vcpkg)
  -OutputDirectory\install        install tree: the runtime layout, import
                                  libraries and PDBs; the organizer is
                                  configured against it (MO2_INSTALL_PATH, and
                                  MO2_USVFS_OVERRIDE_ROOT for USVFS)
  -OutputDirectory\stage          the shipped files and upstream-manifest.json,
                                  for Build-Release.ps1 -UpstreamStageDirectory

Before staging, every binary is checked against source\upstream\abi, written
once from the official 2.5.2 binaries (-WriteAbiReference): uibase and USVFS
export the same symbols, each file keeps its DLL dependencies and its plugin
interface IDs, everything imported from uibase exists, and no local path is
left.
#>
[CmdletBinding()]
param(
    [string]$OutputDirectory,
    # vcpkg for USVFS's dependencies; Visual Studio's own by default
    [string]$VcpkgRoot,
    # folder with boost_1_85_0, spdlog-v1.14.1, zlib-v1.3.1, lz4-v1.9.4,
    # libbsarch-0.0.12-release-x64, DirectXTex, 7zip-26.01 and
    # libloot-0.23.0-win64, as mob prepares them
    [string]$ThirdPartyDirectory,
    [string]$QtDirectory,
    # CPython 3.12 with Include\ and PCBuild\amd64\ (python.exe, python312.lib)
    [string]$PythonDirectory,
    # writes source\upstream\abi from this folder of official 2.5.2 binaries
    # (a runtime tree) instead of building
    [string]$WriteAbiReference
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$upstreamSource = Join-Path $repoRoot 'source\upstream'
$abiRoot = Join-Path $upstreamSource 'abi'

# what the release takes from the build, by runtime path; Build-Release.ps1
# reads the same list and requires the stage to have exactly these files
$listed = foreach ($line in Get-Content -LiteralPath (Join-Path $upstreamSource 'shipped-files.txt')) {
    if ($line -match '^\s*(#|$)') { continue }
    $kind, $path = $line.Trim() -split '\s+', 2
    [pscustomobject]@{ Kind = $kind; Path = $path.Replace('/', '\') }
}
$nativeFiles = @($listed | Where-Object Kind -eq 'native' | ForEach-Object Path)
# the .NET libraries installer_omod restores from NuGet, shipped beside it
$dotNetFiles = @($listed | Where-Object Kind -eq 'dotnet' | ForEach-Object Path)
$shippedFiles = $nativeFiles + $dotNetFiles
if ($nativeFiles.Count -eq 0 -or $dotNetFiles.Count -eq 0 -or
    @($listed | Where-Object { $_.Kind -notin 'native', 'dotnet' }).Count) {
    throw 'source\upstream\shipped-files.txt lists no native or no dotnet files, or an unknown kind.'
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw "vswhere.exe was not found: $vswhere" }
$vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'No Visual Studio with the C++ x64 tools was found.' }
$vsDevCmd = Join-Path $vsRoot 'Common7\Tools\VsDevCmd.bat'
$msvcVersion = (Get-Content -LiteralPath (Join-Path $vsRoot 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt') -TotalCount 1).Trim()
$msvcBin = Join-Path $vsRoot "VC\Tools\MSVC\$msvcVersion\bin\Hostx64\x64"
$dumpbin = Join-Path $msvcBin 'dumpbin.exe'
foreach ($tool in @($vsDevCmd, $dumpbin)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Visual Studio tool was not found: $tool" }
}

# commands that need the developer environment run from a .cmd file: cmd.exe
# and Windows PowerShell disagree about nested quotes on a command line
function Invoke-InVsShell {
    param([string]$CommandLine, [string]$Arch = 'amd64', [string[]]$Setup = @())
    $script = Join-Path $outputRoot 'vs-step.cmd'
    $lines = @(
        '@echo off',
        'set NoDefaultCurrentDirectoryInExePath=',
        "call `"$vsDevCmd`" -no_logo -arch=$Arch -host_arch=amd64 || exit /b 1"
    ) + $Setup + @(
        # warnings on stderr would stop Windows PowerShell's native command handling
        "$CommandLine 2>&1"
    )
    [IO.File]::WriteAllLines($script, $lines, [Text.Encoding]::Default)
    & cmd.exe /d /c "`"$script`""
    if ($LASTEXITCODE -ne 0) { throw "Command failed ($LASTEXITCODE): $CommandLine" }
}

function Get-DumpbinLines {
    param([string]$Switch, [string]$Path)
    $out = & $dumpbin /nologo $Switch $Path
    if ($LASTEXITCODE -ne 0) { throw "dumpbin $Switch failed for $Path" }
    return $out
}

function Get-Exports {
    param([string]$Path)
    $names = foreach ($line in Get-DumpbinLines '/exports' $Path) {
        if ($line -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]{8}\s+(\S+)') { $Matches[1] }
    }
    return @($names | Sort-Object -Unique -CaseSensitive)
}

function Get-Dependents {
    param([string]$Path)
    $names = foreach ($line in Get-DumpbinLines '/dependents' $Path) {
        if ($line -match '^\s+(\S+\.(dll|DLL|exe))\s*$') { $Matches[1].ToLowerInvariant() }
    }
    return @($names | Sort-Object -Unique)
}

function Get-UibaseImports {
    param([string]$Path)
    $inUibase = $false
    $names = foreach ($line in Get-DumpbinLines '/imports' $Path) {
        # the summary after the import list has lines like "3000 .data"
        if ($line -match '^\s+Summary\s*$') { break }
        if ($line -match '^\s{4}(\S+\.dll)\s*$') { $inUibase = ($Matches[1] -ieq 'uibase.dll'); continue }
        if ($inUibase -and $line -match '^\s+[0-9A-F]+\s+(\S+)\s*$') { $Matches[1] }
    }
    return @($names | Sort-Object -Unique -CaseSensitive)
}

function Get-InterfaceIds {
    param([string]$Path)
    $bytes = [IO.File]::ReadAllBytes($Path)
    $text = [Text.Encoding]::ASCII.GetString($bytes) + [Text.Encoding]::Unicode.GetString($bytes)
    $ids = [regex]::Matches($text, 'com\.tannin\.ModOrganizer\.[A-Za-z]+/[0-9][0-9.]*') | ForEach-Object { $_.Value }
    return @($ids | Sort-Object -Unique)
}

function Get-BinaryRecord {
    param([string]$Path)
    $record = [ordered]@{
        dependents = @(Get-Dependents $Path)
        interfaces = @(Get-InterfaceIds $Path)
    }
    # the organizer and the hooked games call into USVFS by these names
    if ([IO.Path]::GetFileName($Path) -match '^usvfs_x(64|86)\.dll$') {
        $record.exports = @(Get-Exports $Path)
    }
    return $record
}

if ($WriteAbiReference) {
    $reference = [IO.Path]::GetFullPath($WriteAbiReference).TrimEnd('\')
    New-Item -ItemType Directory -Force -Path $abiRoot | Out-Null
    # read everything first, so a bad reference leaves the old files whole
    $uibaseExports = [string[]](Get-Exports (Join-Path $reference 'uibase.dll'))
    if (-not $uibaseExports.Count) { throw "uibase.dll in $reference exports nothing" }
    $records = [ordered]@{}
    foreach ($relative in $nativeFiles) {
        $path = Join-Path $reference $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Reference binary was not found: $path" }
        $record = Get-BinaryRecord $path
        if ($record.Contains('exports') -and -not $record.exports.Count) { throw "$relative in $reference exports nothing" }
        $records[$relative.Replace('\', '/')] = $record
    }
    # the NuGet libraries come out of the packages unchanged
    $hashes = foreach ($relative in $dotNetFiles) {
        $path = Join-Path $reference $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Reference file was not found: $path" }
        '{0}  {1}' -f (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant(), $relative.Replace('\', '/')
    }
    [IO.File]::WriteAllLines((Join-Path $abiRoot 'uibase-exports.txt'), $uibaseExports, [Text.UTF8Encoding]::new($false))
    $json = $records | ConvertTo-Json -Depth 4
    [IO.File]::WriteAllText((Join-Path $abiRoot 'binaries.json'), $json + "`n", [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllLines((Join-Path $abiRoot 'dotnet-sha256.txt'), [string[]]$hashes, [Text.UTF8Encoding]::new($false))
    Write-Host "ABI reference written to $abiRoot from $reference"
    return
}

$outputRoot = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
if ($outputRoot.StartsWith($repoRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "The output directory must be outside the repository: $outputRoot"
}
$thirdParty = [IO.Path]::GetFullPath($ThirdPartyDirectory).TrimEnd('\')
$qtRoot = [IO.Path]::GetFullPath($QtDirectory).TrimEnd('\')
$pythonRoot = [IO.Path]::GetFullPath($PythonDirectory).TrimEnd('\')
$roots = [ordered]@{
    BOOST_ROOT       = Join-Path $thirdParty 'boost_1_85_0'
    SPDLOG_ROOT      = Join-Path $thirdParty 'spdlog-v1.14.1'
    ZLIB_ROOT        = Join-Path $thirdParty 'zlib-v1.3.1'
    LZ4_ROOT         = Join-Path $thirdParty 'lz4-v1.9.4'
    LIBBSARCH_ROOT   = Join-Path $thirdParty 'libbsarch-0.0.12-release-x64'
    DIRECTXTEX_ROOT  = Join-Path $thirdParty 'DirectXTex'
    SEVENZ_ROOT      = Join-Path $thirdParty '7zip-26.01'
    LOOT_PATH        = Join-Path $thirdParty 'libloot-0.23.0-win64'
    QT_ROOT          = $qtRoot
    PYTHON_ROOT      = $pythonRoot
}
foreach ($entry in $roots.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $entry.Value -PathType Container)) {
        throw "$($entry.Key) was not found: $($entry.Value)"
    }
}
foreach ($file in @('Include\Python.h', 'PCBuild\amd64\python.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $pythonRoot $file) -PathType Leaf)) {
        throw "The Python directory has no $file`: $pythonRoot"
    }
}
foreach ($file in @('uibase-exports.txt', 'binaries.json', 'dotnet-sha256.txt')) {
    if (-not (Test-Path -LiteralPath (Join-Path $abiRoot $file) -PathType Leaf)) {
        throw "The ABI reference $file is missing from $abiRoot; write it with -WriteAbiReference."
    }
}

$buildRoot = Join-Path $outputRoot 'build'
$installRoot = Join-Path $outputRoot 'install'
$stageRoot = Join-Path $outputRoot 'stage'
foreach ($directory in @($buildRoot, $installRoot)) {
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}
if (Test-Path -LiteralPath $stageRoot) { Remove-Item -LiteralPath $stageRoot -Recurse -Force }
# a shipped file this build no longer installs must not survive from an earlier run
foreach ($relative in $shippedFiles) {
    $old = Join-Path (Join-Path $installRoot 'bin') $relative
    if (Test-Path -LiteralPath $old -PathType Leaf) { Remove-Item -LiteralPath $old -Force }
}

function ConvertTo-CMakePath { param([string]$Path) return $Path.Replace('\', '/') }

$defines = @(
    "-DMO2_INSTALL_PATH=$(ConvertTo-CMakePath $installRoot)",
    "-DBOOST_LIBRARYDIR=$(ConvertTo-CMakePath (Join-Path $roots.BOOST_ROOT 'lib64-msvc-14.3\lib'))",
    '-DCMAKE_DOTNET_TARGET_FRAMEWORK_VERSION=v4.8'
)
foreach ($entry in $roots.GetEnumerator()) {
    $defines += "-D$($entry.Key)=$(ConvertTo-CMakePath $entry.Value)"
}
# USVFS, both architectures, with its pinned vcpkg dependencies
$usvfsSource = Join-Path $repoRoot 'source\usvfs'
if (-not $VcpkgRoot) {
    $VcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { Join-Path $vsRoot 'VC\vcpkg' }
}
if (-not (Test-Path -LiteralPath (Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake') -PathType Leaf)) {
    throw "vcpkg was not found (scripts\buildsystems\vcpkg.cmake): $VcpkgRoot"
}
foreach ($usvfsArch in @(@{ Preset = 'x64'; Vs = 'amd64' }, @{ Preset = 'x86'; Vs = 'x86' })) {
    $usvfsBuild = Join-Path $outputRoot "usvfs-$($usvfsArch.Preset)"
    $setup = @("set `"VCPKG_ROOT=$VcpkgRoot`"", "cd /d `"$usvfsSource`" || exit /b 1")
    Invoke-InVsShell -Arch $usvfsArch.Vs -Setup $setup ("cmake --preset vs2022-windows-$($usvfsArch.Preset) -B `"$usvfsBuild`" " +
        "`"-DCMAKE_INSTALL_PREFIX=$(ConvertTo-CMakePath $installRoot)`" -DBUILD_TESTING=OFF")
    Invoke-InVsShell -Arch $usvfsArch.Vs -Setup $setup "cmake --build `"$usvfsBuild`" --config Release -- /nodeReuse:false /v:minimal /clp:ErrorsOnly"
    Invoke-InVsShell -Arch $usvfsArch.Vs -Setup $setup "cmake --install `"$usvfsBuild`" --config Release"
}

$configure = "cmake -S `"$upstreamSource`" -B `"$buildRoot`" -G `"Visual Studio 17 2022`" -A x64 " + (($defines | ForEach-Object { "`"$_`"" }) -join ' ')
Invoke-InVsShell $configure
Invoke-InVsShell "cmake --build `"$buildRoot`" --config RelWithDebInfo --parallel -- /restore /nodeReuse:false /v:minimal /clp:ErrorsOnly"
Invoke-InVsShell "cmake --install `"$buildRoot`" --config RelWithDebInfo"

# the ABI checks, on the installed files
$installBin = Join-Path $installRoot 'bin'
$reference = Get-Content -LiteralPath (Join-Path $abiRoot 'binaries.json') -Raw | ConvertFrom-Json
$referenceExports = @(Get-Content -LiteralPath (Join-Path $abiRoot 'uibase-exports.txt'))
$problems = New-Object System.Collections.Generic.List[string]

foreach ($relative in $shippedFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $installBin $relative) -PathType Leaf)) {
        $problems.Add("missing from the install tree: $relative")
    }
}
if ($problems.Count -eq 0) {
    $exports = @(Get-Exports (Join-Path $installBin 'uibase.dll'))
    $added = @($exports | Where-Object { $referenceExports -cnotcontains $_ })
    $removed = @($referenceExports | Where-Object { $exports -cnotcontains $_ })
    if ($added.Count -or $removed.Count) {
        $problems.Add("uibase.dll exports differ from 2.5.2: $($removed.Count) missing, $($added.Count) new (first: $(@($removed + $added)[0]))")
    }
    $exportSet = [Collections.Generic.HashSet[string]]::new([string[]]$exports, [StringComparer]::Ordinal)
    foreach ($relative in $nativeFiles) {
        $path = Join-Path $installBin $relative
        $expected = $reference.($relative.Replace('\', '/'))
        if (-not $expected) { $problems.Add("no ABI reference for $relative"); continue }
        $record = Get-BinaryRecord $path
        if (Compare-Object @($expected.dependents) @($record.dependents)) {
            $problems.Add("$relative depends on [$($record.dependents -join ', ')] instead of [$($expected.dependents -join ', ')]")
        }
        if (Compare-Object @($expected.interfaces) @($record.interfaces)) {
            $problems.Add("$relative declares [$($record.interfaces -join ', ')] instead of [$($expected.interfaces -join ', ')]")
        }
        if ($record.Contains('exports') -and -not @($expected.exports).Count) {
            $problems.Add("the ABI reference has no exports for $relative; write it again with -WriteAbiReference")
        } elseif ($expected.exports) {
            $diff = @(Compare-Object -CaseSensitive @($expected.exports) @($record.exports))
            if ($record.exports.Count -eq 0 -or $diff.Count) {
                $problems.Add("$relative exports differ from 2.5.2 ($($diff.Count) differences, first: $($diff[0].InputObject))")
            }
        }
        if ($relative -ne 'uibase.dll') {
            $missing = @(Get-UibaseImports $path | Where-Object { -not $exportSet.Contains($_) })
            if ($missing.Count) { $problems.Add("$relative imports $($missing.Count) symbols uibase.dll lacks (first: $($missing[0]))") }
        }
    }
}

# the NuGet libraries, byte for byte the ones 2.5.2 shipped
foreach ($line in Get-Content -LiteralPath (Join-Path $abiRoot 'dotnet-sha256.txt')) {
    if (-not $line.Trim()) { continue }
    $hash, $relative = $line.Trim() -split '\s+', 2
    $path = Join-Path $installBin $relative.Replace('/', '\')
    if ((Test-Path -LiteralPath $path -PathType Leaf) -and
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $hash) {
        $problems.Add("$relative is not the NuGet library 2.5.2 shipped")
    }
}

# no folder of this machine in what ships
$needles = @($env:USERPROFILE, $repoRoot, $outputRoot, $thirdParty, $qtRoot, $pythonRoot, $VcpkgRoot) |
    Where-Object { $_ } | ForEach-Object {
        $p = [IO.Path]::GetFullPath($_).TrimEnd('\', '/')
        $p
        $p.Replace('\', '/')
    } | Select-Object -Unique
foreach ($relative in $shippedFiles) {
    $path = Join-Path $installBin $relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
    $bytes = [IO.File]::ReadAllBytes($path)
    $ascii = [Text.Encoding]::ASCII.GetString($bytes)
    $unicode = [Text.Encoding]::Unicode.GetString($bytes)
    foreach ($needle in $needles) {
        if ($ascii.IndexOf($needle, [StringComparison]::OrdinalIgnoreCase) -ge 0 -or
            $unicode.IndexOf($needle, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            $problems.Add("$relative contains the local path $needle")
            break
        }
    }
}

if ($problems.Count) {
    $problems | ForEach-Object { Write-Host "  $_" }
    throw "The upstream build failed $($problems.Count) checks; nothing was staged."
}

New-Item -ItemType Directory -Force -Path $stageRoot | Out-Null
$files = [ordered]@{}
foreach ($relative in $shippedFiles) {
    $destination = Join-Path $stageRoot $relative
    New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($destination)) | Out-Null
    Copy-Item -LiteralPath (Join-Path $installBin $relative) -Destination $destination -Force
    $files[$relative.Replace('\', '/')] = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
}
$compiler = "MSVC $msvcVersion"
$sourceCommit = (& git -C $repoRoot rev-parse HEAD)
if ($LASTEXITCODE -ne 0 -or -not $sourceCommit) { throw "git could not read the commit of $repoRoot" }
$sourceChanged = [bool](& git -C $repoRoot status --porcelain -- source/upstream source/usvfs)
if ($LASTEXITCODE -ne 0) { throw "git could not read the status of $repoRoot" }
$manifest = [ordered]@{
    format        = 2
    sources       = @('source/upstream', 'source/usvfs')
    sourceCommit  = $sourceCommit
    sourceChanged = $sourceChanged
    compiler      = "$compiler".Trim()
    files         = $files
}
[IO.File]::WriteAllText((Join-Path $stageRoot 'upstream-manifest.json'), ($manifest | ConvertTo-Json -Depth 4) + "`n", [Text.UTF8Encoding]::new($false))
Write-Host "Upstream build checked and staged: $($files.Count) files in $stageRoot"
