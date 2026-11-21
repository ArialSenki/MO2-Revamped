<#
Golden tests for the Bethesda game plugins (MO2 Revamped): for each case, a fake
tree (make_tree.py), a portable test bed opens it, and the profile's plugins.txt
and loadorder.txt are compared byte for byte with golden\<game>\<case>\.

  run_golden.ps1 -Bed <portable MO2 folder> -Work <scratch folder> [-Game falloutnv] [-Record] [-Cases big,case]

-Game is skyrimse (the default) or falloutnv; -Cases defaults to all of that
game's cases. -Record writes golden\<game>\<case>\ from the bed instead of
comparing. The bed's ModOrganizer.ini and plugins\mo2r_golden_dump.py are
replaced; use a bed made for tests. Needs D:\mo2build\scripts (desk.ps1,
closepid.ps1, mo2lock.ps1).

Not repeatable with the current plugins (checked 2026-11-21, same bed, two runs):
plugins with no place from loadorder.txt (no loadorder.txt, or left out of it)
come out in a different order each run; with New Vegas, the mods' file times
don't decide it (the Data folder's do). So these goldens are one sample, not a
contract: skyrimse mixed; falloutnv mtime, disagree, disagree-old, mixed,
unicode (and big, not run twice). Stable: skyrimse big, case, unicode;
falloutnv case. MO2 also dates every plugin, Data included, to
2000-01-(1 + priority) on each save of the list (PluginList::saveLoadOrder).
#>
param([Parameter(Mandatory)][string]$Bed, [Parameter(Mandatory)][string]$Work,
      [ValidateSet('skyrimse', 'falloutnv')][string]$Game = 'skyrimse',
      [string[]]$Cases, [switch]$Record)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$tools = 'D:\mo2build\scripts'
$allCases = @{
  skyrimse  = @('big', 'case', 'unicode', 'mixed')
  falloutnv = @('mtime', 'disagree', 'disagree-old', 'mixed', 'unicode', 'case', 'big')
}
if (-not $Cases) { $Cases = $allCases[$Game] }
$failed = 0
$locked = $false
try {
  Copy-Item "$here\mo2r_golden_dump.py" "$Bed\plugins\mo2r_golden_dump.py" -Force
  Set-Content "$Bed\portable.txt" ''
  foreach ($case in $Cases) {
    $root = Join-Path $Work "$Game-$case"
    & py -3.12 -I "$here\make_tree.py" $root $case $Game
    if ($LASTEXITCODE -ne 0) { throw "make_tree.py failed for $case" }
    $ini = Get-Content -Raw "$root\instance\ModOrganizer.ini"
    $base = "$root\instance".Replace('\', '/')
    Set-Content -Encoding ascii "$Bed\ModOrganizer.ini" ($ini + "version=2.5.2`n`n[Settings]`nbase_directory=$base`nstyle=dark.qss`nrevamped_theme_choice_prompt_completed=true`n")
    $out = Join-Path $Work "$Game-$case-out"
    Remove-Item $out -Recurse -Force -ErrorAction SilentlyContinue
    $env:MO2R_GOLDEN_OUT = $out
    # the lock for each case, not the whole run: mo2lock counts a lock older
    # than 30 min with no MO2 running as stale, so one taken at the start could
    # be lost between cases. No lock in 30 min: the run stops.
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$tools\mo2lock.ps1" take -Owner bethesda-golden -WaitSeconds 1800 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "no MO2 lock for $case; run stopped" }
    $locked = $true
    $o = & "$tools\vis\desk.ps1" -CommandLine "`"$Bed\ModOrganizer.exe`"" -WorkDir $Bed
    $procId = [int](($o | Select-String 'started pid (\d+)').Matches[0].Groups[1].Value)
    $t = Get-Date
    while (-not (Test-Path "$out\done") -and ((Get-Date) - $t).TotalSeconds -lt 400) { Start-Sleep 2 }
    & "$tools\vis\desk.ps1" -CommandLine "powershell.exe -NoProfile -ExecutionPolicy Bypass -File $tools\vis\closepid.ps1 -Target $procId" -WorkDir "$tools\vis" | Out-Null
    for ($i = 0; $i -lt 60 -and (Get-Process -Id $procId -ErrorAction SilentlyContinue); $i++) { Start-Sleep 1 }
    if (Get-Process -Id $procId -ErrorAction SilentlyContinue) { Stop-Process -Id $procId -Force; Start-Sleep 2 }
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$tools\mo2lock.ps1" release -Owner bethesda-golden | Out-Null
    $locked = $false
    if (-not (Test-Path "$out\done")) { "$case`: no output"; $failed++; continue }
    # what MO2 left in the profile once closed (it saves the lists on exit too)
    foreach ($f in 'plugins.txt', 'loadorder.txt') { Copy-Item "$root\instance\profiles\Default\$f" "$out\$f" -Force }
    $golden = Join-Path $here "golden\$Game\$case"
    if ($Record) {
      New-Item -ItemType Directory -Force $golden | Out-Null
      Copy-Item "$out\plugins.txt", "$out\loadorder.txt" $golden -Force
      "$Game $case`: recorded ($(Get-Content "$out\done") plugins in the list)"
      continue
    }
    foreach ($f in 'plugins.txt', 'loadorder.txt') {
      $same = (Get-FileHash "$out\$f").Hash -eq (Get-FileHash "$golden\$f").Hash
      if ($same) { "$Game $case $f`: same"; continue }
      # the same lines (bytes) in another order: what an unstable golden can
      # still check (see the note at the top)
      $a = [IO.File]::ReadAllBytes("$out\$f"); $b = [IO.File]::ReadAllBytes("$golden\$f")
      $enc = [Text.Encoding]::GetEncoding(28591)
      $la = $enc.GetString($a) -split "`r`n" | Sort-Object -CaseSensitive
      $lb = $enc.GetString($b) -split "`r`n" | Sort-Object -CaseSensitive
      $sameLines = ($la -join "`n") -ceq ($lb -join "`n")
      "$Game $case $f`: $(if ($sameLines) { 'same lines, other order' } else { 'DIFFERENT' })"
      $failed++
    }
  }
} finally {
  Remove-Item "$Bed\plugins\mo2r_golden_dump.py" -Force -ErrorAction SilentlyContinue
  if ($locked) { & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$tools\mo2lock.ps1" release -Owner bethesda-golden | Out-Null }
}
if ($failed) { "FAILED: $failed"; exit 1 } else { 'all passed' }
