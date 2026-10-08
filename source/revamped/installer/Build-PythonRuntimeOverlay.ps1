[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PythonSourceDirectory,
    [Parameter(Mandatory = $true)]
    [string]$PythonBuildDirectory,
    [Parameter(Mandatory = $true)]
    [string]$BasePluginPythonDirectory,
    [Parameter(Mandatory = $true)]
    [string]$OpenSslExtensionDirectory,
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [string]$OpenSslVersion = '3.5.9'
)

$ErrorActionPreference = 'Stop'
$pythonSource = [IO.Path]::GetFullPath($PythonSourceDirectory).TrimEnd('\')
$pythonBuild = [IO.Path]::GetFullPath($PythonBuildDirectory).TrimEnd('\')
$basePlugin = [IO.Path]::GetFullPath($BasePluginPythonDirectory).TrimEnd('\')
$sslExtensions = [IO.Path]::GetFullPath($OpenSslExtensionDirectory).TrimEnd('\')
$output = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
$pythonExe = Join-Path $pythonBuild 'python.exe'
$helper = Join-Path $PSScriptRoot 'build_python_runtime_overlay.py'

foreach ($file in @($pythonExe, (Join-Path $pythonBuild 'python312.dll'), $helper)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Required Python overlay input was not found: $file"
    }
}
foreach ($directory in @($pythonSource, $basePlugin, $sslExtensions)) {
    if (-not (Test-Path -LiteralPath $directory -PathType Container)) {
        throw "Required Python overlay directory was not found: $directory"
    }
}
if (Test-Path -LiteralPath $output) {
    if (@(Get-ChildItem -LiteralPath $output -Force).Count -ne 0) {
        throw "Output directory must be new or empty: $output"
    }
} else {
    New-Item -ItemType Directory -Path $output | Out-Null
}

$previousPythonHome = $env:PYTHONHOME
$previousPythonPath = $env:PYTHONPATH
try {
    $env:PYTHONHOME = $pythonSource
    $env:PYTHONPATH = Join-Path $pythonSource 'Lib'
    & $pythonExe $helper `
        --python-source $pythonSource `
        --python-build $pythonBuild `
        --base-plugin-python $basePlugin `
        --openssl-extension-directory $sslExtensions `
        --output $output `
        --openssl-version $OpenSslVersion
    if ($LASTEXITCODE -ne 0) {
        throw "Python runtime overlay preparation failed with exit code $LASTEXITCODE."
    }
} finally {
    $env:PYTHONHOME = $previousPythonHome
    $env:PYTHONPATH = $previousPythonPath
}