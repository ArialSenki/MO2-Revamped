param(
    [Parameter(Mandatory = $true)]
    [string]$RuntimeDirectory,
    [Parameter(Mandatory = $true)]
    [string]$OrganizerExecutable,
    # nxmhandler.exe built from source/nxmhandler (the Fluent handler window)
    [Parameter(Mandatory = $true)]
    [string]$NxmHandlerExecutable,
    # the stage Build-UpstreamPlugins.ps1 makes from source\upstream: uibase, the
    # plugins, plugin_python, archive, lootcli and helper replace the copies the
    # runtime carries from Mod Organizer 2.5.2
    [Parameter(Mandatory = $true)]
    [string]$UpstreamStageDirectory,
    [Parameter(Mandatory = $true)]
    [string]$ZigPath,
    [Parameter(Mandatory = $true)]
    [string]$OpenSslRuntimeDirectory,
    [Parameter(Mandatory = $true)]
    [string]$PythonRuntimeDirectory,
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [Parameter(Mandatory = $true)]
    [string]$QtSdkDirectory,
    # the official Qt 6.12.0 msvc2022_64 with Qt WebEngine 6.140.0: MO2's Qt
    # runtime comes from it (Copy-QtRuntime.ps1), not from the runtime folder
    [Parameter(Mandatory = $true)]
    [string]$QtRuntimeDirectory,
    [string]$CscPath = "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
)

$ErrorActionPreference = 'Stop'
$version = '1.1.0-beta.8'
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$runtimeRoot = [System.IO.Path]::GetFullPath($RuntimeDirectory).TrimEnd('\')
$organizerPath = [System.IO.Path]::GetFullPath($OrganizerExecutable)
$nxmHandlerPath = [System.IO.Path]::GetFullPath($NxmHandlerExecutable)
$upstreamStageRoot = [System.IO.Path]::GetFullPath($UpstreamStageDirectory).TrimEnd('\')
$openSslRuntimeRoot = [System.IO.Path]::GetFullPath($OpenSslRuntimeDirectory).TrimEnd('\')
$pythonRuntimeRoot = [System.IO.Path]::GetFullPath($PythonRuntimeDirectory).TrimEnd('\')
$outputRoot = [System.IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
$qtSdkRoot = [System.IO.Path]::GetFullPath($QtSdkDirectory).TrimEnd('\')
$qtRuntimeRoot = [System.IO.Path]::GetFullPath($QtRuntimeDirectory).TrimEnd('\')

# the update notice compares GitHub releases with the version built into
# ModOrganizer.exe; a release whose header or executable still names the last
# one would tell its users to update to themselves
$versionHeader = Join-Path $repoRoot 'source\modorganizer\src\revampedversion.h'
if ((Get-Content -LiteralPath $versionHeader -Raw) -notmatch ('(?m)^\s*#define\s+MO2R_VERSION_STRING\s+"' + [regex]::Escape($version) + '"')) {
    throw "source\modorganizer\src\revampedversion.h does not name $version; update MO2R_VERSION_STRING and rebuild ModOrganizer.exe."
}
if (Test-Path -LiteralPath $organizerPath -PathType Leaf) {
    # Qt keeps the string as UTF-16 in the executable
    $organizerText = [Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes($organizerPath))
    if (-not $organizerText.Contains($version)) {
        throw "$organizerPath was not built with MO2R_VERSION_STRING $version; rebuild it from this source."
    }
    Remove-Variable organizerText
}

foreach ($path in @($runtimeRoot, $organizerPath, $nxmHandlerPath, $ZigPath, $openSslRuntimeRoot, $pythonRuntimeRoot, $qtRuntimeRoot, $CscPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -and
        -not (Test-Path -LiteralPath $path -PathType Container)) {
        throw "Required build input was not found: $path"
    }
}
if (-not (Test-Path -LiteralPath (Join-Path $runtimeRoot 'ModOrganizer.exe') -PathType Leaf)) {
    throw "The runtime directory does not contain ModOrganizer.exe: $runtimeRoot"
}
if (-not (Test-Path -LiteralPath $organizerPath -PathType Leaf)) {
    throw "The rebuilt ModOrganizer.exe was not found: $organizerPath"
}
if (-not (Test-Path -LiteralPath $nxmHandlerPath -PathType Leaf) -or
    [IO.Path]::GetFileName($nxmHandlerPath) -ine 'nxmhandler.exe') {
    throw "The rebuilt nxmhandler.exe was not found: $nxmHandlerPath"
}
# the upstream nxmhandler from the runtime would bring back the old window
if ((Get-Item -LiteralPath $nxmHandlerPath).VersionInfo.ProductName -notlike '*MO2 Revamped*') {
    throw "nxmhandler.exe is not the MO2 Revamped build from source\nxmhandler: $nxmHandlerPath"
}
# the upstream stage: every file its manifest lists, unchanged since it was
# built, from the source\upstream this checkout has
$upstreamManifestPath = Join-Path $upstreamStageRoot 'upstream-manifest.json'
if (-not (Test-Path -LiteralPath $upstreamManifestPath -PathType Leaf)) {
    throw "The upstream stage has no upstream-manifest.json; build it with Build-UpstreamPlugins.ps1: $upstreamStageRoot"
}
$upstreamManifest = Get-Content -LiteralPath $upstreamManifestPath -Raw | ConvertFrom-Json
if ($upstreamManifest.sourceChanged) {
    throw 'The upstream stage was built from uncommitted changes in source\upstream or source\usvfs; commit them and build it again.'
}

function Get-GitOutput {
    # git's own errors would stop the script with a bare "fatal:" line
    param([string[]]$Arguments)
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & git -C $repoRoot @Arguments 2>$null
        if ($LASTEXITCODE -ne 0) { return $null }
        return $output
    } finally {
        $ErrorActionPreference = $previous
    }
}

foreach ($sourceFolder in @('source/upstream', 'source/usvfs')) {
    $builtTree = Get-GitOutput @('rev-parse', "$($upstreamManifest.sourceCommit):$sourceFolder")
    $currentTree = Get-GitOutput @('rev-parse', "HEAD:$sourceFolder")
    if (-not $currentTree) {
        throw "This checkout is not a git repository with $sourceFolder committed; the upstream stage cannot be matched to it."
    }
    if (-not $builtTree -or $builtTree -ne $currentTree -or (Get-GitOutput @('status', '--porcelain', '--', $sourceFolder))) {
        throw "The upstream stage was built from another $sourceFolder than this checkout has; build it again."
    }
}
# exactly the files source\upstream\shipped-files.txt names: a stage that left
# one out would let the 2.5.2 runtime's copy through
$upstreamFiles = @(Get-Content -LiteralPath (Join-Path $repoRoot 'source\upstream\shipped-files.txt') |
    Where-Object { $_ -notmatch '^\s*(#|$)' } |
    ForEach-Object { ($_.Trim() -split '\s+', 2)[1].Replace('/', '\') })
$stagedFiles = @($upstreamManifest.files.PSObject.Properties | ForEach-Object { $_.Name.Replace('/', '\') })
if ($upstreamFiles.Count -eq 0 -or (Compare-Object $upstreamFiles $stagedFiles)) {
    throw 'The upstream stage does not have exactly the files source\upstream\shipped-files.txt lists; build it again.'
}
# the Python plugins are copied as they are on disk: only committed sources
if (Get-GitOutput @('status', '--porcelain', '--', 'source/mo2-python-plugins')) {
    throw 'source\mo2-python-plugins has uncommitted changes; commit them before building a release.'
}
foreach ($property in $upstreamManifest.files.PSObject.Properties) {
    $file = Join-Path $upstreamStageRoot $property.Name.Replace('/', '\')
    if (-not (Test-Path -LiteralPath $file -PathType Leaf) -or
        (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $property.Value) {
        throw "The upstream stage file $($property.Name) is missing or changed since it was built."
    }
}

if (-not (Test-Path -LiteralPath $openSslRuntimeRoot -PathType Container)) {
    throw "The OpenSSL runtime directory was not found: $openSslRuntimeRoot"
}
if (-not (Test-Path -LiteralPath $pythonRuntimeRoot -PathType Container)) {
    throw "The Python runtime overlay directory was not found: $pythonRuntimeRoot"
}
if (-not (Test-Path -LiteralPath $CscPath -PathType Leaf)) {
    throw ".NET Framework C# compiler was not found: $CscPath"
}
if (Test-Path -LiteralPath $outputRoot) {
    $existing = @(Get-ChildItem -LiteralPath $outputRoot -Force)
    if ($existing.Count -ne 0) {
        throw "Output directory must be new or empty; no files were changed: $outputRoot"
    }
} else {
    New-Item -ItemType Directory -Path $outputRoot | Out-Null
}

$running = Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -ieq 'ModOrganizer.exe' -and $_.ExecutablePath -and
        $_.ExecutablePath.StartsWith($runtimeRoot + '\', [System.StringComparison]::OrdinalIgnoreCase) }
if ($running) {
    throw "Close the MO2 instance running from the runtime directory before packaging: $runtimeRoot"
}

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$workRoot = Join-Path $outputRoot 'work'
$stageRoot = Join-Path $workRoot 'staging'
$releaseRoot = Join-Path $outputRoot 'release'
$buildRoot = Join-Path $workRoot 'build'
foreach ($directory in @($workRoot, $stageRoot, $releaseRoot, $buildRoot)) {
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}

$sourceDir = Join-Path $repoRoot 'source\revamped\installer'
$sourceFiles = @(
    (Join-Path $sourceDir 'AssemblyInfo.cs'),
    (Join-Path $sourceDir 'Mo2RevampedSetup.cs'),
    (Join-Path $sourceDir 'InstallerText.cs'),
    (Join-Path $sourceDir 'InstallerBackend.cs'),
    (Join-Path $sourceDir 'CompactSetupForm.cs'),
    (Join-Path $sourceDir 'VcRuntime.cs')
)
$iconPath = Join-Path $repoRoot 'source\revamped\branding\MO2-Revamped.ico'
$manifestPath = Join-Path $sourceDir 'app.manifest'
foreach ($path in @($manifestPath) + $sourceFiles) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Installer source file was not found: $path"
    }
}

function Invoke-Compiler {
    param([string[]]$Arguments)
    $quoted = foreach ($argument in $Arguments) {
        '"' + $argument.Replace('"', '\"') + '"'
    }
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $CscPath
    $start.Arguments = [string]::Join(' ', [string[]]$quoted)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [System.Diagnostics.Process]::Start($start)
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($stdout) { Write-Host $stdout.TrimEnd() }
    if ($stderr) { Write-Host $stderr.TrimEnd() }
    if ($process.ExitCode -ne 0) {
        throw "C# compiler failed with exit code $($process.ExitCode)."
    }
}

function Invoke-SetupCompile {
    param(
        [string]$OutputPath,
        [string[]]$Defines,
        [string[]]$Resources
    )
    $arguments = @(
        '/nologo', '/target:winexe', '/platform:anycpu', '/optimize+', '/debug-',
        '/langversion:5', '/reference:System.Windows.Forms.dll',
        '/reference:System.Drawing.dll', '/reference:System.Web.Extensions.dll',
        '/reference:System.IO.Compression.dll', '/reference:System.IO.Compression.FileSystem.dll',
        "/out:$OutputPath", "/win32manifest:$manifestPath"
    )
    if (Test-Path -LiteralPath $iconPath -PathType Leaf) {
        $arguments += "/win32icon:$iconPath"
        # every size of the icon for the Qt window and taskbar button, which
        # an icon read back from the executable would reduce to 32 px
        $arguments += "/resource:$iconPath,Mo2RevampedSetup.icon.ico"
    }
    if ($Defines.Count -gt 0) {
        $arguments += '/define:' + [string]::Join(';', $Defines)
    }
    $arguments += $Resources
    if (-not (Test-Path -LiteralPath $script:qtRuntimeZip -PathType Leaf)) {
        throw "The Qt installer runtime archive was not built: $script:qtRuntimeZip"
    }
    $arguments += "/resource:$script:qtRuntimeZip,Mo2RevampedSetup.qt-runtime.zip"
    $arguments += $sourceFiles
    Invoke-Compiler -Arguments $arguments
    if (-not (Test-Path -LiteralPath $OutputPath -PathType Leaf)) {
        throw "The compiler did not create the expected executable: $OutputPath"
    }
}

function Build-QtInstallerRuntime {
    $qtPrefix = Join-Path $qtSdkRoot '6.10.3\mingw_64'
    $qtBin = Join-Path $qtPrefix 'bin'
    $mingwBin = Join-Path $qtSdkRoot 'Tools\mingw1310_64\bin'
    $cmake = Join-Path $qtSdkRoot 'Tools\CMake_64\bin\cmake.exe'
    $ninja = Join-Path $qtSdkRoot 'Tools\Ninja\ninja.exe'
    $compiler = Join-Path $mingwBin 'g++.exe'
    $windeployqt = Join-Path $qtBin 'windeployqt.exe'
    foreach ($tool in @($qtPrefix, $cmake, $ninja, $compiler, $windeployqt)) {
        if (-not (Test-Path -LiteralPath $tool)) { throw "The Qt 6.10.3 MinGW kit is incomplete: $tool" }
    }

    $buildPath = Join-Path $buildRoot 'qt-installer'
    $deployPath = Join-Path $buildRoot 'qt-installer-runtime'
    $qtSource = Join-Path $sourceDir 'qtui'
    $oldPath = $env:PATH
    try {
        $env:PATH = "$mingwBin;$qtBin;$env:PATH"
        & $cmake -S $qtSource -B $buildPath -G Ninja `
            "-DCMAKE_PREFIX_PATH=$qtPrefix" `
            "-DCMAKE_MAKE_PROGRAM=$ninja" `
            "-DCMAKE_CXX_COMPILER=$compiler" `
            '-DCMAKE_BUILD_TYPE=Release' `
            "-DMO2R_VERSION=$version"
        if ($LASTEXITCODE -ne 0) { throw 'CMake configuration for the Qt installer failed.' }
        & $cmake --build $buildPath --config Release
        if ($LASTEXITCODE -ne 0) { throw 'Building the Qt installer interface failed.' }

        $qtExecutable = Join-Path $buildPath 'MO2RevampedSetupQt.exe'
        if (-not (Test-Path -LiteralPath $qtExecutable -PathType Leaf)) { throw "The Qt installer executable was not created: $qtExecutable" }
        New-Item -ItemType Directory -Force -Path $deployPath | Out-Null
        & $windeployqt --release --no-translations --compiler-runtime --dir $deployPath $qtExecutable
        if ($LASTEXITCODE -ne 0) { throw 'Deploying the Qt installer runtime failed.' }
        Copy-Item -LiteralPath $qtExecutable -Destination (Join-Path $deployPath 'MO2RevampedSetupQt.exe') -Force

        foreach ($required in @('MO2RevampedSetupQt.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'platforms\qwindows.dll', 'libstdc++-6.dll', 'libgcc_s_seh-1.dll', 'libwinpthread-1.dll')) {
            $candidate = Join-Path $deployPath $required
            if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { throw "The deployed Qt installer is missing a runtime file: $candidate" }
        }
        $script:qtRuntimeZip = Join-Path $workRoot 'qt-installer-runtime.zip'
        if (Test-Path -LiteralPath $script:qtRuntimeZip) { Remove-Item -LiteralPath $script:qtRuntimeZip -Force }
        $stream = [IO.File]::Create($script:qtRuntimeZip)
        $archive = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create, $false)
        try {
            foreach ($file in Get-ChildItem -LiteralPath $deployPath -File -Recurse -Force | Sort-Object FullName) {
                $relative = $file.FullName.Substring($deployPath.Length).TrimStart('\')
                $entry = $archive.CreateEntry($relative.Replace('\', '/'), [IO.Compression.CompressionLevel]::Optimal)
                $entry.LastWriteTime = [DateTimeOffset]::new(1980, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
                $input = [IO.File]::OpenRead($file.FullName)
                $output = $entry.Open()
                try { $input.CopyTo($output) } finally { $output.Dispose(); $input.Dispose() }
            }
        } finally { $archive.Dispose() }
        Write-Host "Qt installer UI and runtime bundled: $script:qtRuntimeZip"
    } finally {
        $env:PATH = $oldPath
    }
}

function Test-ExcludedRuntimeFile {
    param([string]$RelativePath)
    $normalized = $RelativePath.Replace('/', '\')
    $parts = $normalized -split '\\'
    $excludedDirectories = @(
        'profiles', 'mods', 'downloads', 'overwrite', 'logs', 'crashdumps',
        'webcache', 'portableinstances', 'globalinstances', 'isolatedinstances', '_revampedupdatebackups', 'crashes',
        'diagnostics', '__pycache__', 'work', 'build'
    )
    # user data a portable runtime keeps at its top level: the cache, and the
    # Elden Ring / Nightreign shared saves and save backups
    $excludedTopDirectories = @(
        'cache', 'elden ring shared saves', 'elden ring global save backups',
        'nightreign shared saves', 'nightreign global save backups'
    )
    foreach ($part in $parts[0..([Math]::Max(0, $parts.Length - 2))]) {
        if ($excludedDirectories -contains $part.ToLowerInvariant()) { return $true }
    }
    if ($parts.Length -gt 1 -and $excludedTopDirectories -contains $parts[0].ToLowerInvariant()) { return $true }
    $leaf = $parts[$parts.Length - 1]
    if ($parts.Length -eq 1 -and $leaf -match '^(ModOrganizer\.ini|nxmhandler\.ini|MO2-Revamped\.ini|MO2-Revamped\.install|MO2-Revamped-Global\.ini|EldenRingOnly\.ini|categories\.dat|nexuscatmap\.dat|portable\.txt|eldenring-only\.portable|unins000\.exe|MO2ForkUninstaller\.exe)$') {
        return $true
    }
    if ($normalized -match '^stylesheets\\\+(dark|light)\.qss$') {
        return $true
    }
    # Dark Black and the classic themes are retired (a stored choice becomes Light
    # or Dark); the 1.0.1 runtime still carries them and their icon folders
    if ($normalized -match '^stylesheets\\(dark-black\.qss$|classic-refined-[^\\]+\.qss$|mo2-classic-refined-[^\\]+\\)') {
        return $true
    }
    if ($normalized -match '^plugins\\plugin_python\\data\\settings\.json$') {
        return $true
    }
    if ($normalized -match '(^|\\)psutil\\tests(\\|$)') {
        return $true
    }
    return [System.IO.Path]::GetExtension($leaf).ToLowerInvariant() -in @(
        '.log', '.dmp', '.mdmp', '.tmp', '.bak', '.old', '.pyc', '.pyo', '.lock'
    )
}

# the runtime's Qt (2.5.2's): MO2's Qt comes from -QtRuntimeDirectory. Only for
# the copy from the runtime: the isolated stage is copied from the complete one,
# whose Qt is the new one
function Test-RuntimeQtFile {
    param([string]$RelativePath)
    $normalized = $RelativePath.Replace('/', '\')
    return $normalized -match '^dlls\\(Qt6[^\\]+\.dll|QtWebEngineProcess\.exe|qt\.conf|d3dcompiler_47\.dll|opengl32sw\.dll|imageformats\\|tls\\)' -or
        $normalized -match '^(platforms|styles|qml)\\' -or
        $normalized -match '^QtWebEngineProcess\.exe$' -or
        $normalized -match '^resources\\(qtwebengine_[^\\]+\.pak|icudtl\.dat|v8_context_snapshot[^\\]*\.bin)$' -or
        $normalized -match '^translations\\(qt_[^\\]+\.qm|qtbase_[^\\]+\.qm|qtwebengine_locales\\)'
}

function Copy-RuntimeTree {
    param([string]$SourceRoot, [string]$DestinationRoot, [switch]$SkipPlugins, [switch]$SkipRuntimeQt, [string[]]$Exclude = @(), [string[]]$ExcludePrefix = @())
    $sourceLength = $SourceRoot.Length
    $files = Get-ChildItem -LiteralPath $SourceRoot -File -Recurse -Force |
        Sort-Object FullName
    foreach ($file in $files) {
        if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { continue }
        $relative = $file.FullName.Substring($sourceLength).TrimStart('\')
        if ($relative -ieq 'ModOrganizer.exe' -or $relative -ieq 'unins000.exe') { continue }
        if ($SkipPlugins -and $relative.StartsWith('plugins\', [StringComparison]::OrdinalIgnoreCase)) { continue }
        if ($Exclude -contains $relative) { continue }
        if (@($ExcludePrefix | Where-Object { $relative.StartsWith($_, [StringComparison]::OrdinalIgnoreCase) }).Count) { continue }
        if (Test-ExcludedRuntimeFile -RelativePath $relative) { continue }
        if ($SkipRuntimeQt -and (Test-RuntimeQtFile -RelativePath $relative)) { continue }
        $destination = Join-Path $DestinationRoot $relative
        New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($destination)) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
    }
}

function Copy-UpstreamBinaries {
    # uibase, the plugins, plugin_python, archive, lootcli and helper, built
    # from source\upstream; the runtime's copies of them are never staged
    param([string]$DestinationRoot)
    foreach ($relative in $upstreamFiles) {
        $destination = Join-Path $DestinationRoot $relative
        New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($destination)) | Out-Null
        Copy-Item -LiteralPath (Join-Path $upstreamStageRoot $relative) -Destination $destination -Force
    }
}

# the organizer's own files beside ModOrganizer.exe, from source\modorganizer
# and the Revamped branding; the runtime still has the 1.0.1 splash and icons
$organizerFiles = [ordered]@{
    'MO2-Revamped.ico'        = 'source\revamped\branding\MO2-Revamped.ico'
    'splash.png'              = 'source\modorganizer\src\splash.png'
    'resources\mo_icon.png'   = 'source\modorganizer\src\resources\mo_icon.png'
    'resources\markdown.html' = 'source\modorganizer\src\resources\markdown.html'
    'dlls\dlls.manifest'      = 'source\modorganizer\src\dlls.manifest.qt6'
}

function Copy-OrganizerFiles {
    param([string]$DestinationRoot)
    foreach ($entry in $organizerFiles.GetEnumerator()) {
        $destination = Join-Path $DestinationRoot $entry.Key
        New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($destination)) | Out-Null
        Copy-Item -LiteralPath (Join-Path $repoRoot $entry.Value) -Destination $destination -Force
    }
}

# the MO2 translations, compiled from source\upstream\translations (the 2.5.2
# .qm files as .ts, which lrelease turns back into the same bytes); only Qt's
# own qt_ and qtbase_ files still come from the runtime
$mo2TranslationSource = Join-Path $repoRoot 'source\upstream\translations'
$mo2Translations = @(Get-ChildItem -LiteralPath $mo2TranslationSource -Filter '*.ts' -File | Sort-Object Name)
if ($mo2Translations.Count -eq 0) { throw "No MO2 translation sources were found in $mo2TranslationSource" }
$mo2TranslationFiles = @($mo2Translations | ForEach-Object { 'translations\' + $_.BaseName + '.qm' })
$lrelease = Join-Path $qtSdkRoot '6.10.3\mingw_64\bin\lrelease.exe'
if (-not (Test-Path -LiteralPath $lrelease -PathType Leaf)) {
    throw "Qt lrelease was not found: $lrelease"
}
$runtimeTranslations = Join-Path $runtimeRoot 'translations'
if (Test-Path -LiteralPath $runtimeTranslations) {
    foreach ($qm in Get-ChildItem -LiteralPath $runtimeTranslations -Filter '*.qm' -File) {
        if ($qm.Name -match '^(qt|qtbase|revamped)_') { continue }
        if ($mo2TranslationFiles -notcontains ('translations\' + $qm.Name)) {
            throw "The runtime translation $($qm.Name) has no source in source\upstream\translations."
        }
    }
}

function Copy-Mo2Translations {
    param([string]$DestinationRoot)
    $destination = Join-Path $DestinationRoot 'translations'
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    foreach ($file in $mo2Translations) {
        $qm = Join-Path $destination ($file.BaseName + '.qm')
        & $lrelease -silent $file.FullName -qm $qm
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $qm -PathType Leaf)) {
            throw "Compiling the MO2 translation failed: $($file.Name)"
        }
    }
}

# the BAIN wizard interpreter is in source\mo2-python-plugins with the plugin;
# the runtime keeps only the antlr4 runtime and chardet it imports
$pythonPluginPrefixes = @(
    'plugins\installer_wizard\lib\wizard\',
    'plugins\installer_wizard\lib\bain_wizard_interpreter-'
)

function Copy-PythonPluginOverlay {
    # the Python plugins come from source\mo2-python-plugins, not from the
    # runtime: the runtime only adds the third-party libraries they bundle
    param([string]$DestinationRoot)
    $source = Join-Path $repoRoot 'source\mo2-python-plugins'
    $files = @(Get-ChildItem -LiteralPath $source -File -Recurse -Force | Where-Object {
        $relative = $_.FullName.Substring($source.Length).TrimStart('\')
        -not (Test-ExcludedRuntimeFile -RelativePath ('plugins\' + $relative))
    })
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($source.Length).TrimStart('\')
        $destination = Join-Path (Join-Path $DestinationRoot 'plugins') $relative
        New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($destination)) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
    }
    # the BAIN wizard interpreter as its wheel installed it: every staged file
    # matches its RECORD hash, and nothing else is in wizard\
    $lib = Join-Path $DestinationRoot 'plugins\installer_wizard\lib'
    $listed = New-Object System.Collections.Generic.HashSet[string] ([StringComparer]::OrdinalIgnoreCase)
    $sha256 = [Security.Cryptography.SHA256]::Create()
    foreach ($line in [IO.File]::ReadAllLines((Join-Path $lib 'bain_wizard_interpreter-1.0.4.dist-info\RECORD'))) {
        $path, $hash, $null = $line -split ','
        if (-not $hash) { continue }
        $staged = Join-Path $lib $path.Replace('/', '\')
        [void]$listed.Add($staged)
        $actual = if (Test-Path -LiteralPath $staged -PathType Leaf) {
            'sha256=' + [Convert]::ToBase64String($sha256.ComputeHash([IO.File]::ReadAllBytes($staged))).TrimEnd('=').Replace('+', '-').Replace('/', '_')
        }
        if ($actual -cne $hash) { throw "The staged BAIN wizard interpreter file $path does not match its RECORD." }
    }
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $lib 'wizard') -File -Recurse -Force) {
        if (-not $listed.Contains($file.FullName)) { throw "The staged BAIN wizard interpreter has a file its RECORD does not list: $($file.FullName)" }
    }
}

function Copy-RevampedSources {
    param([string]$DestinationRoot, [string]$BridgePath, [string]$Me3CompatPath)
    $pluginSource = Join-Path $repoRoot 'source\revamped\plugins'
    $pluginDestination = Join-Path $DestinationRoot 'plugins'
    foreach ($file in Get-ChildItem -LiteralPath $pluginSource -File -Recurse -Force) {
        if ($file.Extension -notin @('.py', '.dll')) { continue }
        # both native DLLs are built for the release; me3_compat\ holds only their source and build output
        if ($file.Name -ieq 'eldenring_mo2_bridge.dll' -or $file.FullName -match '\\(__pycache__|vendor|me3_compat)\\') { continue }
        $relative = $file.FullName.Substring($pluginSource.Length).TrimStart('\')
        if (Test-ExcludedRuntimeFile -RelativePath $relative) { continue }
        $destination = Join-Path $pluginDestination $relative
        New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($destination)) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
    }
    $saveIsolationSource = Join-Path $pluginSource 'eldenring_mo2_save_isolation.py'
    $saveIsolationStage = Join-Path $pluginDestination 'eldenring_mo2_save_isolation.py'
    $saveIsolationSourceHash = (Get-FileHash -LiteralPath $saveIsolationSource -Algorithm SHA256).Hash
    $saveIsolationStageHash = (Get-FileHash -LiteralPath $saveIsolationStage -Algorithm SHA256).Hash
    if ($saveIsolationSourceHash -ine $saveIsolationStageHash) {
        throw 'The Complete stage does not contain the maintained Save Isolation source.'
    }
    # these replace the runtime's baseline copies
    foreach ($overlaid in @('Form43Checker.py', 'installer_wizard\dialog.py')) {
        $overlaySource = Join-Path $pluginSource $overlaid
        $overlayStage = Join-Path $pluginDestination $overlaid
        if ((Get-FileHash -LiteralPath $overlaySource -Algorithm SHA256).Hash -ine
            (Get-FileHash -LiteralPath $overlayStage -Algorithm SHA256).Hash) {
            throw "The Complete stage does not contain the maintained $overlaid."
        }
    }
    $bridgeDestination = Join-Path $pluginDestination 'basic_games\games\eldenring_mo2_bridge.dll'
    New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($bridgeDestination)) | Out-Null
    Copy-Item -LiteralPath $BridgePath -Destination $bridgeDestination -Force
    Copy-Item -LiteralPath $Me3CompatPath -Destination (Join-Path $pluginDestination 'basic_games\games\eldenring_mo2_me3_compat.dll') -Force

    $themeSource = Join-Path $repoRoot 'source\revamped\themes\stylesheets'
    $themeDestination = Join-Path $DestinationRoot 'stylesheets'
    foreach ($file in Get-ChildItem -LiteralPath $themeSource -File -Recurse -Force) {
        if ($file.Extension -notin @('.qss', '.svg', '.png')) { continue }
        if (Test-ExcludedRuntimeFile -RelativePath $file.Name) { continue }
        $relative = $file.FullName.Substring($themeSource.Length).TrimStart('\')
        if ($file.Extension -ieq '.qss') {
            switch -CaseSensitive ($file.Name) {
                # MO offers Light.qss and dark.qss; the Fluent
                # themes are the default under those names
                'MO2 Fluent v4 - Light.qss' { $relative = 'Light.qss' }
                'MO2 Fluent v4 - Dark.qss' { $relative = 'dark.qss' }
                default { throw "Unexpected Revamped theme stylesheet: $($file.Name)" }
            }
        }
        $destination = Join-Path $themeDestination $relative
        New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($destination)) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
    }

    foreach ($theme in @(
        @{ Source = 'MO2 Fluent v4 - Light.qss'; Destination = 'Light.qss' },
        @{ Source = 'MO2 Fluent v4 - Dark.qss'; Destination = 'dark.qss' }
    )) {
        $source = Join-Path $themeSource $theme.Source
        $destination = Join-Path $themeDestination $theme.Destination
        $sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
        $stagedHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
        if ($sourceHash -ine $stagedHash) {
            throw "The staged $($theme.Destination) theme does not match its reviewed source."
        }
    }

    # MO2 Revamped has no tutorials; an older runtime snapshot may still hold them.
    $tutorialDestination = Join-Path $DestinationRoot 'tutorials'
    if (Test-Path -LiteralPath $tutorialDestination) {
        Remove-Item -LiteralPath $tutorialDestination -Recurse -Force
    }

    # MO2's own translation files do not cover the Revamped additions or the
    # Revamped plugins; MO2 installs translations/revamped_<language>.qm next
    # to organizer_<language>.qm.
    $translationSource = Join-Path $repoRoot 'source\revamped\translations'
    $translationDestination = Join-Path $DestinationRoot 'translations'
    New-Item -ItemType Directory -Force -Path $translationDestination | Out-Null
    $translationFiles = @(Get-ChildItem -LiteralPath $translationSource -Filter 'revamped_*.ts' -File)
    if ($translationFiles.Count -eq 0) {
        throw "No Revamped translation sources were found in $translationSource"
    }
    foreach ($file in $translationFiles) {
        $qm = Join-Path $translationDestination ($file.BaseName + '.qm')
        & $lrelease -silent $file.FullName -qm $qm
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $qm -PathType Leaf)) {
            throw "Compiling the Revamped translation failed: $($file.Name)"
        }
    }

    Copy-Item -LiteralPath $organizerPath -Destination (Join-Path $DestinationRoot 'ModOrganizer.exe') -Force
    # the Fluent nxm handler replaces the runtime's upstream nxmhandler.exe
    Copy-Item -LiteralPath $nxmHandlerPath -Destination (Join-Path $DestinationRoot 'nxmhandler.exe') -Force
    [IO.File]::WriteAllText((Join-Path $DestinationRoot 'MO2-Revamped.ini'), "[Settings]`r`nlanguage=en_US`r`n", [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $DestinationRoot 'MO2-Revamped.install'), "MO2-REVAMPED-INSTALLATION-MARKER`r`n", [Text.UTF8Encoding]::new($false))
}

function Move-WebEngineProcessToDlls {
    # QtWebEngineProcess.exe imports the Qt dlls in dlls\. From the root it only
    # found them through MO's PATH, which Chromium's sandbox doesn't give the
    # render process, so web pages never rendered. MO points Qt at dlls\ when
    # the helper is there; dlls\qt.conf leads it back to the root's resources.
    param([string]$DestinationRoot)
    $dlls = Join-Path $DestinationRoot 'dlls'
    $rootHelper = Join-Path $DestinationRoot 'QtWebEngineProcess.exe'
    $helper = Join-Path $dlls 'QtWebEngineProcess.exe'
    if (Test-Path -LiteralPath $rootHelper -PathType Leaf) {
        Move-Item -LiteralPath $rootHelper -Destination $helper -Force
    }
    if (-not (Test-Path -LiteralPath $helper -PathType Leaf)) {
        throw "The runtime does not contain QtWebEngineProcess.exe: $helper"
    }
    Copy-Item -LiteralPath (Join-Path $repoRoot 'source\modorganizer\src\dlls.qt.conf') -Destination (Join-Path $dlls 'qt.conf') -Force
}

function Copy-OpenSslRuntime {
    param([string]$RuntimeSource, [string]$DestinationRoot)
    $sourceFiles = @(
        (Join-Path $RuntimeSource 'bin\libcrypto-3-x64.dll'),
        (Join-Path $RuntimeSource 'bin\libssl-3-x64.dll')
    )
    foreach ($source in $sourceFiles) {
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "The requested OpenSSL runtime is incomplete: $source"
        }
        $version = (Get-Item -LiteralPath $source).VersionInfo.FileVersion
        if (-not $version.StartsWith('3.5.9', [StringComparison]::Ordinal)) {
            throw "OpenSSL 3.5.9 is required for this release; found '$version' in $source"
        }
    }
    foreach ($relative in @('libcrypto-3-x64.dll', 'libssl-3-x64.dll', 'dlls\libcrypto-3-x64.dll', 'dlls\libssl-3-x64.dll')) {
        $name = [IO.Path]::GetFileName($relative)
        $source = if ($name -ieq 'libcrypto-3-x64.dll') { $sourceFiles[0] } else { $sourceFiles[1] }
        $destination = Join-Path $DestinationRoot $relative
        New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($destination)) | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination -Force
        $version = (Get-Item -LiteralPath $destination).VersionInfo.FileVersion
        if (-not $version.StartsWith('3.5.9', [StringComparison]::Ordinal)) {
            throw "OpenSSL staging verification failed for $destination; found '$version'."
        }
    }

}

function Copy-PythonRuntime {
    param([string]$RuntimeSource, [string]$DestinationRoot)

    $manifestPath = Join-Path $RuntimeSource 'runtime-manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "The Python runtime overlay has no manifest: $manifestPath"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.format -ne 1 -or $manifest.python_version -ne '3.12.15' -or $manifest.openssl_version -ne '3.5.9') {
        throw 'The Python runtime overlay must identify Python 3.12.15 and OpenSSL 3.5.9.'
    }

    $pluginRoot = Join-Path $DestinationRoot 'plugins\plugin_python'
    if (-not (Test-Path -LiteralPath $pluginRoot -PathType Container)) {
        throw "The MO2 Python plugin directory was not found: $pluginRoot"
    }
    $requiredOverlayFiles = @(
        'dlls/python312.dll', 'libs/pythoncore.zip', 'libs/_ssl.pyd', 'libs/_hashlib.pyd'
    )
    $manifestNames = @($manifest.files.PSObject.Properties.Name)
    foreach ($name in $requiredOverlayFiles) {
        if ($manifestNames -notcontains $name) {
            throw "The Python runtime overlay is missing a required file: $name"
        }
    }
    $moBaseModule = Join-Path $pluginRoot 'libs\mobase.cp312-win_amd64.pyd'
    if (-not (Test-Path -LiteralPath $moBaseModule -PathType Leaf)) {
        throw "The MO2 Python plugin module was not found in the base runtime: $moBaseModule"
    }

    foreach ($property in $manifest.files.PSObject.Properties) {
        $relative = $property.Name.Replace('/', '\')
        if ($relative -notmatch '^(dlls\\python312\.dll|dlls\\libffi-8\.dll|libs\\pythoncore\.zip|libs\\[A-Za-z0-9_]+(?:\.[A-Za-z0-9_-]+)*\.pyd)$') {
            throw "Unexpected file in the Python runtime overlay manifest: $relative"
        }
        $source = Join-Path $RuntimeSource $relative
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "The Python runtime overlay file is missing: $source"
        }
        $actualHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualHash -ne ([string]$property.Value).ToLowerInvariant()) {
            throw "The Python runtime overlay hash does not match its manifest: $relative"
        }
        $destination = Join-Path $pluginRoot $relative
        New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($destination)) | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination -Force
    }

    $pythonVersion = (Get-Item -LiteralPath (Join-Path $pluginRoot 'dlls\python312.dll')).VersionInfo.FileVersion
    if (-not $pythonVersion.StartsWith('3.12.15', [StringComparison]::Ordinal)) {
        throw "Python runtime staging failed; expected 3.12.15, found $pythonVersion."
    }
}

function Test-FileContainsPathNeedle {
    param(
        [string]$Path,
        [string[]]$Needles,
        [int]$ChunkSize = 1048576
    )

    $lowerNeedles = @()
    $maxNeedleLength = 0
    foreach ($needle in $Needles) {
        if (-not $needle) { continue }
        $lowerNeedles += $needle.ToLowerInvariant()
        $maxNeedleLength = [Math]::Max($maxNeedleLength, $needle.Length)
    }
    if ($lowerNeedles.Count -eq 0) { return $false }

    $overlapLength = [Math]::Max(0, $maxNeedleLength - 1)
    $buffer = [Array]::CreateInstance([char], $ChunkSize)
    foreach ($encoding in @([Text.Encoding]::ASCII, [Text.Encoding]::Unicode)) {
        $stream = [IO.File]::OpenRead($Path)
        try {
            $reader = [IO.StreamReader]::new($stream, $encoding, $false, $ChunkSize, $true)
            try {
                $carry = ''
                while (($read = $reader.Read($buffer, 0, $buffer.Length)) -gt 0) {
                    $segment = $carry + [string]::new($buffer, 0, $read)
                    $folded = $segment.ToLowerInvariant()
                    foreach ($needle in $lowerNeedles) {
                        if ($folded.IndexOf($needle, [StringComparison]::Ordinal) -ge 0) {
                            return $true
                        }
                    }
                    $carryStart = [Math]::Max(0, $segment.Length - $overlapLength)
                    $carry = $segment.Substring($carryStart)
                }
            } finally {
                $reader.Dispose()
            }
        } finally {
            $stream.Dispose()
        }
    }
    return $false
}

function Test-NoLocalBuildPaths {
    param([string[]]$StagePaths)
    $needles = @($env:USERPROFILE, $repoRoot) |
        Where-Object { $_ -and $_.Trim() } |
        ForEach-Object {
            $normalized = [IO.Path]::GetFullPath($_).TrimEnd('\', '/')
            $normalized
            $normalized.Replace('\', '/')
        } |
        Select-Object -Unique
    $repoLeaf = [IO.Path]::GetFileName($repoRoot.TrimEnd('\', '/'))
    # a checkout named after the product ("MO2-Revamped") would match the
    # product's own files (MO2-Revamped.ini, the install marker, the
    # uninstaller's strings); the full checkout paths above still catch it
    $productNames = @('MO2-Revamped-Installation-Marker', 'ModOrganizer', 'Mod Organizer')
    if ($repoLeaf -and ($productNames | Where-Object { $_.IndexOf($repoLeaf, [StringComparison]::OrdinalIgnoreCase) -ge 0 })) {
        $repoLeaf = $null
    }
    if ($repoLeaf) {
        $needles += $repoLeaf
        $needles = @($needles | Select-Object -Unique)
    }
    if ($needles.Count -eq 0) { return }

    foreach ($stagePath in $StagePaths) {
        foreach ($file in Get-ChildItem -LiteralPath $stagePath -File -Recurse -Force) {
            $containsNeedle = $false
            if ($file.Length -gt 4194304) {
                $containsNeedle = Test-FileContainsPathNeedle -Path $file.FullName -Needles $needles
            } else {
                $bytes = [IO.File]::ReadAllBytes($file.FullName)
                $ascii = [Text.Encoding]::ASCII.GetString($bytes)
                $unicode = [Text.Encoding]::Unicode.GetString($bytes)
                foreach ($needle in $needles) {
                    if ($ascii.IndexOf($needle, [StringComparison]::OrdinalIgnoreCase) -ge 0 -or
                        $unicode.IndexOf($needle, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                        $containsNeedle = $true
                        break
                    }
                }
            }
            if ($containsNeedle) {
                $relative = $file.FullName.Substring($stagePath.Length).TrimStart('\')
                throw "A release payload contains a local build path in '$relative'. Rebuild the affected binary with source-path mapping."
            }
        }
    }
}

function Copy-EldenRingPlugins {
    param([string]$SourceRoot, [string]$DestinationRoot)
    $sourcePlugins = Join-Path $SourceRoot 'plugins'
    $targetPlugins = Join-Path $DestinationRoot 'plugins'
    $allowedDirectories = @('basic_games', 'data', 'eldenring_mo2_archive_installer', 'installer_wizard', 'plugin_python')
    $allowedRootFiles = @(
        'DDSPreview.py', 'diagnose_basic.dll', 'eldenring_mo2_native_profile_tool.py',
        'eldenring_mo2_save_isolation.py', 'eldenring_mo2_startup_tool.py',
        'inibakery.dll', 'inieditor.dll', 'installer_bundle.dll',
        'installer_fomod_csharp.dll', 'installer_fomod.dll', 'installer_manual.dll',
        'installer_quick.dll', 'mo2_revamped_info.py', 'preview_base.dll'
    )
    $allowedGameFiles = @('__init__.py', 'eldenring_mo2_bridge.dll', 'eldenring_mo2_me3_compat.dll', 'game_eldenring_mo2.py')
    New-Item -ItemType Directory -Force -Path $targetPlugins | Out-Null
    foreach ($file in Get-ChildItem -LiteralPath $sourcePlugins -File -Recurse -Force) {
        $relative = $file.FullName.Substring($sourcePlugins.Length).TrimStart('\')
        if (Test-ExcludedRuntimeFile -RelativePath $relative) { continue }
        $parts = $relative -split '\\'
        $include = $false
        if ($parts.Length -eq 1) {
            $include = $allowedRootFiles -icontains $parts[0]
        } elseif ($allowedDirectories -icontains $parts[0]) {
            $include = $true
            if ($parts[0] -ieq 'basic_games' -and $parts.Length -ge 3 -and $parts[1] -ieq 'games') {
                $include = $parts.Length -eq 3 -and $allowedGameFiles -icontains $parts[2]
            } elseif ($parts[0] -ieq 'data') {
                # the DDS support files, and the Fluent helpers the BAIN wizard
                # imports
                $include = $parts.Length -ge 2 -and ($parts[1] -ieq 'DDS' -or
                    ($parts.Length -eq 2 -and $parts[1] -ieq 'revamped_fluent.py'))
            }
        }
        if (-not $include) { continue }
        $destination = Join-Path $targetPlugins $relative
        New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($destination)) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
    }
    $saveIsolationSource = Join-Path $sourcePlugins 'eldenring_mo2_save_isolation.py'
    $saveIsolationTarget = Join-Path $targetPlugins 'eldenring_mo2_save_isolation.py'
    $saveIsolationSourceHash = (Get-FileHash -LiteralPath $saveIsolationSource -Algorithm SHA256).Hash
    $saveIsolationTargetHash = (Get-FileHash -LiteralPath $saveIsolationTarget -Algorithm SHA256).Hash
    if ($saveIsolationSourceHash -ine $saveIsolationTargetHash) {
        throw 'The Isolated stage does not match Complete for Save Isolation.'
    }
    foreach ($required in @(
        'eldenring_mo2_native_profile_tool.py', 'eldenring_mo2_save_isolation.py',
        'eldenring_mo2_startup_tool.py', 'eldenring_mo2_archive_installer\installer.py',
        'basic_games\games\game_eldenring_mo2.py',
        'data\revamped_fluent.py', 'installer_wizard\dialog.py',
        'basic_games\games\eldenring_mo2_bridge.dll', 'basic_games\games\eldenring_mo2_me3_compat.dll',
        'plugin_python\plugin_python.dll',
        'plugin_python\dlls\python312.dll', 'plugin_python\libs\pythoncore.zip',
        'plugin_python\libs\_ssl.pyd', 'plugin_python\libs\_hashlib.pyd'
    )) {
        if (-not (Test-Path -LiteralPath (Join-Path $targetPlugins $required) -PathType Leaf)) {
            throw "The isolated Elden Ring payload is missing a required plugin file: $required"
        }
    }
    foreach ($unwanted in @(
        'pyCfg.py', 'data\settings.json', 'data\pyCfgDialog.py',
        'installer_omod\installer_omod.dll', 'basic_games\lib\psutil\tests'
    )) {
        if (Test-Path -LiteralPath (Join-Path $targetPlugins $unwanted)) {
            throw "The Elden Ring-only payload includes an unrelated or user-specific plugin file: $unwanted"
        }
    }
    $gameDirectory = Join-Path $targetPlugins 'basic_games\games'
    $nestedGameFiles = @(
        Get-ChildItem -LiteralPath $gameDirectory -File -Recurse -Force |
            Where-Object { $_.DirectoryName -ine $gameDirectory }
    )
    if ($nestedGameFiles.Count -gt 0) {
        $names = $nestedGameFiles | ForEach-Object { $_.FullName.Substring($gameDirectory.Length).TrimStart('\') }
        throw "The Elden Ring-only payload includes nested files for another game: $([string]::Join(', ', $names))"
    }
}

function Test-IsolatedCoreParity {
    param([string]$CompleteStage, [string]$IsolatedStage)
    $excludedRelativePaths = @('unins000.exe')
    $completeCore = @{}
    $isolatedCore = @{}
    foreach ($file in Get-ChildItem -LiteralPath $CompleteStage -File -Recurse -Force) {
        $relative = $file.FullName.Substring($CompleteStage.Length).TrimStart('\')
        if ($relative.StartsWith('plugins\', [StringComparison]::OrdinalIgnoreCase) -or
            $excludedRelativePaths -icontains $relative) { continue }
        $completeCore[$relative] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    }
    foreach ($file in Get-ChildItem -LiteralPath $IsolatedStage -File -Recurse -Force) {
        $relative = $file.FullName.Substring($IsolatedStage.Length).TrimStart('\')
        if ($relative.StartsWith('plugins\', [StringComparison]::OrdinalIgnoreCase) -or
            $excludedRelativePaths -icontains $relative) { continue }
        $isolatedCore[$relative] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    }
    $completePaths = @($completeCore.Keys | Sort-Object)
    $isolatedPaths = @($isolatedCore.Keys | Sort-Object)
    if ([string]::Join("`n", $completePaths) -cne [string]::Join("`n", $isolatedPaths)) {
        throw 'The Complete and Isolated stages do not contain the same non-plugin core files.'
    }
    foreach ($relative in $completePaths) {
        if ($completeCore[$relative] -ine $isolatedCore[$relative]) {
            throw "Complete and Isolated core payloads differ at '$relative'."
        }
    }

    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $IsolatedStage 'plugins') -File -Recurse -Force) {
        $relative = $file.FullName.Substring($IsolatedStage.Length).TrimStart('\')
        $completeFile = Join-Path $CompleteStage $relative
        if (-not (Test-Path -LiteralPath $completeFile -PathType Leaf)) {
            throw "Isolated includes a plugin file absent from Complete: $relative"
        }
        $completeHash = (Get-FileHash -LiteralPath $completeFile -Algorithm SHA256).Hash
        $isolatedHash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        if ($completeHash -ine $isolatedHash) {
            throw "Complete and Isolated plugin versions differ at '$relative'."
        }
    }
}

# The installed licenses\ folder: the runtime input brings MO2's own licence
# files, and the repository's licenses\ adds and updates what MO2 Revamped
# ships (the bridge's and the me3 compatibility DLL's components among them).
# The notices themselves are THIRD-PARTY-NOTICES.md as it is in this source.
function Copy-RevampedLicenses {
    param([string]$DestinationRoot)
    $destination = Join-Path $DestinationRoot 'licenses'
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Copy-Item -Path (Join-Path $repoRoot 'licenses\*') -Destination $destination -Recurse -Force
    # the older plain-text copy of the notices; the current ones replace it
    Remove-Item -LiteralPath (Join-Path $destination 'MO2-Revamped-THIRD-PARTY-NOTICES.txt') -Force -ErrorAction SilentlyContinue
    Copy-Item -LiteralPath (Join-Path $repoRoot 'THIRD-PARTY-NOTICES.md') -Destination (Join-Path $destination 'MO2-Revamped-THIRD-PARTY-NOTICES.md') -Force
}

# Every licences\ file the notices name must be in the installed folder
function Test-RevampedLicenses {
    param([string]$StageRoot)
    $notices = Get-Content -LiteralPath (Join-Path $repoRoot 'THIRD-PARTY-NOTICES.md') -Raw
    $named = [regex]::Matches($notices, '\(licenses/([^)\s]+\.txt)\)') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
    if (@($named).Count -eq 0) { throw 'THIRD-PARTY-NOTICES.md names no licence file; the check would prove nothing.' }
    $missing = @($named | Where-Object { -not (Test-Path -LiteralPath (Join-Path $StageRoot ('licenses\' + ($_ -replace '/', '\'))) -PathType Leaf) })
    if (-not (Test-Path -LiteralPath (Join-Path $StageRoot 'licenses\MO2-Revamped-THIRD-PARTY-NOTICES.md') -PathType Leaf)) {
        $missing += 'MO2-Revamped-THIRD-PARTY-NOTICES.md'
    }
    if ($missing.Count -gt 0) {
        throw "Licence files missing from ${StageRoot}\licenses: $($missing -join ', ')"
    }
}

function New-PayloadArchive {
    param([string]$StagePath, [string]$ZipPath, [string]$ManifestPath)
    $files = @(Get-ChildItem -LiteralPath $StagePath -File -Recurse -Force | Sort-Object FullName)
    if ($files.Count -eq 0) { throw "Refusing to create an empty payload: $StagePath" }
    $rootLength = $StagePath.Length
    $fileRecords = [System.Collections.Generic.List[object]]::new()
    $totalBytes = [long]0
    $stream = [IO.File]::Create($ZipPath)
    $archive = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create, $false)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        foreach ($file in $files) {
            $relative = $file.FullName.Substring($rootLength).TrimStart('\')
            $hashStream = [IO.File]::OpenRead($file.FullName)
            try { $hash = ([BitConverter]::ToString($sha.ComputeHash($hashStream))).Replace('-', '') }
            finally { $hashStream.Dispose() }
            $entryName = $relative.Replace('\', '/')
            $entry = $archive.CreateEntry($entryName, [IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime = [DateTimeOffset]::new(1980, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
            $input = [IO.File]::OpenRead($file.FullName)
            $output = $entry.Open()
            try { $input.CopyTo($output) }
            finally { $output.Dispose(); $input.Dispose() }
            $fileRecords.Add([ordered]@{ Path = $relative; Length = [long]$file.Length; Sha256 = $hash })
            $totalBytes += [long]$file.Length
        }
    } finally {
        $sha.Dispose()
        $archive.Dispose()
    }
    $manifest = [ordered]@{ Version = $version; TotalBytes = $totalBytes; Files = @($fileRecords.ToArray()) }
    $json = ConvertTo-Json -InputObject $manifest -Depth 8
    [IO.File]::WriteAllText($ManifestPath, $json + "`n", [Text.UTF8Encoding]::new($false))
    return $files.Count
}

function Test-PayloadArchive {
    param([string]$ZipPath, [string]$ManifestPath)
    $manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    $archive = [IO.Compression.ZipFile]::OpenRead($ZipPath)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        if ($archive.Entries.Count -ne $manifest.Files.Count) {
            throw "ZIP/manifest entry count differs: $ZipPath"
        }
        $entries = @{}
        foreach ($entry in $archive.Entries) {
            $key = $entry.FullName.Replace('/', '\')
            if ($entries.ContainsKey($key)) { throw "Duplicate ZIP entry: $key" }
            $entries[$key] = $entry
        }
        foreach ($record in $manifest.Files) {
            $entry = $entries[$record.Path]
            if ($null -eq $entry -or $entry.Length -ne [long]$record.Length) {
                throw "Payload file is missing or has a different size: $($record.Path)"
            }
            $content = $entry.Open()
            try { $hash = ([BitConverter]::ToString($sha.ComputeHash($content))).Replace('-', '') }
            finally { $content.Dispose() }
            if ($hash -ine [string]$record.Sha256) { throw "Payload hash mismatch: $($record.Path)" }
        }
    } finally {
        $sha.Dispose()
        $archive.Dispose()
    }
}

function Test-EmbeddedResources {
    param([string]$InstallerPath, [string[]]$ExpectedResources)
    $assembly = [Reflection.Assembly]::LoadFrom($InstallerPath)
    $actual = @($assembly.GetManifestResourceNames() | Sort-Object)
    $withIcon = @($ExpectedResources)
    if (Test-Path -LiteralPath $iconPath -PathType Leaf) { $withIcon += 'Mo2RevampedSetup.icon.ico' }
    $expected = @($withIcon | Sort-Object)
    if ([string]::Join("`n", $actual) -cne [string]::Join("`n", $expected)) {
        throw "Embedded resources do not match the requested package in $InstallerPath. Found: $([string]::Join(', ', $actual))"
    }
}

$bridgePath = Join-Path $buildRoot 'eldenring_mo2_bridge.dll'
Build-QtInstallerRuntime
$bridgeBuildScript = Join-Path $repoRoot 'source\revamped\plugins\basic_games\games\build_bridge.ps1'
& $bridgeBuildScript -ZigPath $ZigPath -OutputPath $bridgePath
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $bridgePath -PathType Leaf)) {
    throw 'Building the Elden Ring native bridge failed.'
}
# ELDEN RING Reforged's Arxan and regulation fixes (Rust, see me3_compat\build.cmd)
$me3CompatSource = Join-Path $repoRoot 'source\revamped\plugins\basic_games\games\me3_compat'
$me3CompatPath = Join-Path $buildRoot 'eldenring_mo2_me3_compat.dll'
& cmd.exe /c (Join-Path $me3CompatSource 'build.cmd')
$me3CompatBuilt = Join-Path $me3CompatSource 'eldenring_mo2_me3_compat.dll'
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $me3CompatBuilt -PathType Leaf)) {
    throw 'Building the Elden Ring me3 compatibility DLL failed.'
}
Copy-Item -LiteralPath $me3CompatBuilt -Destination $me3CompatPath -Force

$generalUninstallerStub = Join-Path $buildRoot 'unins000-general.exe'
$eldenRingUninstallerStub = Join-Path $buildRoot 'unins000-eldenring.exe'
Invoke-SetupCompile -OutputPath $generalUninstallerStub -Defines @('UNINSTALLER_STUB') -Resources @()
Invoke-SetupCompile -OutputPath $eldenRingUninstallerStub -Defines @('UNINSTALLER_STUB', 'ELDENRING_ONLY_INSTALLER') -Resources @()
Test-EmbeddedResources -InstallerPath $generalUninstallerStub -ExpectedResources @('Mo2RevampedSetup.qt-runtime.zip')
Test-EmbeddedResources -InstallerPath $eldenRingUninstallerStub -ExpectedResources @('Mo2RevampedSetup.qt-runtime.zip')

$fullStage = Join-Path $stageRoot 'complete'
$isolatedStage = Join-Path $stageRoot 'eldenring-isolated'
New-Item -ItemType Directory -Force -Path $fullStage | Out-Null
Copy-RuntimeTree -SourceRoot $runtimeRoot -DestinationRoot $fullStage -SkipRuntimeQt `
    -Exclude ($upstreamFiles + @($organizerFiles.Keys) + $mo2TranslationFiles + @('loot\loot.dll')) -ExcludePrefix $pythonPluginPrefixes
Copy-UpstreamBinaries -DestinationRoot $fullStage
Copy-OrganizerFiles -DestinationRoot $fullStage
Copy-Mo2Translations -DestinationRoot $fullStage
Copy-PythonPluginOverlay -DestinationRoot $fullStage
Copy-RevampedSources -DestinationRoot $fullStage -BridgePath $bridgePath -Me3CompatPath $me3CompatPath
Copy-OpenSslRuntime -RuntimeSource $openSslRuntimeRoot -DestinationRoot $fullStage
Copy-PythonRuntime -RuntimeSource $pythonRuntimeRoot -DestinationRoot $fullStage
# Qt last: the Qt libraries it copies are the ones everything above imports,
# and it writes dlls\dlls.manifest for what dlls\ then holds
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) { throw "vswhere.exe was not found: $vswhere" }
$vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'No Visual Studio with the C++ x64 tools was found (dumpbin is needed to copy Qt).' }
$msvcVersion = (Get-Content -LiteralPath (Join-Path $vsRoot 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt') -TotalCount 1).Trim()
$dumpbin = Join-Path $vsRoot "VC\Tools\MSVC\$msvcVersion\bin\Hostx64\x64\dumpbin.exe"
if (-not (Test-Path -LiteralPath $dumpbin -PathType Leaf)) { throw "dumpbin.exe was not found: $dumpbin" }
& (Join-Path $PSScriptRoot 'Copy-QtRuntime.ps1') -QtDirectory $qtRuntimeRoot -Destination $fullStage -Dumpbin $dumpbin
Move-WebEngineProcessToDlls -DestinationRoot $fullStage
# the borderless window DLL MO2 loads into the 32-bit games (Fallout 3, New
# Vegas, Oblivion, the original Skyrim) when the Run menu's option is on; see
# source\revamped\borderless
$borderlessSource = Join-Path $repoRoot 'source\revamped\borderless\mo2r_borderless.cpp'
$borderlessBuild = Join-Path $buildRoot 'borderless'
New-Item -ItemType Directory -Force -Path $borderlessBuild | Out-Null
$borderlessDll = Join-Path $borderlessBuild 'mo2r_borderless.dll'
$borderlessCmd = Join-Path $borderlessBuild 'build.cmd'
$devCmd = Join-Path $vsRoot 'Common7\Tools\VsDevCmd.bat'
[IO.File]::WriteAllText($borderlessCmd, (
    "@echo off`r`ncall `"$devCmd`" -no_logo -arch=x86 -host_arch=amd64`r`n" +
    "cl /nologo /O2 /MT /EHsc /W4 /LD /DUNICODE /D_UNICODE `"$borderlessSource`" " +
    "/Fo`"$borderlessBuild\\`" /Fe`"$borderlessDll`" /link user32.lib dwmapi.lib gdi32.lib`r`n"),
    [Text.Encoding]::ASCII)
& cmd.exe /c "`"$borderlessCmd`""
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $borderlessDll -PathType Leaf)) {
    throw 'The borderless window DLL did not build.'
}
New-Item -ItemType Directory -Force -Path (Join-Path $fullStage 'borderless') | Out-Null
Copy-Item -LiteralPath $borderlessDll -Destination (Join-Path $fullStage 'borderless\mo2r_borderless.dll') -Force
Copy-Item -LiteralPath $generalUninstallerStub -Destination (Join-Path $fullStage 'unins000.exe') -Force
Copy-RevampedLicenses -DestinationRoot $fullStage

New-Item -ItemType Directory -Force -Path $isolatedStage | Out-Null
Copy-RuntimeTree -SourceRoot $fullStage -DestinationRoot $isolatedStage -SkipPlugins
Copy-EldenRingPlugins -SourceRoot $fullStage -DestinationRoot $isolatedStage
foreach ($rootFile in @('ModOrganizer.exe', 'MO2-Revamped.ini', 'MO2-Revamped.install')) {
    Copy-Item -LiteralPath (Join-Path $fullStage $rootFile) -Destination (Join-Path $isolatedStage $rootFile) -Force
}
Copy-Item -LiteralPath $eldenRingUninstallerStub -Destination (Join-Path $isolatedStage 'unins000.exe') -Force

Test-IsolatedCoreParity -CompleteStage $fullStage -IsolatedStage $isolatedStage
Test-RevampedLicenses -StageRoot $fullStage
Test-RevampedLicenses -StageRoot $isolatedStage
Test-NoLocalBuildPaths -StagePaths @($fullStage, $isolatedStage)

$fullZip = Join-Path $workRoot 'complete-payload.zip'
$fullManifest = Join-Path $workRoot 'complete-payload-manifest.json'
$erZip = Join-Path $workRoot 'eldenring-payload.zip'
$erManifest = Join-Path $workRoot 'eldenring-payload-manifest.json'
$fullCount = New-PayloadArchive -StagePath $fullStage -ZipPath $fullZip -ManifestPath $fullManifest
$erCount = New-PayloadArchive -StagePath $isolatedStage -ZipPath $erZip -ManifestPath $erManifest
Test-PayloadArchive -ZipPath $fullZip -ManifestPath $fullManifest
Test-PayloadArchive -ZipPath $erZip -ManifestPath $erManifest

$generalInstaller = Join-Path $releaseRoot "MO2-Revamped-Setup-$version.exe"
$generalResources = @(
    "/resource:$fullZip,Mo2RevampedSetup.payload.zip",
    "/resource:$fullManifest,Mo2RevampedSetup.payload-manifest.json",
    "/resource:$erZip,Mo2RevampedSetup.eldenring-payload.zip",
    "/resource:$erManifest,Mo2RevampedSetup.eldenring-payload-manifest.json"
)
Invoke-SetupCompile -OutputPath $generalInstaller -Defines @() -Resources $generalResources
Test-EmbeddedResources -InstallerPath $generalInstaller -ExpectedResources @(
    'Mo2RevampedSetup.payload.zip', 'Mo2RevampedSetup.payload-manifest.json',
    'Mo2RevampedSetup.eldenring-payload.zip', 'Mo2RevampedSetup.eldenring-payload-manifest.json',
    'Mo2RevampedSetup.qt-runtime.zip'
)

Copy-Item -LiteralPath (Join-Path $repoRoot 'CHANGELOG.md') -Destination $releaseRoot -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination $releaseRoot -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'THIRD-PARTY-NOTICES.md') -Destination $releaseRoot -Force
$releaseLicenseDirectory = Join-Path $releaseRoot 'licenses'
New-Item -ItemType Directory -Force -Path $releaseLicenseDirectory | Out-Null
foreach ($license in @('LGPL-v3.0.txt', 'GCC-RUNTIME-LIBRARY-EXCEPTION.txt', 'MinGW-Winpthreads-BSD.txt')) {
    Copy-Item -LiteralPath (Join-Path $repoRoot ('licenses\' + $license)) -Destination $releaseLicenseDirectory -Force
}
foreach ($document in @('HISTORICAL-CHANGELOG.md', 'RELEASE-AUDIT.md', 'UPSTREAMS.md')) {
    Copy-Item -LiteralPath (Join-Path $repoRoot ('docs\' + $document)) -Destination $releaseRoot -Force
}
$checksumLines = foreach ($file in @($generalInstaller)) {
    $hash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $([IO.Path]::GetFileName($file))"
}
[IO.File]::WriteAllLines((Join-Path $releaseRoot 'SHA256SUMS.txt'), $checksumLines, [Text.UTF8Encoding]::new($false))
Test-NoLocalBuildPaths -StagePaths @($releaseRoot)

Write-Host "Complete payload: $fullCount files; Elden Ring isolated payload: $erCount files."
Write-Host "Release installers are in: $releaseRoot"
