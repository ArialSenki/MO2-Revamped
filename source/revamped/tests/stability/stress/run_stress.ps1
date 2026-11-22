<#
Stability stress run (MO2 Revamped, test only): a big fake Skyrim SE instance
(make_stress_tree.py, written into -Tree, outside the repo), a test bed opened on it
on the private desktop under the MO2 lock, and mo2r_stress.py driving it: fast
mod toggles, the plugin view's selection across a fast-path disable, mods
enabled during a refresh, then a check that every active mod's plugins are
listed. Result in <Out>\result.json.

  py -3.12 -I make_stress_tree.py <tree> 1500
  run_stress.ps1 -Bed <test bed> -Tree <tree> -Out <folder> [-Exe <ModOrganizer.exe to copy in>]

MO2R_STRESS_PHASES=selection stops after the selection phase.
Needs D:\mo2build\scripts (desk.ps1, closepid.ps1, mo2lock.ps1).
#>
param([Parameter(Mandatory)][string]$Bed, [Parameter(Mandatory)][string]$Out,
      [Parameter(Mandatory)][string]$Tree, [string]$Exe = '', [int]$TimeoutSeconds = 900)
$ErrorActionPreference = 'Stop'
$here  = $PSScriptRoot
$tools = 'D:\mo2build\scripts'
$tree  = $Tree
while (Get-Process ModOrganizer -ErrorAction SilentlyContinue) { Start-Sleep 20 }
& "$tools\mo2lock.ps1" take -Owner audit-stability -WaitSeconds 3600 | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'no MO2 lock' }
try {
  if ($Exe) { Copy-Item $Exe "$Bed\ModOrganizer.exe" -Force }
  Copy-Item "$here\mo2r_stress.py" "$Bed\plugins\mo2r_stress.py" -Force
  Set-Content "$Bed\portable.txt" ''
  $ini  = Get-Content -Raw "$tree\instance\ModOrganizer.ini"
  $base = "$tree\instance".Replace('\', '/')
  Set-Content -Encoding ascii "$Bed\ModOrganizer.ini" ($ini + "version=2.5.2`n`n[Settings]`nbase_directory=$base`nstyle=dark.qss`nrevamped_theme_choice_prompt_completed=true`n")
  Remove-Item $Out -Recurse -Force -ErrorAction SilentlyContinue
  $env:MO2R_STRESS_OUT = $Out
  $o = & "$tools\vis\desk.ps1" -CommandLine "`"$Bed\ModOrganizer.exe`"" -WorkDir $Bed
  $procId = [int](($o | Select-String 'started pid (\d+)').Matches[0].Groups[1].Value)
  "started pid $procId"
  $t = Get-Date
  $alive = $true
  while (-not (Test-Path "$Out\done") -and ((Get-Date) - $t).TotalSeconds -lt $TimeoutSeconds) {
    Start-Sleep 3
    if (-not (Get-Process -Id $procId -ErrorAction SilentlyContinue)) { $alive = $false; break }
  }
  "elapsed $([int]((Get-Date) - $t).TotalSeconds) s, alive=$alive, done=$(Test-Path "$Out\done")"
  if ($alive) {
    & "$tools\vis\desk.ps1" -CommandLine "powershell.exe -NoProfile -ExecutionPolicy Bypass -File $tools\vis\closepid.ps1 -Target $procId" -WorkDir "$tools\vis" | Out-Null
    for ($i = 0; $i -lt 60 -and (Get-Process -Id $procId -ErrorAction SilentlyContinue); $i++) { Start-Sleep 1 }
    if (Get-Process -Id $procId -ErrorAction SilentlyContinue) { 'still running after close, stopping it'; Stop-Process -Id $procId -Force }
  }
  New-Item -ItemType Directory -Force $Out | Out-Null
  Copy-Item "$Bed\logs\mo_interface.log" "$Out\mo_interface.log" -ErrorAction SilentlyContinue
  if (Test-Path "$Out\result.json") { Get-Content "$Out\result.json" -Raw }
} finally {
  Remove-Item "$Bed\plugins\mo2r_stress.py" -Force -ErrorAction SilentlyContinue
  & "$tools\mo2lock.ps1" release -Owner audit-stability | Out-Null
}
