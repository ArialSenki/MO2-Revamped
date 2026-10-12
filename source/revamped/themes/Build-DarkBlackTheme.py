"""Regenerate the neutral Dark Black theme from the maintained Dark Blue layout.

Only presentation colors and theme asset URLs change. Semantic warning/error/
success colors stay recognizable. No widget geometry or selectors are dropped.
Run with Python 3; no third-party packages are required.
"""
import colorsys
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent
STYLES = ROOT / 'stylesheets'
SOURCE = STYLES / 'MO2 Classic Refined - Dark.qss'
OUTPUT = STYLES / 'MO2 Classic Refined - Dark Black.qss'
ASSETS = STYLES / 'mo2-classic-refined-black'

# Preserve the approved surface hierarchy with neutral charcoal tones.
OVERRIDES = {
    '#20262B': '#202020', '#252C32': '#272727', '#303A41': '#333333',
    '#354956': '#454545', '#2D3944': '#303030', '#242C32': '#242424',
    '#414D56': '#4A4A4A', '#343D43': '#383838', '#293F50': '#303030',
    '#4D697E': '#606060', '#0078D4': '#626262', '#35586F': '#484848',
    '#3D6B87': '#555555', '#39779A': '#505050', '#2F6685': '#5B5B5B',
}

def neutral_rgb(r, g, b):
    hue, saturation, _ = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
    # Blue, cyan and violet are decorative theme colors; retain warm/green status.
    if saturation > .035 and 165 <= hue * 360 <= 285:
        key = f'#{r:02X}{g:02X}{b:02X}'
        if key in OVERRIDES:
            value = int(OVERRIDES[key][1:3], 16)
        else:
            value = round(.2126 * r + .7152 * g + .0722 * b)
        return value, value, value
    return r, g, b

def neutralize(text):
    def hex_color(match):
        raw = match[0][1:]
        if len(raw) == 3:
            raw = ''.join(ch * 2 for ch in raw)
        rgb = tuple(int(raw[i:i + 2], 16) for i in (0, 2, 4))
        return '#%02X%02X%02X' % neutral_rgb(*rgb)
    text = re.sub(r'#[0-9a-fA-F]{6}\b|#[0-9a-fA-F]{3}\b', hex_color, text)
    def rgb_color(match):
        rgb = neutral_rgb(*(int(match[i]) for i in (2, 3, 4)))
        return f'{match[1]}({rgb[0]}, {rgb[1]}, {rgb[2]}{match[5]})'
    return re.sub(r'\b(rgb|rgba)\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*([^)]*)\)', rgb_color, text)

def neutralize_qss(text):
    return re.sub(r'\{([^{}]*)\}', lambda m: '{' + neutralize(m[1]) + '}', text)

def deepen_black_surfaces(text):
    # Keep layered controls close to the charcoal canvas; the original neutral
    # conversion made some cards and dialogs read as light-gray islands.
    # Compress gray surface values toward the #242424 canvas while preserving
    # their order, so controls remain distinguishable without looking detached.
    selection_map = {'#626262': '#454545', '#555555': '#414141',
                     '#535353': '#404040', '#4F4F4F': '#3E3E3E',
                     '#484848': '#3A3A3A', '#454545': '#393939'}

    def deepen_gray(value, strength=.18):
        raw = value[1:]
        if len(raw) == 3:
            raw = ''.join(ch * 2 for ch in raw)
        rgb = tuple(int(raw[i:i + 2], 16) for i in (0, 2, 4))
        if rgb[0] == rgb[1] == rgb[2] and 36 < rgb[0] <= 100:
            level = round(36 + (rgb[0] - 36) * strength)
            return '#%02X%02X%02X' % (level, level, level)
        return value

    def adjust_block(match):
        body = match[1]
        def adjust_property(prop, mapping=None):
            nonlocal body
            pattern = re.compile(r'(?i)(\b' + re.escape(prop) + r'\s*:\s*)(#[0-9a-f]{6})(\b)')
            body = pattern.sub(lambda m: m[1] + (mapping.get(m[2].upper(), m[2]) if mapping else deepen_gray(m[2])) + m[3], body)
            rgb_pattern = re.compile(r'(?i)(\b' + re.escape(prop) + r'\s*:\s*)(rgba?\(\s*\d+\s*,\s*\d+\s*,\s*\d+[^)]*\))(;)')
            def adjust_rgb(match):
                values = list(map(int, re.findall(r'\d+', match[2])[:3]))
                if values[0] == values[1] == values[2] and 36 < values[0] <= 100 and mapping is None:
                    level = round(36 + (values[0] - 36) * .18)
                    replacement = re.sub(r'\d+', str(level), match[2], count=3)
                    return match[1] + replacement + match[3]
                return match[0]
            body = rgb_pattern.sub(adjust_rgb, body)
        adjust_property('background-color')
        adjust_property('alternate-background-color')
        adjust_property('selection-background-color', selection_map)
        border_pattern = re.compile(r'(?i)(\bborder(?:-(?:color|top|right|bottom|left))?\s*:[^;]*?)(#[0-9a-f]{6})([^;]*;)')
        def adjust_border(match):
            color = deepen_gray(match[2], .25)
            return match[1] + color + match[3]
        body = border_pattern.sub(adjust_border, body)
        return '{' + body + '}'

    return re.sub(r'\{([^{}]*)\}', adjust_block, text)

def build():
    source = SOURCE.read_text(encoding='utf-8-sig')
    black = neutralize_qss(source).replace('MO2 Revamped Dark Blue', 'MO2 Revamped Dark Black')
    black = black.replace('MO2 Revamped Dark */', 'MO2 Revamped Dark Black */')
    black = black.replace('./mo2-classic-refined-light/', './mo2-classic-refined-black/')
    black = deepen_black_surfaces(black)
    ASSETS.mkdir(exist_ok=True)
    for file in (STYLES / 'mo2-classic-refined-light').glob('*.svg'):
        (ASSETS / file.name).write_text(neutralize(file.read_text(encoding='utf-8-sig')), encoding='utf-8')
    # Native vector indicators stay crisp at all Windows scaling levels.
    for disabled in (False, True):
        suffix = '-disabled' if disabled else ''
        fill, stroke, glyph = ('#383838', '#666666', '#A6A6A6') if disabled else ('#626262', '#A0A0A0', '#FFFFFF')
        for shape in ('checkbox', 'radio'):
            if shape == 'checkbox':
                body = f'<rect x="1" y="1" width="16" height="16" rx="3" fill="{fill}" stroke="{stroke}"/><path d="M4.5 9 7.5 12 13.5 6" fill="none" stroke="{glyph}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>'
            else:
                body = f'<circle cx="9" cy="9" r="8" fill="{fill}" stroke="{stroke}"/><circle cx="9" cy="9" r="3.5" fill="{glyph}"/>'
            name = f'{shape}-checked{suffix}.svg'
            (ASSETS / name).write_text(f'<svg xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 18 18">{body}</svg>\n', encoding='utf-8')
            black = black.replace(f'{shape}-checked{suffix}.png', name)
    OUTPUT.write_text(black, encoding='utf-8')
    # Selector-by-selector coverage is recorded for every dialog and state.
    blocks = re.findall(r'([^{}]+)\{([^{}]*)\}', re.sub(r'/\*.*?\*/', '', black, flags=re.S))
    original = re.findall(r'([^{}]+)\{([^{}]*)\}', re.sub(r'/\*.*?\*/', '', source, flags=re.S))
    assert [a.strip() for a, _ in blocks] == [a.strip() for a, _ in original]
    missing = [url for url in re.findall(r'url\(([^)]+)\)', black) if not (STYLES / url.strip('"\'')).exists()]
    assert not missing, missing
    assert neutralize_qss(black) == black, 'Unconverted decorative color'
    audit = {
        'style_blocks': len(blocks),
        'sections': re.findall(r'/\*\s*([^*]+)\*/', black),
        'selectors': [a.strip() for a, _ in blocks],
        'all_selectors_preserved': True,
        'all_assets_exist': True,
        'decorative_blue_remaining': False,
    }
    (ROOT / 'DarkBlack-coverage.json').write_text(json.dumps(audit, indent=2, ensure_ascii=False), encoding='utf-8')
    print(f'Dark Black: {len(blocks)} style blocks, {len(audit["sections"])} sections, all assets resolved.')

if __name__ == '__main__':
    build()
