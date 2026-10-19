"""Renders the MO2 Revamped logo (the original MO2 silhouette as a Fluent
outline, "M2" inside) into the program's images. Writes to the folder given as
the first argument:

  logo.ico       -> src/mo_icon.ico, branding/MO2-Revamped.ico, installer app.ico
  logo-1254.png  -> src/resources/mo_icon.png, branding/MO2-Revamped-icon.png
  logo-256.png   -> installer/qtui/resources/icon.png
  banner.png     -> src/splash.png, branding/MO2-Revamped-about.png
  preview.png    -> every icon size on light and dark, to look at

Needs Python 3.12 and the PyQt6 that ships with MO2's Python plugin; point
MO2_QT_BIN at Qt 6.7's bin folder and MO2_PYQT_LIBS at plugin_python/libs:
  py -3.12 -I make_logo.py <output folder>
"""

import os
import struct
import sys

os.add_dll_directory(os.environ.get("MO2_QT_BIN", r"D:\mo2build\Qt\6.7.1\msvc2019_64\bin"))
sys.path.insert(
    0, os.environ.get("MO2_PYQT_LIBS", r"D:\mo2build\test-instance\plugins\plugin_python\libs"))

from PyQt6.QtCore import QBuffer, QByteArray, QIODevice, QPointF, QRectF, Qt
from PyQt6.QtGui import (QColor, QFont, QFontMetricsF, QGuiApplication, QImage,
                         QPainter, QPainterPath, QPen)
from PyQt6.QtSvg import QSvgRenderer

OUT = sys.argv[1]
app = QGuiApplication([])

ACCENT = "#0F6CBD"
LIGHT = "#4CC2FF"
SHAPE = "M20 22 H64 A26 26 0 0 1 64 74 H20 Q8 74 8 62 V34 Q8 22 20 22 Z"
FONT = "Segoe UI Semibold"
VIEW = "2 1 94 94"  # the silhouette (with its stroke) centered in a square


def full(stroke=6.0):
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="{VIEW}">
<defs><linearGradient id="g" gradientUnits="userSpaceOnUse" x1="8" y1="22" x2="90" y2="74">
<stop offset="0" stop-color="{LIGHT}"/><stop offset="1" stop-color="{ACCENT}"/></linearGradient></defs>
<path d="{SHAPE}" fill="#E6F4FD" stroke="url(#g)" stroke-width="{stroke}" stroke-linejoin="round"/>
<text x="48" y="56.5" text-anchor="middle" font-family="{FONT}" font-weight="600" font-size="24" fill="{ACCENT}">M2</text>
</svg>"""


def solid(letter):
    text = (f'<text x="48" y="60" text-anchor="middle" font-family="{FONT}" font-weight="600" '
            f'font-size="34" fill="#FFFFFF">{letter}</text>') if letter else ""
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="{VIEW}">
<path d="{SHAPE}" fill="{ACCENT}" stroke="{ACCENT}" stroke-width="8" stroke-linejoin="round"/>{text}
</svg>"""


def render(svg, size):
    image = QImage(size, size, QImage.Format.Format_ARGB32_Premultiplied)
    image.fill(Qt.GlobalColor.transparent)
    p = QPainter(image)
    p.setRenderHint(QPainter.RenderHint.Antialiasing)
    p.setRenderHint(QPainter.RenderHint.TextAntialiasing)
    QSvgRenderer(QByteArray(svg.encode())).render(p, QRectF(0, 0, size, size))
    p.end()
    return image


def icon_image(size):
    # the same steps as the canvas: full logo from 48 px, "M" at 32, plain
    # silhouette below
    if size >= 64:
        return render(full(), size)
    if size == 48:
        return render(full(7.0), size)
    if size >= 32:
        return render(solid("M"), size)
    return render(solid(""), size)


def png_bytes(image):
    data = QByteArray()
    buf = QBuffer(data)
    buf.open(QIODevice.OpenModeFlag.WriteOnly)
    image.save(buf, "PNG")
    return bytes(data)


def write_ico(path, sizes):
    # PNG-compressed entries, which Windows and rc.exe accept for every size
    blobs = [png_bytes(icon_image(s)) for s in sizes]
    header = struct.pack("<HHH", 0, 1, len(sizes))
    offset = 6 + 16 * len(sizes)
    entries = b""
    for s, blob in zip(sizes, blobs):
        dim = 0 if s >= 256 else s
        entries += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    with open(path, "wb") as f:
        f.write(header + entries + b"".join(blobs))


def banner(path):
    # 1280x720, transparent: the logo, "Mod Organizer 2" and "REVAMPED", like
    # the previous splash; the title has a thin dark edge so it reads on light
    # and dark desktops
    image = QImage(1280, 720, QImage.Format.Format_ARGB32_Premultiplied)
    image.fill(Qt.GlobalColor.transparent)
    p = QPainter(image)
    p.setRenderHint(QPainter.RenderHint.Antialiasing)
    QSvgRenderer(QByteArray(full().encode())).render(p, QRectF(500, 150, 280, 280))

    title = QFont("Segoe UI Variable Display", 1)
    title.setPixelSize(52)
    title.setWeight(QFont.Weight.Normal)
    text = "Mod Organizer 2"
    width = QFontMetricsF(title).horizontalAdvance(text)
    shape = QPainterPath()
    shape.addText(QPointF(640 - width / 2, 500), title, text)
    p.setPen(QPen(QColor(11, 37, 64, 170), 2.0))
    p.setBrush(QColor("#F7FBFF"))
    p.drawPath(shape)

    sub = QFont("Segoe UI", 1)
    sub.setPixelSize(22)
    sub.setWeight(QFont.Weight.DemiBold)
    sub.setLetterSpacing(QFont.SpacingType.AbsoluteSpacing, 8)
    text = "REVAMPED"
    width = QFontMetricsF(sub).horizontalAdvance(text) - 8
    p.setPen(QColor("#2B88D8"))
    p.setFont(sub)
    p.drawText(QPointF(640 - width / 2, 552), text)
    p.end()
    image.save(path, "PNG")


os.makedirs(OUT, exist_ok=True)
write_ico(os.path.join(OUT, "logo.ico"), [16, 24, 32, 48, 64, 128, 256])
render(full(), 1254).save(os.path.join(OUT, "logo-1254.png"), "PNG")
render(full(), 256).save(os.path.join(OUT, "logo-256.png"), "PNG")
banner(os.path.join(OUT, "banner.png"))

# a preview sheet to look at: every icon size on light and dark
sheet = QImage(760, 300, QImage.Format.Format_ARGB32_Premultiplied)
sheet.fill(QColor("#F3F3F3"))
p = QPainter(sheet)
p.fillRect(0, 150, 760, 150, QColor("#202020"))
for row in (0, 1):
    x = 20
    for s in [256, 128, 64, 48, 32, 24, 16]:
        img = icon_image(s)
        shown = min(s, 128)
        p.drawImage(QRectF(x, row * 150 + (150 - shown) / 2, shown, shown), img)
        x += shown + 24
p.end()
sheet.save(os.path.join(OUT, "preview.png"), "PNG")
print("done")
