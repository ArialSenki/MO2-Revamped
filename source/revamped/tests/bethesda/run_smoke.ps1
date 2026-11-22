<#
Smoke tests for the 14 Bethesda game plugins (MO2 Revamped): each game opened
in MO2 once with the old game_*.dll and once with the new one, on a fresh fake
tree each time (make_smoke.py), and what mobase shows compared
(mo2r_smoke_dump.py, then make_smoke.py diff).

  run_smoke.ps1 [-Games skyrimse,morrowind] [-OldBed ...] [-NewBed ...] [-Work ...] [-SavesRoot ...]

The beds differ only in their game plugins. Their ModOrganizer.ini and
plugins\mo2r_smoke_dump.py are replaced per case. The MO2 lock is taken for
each start and released after it (in finally too); no lock within
-LockWaitSeconds stops the run. MO2 runs on a private desktop (desk.ps1) and
is closed with closepid.ps1. Each run's mo_interface.log is kept next to its
JSON, and its warnings and errors printed. Folders the old or new plugin made
under Documents\My Games (Starfield makes its Data folder there) are removed
again if they are empty.

-SavesRoot is the deep pass: each game's profile gets the files of
<SavesRoot>\<game> (real saves, for the Saves tab and its missing plugins) and
automatic archive invalidation on; use its own -Work.
#>
param([string[]]$Games, [string]$OldBed = 'D:\mo2build\bed-smoke-old', [string]$NewBed = 'D:\mo2build\bed-smoke-new',
      [string]$Work = 'D:\mo2build\smoke-work', [int]$LockWaitSeconds = 1800, [int]$StartTimeout = 180,
      [string]$SavesRoot)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$tools = 'D:\mo2build\scripts'
$all = 'skyrim', 'skyrimse', 'skyrimvr', 'enderal', 'enderalse', 'fallout3', 'falloutnv', 'ttw',
       'fallout4', 'fallout4vr', 'oblivion', 'nehrim', 'morrowind', 'starfield'
if (-not $Games) { $Games = $all }
$Games = $Games | ForEach-Object { $_ -split ',' } | Where-Object { $_ }
foreach ($g in $Games) { if ($all -notcontains $g) { throw "unknown game $g" } }
New-Item -ItemType Directory -Force $Work | Out-Null
$myGames = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games'
$locked = $false
$summary = @()

function Get-Dirs($path) {
  if (Test-Path $path) { @(Get-ChildItem $path -Directory -Recurse -Force | ForEach-Object FullName) } else { @() }
}

function Invoke-Case($game, $bed, $tag) {
  $root = Join-Path $Work "$game-$tag"
  $out = Join-Path $Work "$game-$tag-out"
  $extra = @()
  if ($SavesRoot) { $sv = Join-Path $SavesRoot $game; New-Item -ItemType Directory -Force $sv | Out-Null; $extra = @($sv) }
  & py -3.12 -I "$here\make_smoke.py" $root $game @extra | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "make_smoke.py failed for $game" }
  Remove-Item $out -Recurse -Force -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force $out | Out-Null
  $ini = Get-Content -Raw "$root\instance\ModOrganizer.ini"
  $base = "$root\instance".Replace('\', '/')
  Set-Content -Encoding ascii "$bed\ModOrganizer.ini" ($ini + "version=2.5.2`n`n[Settings]`nbase_directory=$base`nstyle=dark.qss`nrevamped_theme_choice_prompt_completed=true`n")
  Copy-Item "$here\mo2r_smoke_dump.py" "$bed\plugins\mo2r_smoke_dump.py" -Force
  Set-Content "$bed\portable.txt" ''
  $env:MO2R_SMOKE_OUT = $out
  $before = Get-Dirs $myGames
  $logStart = Get-Date

  & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$tools\mo2lock.ps1" take -Owner bethesda-smoke -WaitSeconds $LockWaitSeconds | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "no MO2 lock for $game ($tag); run stopped" }
  $script:locked = $true
  try {
    $o = & "$tools\vis\desk.ps1" -CommandLine "`"$bed\ModOrganizer.exe`"" -WorkDir $bed
    $procId = [int](($o | Select-String 'started pid (\d+)').Matches[0].Groups[1].Value)
    $t = Get-Date
    while (-not (Test-Path "$out\done") -and ((Get-Date) - $t).TotalSeconds -lt $StartTimeout -and
           (Get-Process -Id $procId -ErrorAction SilentlyContinue)) { Start-Sleep 2 }
    $exitedEarly = -not (Get-Process -Id $procId -ErrorAction SilentlyContinue)
    & "$tools\vis\desk.ps1" -CommandLine "powershell.exe -NoProfile -ExecutionPolicy Bypass -File $tools\vis\closepid.ps1 -Target $procId" -WorkDir "$tools\vis" | Out-Null
    for ($i = 0; $i -lt 60 -and (Get-Process -Id $procId -ErrorAction SilentlyContinue); $i++) { Start-Sleep 1 }
    if (Get-Process -Id $procId -ErrorAction SilentlyContinue) { Stop-Process -Id $procId -Force; Start-Sleep 2; Write-Host "  $game $tag`: MO2 hung on close, stopped" }
  } finally {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$tools\mo2lock.ps1" release -Owner bethesda-smoke | Out-Null
    $script:locked = $false
    Remove-Item "$bed\plugins\mo2r_smoke_dump.py" -Force -ErrorAction SilentlyContinue
  }

  # this start's log (the bed's, or the instance's)
  foreach ($l in "$bed\logs\mo_interface.log", "$root\instance\logs\mo_interface.log") {
    if ((Test-Path $l) -and (Get-Item $l).LastWriteTime -ge $logStart) { Copy-Item $l "$out\mo_interface.log" -Force; break }
  }
  # what MO2 left in the profile once closed
  foreach ($f in 'plugins.txt', 'loadorder.txt') {
    if (Test-Path "$root\instance\profiles\Default\$f") { Copy-Item "$root\instance\profiles\Default\$f" "$out\$f.closed" -Force }
  }
  # folders made under My Games: kept in the report, removed if empty
  $made = @(Get-Dirs $myGames | Where-Object { $before -notcontains $_ })
  foreach ($d in ($made | Sort-Object Length -Descending)) {
    if (-not (Get-ChildItem $d -Force -Recurse -File -ErrorAction SilentlyContinue)) { Remove-Item $d -Recurse -Force -ErrorAction SilentlyContinue }
  }
  [pscustomobject]@{ Opened = (Test-Path "$out\smoke.json"); ExitedEarly = $exitedEarly; Out = $out; Root = $root; MyGames = $made }
}

function Get-LogProblems($out) {
  $log = "$out\mo_interface.log"
  if (-not (Test-Path $log)) { return @('(no log)') }
  # "[2026-11-21 13:07:01.132 W] text": warnings and errors without the time, once each
  @(Get-Content $log -Encoding UTF8 | Where-Object { $_ -match '^\[[\d-]+ [\d:.]+ (W|E)\] ' } |
    ForEach-Object { $_ -replace '^\[[\d-]+ [\d:.]+ (\w)\] ', '$1: ' } | Select-Object -Unique)
}

try {
  foreach ($game in $Games) {
    "=== $game"
    $old = Invoke-Case $game $OldBed 'old'
    $new = Invoke-Case $game $NewBed 'new'
    "  old opened: $($old.Opened)$(if ($old.ExitedEarly -and -not $old.Opened) { ' (MO2 exited)' }); new opened: $($new.Opened)$(if ($new.ExitedEarly -and -not $new.Opened) { ' (MO2 exited)' })"
    $result = 'not compared'
    if ($old.Opened -and $new.Opened) {
      & py -3.12 -I "$here\make_smoke.py" diff "$($old.Out)\smoke.json" "$($new.Out)\smoke.json" $old.Root $new.Root $OldBed $NewBed
      $result = if ($LASTEXITCODE -eq 0) { 'identical' } else { 'differences' }
    }
    # the profile files MO2 wrote, byte for byte
    foreach ($f in 'plugins.txt', 'loadorder.txt', 'plugins.txt.closed', 'loadorder.txt.closed') {
      $a = "$($old.Out)\$f"; $b = "$($new.Out)\$f"
      if ((Test-Path $a) -ne (Test-Path $b)) { "  $f`: only in $(if (Test-Path $a) { 'old' } else { 'new' })"; $result = 'differences' }
      elseif ((Test-Path $a) -and (Get-FileHash $a).Hash -ne (Get-FileHash $b).Hash) { "  $f`: different"; $result = 'differences' }
    }
    foreach ($r in @(@('old', $old), @('new', $new))) {
      $p = Get-LogProblems $r[1].Out
      if ($p.Count) { "  log $($r[0]) ($($p.Count) warnings/errors):"; $p | ForEach-Object { "    $_" } }
      if ($r[1].MyGames.Count) { "  $($r[0]) made under My Games: $($r[1].MyGames -join ', ')" }
    }
    $summary += "{0,-11} old={1,-5} new={2,-5} {3}" -f $game, $old.Opened, $new.Opened, $result
  }
} finally {
  if ($locked) { & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$tools\mo2lock.ps1" release -Owner bethesda-smoke | Out-Null }
  Remove-Item "$OldBed\plugins\mo2r_smoke_dump.py", "$NewBed\plugins\mo2r_smoke_dump.py" -Force -ErrorAction SilentlyContinue
}
''
'=== summary'
$summary
# 1 when a game didn't open with one of the plugins; differences are for reading
if ($summary | Where-Object { $_ -match '=False' }) { exit 1 } else { exit 0 }
