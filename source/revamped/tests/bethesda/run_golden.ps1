<#
Golden tests for the Bethesda game plugins (MO2 Revamped): for each case, a fake
tree (make_tree.py), a portable test bed opens it, and the profile's plugins.txt
and loadorder.txt are compared byte for byte with golden\<case>\.

  run_golden.ps1 -Bed <portable MO2 folder> -Work <scratch folder> [-Record] [-Cases big,case]

-Record writes golden\<case>\ from the bed instead of comparing. The bed's
ModOrganizer.ini and plugins\mo2r_golden_dump.py are replaced; use a bed made
for tests. Needs D:\mo2build\scripts (desk.ps1, closepid.ps1, mo2lock.ps1).
#>
param([Parameter(Mandatory)][string]$Bed, [Parameter(Mandatory)][string]$Work,
      [string[]]$Cases = @('big', 'case', 'unicode', 'mixed'), [switch]$Record)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$tools = 'D:\mo2build\scripts'
if (Get-Process ModOrganizer -ErrorAction SilentlyContinue) { throw 'a ModOrganizer.exe is running' }
& "$tools\mo2lock.ps1" take -Owner bethesda-golden -WaitSeconds 1800 | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'no MO2 lock' }
$failed = 0
try {
  Copy-Item "$here\mo2r_golden_dump.py" "$Bed\plugins\mo2r_golden_dump.py" -Force
  Set-Content "$Bed\portable.txt" ''
  foreach ($case in $Cases) {
    $root = Join-Path $Work $case
    & py -3.12 -I "$here\make_tree.py" $root $case
    $ini = Get-Content -Raw "$root\instance\ModOrganizer.ini"
    $base = "$root\instance".Replace('\', '/')
    Set-Content -Encoding ascii "$Bed\ModOrganizer.ini" ($ini + "version=2.5.2`n`n[Settings]`nbase_directory=$base`nstyle=dark.qss`nrevamped_theme_choice_prompt_completed=true`n")
    $out = Join-Path $Work "$case-out"
    Remove-Item $out -Recurse -Force -ErrorAction SilentlyContinue
    $env:MO2R_GOLDEN_OUT = $out
    $o = & "$tools\vis\desk.ps1" -CommandLine "`"$Bed\ModOrganizer.exe`"" -WorkDir $Bed
    $procId = [int](($o | Select-String 'started pid (\d+)').Matches[0].Groups[1].Value)
    $t = Get-Date
    while (-not (Test-Path "$out\done") -and ((Get-Date) - $t).TotalSeconds -lt 400) { Start-Sleep 2 }
    & "$tools\vis\desk.ps1" -CommandLine "powershell.exe -NoProfile -ExecutionPolicy Bypass -File $tools\vis\closepid.ps1 -Target $procId" -WorkDir "$tools\vis" | Out-Null
    for ($i = 0; $i -lt 60 -and (Get-Process -Id $procId -ErrorAction SilentlyContinue); $i++) { Start-Sleep 1 }
    if (-not (Test-Path "$out\done")) { "$case`: no output"; $failed++; continue }
    # what MO2 left in the profile once closed (it saves the lists on exit too)
    foreach ($f in 'plugins.txt', 'loadorder.txt') { Copy-Item "$root\instance\profiles\Default\$f" "$out\$f" -Force }
    $golden = Join-Path $here "golden\$case"
    if ($Record) {
      New-Item -ItemType Directory -Force $golden | Out-Null
      Copy-Item "$out\plugins.txt", "$out\loadorder.txt" $golden -Force
      "$case`: recorded ($(Get-Content "$out\done") plugins in the list)"
      continue
    }
    foreach ($f in 'plugins.txt', 'loadorder.txt') {
      $same = (Get-FileHash "$out\$f").Hash -eq (Get-FileHash "$golden\$f").Hash
      "$case $f`: $(if ($same) { 'same' } else { 'DIFFERENT' })"
      if (-not $same) { $failed++ }
    }
  }
} finally {
  Remove-Item "$Bed\plugins\mo2r_golden_dump.py" -Force -ErrorAction SilentlyContinue
  & "$tools\mo2lock.ps1" release -Owner bethesda-golden | Out-Null
}
if ($failed) { "FAILED: $failed"; exit 1 } else { 'all passed' }
