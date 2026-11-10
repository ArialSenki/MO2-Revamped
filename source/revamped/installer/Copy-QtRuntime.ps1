<#
.SYNOPSIS
Copies the Qt runtime MO2 Revamped needs from an official Qt installation into
an MO2 folder, in MO2's layout.

.DESCRIPTION
MO2 keeps the Qt libraries in dlls\ (with QtWebEngineProcess.exe and a qt.conf
whose prefix is MO2's folder), and the Qt plugins, QML modules, WebEngine
resources and translations at its root. The Qt libraries copied are the ones
MO2's own binaries, the Qt plugins it ships and QtWebEngineProcess.exe import,
followed through their own imports; nothing else.

The Qt files already in the destination are replaced; Qt libraries there that
nothing imports any more are removed.
#>
[CmdletBinding()]
param(
    # Qt 6.12.0 msvc2022_64 with Qt WebEngine 6.140.0
    [Parameter(Mandatory)] [string]$QtDirectory,
    # the MO2 folder (with ModOrganizer.exe, uibase.dll and plugins\)
    [Parameter(Mandatory)] [string]$Destination,
    # dumpbin.exe of the Visual Studio C++ tools
    [Parameter(Mandatory)] [string]$Dumpbin,
    # MO2's binaries are built against this Qt: an older one fails to load them
    [string]$ExpectedVersion = '6.12.0'
)

$ErrorActionPreference = 'Stop'
$qt = [IO.Path]::GetFullPath($QtDirectory).TrimEnd('\')
$dest = [IO.Path]::GetFullPath($Destination).TrimEnd('\')
foreach ($required in @('bin\Qt6Core.dll', 'bin\QtWebEngineProcess.exe', 'plugins\platforms\qwindows.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $qt $required) -PathType Leaf)) { throw "$qt has no $required" }
}
$info = (Get-Item -LiteralPath (Join-Path $qt 'bin\Qt6Core.dll')).VersionInfo
$found = '{0}.{1}.{2}' -f $info.ProductMajorPart, $info.ProductMinorPart, $info.ProductBuildPart
if ($found -ne $ExpectedVersion) { throw "$qt is Qt $found, not $ExpectedVersion" }

# the Qt plugins, QML modules and data MO2 ships, as Qt lays them out
$pluginFiles = @(
    'platforms\qwindows.dll',
    'styles\qmodernwindowsstyle.dll',
    'tls\qcertonlybackend.dll', 'tls\qopensslbackend.dll', 'tls\qschannelbackend.dll',
    'imageformats\qgif.dll', 'imageformats\qicns.dll', 'imageformats\qico.dll', 'imageformats\qjpeg.dll',
    'imageformats\qsvg.dll', 'imageformats\qtga.dll', 'imageformats\qtiff.dll', 'imageformats\qwbmp.dll',
    'imageformats\qwebp.dll'
)
# uibase's QML panels need QtQml (with Models and WorkerScript) and QtQuick:
# the modules 2.5.2 shipped, release builds only
$qmlModules = @(
    'QtQml\qmldir', 'QtQml\qmlplugin.dll',
    'QtQml\Models\qmldir', 'QtQml\Models\modelsplugin.dll',
    'QtQml\WorkerScript\qmldir', 'QtQml\WorkerScript\workerscriptplugin.dll',
    'QtQuick\qmldir', 'QtQuick\qtquick2plugin.dll'
)

function Get-Imports([string]$Path) {
    $lines = & $Dumpbin /nologo /dependents $Path
    if ($LASTEXITCODE -ne 0) { throw "dumpbin failed on $Path" }
    $lines | Where-Object { $_ -match '^\s+(\S+\.dll)\s*$' } | ForEach-Object { $matches[1] }
}

# what imports Qt: MO2's binaries and the Qt plugins and QML plugins it ships
# (by the path below the destination: Qt's own folders and an installation's
# user data are not MO2's binaries)
$roots = @(Get-ChildItem -LiteralPath $dest -Recurse -File |
    Where-Object {
        $relative = $_.FullName.Substring($dest.Length).TrimStart('\')
        $_.Extension -in '.exe', '.dll', '.pyd' -and $_.Name -notlike 'Qt6*.dll' -and
        $relative -notmatch '^(dlls\\(imageformats|tls)|platforms|styles|qml|mods|downloads|overwrite|profiles|webcache|crashDumps|logs)\\'
    } |
    ForEach-Object FullName)
$roots += $pluginFiles | ForEach-Object { Join-Path $qt "plugins\$_" }
$qmlFiles = foreach ($module in $qmlModules) { Get-Item -LiteralPath (Join-Path $qt "qml\$module") }
$roots += $qmlFiles | Where-Object Extension -eq '.dll' | ForEach-Object FullName
$roots += Join-Path $qt 'bin\QtWebEngineProcess.exe'

$needed = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
$queue = New-Object 'System.Collections.Generic.Queue[string]'
foreach ($root in $roots) { $queue.Enqueue($root) }
$seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
while ($queue.Count) {
    $file = $queue.Dequeue()
    if (-not $seen.Add($file)) { continue }
    foreach ($import in Get-Imports $file) {
        $qtLib = Join-Path $qt "bin\$import"
        if ($import -like 'Qt6*.dll' -and (Test-Path -LiteralPath $qtLib -PathType Leaf) -and $needed.Add($import)) {
            $queue.Enqueue($qtLib)
        }
    }
}

$dlls = Join-Path $dest 'dlls'
New-Item -ItemType Directory -Force -Path $dlls | Out-Null
# Qt libraries an earlier Qt left that nothing imports now
foreach ($old in Get-ChildItem -LiteralPath $dlls -Filter 'Qt6*.dll' -File) {
    if (-not $needed.Contains($old.Name)) { Remove-Item -LiteralPath $old.FullName -Force; Write-Host "removed dlls\$($old.Name)" }
}
foreach ($name in $needed) { Copy-Item -LiteralPath (Join-Path $qt "bin\$name") -Destination $dlls -Force }
# software OpenGL and the shader compiler Qt falls back to
foreach ($name in @('d3dcompiler_47.dll', 'opengl32sw.dll')) {
    Copy-Item -LiteralPath (Join-Path $qt "bin\$name") -Destination $dlls -Force
}
Copy-Item -LiteralPath (Join-Path $qt 'bin\QtWebEngineProcess.exe') -Destination $dlls -Force

# MO2's layout: the platform and style plugins at its root, the image format
# and TLS plugins in dlls\ (the plugin path MO2 adds); each of those folders
# holds only this Qt's plugins, or Qt would load an older one beside them
$pluginRoot = @{ platforms = $dest; styles = $dest; imageformats = $dlls; tls = $dlls }
foreach ($folder in $pluginRoot.Keys) {
    foreach ($base in @($dest, $dlls)) {
        $old = Join-Path $base $folder
        if (Test-Path -LiteralPath $old) { Remove-Item -LiteralPath $old -Recurse -Force }
    }
}
foreach ($relative in $pluginFiles) {
    $folder = ($relative -split '\\')[0]
    $target = Join-Path $pluginRoot[$folder] $relative
    New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
    Copy-Item -LiteralPath (Join-Path $qt "plugins\$relative") -Destination $target -Force
}
# the qml folder is Qt's alone: made again from this Qt
$qmlDest = Join-Path $dest 'qml'
if (Test-Path -LiteralPath $qmlDest) { Remove-Item -LiteralPath $qmlDest -Recurse -Force }
foreach ($file in $qmlFiles) {
    $relative = $file.FullName.Substring((Join-Path $qt 'qml').Length).TrimStart('\')
    $target = Join-Path $qmlDest $relative
    New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $target -Force
}

# WebEngine's resources and its English locale (MO2's web view shows Nexus)
$resources = Join-Path $dest 'resources'
New-Item -ItemType Directory -Force -Path $resources | Out-Null
# (not the debug builds' .debug.pak and .debug.bin, which only Qt's debug libraries read)
Get-ChildItem -LiteralPath (Join-Path $qt 'resources') -File | Where-Object Name -notmatch '\.debug\.' |
    Copy-Item -Destination $resources -Force
Get-ChildItem -LiteralPath $resources -File | Where-Object Name -match '\.debug\.' | Remove-Item -Force
$locales = Join-Path $dest 'translations\qtwebengine_locales'
New-Item -ItemType Directory -Force -Path $locales | Out-Null
Copy-Item -LiteralPath (Join-Path $qt 'translations\qtwebengine_locales\en-US.pak') -Destination $locales -Force
# Qt's own translations for the languages MO2 ships
$translations = Join-Path $dest 'translations'
Get-ChildItem -LiteralPath (Join-Path $qt 'translations') -File -Filter '*.qm' |
    Where-Object { $_.Name -match '^(qt|qtbase)_[a-zA-Z_]+\.qm$' -and $_.Name -notlike 'qt_help_*' } |
    Copy-Item -Destination $translations -Force

# dlls\ is a private assembly: Windows loads from it only the files its manifest
# names, so the manifest lists exactly what is there now
$names = Get-ChildItem -LiteralPath $dlls -Filter '*.dll' -File | ForEach-Object Name | Sort-Object { $_.ToLowerInvariant() }
$manifest = @(
    '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
    '<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">',
    '  <assemblyIdentity type="win32" name="dlls" version="1.0.0.0" processorArchitecture="x86" />'
) + ($names | ForEach-Object { "  <file name=`"$_`" />" }) + '</assembly>'
[IO.File]::WriteAllLines((Join-Path $dlls 'dlls.manifest'), [string[]]$manifest, [Text.UTF8Encoding]::new($false))

"Qt $((Get-Item (Join-Path $qt 'bin\Qt6Core.dll')).VersionInfo.ProductVersion): $($needed.Count) libraries in dlls\, $($names.Count) files in its manifest"
($needed | Sort-Object) -join ', '
