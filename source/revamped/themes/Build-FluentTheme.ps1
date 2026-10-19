<#
.SYNOPSIS
Regenerates the MO2 Fluent v4 Light and Dark themes.

.DESCRIPTION
Each Fluent theme starts from the maintained Classic Refined theme of the same
brightness, so every dialog keeps its reviewed layout. Decorative blue tones
are mapped to the Windows 11 neutral palette and accent; warning, error, and
success colors keep their hue. Fluent-v4.template.qss is then appended with the
theme tokens, and the indicator, switch, and chevron assets are written as SVG.
Run with Windows PowerShell 5.1 or later; no modules are required.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$styles = Join-Path $root 'stylesheets'
$template = [IO.File]::ReadAllText((Join-Path $root 'Fluent-v4.template.qss'))
$utf8 = New-Object Text.UTF8Encoding $false

$themes = @(
    @{
        Name = 'Light'; Source = 'MO2 Classic Refined - Light.qss'
        Output = 'MO2 Fluent v4 - Light.qss'; Assets = 'mo2-fluent-v4-light'; Dark = $false
        Tokens = [ordered]@{
            bg = '#EFF1F4'; card = '#FBFBFB'; cardLine = '#E5E5E5'; line = '#EBEBEB'
            rowAlt = '#F6F6F6'; rowHover = '#F2F2F2'; hover = '#E6E8EB'; pressed = '#DFE1E4'
            sel = '#E6EBF1'; menu = '#F9F9F9'; menuLine = '#E0E0E0'
            field = '#FFFFFF'; fieldHover = '#F9F9F9'; fieldLine = '#E5E5E5'; fieldBottom = '#8A8A8A'
            btn = '#FBFBFB'; btnHover = '#F6F6F6'; btnPressed = '#F2F2F2'; btnLine = '#E5E5E5'; btnBottom = '#CCCCCC'
            disabledBg = '#F5F5F5'
            text = '#1A1A1A'; muted = '#5D5D5D'; faint = '#8A8A8A'
            accent = '#005FB8'; accentHover = '#1A6FC0'; accentPressed = '#3B82C8'
            accentFg = '#FFFFFF'; accentFgPressed = '#D6E6F5'; accentText = '#005FB8'
            accentDivider = '#4D8FCD'; accentSubtle = '#DCE8F5'
            warnBg = '#FFF4CE'; warnFg = '#6B4A00'; critBg = '#FDE7E9'; critFg = '#C42B1C'; running = '#0F7B0F'; runningFg = '#FFFFFF'
            scroll = '#B8B8B8'; scrollHover = '#8A8A8A'
            trackOff = '#F7F7F7'; trackOffHover = '#EDEDED'; trackLine = '#8A8A8A'; knobOff = '#5D5D5D'; knobOn = '#FFFFFF'
            boxLine = '#8A8A8A'; boxFill = '#FFFFFF'; boxHover = '#F0F0F0'; disabledLine = '#C8C8C8'; disabledFill = '#E0E0E0'
        }
    },
    @{
        Name = 'Dark'; Source = 'MO2 Classic Refined - Dark.qss'
        Output = 'MO2 Fluent v4 - Dark.qss'; Assets = 'mo2-fluent-v4-dark'; Dark = $true
        Tokens = [ordered]@{
            bg = '#202226'; card = '#2B2B2B'; cardLine = '#363636'; line = '#353535'
            rowAlt = '#2E2E2E'; rowHover = '#323232'; hover = '#2F3237'; pressed = '#292B2F'
            sel = '#343B44'; menu = '#2C2C2C'; menuLine = '#3A3A3A'
            field = '#2F2F2F'; fieldHover = '#343434'; fieldLine = '#3D3D3D'; fieldBottom = '#9A9A9A'
            btn = '#2D2D2D'; btnHover = '#323232'; btnPressed = '#272727'; btnLine = '#3D3D3D'; btnBottom = '#454545'
            disabledBg = '#2A2A2A'
            text = '#FFFFFF'; muted = '#C5C5C5'; faint = '#9A9A9A'
            accent = '#4CC2FF'; accentHover = '#48B1E8'; accentPressed = '#42A0D2'
            accentFg = '#000000'; accentFgPressed = '#1B3A4C'; accentText = '#60CDFF'
            accentDivider = '#2E7EA8'; accentSubtle = '#1F3443'
            warnBg = '#433519'; warnFg = '#F5D58C'; critBg = '#442726'; critFg = '#FF99A4'; running = '#6CCB5F'; runningFg = '#000000'
            scroll = '#5C5C5C'; scrollHover = '#8A8A8A'
            trackOff = '#2F2F2F'; trackOffHover = '#363636'; trackLine = '#9A9A9A'; knobOff = '#C5C5C5'; knobOn = '#000000'
            boxLine = '#9A9A9A'; boxFill = '#2F2F2F'; boxHover = '#363636'; disabledLine = '#555555'; disabledFill = '#3A3A3A'
        }
    },
    @{
        # Dark Black is the neutral variant: charcoal surfaces one step below
        # Fluent Dark and a gray accent instead of the Windows blue.
        Name = 'Dark Black'; Source = 'MO2 Classic Refined - Dark.qss'
        Output = 'MO2 Fluent v4 - Dark Black.qss'; Assets = 'mo2-fluent-v4-black'; Dark = $true
        Tokens = [ordered]@{
            bg = '#181818'; card = '#222222'; cardLine = '#2E2E2E'; line = '#2C2C2C'
            rowAlt = '#252525'; rowHover = '#2A2A2A'; hover = '#262626'; pressed = '#1F1F1F'
            sel = '#383838'; menu = '#232323'; menuLine = '#333333'
            field = '#262626'; fieldHover = '#2B2B2B'; fieldLine = '#353535'; fieldBottom = '#8F8F8F'
            btn = '#252525'; btnHover = '#2B2B2B'; btnPressed = '#1F1F1F'; btnLine = '#353535'; btnBottom = '#3E3E3E'
            disabledBg = '#212121'
            text = '#EDEDED'; muted = '#BDBDBD'; faint = '#8F8F8F'
            accent = '#D6D6D6'; accentHover = '#C6C6C6'; accentPressed = '#B3B3B3'
            accentFg = '#000000'; accentFgPressed = '#2A2A2A'; accentText = '#E6E6E6'
            accentDivider = '#6A6A6A'; accentSubtle = '#333333'
            warnBg = '#3D3218'; warnFg = '#F5D58C'; critBg = '#3A2627'; critFg = '#F0A0A0'; running = '#6CCB5F'; runningFg = '#000000'
            scroll = '#4E4E4E'; scrollHover = '#7A7A7A'
            trackOff = '#262626'; trackOffHover = '#2E2E2E'; trackLine = '#8F8F8F'; knobOff = '#BDBDBD'; knobOn = '#000000'
            boxLine = '#8F8F8F'; boxFill = '#262626'; boxHover = '#2E2E2E'; disabledLine = '#4F4F4F'; disabledFill = '#353535'
        }
    }
)

function Get-Hsv([int]$r, [int]$g, [int]$b) {
    $max = [Math]::Max($r, [Math]::Max($g, $b)) / 255.0
    $min = [Math]::Min($r, [Math]::Min($g, $b)) / 255.0
    $delta = $max - $min
    $hue = 0.0
    if ($delta -gt 0) {
        $rf = $r / 255.0; $gf = $g / 255.0; $bf = $b / 255.0
        if ($max -eq $rf) { $hue = 60 * ((($gf - $bf) / $delta) % 6) }
        elseif ($max -eq $gf) { $hue = 60 * ((($bf - $rf) / $delta) + 2) }
        else { $hue = 60 * ((($rf - $gf) / $delta) + 4) }
        if ($hue -lt 0) { $hue += 360 }
    }
    $sat = if ($max -gt 0) { $delta / $max } else { 0 }
    return @($hue, $sat, $max)
}

# Maps one decorative color to the Fluent palette. Background-type properties
# keep their surface role; foreground and border properties keep their ink role.
function Convert-Color([int]$r, [int]$g, [int]$b, [bool]$isBackground, $theme) {
    $t = $theme.Tokens
    $hsv = Get-Hsv $r $g $b
    $hue = $hsv[0]; $sat = $hsv[1]; $val = $hsv[2]
    $lum = [Math]::Round(0.2126 * $r + 0.7152 * $g + 0.0722 * $b)
    $isBlue = $sat -gt 0.012 -and $hue -ge 165 -and $hue -le 285
    if (-not $isBlue) {
        # Neutral grays are retuned only where they act as text.
        if ($sat -le 0.06 -and -not $isBackground) {
            if ($theme.Dark) {
                if ($lum -ge 200) { return $t.text }
                if ($lum -ge 150) { return $t.muted }
            } elseif ($lum -le 80) { return $t.text }
        }
        return $null
    }
    $isAccent = $sat -gt 0.40 -and $val -ge 0.45 -and $val -le 0.95
    if ($theme.Dark) {
        if ($isAccent) { if ($isBackground) { return $t.accentSubtle } else { return $t.accentText } }
        if (-not $isBackground) {
            if ($lum -ge 200) { return $t.text }
            if ($lum -ge 150) { return $t.muted }
            if ($lum -ge 110) { return $t.faint }
        }
    } else {
        if ($isAccent) { return $t.accent }
        if (-not $isBackground) {
            if ($lum -lt 80) { return $t.text }
            if ($lum -lt 125) { return $t.muted }
            if ($lum -lt 165) { return $t.faint }
        }
    }
    return ('#{0:X2}{0:X2}{0:X2}' -f [int]$lum)
}

function Convert-Declarations([string]$body, $theme) {
    $declaration = [regex]'(?<prop>[A-Za-z-]+)(?<sep>\s*:\s*)(?<value>[^;{}]*)'
    return $declaration.Replace($body, [Text.RegularExpressions.MatchEvaluator]{
        param($d)
        $prop = $d.Groups['prop'].Value.ToLowerInvariant()
        $isBackground = $prop -match 'background'
        $value = $d.Groups['value'].Value
        $value = [regex]::Replace($value, '#([0-9A-Fa-f]{6})\b', [Text.RegularExpressions.MatchEvaluator]{
            param($c)
            $hex = $c.Groups[1].Value
            $mapped = Convert-Color ([Convert]::ToInt32($hex.Substring(0, 2), 16)) ([Convert]::ToInt32($hex.Substring(2, 2), 16)) ([Convert]::ToInt32($hex.Substring(4, 2), 16)) $isBackground $theme
            if ($mapped) { $mapped } else { '#' + $hex.ToUpperInvariant() }
        })
        $value = [regex]::Replace($value, 'rgba\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*([^)]*)\)', [Text.RegularExpressions.MatchEvaluator]{
            param($c)
            $hsv = Get-Hsv ([int]$c.Groups[1].Value) ([int]$c.Groups[2].Value) ([int]$c.Groups[3].Value)
            if ($hsv[1] -gt 0.012 -and $hsv[0] -ge 165 -and $hsv[0] -le 285) {
                $ink = if ($theme.Dark) { '255, 255, 255' } else { '0, 0, 0' }
                "rgba($ink, 5%)"
            } else { $c.Value }
        })
        $d.Groups['prop'].Value + $d.Groups['sep'].Value + $value
    })
}

function Convert-Qss([string]$text, $theme) {
    return [regex]::Replace($text, '\{([^{}]*)\}', [Text.RegularExpressions.MatchEvaluator]{
        param($m) '{' + (Convert-Declarations $m.Groups[1].Value $theme) + '}'
    })
}

function Expand-Tokens([string]$text, $tokens) {
    foreach ($key in $tokens.Keys) { $text = $text.Replace("@$key@", $tokens[$key]) }
    return $text
}

function Write-Svg([string]$dir, [string]$name, [int]$w, [int]$h, [string]$body) {
    $svg = "<svg xmlns=""http://www.w3.org/2000/svg"" width=""$w"" height=""$h"" viewBox=""0 0 $w $h"">$body</svg>`n"
    [IO.File]::WriteAllText((Join-Path $dir $name), $svg, $utf8)
}

function Write-Assets($theme) {
    $t = $theme.Tokens
    $dir = Join-Path $styles $theme.Assets
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $check = '<path d="M5 9.2 7.8 12 13 6.4" fill="none" stroke="{0}" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/>'
    Write-Svg $dir 'checkbox-unchecked.svg' 18 18 "<rect x=""1.5"" y=""1.5"" width=""15"" height=""15"" rx=""3.5"" fill=""$($t.boxFill)"" stroke=""$($t.boxLine)""/>"
    Write-Svg $dir 'checkbox-unchecked-hover.svg' 18 18 "<rect x=""1.5"" y=""1.5"" width=""15"" height=""15"" rx=""3.5"" fill=""$($t.boxHover)"" stroke=""$($t.boxLine)""/>"
    Write-Svg $dir 'checkbox-checked.svg' 18 18 ("<rect x=""1"" y=""1"" width=""16"" height=""16"" rx=""4"" fill=""$($t.accent)""/>" + ($check -f $t.accentFg))
    Write-Svg $dir 'checkbox-indeterminate.svg' 18 18 "<rect x=""1"" y=""1"" width=""16"" height=""16"" rx=""4"" fill=""$($t.accent)""/><path d=""M5.5 9h7"" stroke=""$($t.accentFg)"" stroke-width=""1.6"" stroke-linecap=""round""/>"
    Write-Svg $dir 'checkbox-disabled.svg' 18 18 "<rect x=""1.5"" y=""1.5"" width=""15"" height=""15"" rx=""3.5"" fill=""none"" stroke=""$($t.disabledLine)""/>"
    Write-Svg $dir 'checkbox-checked-disabled.svg' 18 18 ("<rect x=""1"" y=""1"" width=""16"" height=""16"" rx=""4"" fill=""$($t.disabledFill)""/>" + ($check -f $t.faint))
    Write-Svg $dir 'radio-unchecked.svg' 18 18 "<circle cx=""9"" cy=""9"" r=""7.5"" fill=""$($t.boxFill)"" stroke=""$($t.boxLine)""/>"
    Write-Svg $dir 'radio-unchecked-hover.svg' 18 18 "<circle cx=""9"" cy=""9"" r=""7.5"" fill=""$($t.boxHover)"" stroke=""$($t.boxLine)""/><circle cx=""9"" cy=""9"" r=""3.5"" fill=""$($t.muted)""/>"
    Write-Svg $dir 'radio-checked.svg' 18 18 "<circle cx=""9"" cy=""9"" r=""8"" fill=""$($t.accent)""/><circle cx=""9"" cy=""9"" r=""3.5"" fill=""$($t.accentFg)""/>"
    Write-Svg $dir 'radio-disabled.svg' 18 18 "<circle cx=""9"" cy=""9"" r=""7.5"" fill=""none"" stroke=""$($t.disabledLine)""/>"
    Write-Svg $dir 'radio-checked-disabled.svg' 18 18 "<circle cx=""9"" cy=""9"" r=""8"" fill=""$($t.disabledFill)""/><circle cx=""9"" cy=""9"" r=""3.5"" fill=""$($t.faint)""/>"
    # Switches are 40x20 like Windows 11; Qt scales them to the indicator size.
    $switches = @(
        @('switch-off.svg', $t.trackOff, $t.trackLine, $t.knobOff, 10, 5),
        @('switch-off-hover.svg', $t.trackOffHover, $t.trackLine, $t.knobOff, 10, 6),
        @('switch-on.svg', $t.accent, $t.accent, $t.knobOn, 30, 6),
        @('switch-on-hover.svg', $t.accentHover, $t.accentHover, $t.knobOn, 30, 7),
        @('switch-off-disabled.svg', 'none', $t.disabledLine, $t.disabledLine, 10, 5),
        @('switch-on-disabled.svg', $t.disabledFill, $t.disabledFill, $t.faint, 30, 5)
    )
    foreach ($s in $switches) {
        Write-Svg $dir $s[0] 40 20 "<rect x=""0.5"" y=""0.5"" width=""39"" height=""19"" rx=""9.5"" fill=""$($s[1])"" stroke=""$($s[2])""/><circle cx=""$($s[4])"" cy=""10"" r=""$($s[5])"" fill=""$($s[3])""/>"
    }
    $chevron = '<path d="{0}" fill="none" stroke="{1}" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>'
    Write-Svg $dir 'chevron-down.svg' 12 12 ($chevron -f 'M2.5 4.5 6 8l3.5-3.5', $t.muted)
    Write-Svg $dir 'chevron-down-accent.svg' 12 12 ($chevron -f 'M2.5 4.5 6 8l3.5-3.5', $t.accentFg)
    Write-Svg $dir 'chevron-right.svg' 12 12 ($chevron -f 'M4.5 2.5 8 6l-3.5 3.5', $t.muted)
    Write-Svg $dir 'sort-up.svg' 16 12 ($chevron -f 'M4 8l4-4 4 4', $t.faint)
    Write-Svg $dir 'sort-down.svg' 16 12 ($chevron -f 'M4 4l4 4 4-4', $t.faint)
    Write-Svg $dir 'spin-up.svg' 8 6 ($chevron -f 'M1.5 4.5 4 2l2.5 2.5', $t.muted)
    Write-Svg $dir 'spin-down.svg' 8 6 ($chevron -f 'M1.5 1.5 4 4l2.5-2.5', $t.muted)
    # Remaining base-theme assets (dock buttons, legacy sort arrows) are recolored copies.
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $styles 'mo2-classic-refined-light') -Filter '*.svg') {
        $target = Join-Path $dir $file.Name
        if (Test-Path -LiteralPath $target) { continue }
        $svg = [IO.File]::ReadAllText($file.FullName)
        $svg = [regex]::Replace($svg, '#([0-9A-Fa-f]{6})', [Text.RegularExpressions.MatchEvaluator]{
            param($c)
            $hex = $c.Groups[1].Value
            $mapped = Convert-Color ([Convert]::ToInt32($hex.Substring(0, 2), 16)) ([Convert]::ToInt32($hex.Substring(2, 2), 16)) ([Convert]::ToInt32($hex.Substring(4, 2), 16)) $false $theme
            if ($mapped) { $mapped } else { $c.Value }
        })
        [IO.File]::WriteAllText($target, $svg, $utf8)
    }
}

foreach ($theme in $themes) {
    $sourcePath = Join-Path $styles $theme.Source
    $source = [IO.File]::ReadAllText($sourcePath).TrimStart([char]0xFEFF)
    $fluent = Convert-Qss $source $theme
    $fluent = $fluent -replace '/\* MO2 Revamped (Light|Dark Blue|Dark) \*/', "/* MO2 Revamped Fluent v4 $($theme.Name) */"
    $fluent = $fluent.Replace('./mo2-classic-refined-light/', "./$($theme.Assets)/")
    # Point PNG indicators at the generated vector files.
    $fluent = [regex]::Replace($fluent, '(checkbox|radio)-([a-z-]+)\.png', '$1-$2.svg')
    $tokens = [ordered]@{ assets = $theme.Assets }
    foreach ($key in $theme.Tokens.Keys) { $tokens[$key] = $theme.Tokens[$key] }
    $fluent = $fluent.TrimEnd() + "`n" + (Expand-Tokens $template $tokens)
    Write-Assets $theme
    $leftover = [regex]::Matches($fluent, '@(?!name@)[A-Za-z]+@') | ForEach-Object Value | Sort-Object -Unique
    if ($leftover) { throw "Unexpanded tokens in $($theme.Output): $($leftover -join ', ')" }
    $missing = [regex]::Matches($fluent, 'url\(([^)]+)\)') | ForEach-Object { $_.Groups[1].Value.Trim('"', "'") } |
        Sort-Object -Unique | Where-Object { -not (Test-Path -LiteralPath (Join-Path $styles $_)) }
    if ($missing) { throw "Missing assets for $($theme.Output): $($missing -join ', ')" }
    $sourceBlocks = ([regex]::Matches(($source -replace '(?s)/\*.*?\*/', ''), '\{')).Count
    [IO.File]::WriteAllText((Join-Path $styles $theme.Output), $fluent, $utf8)
    Write-Host "$($theme.Output): $sourceBlocks base blocks recolored, Fluent layer appended, assets in $($theme.Assets)."
}
