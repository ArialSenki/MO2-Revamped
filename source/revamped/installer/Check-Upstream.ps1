<#
Lists what Mod Organizer 2's own repositories, USVFS and the third-party
components built here have published since the versions this repository
carries, so their fixes (new game versions, crashes, Windows changes) can be
brought in by hand and checked with Build-UpstreamPlugins.ps1.

It only reads public GitHub and PyPI data. With the GitHub CLI signed in (gh)
it uses gh's token, which allows 5,000 requests an hour; without it, the
public API's 60 an hour are enough for one run.

  -Report <file>   also writes the report there (Markdown)
  -Since <date>    lists only commits after that date (yyyy-MM-dd)
#>
[CmdletBinding()]
param(
    [string]$Report,
    [string]$Since
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$commitsFile = Join-Path $repoRoot 'source\upstream\UPSTREAM-COMMITS.txt'
$sinceDate = if ($Since) { [datetime]::ParseExact($Since, 'yyyy-MM-dd', $null) } else { $null }

$gh = Get-Command gh -ErrorAction SilentlyContinue
$useGh = $false
if ($gh) {
    & $gh.Source auth status *> $null
    $useGh = ($LASTEXITCODE -eq 0)
}

function Invoke-GitHub {
    param([string]$Path)
    # GitHub answers a large compare with an occasional error: one more try
    foreach ($attempt in 1, 2) {
        $result = Invoke-GitHubOnce $Path
        if ($null -ne $result) { return $result }
        Start-Sleep -Seconds 2
    }
    return $null
}

function Invoke-GitHubOnce {
    param([string]$Path)
    if ($useGh) {
        $previous = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        $json = & $gh.Source api -H 'Accept: application/vnd.github+json' $Path 2>$null
        $code = $LASTEXITCODE
        $ErrorActionPreference = $previous
        if ($code -ne 0) { return $null }
        return ($json | Out-String | ConvertFrom-Json)
    }
    try {
        return Invoke-RestMethod -Uri "https://api.github.com/$Path" -Headers @{
            'Accept'     = 'application/vnd.github+json'
            'User-Agent' = 'mo2-revamped-upstream-check'
        }
    } catch {
        return $null
    }
}

# the bases this repository records
$sources = New-Object System.Collections.Generic.List[object]
$inSuper = $false
foreach ($line in Get-Content -LiteralPath $commitsFile) {
    if ($line -match '^modorganizer_super:') { $inSuper = $true; continue }
    if ($line -match '^\S') { $inSuper = $false }
    if ($inSuper -and $line -match '^\s+(\S+)\s+([0-9a-f]{40})\s*$') {
        $name = $Matches[1]
        $repo = switch ($name) {
            'cmake_common' { 'cmake_common' }
            'githubpp' { 'githubpp' }
            default { "modorganizer-$name" }
        }
        $sources.Add([pscustomobject]@{ Name = $name; Repo = "ModOrganizer2/$repo"; Base = $Matches[2] })
    }
}
if ($sources.Count -eq 0) { throw "No commits were read from $commitsFile" }
# recorded in docs/UPSTREAMS.md
# the organizer is v2.5.2 plus two commits that are not on MO2's main branch
$sources.Insert(0, [pscustomobject]@{ Name = 'organizer'; Repo = 'ModOrganizer2/modorganizer'; Base = 'v2.5.2' })
$sources.Add([pscustomobject]@{ Name = 'usvfs'; Repo = 'ModOrganizer2/usvfs'; Base = 'a50d84c64c9244f80dc67e9fe7af209bfe514d5b' })
$sources.Add([pscustomobject]@{ Name = 'nxmhandler'; Repo = 'ModOrganizer2/modorganizer-nxmhandler'; Base = '9876f57cd508c56028ec16ba0967ef1ead6fc47d' })
# repositories without a commit in common with ours: MO2 moved the Bethesda game
# plugins into game_bethesda (the old repositories are archived mirrors), and
# the Python plugins in source/mo2-python-plugins each have their own. Their
# commits since the 2.5.2 release are listed
$released = '2024-08-04T00:00:00Z'
foreach ($name in 'game_bethesda', 'basic_games', 'installer_wizard', 'fnistool',
    'script_extender_plugin_checker', 'preview_dds', 'form43_checker') {
    $sources.Add([pscustomobject]@{ Name = $name; Repo = "ModOrganizer2/modorganizer-$name"; Base = $null; From = $released })
}

$out = New-Object System.Collections.Generic.List[string]
$out.Add('# Upstream changes since the sources in this repository')
$out.Add('')
$out.Add("Checked $(Get-Date -Format 'yyyy-MM-dd HH:mm')" + $(if ($Since) { ", commits after $Since" } else { '' }) + '.')
$out.Add('')

$changed = 0
$failed = New-Object System.Collections.Generic.List[string]
$summary = New-Object System.Collections.Generic.List[object]
$details = New-Object System.Collections.Generic.List[string]

foreach ($source in $sources) {
    $info = Invoke-GitHub "repos/$($source.Repo)"
    if (-not $info) { $failed.Add($source.Name); continue }
    $branch = $info.default_branch
    if ($source.Base) {
        $compare = Invoke-GitHub "repos/$($source.Repo)/compare/$($source.Base)...$branch"
        if (-not $compare) { $failed.Add($source.Name); continue }
        $all = @($compare.commits)
        $ahead = [int]$compare.ahead_by
        $files = @($compare.files).Count
        $range = "$($ahead) commits after ``$($source.Base.Substring(0, [Math]::Min(7, $source.Base.Length)))``, $files files changed. Compare: https://github.com/$($source.Repo)/compare/$($source.Base.Substring(0, [Math]::Min(7, $source.Base.Length)))...$branch"
    } else {
        $all = @(Invoke-GitHub "repos/$($source.Repo)/commits?sha=$branch&since=$($source.From)&per_page=100")
        if ($all.Count -eq 1 -and $null -eq $all[0]) { $failed.Add($source.Name); continue }
        # the list API is newest first
        [array]::Reverse($all)
        $ahead = $all.Count
        $files = $null
        $range = "$ahead commits since the 2.5.2 release ($($source.From.Substring(0, 10))). History: https://github.com/$($source.Repo)/commits/$branch"
    }
    $commits = @($all | Where-Object {
        -not $sinceDate -or [datetime]$_.commit.committer.date -gt $sinceDate
    })
    $summary.Add([pscustomobject]@{
        Name   = $source.Name
        Ahead  = $ahead
        Listed = $commits.Count
        Files  = $files
        Branch = $branch
        Archived = [bool]$info.archived
    })
    if ($commits.Count -eq 0) { continue }
    $changed++
    $details.Add("## $($source.Name) ($($source.Repo), $branch)")
    $details.Add('')
    $details.Add($range)
    $details.Add('')
    foreach ($c in $commits) {
        $first = ($c.commit.message -split "`n")[0].Trim()
        $date = ([datetime]$c.commit.committer.date).ToString('yyyy-MM-dd')
        $details.Add("- $date ``$($c.sha.Substring(0, 7))`` $first")
    }
    if ($source.Base -and $ahead -gt $commits.Count -and -not $sinceDate) {
        $details.Add("- ... GitHub lists the first $($commits.Count); see the compare link for the rest")
    }
    $details.Add('')
}

$out.Add('| Source | Commits since ours | Files changed | Branch |')
$out.Add('| --- | ---: | ---: | --- |')
foreach ($s in $summary | Sort-Object Ahead -Descending) {
    $note = if ($s.Archived) { ' (archived)' } else { '' }
    $filesText = if ($null -eq $s.Files) { '-' } else { $s.Files }
    $out.Add("| $($s.Name) | $($s.Ahead) | $filesText | $($s.Branch)$note |")
}
$out.Add('')
if ($failed.Count) {
    $out.Add("Could not be read: $($failed -join ', ').")
    $out.Add('')
}

# third-party components built or shipped here, against their latest releases
$out.Add('## Third-party components')
$out.Add('')
$out.Add('| Component | Here | Latest |')
$out.Add('| --- | --- | --- |')
$third = @(
    @{ Name = '7-Zip'; Here = '26.06'; Repo = 'ip7z/7zip' },
    @{ Name = 'libloot'; Here = '0.23.0'; Repo = 'loot/libloot' },
    @{ Name = 'pybind11'; Here = '2.12.0'; Repo = 'pybind/pybind11' },
    @{ Name = 'USVFS release'; Here = '0.5.7.2'; Repo = 'ModOrganizer2/usvfs' },
    @{ Name = 'Mod Organizer 2 release'; Here = '2.5.2'; Repo = 'ModOrganizer2/modorganizer' }
)
foreach ($t in $third) {
    $release = Invoke-GitHub "repos/$($t.Repo)/releases/latest"
    $latest = if ($release) { "$($release.tag_name) ($(([datetime]$release.published_at).ToString('yyyy-MM-dd')))" } else { 'not found' }
    $out.Add("| $($t.Name) | $($t.Here) | $latest |")
}
foreach ($p in @(@{ Name = 'PyQt6'; Here = '6.7.1' }, @{ Name = 'PyQt6-sip'; Here = '13.8.0' })) {
    try {
        $pypi = Invoke-RestMethod -Uri "https://pypi.org/pypi/$($p.Name)/json" -Headers @{ 'User-Agent' = 'mo2-revamped-upstream-check' }
        $latest = $pypi.info.version
    } catch {
        $latest = 'not found'
    }
    $out.Add("| $($p.Name) | $($p.Here) | $latest |")
}
$out.Add('')
$out.AddRange($details)

$text = $out -join "`n"
if ($Report) {
    [IO.File]::WriteAllText([IO.Path]::GetFullPath($Report), $text + "`n", [Text.UTF8Encoding]::new($false))
    Write-Host "Report written to $Report"
}
$text
Write-Host ''
Write-Host "$changed of $($summary.Count) sources have commits after ours$(if ($failed.Count) { "; $($failed.Count) could not be read" })."
