param(
    [Parameter(Mandatory = $true)]
    [string]$RuntimeDirectory,
    [Parameter(Mandatory = $true)]
    [string]$OrganizerExecutable,
    [Parameter(Mandatory = $true)]
    [string]$ZigPath,
    [Parameter(Mandatory = $true)]
    [string]$OpenSslRuntimeDirectory,
    [Parameter(Mandatory = $true)]
    [string]$PythonRuntimeDirectory,
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [string]$CscPath = "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
)

$ErrorActionPreference = 'Stop'
$version = '1.0.0'
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$runtimeRoot = [System.IO.Path]::GetFullPath($RuntimeDirectory).TrimEnd('\')
$organizerPath = [System.IO.Path]::GetFullPath($OrganizerExecutable)
$openSslRuntimeRoot = [System.IO.Path]::GetFullPath($OpenSslRuntimeDirectory).TrimEnd('\')
$pythonRuntimeRoot = [System.IO.Path]::GetFullPath($PythonRuntimeDirectory).TrimEnd('\')
$outputRoot = [System.IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')

foreach ($path in @($runtimeRoot, $organizerPath, $ZigPath, $openSslRuntimeRoot, $pythonRuntimeRoot, $CscPath)) {
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
    (Join-Path $sourceDir 'CompactSetupForm.cs')
)
$iconPath = Join-Path $runtimeRoot 'MO2-Revamped.ico'
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
    }
    if ($Defines.Count -gt 0) {
        $arguments += '/define:' + [string]::Join(';', $Defines)
    }
    $arguments += $Resources
    $arguments += $sourceFiles
    Invoke-Compiler -Arguments $arguments
    if (-not (Test-Path -LiteralPath $OutputPath -PathType Leaf)) {
        throw "The compiler did not create the expected executable: $OutputPath"
    }
}

function Test-ExcludedRuntimeFile {
    param([string]$RelativePath)
    $normalized = $RelativePath.Replace('/', '\')
    $parts = $normalized -split '\\'
    $excludedDirectories = @(
        'profiles', 'mods', 'downloads', 'overwrite', 'logs', 'crashdumps',
        'webcache', 'portableinstances', '_revampedupdatebackups', 'crashes',
        'diagnostics', '__pycache__', 'work', 'build'
    )
    foreach ($part in $parts[0..([Math]::Max(0, $parts.Length - 2))]) {
        if ($excludedDirectories -contains $part.ToLowerInvariant()) { return $true }
    }
    $leaf = $parts[$parts.Length - 1]
    if ($parts.Length -eq 1 -and $leaf -match '^(ModOrganizer\.ini|MO2-Revamped\.ini|categories\.dat|nexuscatmap\.dat|portable\.txt|eldenring-only\.portable|unins000\.exe|MO2ForkUninstaller\.exe)$') {
        return $true
    }
    if ($normalized -match '^stylesheets\\\+(dark|light)\.qss$') {
        return $true
    }
    return [System.IO.Path]::GetExtension($leaf).ToLowerInvariant() -in @(
        '.log', '.dmp', '.mdmp', '.tmp', '.bak', '.old', '.pyc', '.pyo', '.lock'
    )
}

function Copy-RuntimeTree {
    param([string]$SourceRoot, [string]$DestinationRoot, [switch]$SkipPlugins)
    $sourceLength = $SourceRoot.Length
    $files = Get-ChildItem -LiteralPath $SourceRoot -File -Recurse -Force |
        Sort-Object FullName
    foreach ($file in $files) {
        if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { continue }
        $relative = $file.FullName.Substring($sourceLength).TrimStart('\')
        if ($relative -ieq 'ModOrganizer.exe' -or $relative -ieq 'unins000.exe') { continue }
        if ($SkipPlugins -and $relative.StartsWith('plugins\', [StringComparison]::OrdinalIgnoreCase)) { continue }
        if (Test-ExcludedRuntimeFile -RelativePath $relative) { continue }
        $destination = Join-Path $DestinationRoot $relative
        New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($destination)) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
    }
}

function Copy-RevampedSources {
    param([string]$DestinationRoot, [string]$BridgePath)
    $pluginSource = Join-Path $repoRoot 'source\revamped\plugins'
    $pluginDestination = Join-Path $DestinationRoot 'plugins'
    foreach ($file in Get-ChildItem -LiteralPath $pluginSource -File -Recurse -Force) {
        if ($file.Extension -notin @('.py', '.dll')) { continue }
        if ($file.Name -ieq 'eldenring_mo2_bridge.dll' -or $file.FullName -match '\\(__pycache__|vendor)\\') { continue }
        $relative = $file.FullName.Substring($pluginSource.Length).TrimStart('\')
        if (Test-ExcludedRuntimeFile -RelativePath $relative) { continue }
        $destination = Join-Path $pluginDestination $relative
        New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($destination)) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
    }
    $bridgeDestination = Join-Path $pluginDestination 'basic_games\games\eldenring_mo2_bridge.dll'
    New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($bridgeDestination)) | Out-Null
    Copy-Item -LiteralPath $BridgePath -Destination $bridgeDestination -Force

    $themeSource = Join-Path $repoRoot 'source\revamped\themes\stylesheets'
    $themeDestination = Join-Path $DestinationRoot 'stylesheets'
    foreach ($file in Get-ChildItem -LiteralPath $themeSource -File -Recurse -Force) {
        if ($file.Extension -notin @('.qss', '.svg', '.png')) { continue }
        if (Test-ExcludedRuntimeFile -RelativePath $file.Name) { continue }
        $relative = $file.FullName.Substring($themeSource.Length).TrimStart('\')
        if ($file.Extension -ieq '.qss') {
            switch -CaseSensitive ($file.Name) {
                'MO2 Classic Refined - Light.qss' { $relative = 'Light.qss' }
                'MO2 Classic Refined - Dark.qss' { $relative = 'dark.qss' }
                default { throw "Unexpected Revamped theme stylesheet: $($file.Name)" }
            }
        }
        $destination = Join-Path $themeDestination $relative
        New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($destination)) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
    }

    foreach ($theme in @(
        @{ Source = 'MO2 Classic Refined - Light.qss'; Destination = 'Light.qss' },
        @{ Source = 'MO2 Classic Refined - Dark.qss'; Destination = 'dark.qss' }
    )) {
        $source = Join-Path $themeSource $theme.Source
        $destination = Join-Path $themeDestination $theme.Destination
        $sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
        $stagedHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
        if ($sourceHash -ine $stagedHash) {
            throw "The staged $($theme.Destination) theme does not match its reviewed source."
        }
    }

    Copy-Item -LiteralPath $organizerPath -Destination (Join-Path $DestinationRoot 'ModOrganizer.exe') -Force
    [IO.File]::WriteAllText((Join-Path $DestinationRoot 'MO2-Revamped.ini'), "[Settings]`r`nlanguage=en_US`r`n", [Text.UTF8Encoding]::new($false))
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
    $required = @('dlls/python312.dll', 'libs/pythoncore.zip', 'libs/_ssl.pyd', 'libs/_hashlib.pyd')
    $manifestNames = @($manifest.files.PSObject.Properties.Name)
    foreach ($name in $required) {
        if ($manifestNames -notcontains $name) {
            throw "The Python runtime overlay is missing a required file: $name"
        }
    }

    foreach ($property in $manifest.files.PSObject.Properties) {
        $relative = $property.Name.Replace('/', '\')
        if ($relative -notmatch '^(dlls\\python312\.dll|dlls\\libffi-8\.dll|libs\\pythoncore\.zip|libs\\[A-Za-z0-9_]+\.pyd)$') {
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
    if ($needles.Count -eq 0) { return }

    foreach ($stagePath in $StagePaths) {
        foreach ($file in Get-ChildItem -LiteralPath $stagePath -File -Recurse -Force) {
            $bytes = [IO.File]::ReadAllBytes($file.FullName)
            $ascii = [Text.Encoding]::ASCII.GetString($bytes)
            $unicode = [Text.Encoding]::Unicode.GetString($bytes)
            foreach ($needle in $needles) {
                if ($ascii.IndexOf($needle, [StringComparison]::OrdinalIgnoreCase) -ge 0 -or
                    $unicode.IndexOf($needle, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                    $relative = $file.FullName.Substring($stagePath.Length).TrimStart('\')
                    throw "A release payload contains a local build path in '$relative'. Rebuild the affected binary with source-path mapping."
                }
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
    $allowedGameFiles = @('__init__.py', 'eldenring_mo2_bridge.dll', 'game_eldenring_mo2.py')
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
                $include = $allowedGameFiles -icontains $parts[$parts.Length - 1]
            } elseif ($parts[0] -ieq 'data') {
                $include = $parts.Length -ge 2 -and $parts[1] -ieq 'DDS'
            }
        }
        if (-not $include) { continue }
        $destination = Join-Path $targetPlugins $relative
        New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($destination)) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
    }
    foreach ($required in @(
        'eldenring_mo2_native_profile_tool.py', 'eldenring_mo2_save_isolation.py',
        'eldenring_mo2_startup_tool.py', 'eldenring_mo2_archive_installer\installer.py',
        'basic_games\games\game_eldenring_mo2.py',
        'basic_games\games\eldenring_mo2_bridge.dll', 'plugin_python\plugin_python.dll',
        'plugin_python\dlls\python312.dll', 'plugin_python\libs\pythoncore.zip',
        'plugin_python\libs\_ssl.pyd', 'plugin_python\libs\_hashlib.pyd'
    )) {
        if (-not (Test-Path -LiteralPath (Join-Path $targetPlugins $required) -PathType Leaf)) {
            throw "The isolated Elden Ring payload is missing a required plugin file: $required"
        }
    }
    foreach ($unwanted in @('pyCfg.py', 'data\settings.json', 'data\pyCfgDialog.py', 'installer_omod\installer_omod.dll')) {
        if (Test-Path -LiteralPath (Join-Path $targetPlugins $unwanted)) {
            throw "The Elden Ring-only payload includes an unrelated or user-specific plugin file: $unwanted"
        }
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
    $expected = @($ExpectedResources | Sort-Object)
    if ([string]::Join("`n", $actual) -cne [string]::Join("`n", $expected)) {
        throw "Embedded resources do not match the requested package in $InstallerPath. Found: $([string]::Join(', ', $actual))"
    }
}

$bridgePath = Join-Path $buildRoot 'eldenring_mo2_bridge.dll'
$bridgeBuildScript = Join-Path $repoRoot 'source\revamped\plugins\basic_games\games\build_bridge.ps1'
& $bridgeBuildScript -ZigPath $ZigPath -OutputPath $bridgePath
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $bridgePath -PathType Leaf)) {
    throw 'Building the Elden Ring native bridge failed.'
}

$generalUninstallerStub = Join-Path $buildRoot 'unins000-general.exe'
$eldenRingUninstallerStub = Join-Path $buildRoot 'unins000-eldenring.exe'
Invoke-SetupCompile -OutputPath $generalUninstallerStub -Defines @('UNINSTALLER_STUB') -Resources @()
Invoke-SetupCompile -OutputPath $eldenRingUninstallerStub -Defines @('UNINSTALLER_STUB', 'ELDENRING_ONLY_INSTALLER') -Resources @()

$fullStage = Join-Path $stageRoot 'complete'
$isolatedStage = Join-Path $stageRoot 'eldenring-isolated'
New-Item -ItemType Directory -Force -Path $fullStage | Out-Null
Copy-RuntimeTree -SourceRoot $runtimeRoot -DestinationRoot $fullStage
Copy-RevampedSources -DestinationRoot $fullStage -BridgePath $bridgePath
Copy-OpenSslRuntime -RuntimeSource $openSslRuntimeRoot -DestinationRoot $fullStage
Copy-PythonRuntime -RuntimeSource $pythonRuntimeRoot -DestinationRoot $fullStage
Copy-Item -LiteralPath $generalUninstallerStub -Destination (Join-Path $fullStage 'unins000.exe') -Force

New-Item -ItemType Directory -Force -Path $isolatedStage | Out-Null
Copy-RuntimeTree -SourceRoot $fullStage -DestinationRoot $isolatedStage -SkipPlugins
Copy-EldenRingPlugins -SourceRoot $fullStage -DestinationRoot $isolatedStage
foreach ($rootFile in @('ModOrganizer.exe', 'MO2-Revamped.ini')) {
    Copy-Item -LiteralPath (Join-Path $fullStage $rootFile) -Destination (Join-Path $isolatedStage $rootFile) -Force
}
Copy-Item -LiteralPath $eldenRingUninstallerStub -Destination (Join-Path $isolatedStage 'unins000.exe') -Force

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
    'Mo2RevampedSetup.eldenring-payload.zip', 'Mo2RevampedSetup.eldenring-payload-manifest.json'
)

$isolatedInstaller = Join-Path $releaseRoot "MO2-Revamped-Elden-Ring-Isolated-Setup-$version.exe"
$isolatedResources = @(
    "/resource:$erZip,Mo2RevampedSetup.eldenring-payload.zip",
    "/resource:$erManifest,Mo2RevampedSetup.eldenring-payload-manifest.json"
)
Invoke-SetupCompile -OutputPath $isolatedInstaller -Defines @('ELDENRING_ONLY_INSTALLER') -Resources $isolatedResources
Test-EmbeddedResources -InstallerPath $isolatedInstaller -ExpectedResources @(
    'Mo2RevampedSetup.eldenring-payload.zip', 'Mo2RevampedSetup.eldenring-payload-manifest.json'
)

Copy-Item -LiteralPath (Join-Path $repoRoot 'CHANGELOG.md') -Destination $releaseRoot -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination $releaseRoot -Force
Copy-Item -LiteralPath (Join-Path $repoRoot 'THIRD-PARTY-NOTICES.md') -Destination $releaseRoot -Force
foreach ($document in @('HISTORICAL-CHANGELOG.md', 'RELEASE-AUDIT.md', 'UPSTREAMS.md')) {
    Copy-Item -LiteralPath (Join-Path $repoRoot ('docs\' + $document)) -Destination $releaseRoot -Force
}
$checksumLines = foreach ($file in @($generalInstaller, $isolatedInstaller)) {
    $hash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $([IO.Path]::GetFileName($file))"
}
[IO.File]::WriteAllLines((Join-Path $releaseRoot 'SHA256SUMS.txt'), $checksumLines, [Text.UTF8Encoding]::new($false))
Test-NoLocalBuildPaths -StagePaths @($releaseRoot)

Write-Host "Complete payload: $fullCount files; Elden Ring isolated payload: $erCount files."
Write-Host "Release installers are in: $releaseRoot"
