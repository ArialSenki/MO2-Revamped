param(
    [Parameter(Mandatory = $true)]
    [string]$ZigPath
)

$ErrorActionPreference = 'Stop'
$pluginDirectory = $PSScriptRoot
$source = Join-Path $pluginDirectory 'eldenring_mo2_bridge.cpp'
$vendor = Join-Path $pluginDirectory 'vendor\minhook'
$output = Join-Path $pluginDirectory 'eldenring_mo2_bridge.dll'

if (-not (Test-Path -LiteralPath $ZigPath -PathType Leaf)) {
    throw "Zig compiler not found: $ZigPath"
}

$zigVersion = (& $ZigPath version).Trim()
if ($LASTEXITCODE -ne 0) {
    throw "Could not query the Zig compiler version: $ZigPath"
}
if ($zigVersion -ne '0.13.0') {
    throw "This bridge is pinned to Zig 0.13.0; found $zigVersion at $ZigPath"
}

$cacheRoot = Join-Path ([System.IO.Path]::GetTempPath()) 'eldenring-mo2-zig-cache'
New-Item -ItemType Directory -Force -Path $cacheRoot | Out-Null
$artifactDirectory = Join-Path $cacheRoot 'artifact'
New-Item -ItemType Directory -Force -Path $artifactDirectory | Out-Null
$buildOutput = Join-Path $artifactDirectory 'eldenring_mo2_bridge.dll'
$oldGlobalCache = $env:ZIG_GLOBAL_CACHE_DIR
$oldLocalCache = $env:ZIG_LOCAL_CACHE_DIR
try {
    $env:ZIG_GLOBAL_CACHE_DIR = Join-Path $cacheRoot 'global'
    $env:ZIG_LOCAL_CACHE_DIR = Join-Path $cacheRoot 'local'

    & $ZigPath c++ `
        -target x86_64-windows-gnu `
        -O2 `
        -shared `
        -o $buildOutput `
        $source `
        (Join-Path $vendor 'hook.c') `
        (Join-Path $vendor 'trampoline.c') `
        (Join-Path $vendor 'buffer.c') `
        (Join-Path $vendor 'hde64.c') `
        -luser32 `
        -lgdi32 `
        -lkernel32

    if ($LASTEXITCODE -ne 0) {
        throw "Zig exited with code $LASTEXITCODE"
    }
    Copy-Item -LiteralPath $buildOutput -Destination $output -Force
} finally {
    $env:ZIG_GLOBAL_CACHE_DIR = $oldGlobalCache
    $env:ZIG_LOCAL_CACHE_DIR = $oldLocalCache
}

Write-Host "Built $output"
