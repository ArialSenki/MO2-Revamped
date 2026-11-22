<#
Download and install stress run (MO2 Revamped, test only): a small fake Skyrim SE
instance with its own downloads folder, a local server (server.py) with the
archives of make_archives.py, and mo2r_dlstress.py driving MO2 on the private
desktop under the MO2 lock. Result in <Out>\result.json.

  run_dlstress.ps1 -Bed <test bed> -Work <scratch folder> -Out <folder> [-Exe <exe>]
#>
param([Parameter(Mandatory)][string]$Bed, [Parameter(Mandatory)][string]$Work,
      [Parameter(Mandatory)][string]$Out, [string]$Exe = '', [int]$TimeoutSeconds = 600)
$ErrorActionPreference = 'Stop'
$here  = $PSScriptRoot
$tools = 'D:\mo2build\scripts'
$tree  = Join-Path $Work 'tree'
$srv   = Join-Path $Work 'srv'

& py -3.12 -I "$here\..\stress\make_stress_tree.py" $tree 5
& py -3.12 -I "$here\make_archives.py" $srv | Out-Null
$downloads = Join-Path $tree 'instance\downloads'
New-Item -ItemType Directory -Force $downloads | Out-Null

$portFile = Join-Path $Work 'port.txt'
Remove-Item $portFile, "$portFile.log" -ErrorAction SilentlyContinue
$server = Start-Process py -ArgumentList '-3.12', '-I', "`"$here\server.py`"", "`"$srv`"", "`"$portFile`"" -PassThru -WindowStyle Hidden
for ($i = 0; $i -lt 50 -and -not (Test-Path $portFile); $i++) { Start-Sleep -Milliseconds 200 }
$port = (Get-Content $portFile).Trim()
"server on port $port"

while (Get-Process ModOrganizer -ErrorAction SilentlyContinue) { Start-Sleep 20 }
& "$tools\mo2lock.ps1" take -Owner audit-stability -WaitSeconds 7200 | Out-Null
if ($LASTEXITCODE -ne 0) { Stop-Process -Id $server.Id -Force; throw 'no MO2 lock' }
try {
  if ($Exe) { Copy-Item $Exe "$Bed\ModOrganizer.exe" -Force }
  Copy-Item "$here\mo2r_dlstress.py" "$Bed\plugins\mo2r_dlstress.py" -Force
  Set-Content "$Bed\portable.txt" ''
  $ini  = Get-Content -Raw "$tree\instance\ModOrganizer.ini"
  $base = "$tree\instance".Replace('\', '/')
  Set-Content -Encoding ascii "$Bed\ModOrganizer.ini" ($ini + "version=2.5.2`n`n[Settings]`nbase_directory=$base`nstyle=dark.qss`nrevamped_theme_choice_prompt_completed=true`n")
  Remove-Item $Out -Recurse -Force -ErrorAction SilentlyContinue
  $env:MO2R_DL_OUT  = $Out
  $env:MO2R_DL_BASE = "http://127.0.0.1:$port"
  $env:MO2R_DL_SRV  = $srv
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
  Copy-Item "$portFile.log" "$Out\server.log" -ErrorAction SilentlyContinue
  if (Test-Path "$Out\result.json") { Get-Content "$Out\result.json" -Raw -Encoding utf8 }
} finally {
  Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
  Remove-Item "$Bed\plugins\mo2r_dlstress.py" -Force -ErrorAction SilentlyContinue
  icacls $downloads /remove:d $env:USERNAME | Out-Null
  & "$tools\mo2lock.ps1" release -Owner audit-stability | Out-Null
}
