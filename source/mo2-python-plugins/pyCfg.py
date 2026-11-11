# MO2 Revamped: the Configurator, rewritten for Revamped. It keeps the plugin
# name, its settings database (data/settings.json, from Mod Organizer 2's
# Configurator by Tannin, GNU GPL version 3) and the INI files it edits per
# game. The window and the INI reading and writing are new: saving changes
# only the lines of the settings that changed, so comments, order and the
# settings the Configurator doesn't know stay as they were.

from __future__ import annotations

import json
import os
import re
import tempfile

from PyQt6.QtCore import QCoreApplication, QEvent, QObject, Qt, QTimer
from PyQt6.QtGui import QColor, QIcon, QPainter, QPixmap
from PyQt6.QtWidgets import (
    QAbstractSpinBox,
    QCheckBox,
    QColorDialog,
    QComboBox,
    QDialog,
    QDoubleSpinBox,
    QFrame,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidgetItem,
    QMessageBox,
    QPushButton,
    QScrollArea,
    QSlider,
    QSpinBox,
    QToolButton,
    QVBoxLayout,
    QWidget,
)

import mobase

import revamped_fluent as fluent

# MO2 installs translations/revamped_<language>.qm; the source text is English
_TR_CONTEXT = "RevampedConfigurator"

GLYPH_RESTORE = 0xE777
GLYPH_SEARCH = 0xE721
GLYPH_TEXT = 0xE8D2
GLYPH_NUMBER = 0xE8EF
GLYPH_COLOR = 0xE790
GLYPH_TOGGLE = 0xE73E
SEARCH_LIMIT = 150


def tr(text: str) -> str:
    return QCoreApplication.translate(_TR_CONTEXT, text)


# --- INI files ---------------------------------------------------------------

_SECTION = re.compile(r"^\s*\[([^\]]*)\]")
_KEY_PATTERN = re.compile(r"^(\s*)([^=;#\[\s][^=]*?)(\s*=\s*)(.*?)(\r?\n?)$")


class _Key:
    """_KEY_PATTERN, tried only on lines with an "=": on a long line without
    one its lazy name backtracked for seconds."""

    @staticmethod
    def match(line: str):
        return _KEY_PATTERN.match(line) if "=" in line else None


_KEY = _Key
# a value's trailing "// comment", kept when the value changes
_TAIL = re.compile(r"(\s*//.*)$")


class IniFile:
    """A game INI read line by line. Values change in place; every other
    line, comments and the order stay as they were, byte for byte. The games
    read INIs with Windows' GetPrivateProfileString: for a key that appears
    more than once, the first one counts, so that is the one shown, and a
    change goes to every copy."""

    def __init__(self, path: str):
        self.path = path
        self.exists = os.path.isfile(path)
        self.writable = True
        # the games' code page, unless the file is already UTF-8
        self._encoding = "cp1252"
        self._bom = False
        self._newline = "\r\n"
        self._lines: list[str] = []
        if self.exists:
            raw = open(path, "rb").read()
            if raw.startswith((b"\xff\xfe", b"\xfe\xff")):
                # UTF-16: read for the values, never written back
                self.writable = False
                text = raw.decode("utf-16", errors="replace")
            else:
                if raw.startswith(b"\xef\xbb\xbf"):
                    self._bom, raw = True, raw[3:]
                try:
                    text = raw.decode("utf-8")
                    if not self._bom and raw.isascii():
                        self._encoding = "cp1252"
                        text = raw.decode("cp1252", errors="surrogateescape")
                except UnicodeDecodeError:
                    # the system code page; surrogateescape gives back every
                    # byte as it was, also those cp1252 doesn't define
                    self._encoding = "cp1252"
                    text = raw.decode("cp1252", errors="surrogateescape")
            if "\r\n" not in text and "\n" in text:
                self._newline = "\n"
            self._lines = text.splitlines(keepends=True)

    def _matches(self, section: str, key: str) -> tuple[list[int], int]:
        """(every line of the key in the section, index after the last key of
        the section's first block, or -1 when the section isn't there)."""
        current = None
        found: list[int] = []
        end = -1
        blocks = 0
        section, key = section.lower(), key.lower()
        for index, line in enumerate(self._lines):
            match = _SECTION.match(line)
            if match:
                current = match.group(1).strip().lower()
                if current == section:
                    blocks += 1
                    if blocks == 1:
                        end = index + 1
                continue
            if current != section:
                continue
            match = _KEY.match(line)
            if match:
                if blocks == 1:
                    end = index + 1
                if match.group(2).strip().lower() == key:
                    found.append(index)
        return found, end

    def items(self) -> list[tuple[str, str, str]]:
        """(section, key, value) for every key, in file order."""
        out = []
        current = None
        for line in self._lines:
            match = _SECTION.match(line)
            if match:
                current = match.group(1).strip()
                continue
            match = _KEY.match(line)
            if current is not None and match:
                out.append((current, match.group(2).strip(), match.group(4)))
        return out

    def set(self, section: str, key: str, value: str, keep_comment: bool) -> None:
        found, end = self._matches(section, key)
        for index in found:
            match = _KEY.match(self._lines[index])
            ending = match.group(5) or self._newline
            tail = _TAIL.search(match.group(4)) if keep_comment else None
            self._lines[index] = (match.group(1) + match.group(2) + match.group(3) + value
                                  + (tail.group(1) if tail else "") + ending)
        if found:
            return
        line = f"{key}={value}{self._newline}"
        if end >= 0:
            if end > 0 and not self._lines[end - 1].endswith(("\n", "\r")):
                self._lines[end - 1] += self._newline
            self._lines.insert(end, line)
            return
        if self._lines and not self._lines[-1].endswith(("\n", "\r")):
            self._lines[-1] += self._newline
        if self._lines and self._lines[-1].strip():
            self._lines.append(self._newline)
        self._lines += [f"[{section}]{self._newline}", line]

    def save(self) -> None:
        errors = "surrogateescape" if self._encoding == "cp1252" else "strict"
        try:
            data = "".join(self._lines).encode(self._encoding, errors=errors)
        except UnicodeEncodeError as error:
            raise OSError(
                tr("{file} can't hold the character \u201c{char}\u201d: the game reads it in "
                   "the Windows code page.").format(
                    file=os.path.basename(self.path), char=error.object[error.start:error.end])
            ) from error
        if self._bom:
            data = b"\xef\xbb\xbf" + data
        folder = os.path.dirname(self.path)
        handle, temporary = tempfile.mkstemp(prefix=os.path.basename(self.path) + ".", suffix=".tmp", dir=folder)
        try:
            with os.fdopen(handle, "wb") as file:
                file.write(data)
            os.replace(temporary, self.path)
        except BaseException:
            try:
                os.remove(temporary)
            except OSError:
                pass
            raise


# --- the settings ------------------------------------------------------------

_KINDS = {"b": "bool", "f": "float", "i": "int", "u": "uint", "r": "color"}
# QSpinBox holds 32-bit numbers; a larger one is edited as text
_INT_LIMIT = 2147483647


def kind_of(key: str, meta: dict) -> str:
    if "values" in meta:
        return "choice"
    # Bethesda's names are a type letter and a capital: bFullScreen, fGamma
    if len(key) > 1 and not (key[1].isupper() or key[1].isdigit()):
        return "text"
    return _KINDS.get(key[:1], "text")


def _base_kind(kind: str, key: str) -> str:
    if kind != "choice":
        return kind
    kind = _KINDS.get(key[:1], "text")
    return "text" if kind in ("bool", "color") else kind


def parse_value(kind: str, text: str, key: str = ""):
    kind = _base_kind(kind, key)
    if kind in ("bool", "int", "uint", "float"):
        # numbers can carry a // comment; text keeps everything
        text = text.split("//")[0].strip()
    else:
        text = text.strip()
    if kind == "bool":
        if text.lower() in ("true", "false"):
            return text.lower() == "true"
        return float(text) != 0
    if kind in ("int", "uint"):
        try:
            value = int(float(text))
        except OverflowError:
            # 1e999 and inf: a ValueError like any other value it can't read
            raise ValueError(text) from None
        if kind == "uint" and value < 0:
            raise ValueError(text)
        return value
    if kind == "float":
        return float(text)
    return text


def float_text(value: float) -> str:
    """Four decimals, as the games write them, unless that loses the value
    (3.5e-05 would become 0.0000)."""
    text = f"{float(value):.4f}"
    return text if float(text) == float(value) else f"{float(value):.10f}".rstrip("0")


def float_decimals(*values) -> int:
    """Decimals a number box needs to show these values whole."""
    decimals = 4
    for value in values:
        try:
            text = f"{float(value):.10f}".rstrip("0")
        except (TypeError, ValueError):
            continue
        decimals = max(decimals, len(text.split(".")[1]) if "." in text else 0)
    return min(decimals, 10)


def format_value(kind: str, value) -> str:
    if kind == "bool":
        return "1" if value else "0"
    if isinstance(value, float) and kind in ("float", "choice"):
        return float_text(value)
    if kind in ("int", "uint"):
        return str(int(value))
    return str(value)


class Setting:
    def __init__(self, section: str, key: str, meta: dict, file: str, known: bool = True):
        self.section = section
        self.key = key
        self.meta = meta
        self.file = file
        self.known = known
        self.kind = kind_of(key, meta)
        default = meta.get("default", "")
        if isinstance(default, str) and default != "":
            try:
                default = parse_value(self.kind, default, key)
            except ValueError:
                pass
        if self.kind == "bool" and not isinstance(default, bool):
            default = bool(default) if default != "" else False
        if self.kind in ("int", "uint") and isinstance(default, int) and abs(default) > _INT_LIMIT:
            self.kind = "text"
            default = str(default)
        self.default = default
        self.saved = self.default
        self.value = self.default
        self.in_file = False
        self.problem = ""

    @property
    def flags(self) -> list:
        return self.meta.get("flags", [])

    @property
    def basic(self) -> bool:
        return "basic" in self.flags

    @property
    def readonly(self) -> bool:
        return "readonly" in self.flags

    def has_default(self) -> bool:
        return self.known and self.default != ""

    def changed(self) -> bool:
        return self.value != self.saved

    def description(self) -> str:
        text = self.meta.get("description", "")
        return tr(text) if text else ""


class Model:
    """The settings that apply to the managed game, with what its INIs hold.
    The files are read in the game's order (Fallout4Custom.ini last): a
    setting found in a later file is shown, and saved, from that file, since
    the game uses its value."""

    def __init__(self, database: dict, game: str, files: list[str], folder: str):
        self.folder = folder
        self.order = list(files)
        self.files = {name: IniFile(os.path.join(folder, name)) for name in files}
        self.sections: dict[str, dict[str, Setting]] = {}
        for section, entries in database.items():
            for key, meta in entries.items():
                if "games" in meta and game not in meta["games"]:
                    continue
                if "hidden" in meta.get("flags", []):
                    continue
                file = files[1] if "prefs" in meta.get("flags", []) and len(files) > 1 else files[0]
                self.sections.setdefault(section, {})[key.lower()] = Setting(section, key, meta, file)
        self._read()

    def _section(self, name: str) -> dict[str, Setting] | None:
        for section, entries in self.sections.items():
            if section.lower() == name.lower():
                return entries
        return None

    def _invalid(self, setting: Setting, text: str) -> None:
        setting.problem = tr("The INI has \"{value}\", which isn't a valid value here; the game "
                             "probably ignores it.").format(value=text.strip())

    def _read(self) -> None:
        for position, name in enumerate(self.order):
            ini = self.files[name]
            seen = set()
            overrides = position >= 2  # Fallout4Custom.ini: read after the others, it wins
            for section, key, text in ini.items():
                # the first copy of a key in a file is the one the game reads
                identity = (section.lower(), key.lower())
                if identity in seen:
                    continue
                seen.add(identity)
                entries = self._section(section)
                setting = entries.get(key.lower()) if entries is not None else None
                if setting is not None and setting.known:
                    if not overrides and "both" not in setting.flags and setting.file.lower() != name.lower():
                        continue
                elif setting is None:
                    # a setting the Configurator doesn't know: shown with every setting
                    setting = Setting(section, key, {"default": ""}, name, known=False)
                    try:
                        value = parse_value(setting.kind, text, key)
                        if setting.kind in ("int", "uint") and abs(value) > _INT_LIMIT:
                            setting.kind = "text"
                    except ValueError:
                        setting.kind = "text"
                    if entries is None:
                        entries = self.sections.setdefault(section, {})
                    entries[key.lower()] = setting
                try:
                    value = parse_value(setting.kind, text, setting.key)
                except ValueError:
                    self._invalid(setting, text)
                    continue
                if setting.kind in ("int", "uint") and abs(value) > _INT_LIMIT:
                    self._invalid(setting, text)
                    continue
                # a later file overrides an earlier one, and changes go there
                setting.file = name
                setting.problem = ""
                setting.value = setting.saved = value
                setting.in_file = True
                if not setting.known:
                    setting.default = value

    def missing_files(self) -> list[str]:
        # Fallout4Custom.ini is optional: the game doesn't make it
        return [name for position, name in enumerate(self.order)
                if position < 2 and not self.files[name].exists]

    def editable(self, setting: Setting) -> bool:
        ini = self.files.get(setting.file)
        return ini is not None and ini.exists and ini.writable and not setting.readonly

    def changes(self) -> list[Setting]:
        return [s for entries in self.sections.values() for s in entries.values() if s.changed()]

    def save(self) -> None:
        """Writes the changes, each file read again first: what another
        program changed in it meanwhile stays."""
        pending: dict[str, list[Setting]] = {}
        for setting in self.changes():
            if self.editable(setting):
                pending.setdefault(setting.file, []).append(setting)
        for name, settings in pending.items():
            fresh = IniFile(self.files[name].path)
            for setting in settings:
                keep_comment = setting.kind in ("bool", "int", "uint", "float") or setting.kind == "choice"
                fresh.set(setting.section, setting.key, format_value(setting.kind, setting.value), keep_comment)
            try:
                fresh.save()
            except OSError as error:
                raise OSError(f"{fresh.path}: {error.strerror or error}") from error
            self.files[name] = fresh
            for setting in settings:
                setting.saved = setting.value
                setting.in_file = True


# --- the window --------------------------------------------------------------

class _WheelOnlyWhenFocused(QObject):
    """A number box or slider in a scrolling page ignores the wheel until it is
    clicked: scrolling the page doesn't change settings by accident."""

    def eventFilter(self, watched: QObject, event: QEvent) -> bool:
        if event.type() == QEvent.Type.Wheel and not watched.hasFocus():
            event.ignore()
            return True
        return False


class _EnterGoesToResults(QObject):
    """Enter in the search box moves to the first result instead of pressing
    a button."""

    def __init__(self, dialog: "ConfiguratorDialog"):
        super().__init__(dialog)
        self._dialog = dialog

    def eventFilter(self, watched: QObject, event: QEvent) -> bool:
        if event.type() == QEvent.Type.KeyPress and event.key() in (Qt.Key.Key_Return, Qt.Key.Key_Enter):
            self._dialog.focus_first_control()
            return True
        return False


def _color_icon(color: QColor) -> QIcon:
    pixmap = QPixmap(32, 32)
    pixmap.setDevicePixelRatio(2.0)
    pixmap.fill(Qt.GlobalColor.transparent)
    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    painter.setPen(QColor(128, 128, 128, 160))
    painter.setBrush(color)
    painter.drawRoundedRect(1, 1, 14, 14, 3, 3)
    painter.end()
    return QIcon(pixmap)


def _parse_color(text: str) -> QColor:
    try:
        parts = [int(float(p)) for p in str(text).split(",")[:3]]
        if len(parts) == 3:
            return QColor(*(max(0, min(255, p)) for p in parts))
    except ValueError:
        pass
    return QColor(0, 0, 0)


class ConfiguratorDialog(QDialog):

    def __init__(self, parent: QWidget | None, model: Model, game: str, where: str):
        super().__init__(parent)
        self._model = model
        self._wheel = _WheelOnlyWhenFocused(self)
        self._restore_icon = fluent.glyph_icon(GLYPH_RESTORE)
        self._controls: list[QWidget] = []
        self._building = False
        self.setObjectName("ConfiguratorDialog")
        self.setWindowTitle(tr("Configurator"))
        self.setMinimumSize(860, 560)
        self.resize(1040, 680)
        self.setSizeGripEnabled(True)
        root = QVBoxLayout(self)
        # the pages scroll at the window's edge, as in Windows 11
        root.setContentsMargins(16, 16, 0, 12)
        root.setSpacing(12)
        body = QHBoxLayout()
        body.setSpacing(16)

        side = QVBoxLayout()
        side.setSpacing(8)
        info = QFrame(self)
        info.setProperty("fluentCard", True)
        info.setFixedWidth(232)
        info_layout = QVBoxLayout(info)
        info_layout.setContentsMargins(14, 12, 14, 12)
        info_layout.setSpacing(2)
        name = QLabel(game, info)
        name.setObjectName("instanceDetailName")
        name.setWordWrap(True)
        caption = QLabel(where, info)
        caption.setObjectName("instanceDetailCaption")
        caption.setWordWrap(True)
        info_layout.addWidget(name)
        info_layout.addWidget(caption)
        side.addWidget(info)
        self._search = QLineEdit(self)
        self._search.setPlaceholderText(tr("Search settings"))
        self._search.setAccessibleName(tr("Search settings"))
        self._search.setClearButtonEnabled(True)
        self._search.setFixedWidth(232)
        self._search.installEventFilter(_EnterGoesToResults(self))
        side.addWidget(self._search)
        self._nav = fluent.navigation_list(self)
        self._nav.setAccessibleName(tr("Sections"))
        side.addWidget(self._nav, 1)
        all_row = QHBoxLayout()
        all_row.setContentsMargins(4, 0, 0, 0)
        self._all = QCheckBox(self)
        fluent.make_switch(self._all, tr("Show every setting"))
        all_label = QLabel(tr("Show every setting"), self)
        all_label.setBuddy(self._all)
        # a click on the words flips the switch too
        all_label.mousePressEvent = lambda _event: self._all.toggle()
        all_row.addWidget(all_label, 1)
        all_row.addWidget(self._all)
        side.addLayout(all_row)
        body.addLayout(side)

        column = QVBoxLayout()
        column.setSpacing(4)
        self._title = QLabel(self)
        self._title.setProperty("wizardPageTitle", True)
        self._title.setMinimumWidth(1)
        self._subtitle = QLabel(self)
        self._subtitle.setProperty("wizardPageText", True)
        self._subtitle.setWordWrap(True)
        self._subtitle.setContentsMargins(0, 0, 16, 0)
        column.addWidget(self._title)
        column.addWidget(self._subtitle)
        column.addSpacing(8)
        self._scroll = QScrollArea(self)
        self._scroll.setObjectName("instanceDetailsScroll")
        self._scroll.setFrameShape(QFrame.Shape.NoFrame)
        self._scroll.setWidgetResizable(True)
        self._scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self._scroll.verticalScrollBar().rangeChanged.connect(self._fit_margin)
        column.addWidget(self._scroll, 1)
        body.addLayout(column, 1)
        root.addLayout(body, 1)

        footer = QHBoxLayout()
        footer.setContentsMargins(0, 0, 16, 0)
        self._pending = QLabel(self)
        self._pending.setObjectName("fluentSettingDescription")
        footer.addWidget(self._pending, 1)
        # not the default button: Enter in a field must not write the INIs
        self._save = fluent.enter_presses_itself(QPushButton(tr("Save"), self))
        self._save.setAutoDefault(False)
        self._save.setProperty("fluentPrimary", True)
        self._save.setMinimumWidth(110)
        close = fluent.enter_presses_itself(QPushButton(tr("Close"), self))
        close.setAutoDefault(False)
        close.setMinimumWidth(110)
        footer.addWidget(self._save)
        footer.addWidget(close)
        root.addLayout(footer)
        self._save.clicked.connect(self._save_changes)
        close.clicked.connect(self.reject)

        # a short pause before a page is built: typing or holding an arrow key
        # through the sections doesn't build every page on the way
        self._page_timer = QTimer(self)
        self._page_timer.setSingleShot(True)
        self._page_timer.setInterval(150)
        self._page_timer.timeout.connect(self._show_page)
        self._search.textChanged.connect(lambda _: self._page_timer.start())
        self._all.toggled.connect(self._fill_nav)
        self._nav.currentRowChanged.connect(self._section_chosen)
        self._fill_nav()
        self._update_pending()

    # --- pages -------------------------------------------------------------
    def _visible(self, setting: Setting) -> bool:
        return self._all.isChecked() or (setting.basic and setting.known)

    def _section_chosen(self, _row: int) -> None:
        # a section chosen in the list replaces the search results
        if self._search.text():
            self._search.blockSignals(True)
            self._search.clear()
            self._search.blockSignals(False)
        self._page_timer.start()

    def _fill_nav(self) -> None:
        current = self._nav.currentItem().text() if self._nav.currentItem() else ""
        self._nav.blockSignals(True)
        self._nav.clear()
        for section in sorted(self._model.sections, key=lambda s: s.lstrip(":").casefold()):
            entries = self._model.sections[section]
            if any(self._visible(s) for s in entries.values()):
                item = QListWidgetItem(section, self._nav)
                item.setToolTip(section)
        rows = [self._nav.item(i).text() for i in range(self._nav.count())]
        self._nav.setCurrentRow(rows.index(current) if current in rows else (0 if rows else -1))
        self._nav.blockSignals(False)
        self._show_page()

    def _show_page(self, keep_scroll: bool = False) -> None:
        self._page_timer.stop()
        scroll = self._scroll.verticalScrollBar().value() if keep_scroll else 0
        needle = self._search.text().strip().casefold()
        if needle:
            settings = [s for entries in self._model.sections.values() for s in entries.values()
                        if self._visible(s) and (needle in s.key.casefold() or needle in s.description().casefold()
                                                 or needle in s.section.casefold())]
            settings.sort(key=lambda s: (s.section.casefold(), s.key[1:].casefold()))
            self._set_title(tr("Search results"))
            if not settings:
                subtitle = tr("No setting matches. \"Show every setting\" searches the advanced ones too.") \
                    if not self._all.isChecked() else tr("No setting matches.")
            elif len(settings) > SEARCH_LIMIT:
                subtitle = tr("The first {shown} of {count} settings; type more to narrow it down.").format(
                    shown=SEARCH_LIMIT, count=len(settings))
            elif len(settings) == 1:
                subtitle = tr("1 setting")
            else:
                subtitle = tr("{count} settings").format(count=len(settings))
            self._build(settings[:SEARCH_LIMIT], True, subtitle)
        else:
            item = self._nav.currentItem()
            if item is None:
                self._set_title(tr("Configurator"))
                self._build([], False, tr("There is nothing to configure for this game."))
            else:
                section = item.text()
                settings = sorted((s for s in self._model.sections[section].values() if self._visible(s)),
                                  key=lambda s: s.key[1:].casefold())
                files = sorted({s.file for s in settings})
                self._set_title(section)
                text = tr("1 setting, in {files}") if len(settings) == 1 else tr("{count} settings, in {files}")
                self._build(settings, False, text.format(count=len(settings), files=", ".join(files)))
        if scroll:
            QTimer.singleShot(0, lambda: self._scroll.verticalScrollBar().setValue(scroll))

    def _set_title(self, text: str) -> None:
        metrics = self._title.fontMetrics()
        width = max(200, self._scroll.width() - 16)
        self._title.setText(metrics.elidedText(text, Qt.TextElideMode.ElideRight, width))
        self._title.setToolTip(text if self._title.text() != text else "")

    def _build(self, settings: list[Setting], show_section: bool, subtitle: str) -> None:
        self._subtitle.setText(subtitle)
        self._building = True
        self._controls = []
        content = QWidget()
        content.setObjectName("fluentPageContent")
        content.setUpdatesEnabled(False)
        layout = QVBoxLayout(content)
        layout.setContentsMargins(0, 0, 16, 0)
        layout.setSpacing(12)
        missing = self._model.missing_files()
        if missing:
            banner = fluent.note(tr("Not found yet: {files}. These settings can be changed here once the file "
                                    "exists (the game makes it on its first start).").format(files=", ".join(missing)),
                                 content)
            banner.setObjectName("instanceNameValidation")
            layout.addWidget(banner)
        if settings:
            card = fluent.card("", content)
            for setting in settings:
                self._row(card, setting, show_section)
            layout.addWidget(card)
        layout.addStretch(1)
        # before setWidget(): it deletes the old page, whose layout this held
        self._page_layout = layout
        self._scroll.setWidget(content)
        content.setUpdatesEnabled(True)
        self._fit_margin()
        self._building = False
        self._update_pending()
        # Tab goes from the switch under the sections through the rows to Save
        chain = [self._all] + self._controls + [self._save]
        for first, second in zip(chain, chain[1:]):
            QWidget.setTabOrder(first, second)

    def _fit_margin(self, *_unused) -> None:
        """The scroll bar takes its width from the gap at the window's edge:
        the rows end at one place on every page, with a bar or without."""
        bar = self._scroll.verticalScrollBar()
        layout = getattr(self, "_page_layout", None)
        if layout is not None:
            layout.setContentsMargins(0, 0, 16 - bar.sizeHint().width() if bar.maximum() > 0 else 16, 0)

    def focus_first_control(self) -> None:
        self._show_page()
        if self._controls:
            self._controls[0].setFocus()

    def _row(self, card: QFrame, setting: Setting, show_section: bool) -> None:
        control, glyph = self._control(setting)
        restore = QToolButton()
        restore.setAutoRaise(True)
        restore.setIcon(self._restore_icon)
        restore.setToolTip(tr("Restore the default value"))
        restore.setAccessibleName(tr("Restore the default value of {key}").format(key=setting.key))
        title = f"{setting.section} › {setting.key}" if show_section else setting.key
        row = fluent.SettingRow(card, glyph, title, "", (restore, control))
        row.setToolTip(setting.file)
        row.title.setWordWrap(False)
        row.title.setMinimumWidth(1)
        for widget in [control] + control.findChildren(QWidget):
            if not widget.accessibleName():
                widget.setAccessibleName(setting.key)
        editable = self._model.editable(setting)
        if not editable:
            control.setEnabled(False)
            restore.setEnabled(False)
        focus_target = control.property("spin") or control
        self._controls.append(focus_target)

        def refresh() -> None:
            parts = [setting.description()]
            if setting.readonly:
                parts.append(tr("Read-only: the game's launcher writes it."))
            elif not editable and self._model.files.get(setting.file) is not None \
                    and not self._model.files[setting.file].writable:
                parts.append(tr("{file} is in a format MO2 doesn't write (UTF-16).").format(file=setting.file))
            if not setting.known:
                parts.append(tr("In {file}. The Configurator doesn't know this setting.").format(file=setting.file))
            elif setting.has_default():
                shown = (tr("on") if setting.default else tr("off")) if setting.kind == "bool" \
                    else format_value(setting.kind, setting.default)
                parts.append(tr("Default: {value}").format(value=shown))
            if setting.changed():
                parts.append(tr("Changed, not saved yet."))
            row.setDescription("\n".join(p for p in parts if p))
            row.setProblem(setting.problem)
            restore.setVisible(editable and setting.has_default() and setting.value != setting.default)
            if not self._building:
                self._update_pending()

        def restore_default() -> None:
            self._set_control(control, setting, setting.default)
            setting.value = setting.default
            setting.problem = ""
            refresh()
            focus_target.setFocus()

        restore.clicked.connect(restore_default)
        self._connect(control, setting, refresh)
        refresh()

    # --- controls ------------------------------------------------------------
    def _control(self, setting: Setting) -> tuple[QWidget, int]:
        meta, kind = setting.meta, setting.kind
        if kind == "choice":
            combo = QComboBox()
            for value in meta["values"]:
                try:
                    value = parse_value("choice", str(value), setting.key)
                except ValueError:
                    pass
                combo.addItem(format_value(kind, value), value)
            index = combo.findData(setting.value)
            if index < 0:
                combo.addItem(format_value(kind, setting.value), setting.value)
                index = combo.count() - 1
            combo.setCurrentIndex(index)
            combo.installEventFilter(self._wheel)
            combo.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
            return combo, fluent.GLYPH_LIST
        if kind == "bool":
            box = QCheckBox()
            box.setChecked(bool(setting.value))
            return box, GLYPH_TOGGLE
        if kind in ("int", "uint"):
            low, high = (-_INT_LIMIT - 1, _INT_LIMIT) if kind == "int" else (0, _INT_LIMIT)
            if "range" in meta:
                low, high = int(meta["range"]["lower"]), int(meta["range"]["upper"])
            if not low <= int(setting.value) <= high and not setting.problem:
                setting.problem = tr("{value} is outside what this setting allows ({low} to {high}).").format(
                    value=setting.value, low=low, high=high)
            spin = QSpinBox()
            spin.setRange(low, high)
            spin.setSingleStep(int(meta.get("step", 1)) or 1)
            spin.setValue(max(low, min(high, int(setting.value))))
            spin.setMinimumWidth(120)
            self._number_box(spin)
            if "range" in meta:
                holder = QWidget()
                line = QHBoxLayout(holder)
                line.setContentsMargins(0, 0, 0, 0)
                slider = QSlider(Qt.Orientation.Horizontal, holder)
                slider.setRange(low, high)
                slider.setSingleStep(spin.singleStep())
                slider.setValue(spin.value())
                slider.setMinimumWidth(160)
                slider.installEventFilter(self._wheel)
                slider.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
                slider.valueChanged.connect(spin.setValue)
                spin.valueChanged.connect(slider.setValue)
                line.addWidget(slider)
                line.addWidget(spin)
                holder.setProperty("spin", spin)
                return holder, GLYPH_NUMBER
            return spin, GLYPH_NUMBER
        if kind == "float":
            spin = QDoubleSpinBox()
            spin.setDecimals(float_decimals(setting.value, setting.default, meta.get("step", 0.1)))
            if "range" in meta:
                spin.setRange(float(meta["range"]["lower"]), float(meta["range"]["upper"]))
                if not spin.minimum() <= float(setting.value) <= spin.maximum() and not setting.problem:
                    setting.problem = tr("{value} is outside what this setting allows ({low} to {high}).").format(
                        value=float_text(setting.value), low=float_text(spin.minimum()), high=float_text(spin.maximum()))
            else:
                spin.setRange(-1e9, 1e9)
            spin.setSingleStep(float(meta.get("step", 0.1)) or 0.1)
            spin.setValue(float(setting.value))
            spin.setMinimumWidth(140)
            self._number_box(spin)
            return spin, GLYPH_NUMBER
        if kind == "color":
            button = fluent.content_button(tr("Choose…"))
            button.setIcon(_color_icon(_parse_color(setting.value)))
            return button, GLYPH_COLOR
        edit = QLineEdit(str(setting.value))
        edit.setMinimumWidth(220)
        return edit, GLYPH_TEXT

    def _number_box(self, spin: QAbstractSpinBox) -> None:
        spin.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        spin.installEventFilter(self._wheel)
        spin.setKeyboardTracking(False)

    def _set_control(self, control: QWidget, setting: Setting, value) -> None:
        kind = setting.kind
        target = control.property("spin") or control
        target.blockSignals(True)
        if kind == "choice":
            index = control.findData(value)
            control.setCurrentIndex(index if index >= 0 else 0)
        elif kind == "bool":
            control.setChecked(bool(value))
        elif kind in ("int", "uint", "float"):
            target.setValue(value)
            slider = control.findChild(QSlider)
            if slider is not None:
                slider.blockSignals(True)
                slider.setValue(int(value))
                slider.blockSignals(False)
        elif kind == "color":
            control.setIcon(_color_icon(_parse_color(value)))
        else:
            control.setText(str(value))
        target.blockSignals(False)

    def _connect(self, control: QWidget, setting: Setting, refresh) -> None:
        def changed(value) -> None:
            setting.value = value
            setting.problem = ""
            refresh()

        kind = setting.kind
        if kind == "choice":
            control.currentIndexChanged.connect(lambda i: changed(control.itemData(i)))
        elif kind == "bool":
            control.toggled.connect(changed)
        elif kind in ("int", "uint", "float"):
            (control.property("spin") or control).valueChanged.connect(changed)
        elif kind == "color":
            def pick() -> None:
                color = QColorDialog.getColor(_parse_color(setting.value), self, setting.key)
                if color.isValid():
                    control.setIcon(_color_icon(color))
                    changed(f"{color.red()},{color.green()},{color.blue()}")
            control.clicked.connect(pick)
        else:
            control.editingFinished.connect(lambda: changed(control.text()))

    # --- saving ------------------------------------------------------------
    def _update_pending(self) -> None:
        count = len(self._model.changes())
        if count == 1:
            self._pending.setText(tr("1 change not saved yet"))
        else:
            self._pending.setText(tr("{count} changes not saved yet").format(count=count) if count else "")
        self._save.setEnabled(count > 0)

    def _save_changes(self) -> bool:
        try:
            self._model.save()
        except OSError as error:
            QMessageBox.warning(self, tr("Configurator"),
                                tr("The INI files could not be saved:\n{error}").format(error=error))
            return False
        self._show_page(keep_scroll=True)
        self._update_pending()
        return True

    def reject(self) -> None:
        """Close, Escape and the title bar's X all come here, once."""
        # a value still being typed counts too
        focused = self.focusWidget()
        if focused is not None:
            focused.clearFocus()
        if self._model.changes():
            answer = QMessageBox.question(
                self, tr("Unsaved changes"), tr("Save the changes before closing?"),
                QMessageBox.StandardButton.Save | QMessageBox.StandardButton.Discard
                | QMessageBox.StandardButton.Cancel, QMessageBox.StandardButton.Save)
            if answer == QMessageBox.StandardButton.Cancel or (
                    answer == QMessageBox.StandardButton.Save and not self._save_changes()):
                # the window stays: the keyboard goes back where it was
                if focused is not None:
                    focused.setFocus()
                return
        super().reject()


# --- the plugin ----------------------------------------------------------------

class IniEdit(mobase.IPluginTool):

    def __init__(self):
        super().__init__()
        self._organizer: mobase.IOrganizer | None = None
        self._parent: QWidget | None = None
        self._database: dict = {}

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        try:
            with open(os.path.join(organizer.pluginDataPath(), "settings.json"), encoding="utf-8") as file:
                self._database = json.load(file)
        except (OSError, ValueError):
            return False
        return True

    def name(self) -> str:
        return "Configurator"

    def localizedName(self) -> str:
        return tr("Configurator")

    def author(self) -> str:
        return "Tannin, MO2 Revamped"

    def description(self) -> str:
        return tr("Edits the game's INI settings, with what each one does.")

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(2, 0, 0, mobase.ReleaseType.FINAL)

    def settings(self) -> list:
        return []

    def requirements(self) -> list:
        # only the games ini_files() knows have something to edit here
        return [mobase.PluginRequirementFactory.basic(
            lambda organizer: bool(organizer.managedGame() and self._ini_files()),
            tr("The managed game has no INI files to edit."),
        )]

    def enabledByDefault(self) -> bool:
        return False

    def displayName(self) -> str:
        return tr("Configurator")

    def tooltip(self) -> str:
        return self.description()

    def icon(self) -> QIcon:
        return QIcon()

    def setParentWidget(self, widget: QWidget) -> None:
        self._parent = widget

    def _game(self) -> str:
        # settings.json names its games in lower case ("skyrim", "falloutnv"),
        # and Tale of Two Wastelands runs New Vegas
        name = str(self._organizer.managedGame().gameShortName()).lower()
        return {"ttw": "falloutnv"}.get(name, name)

    def _ini_files(self) -> list[str]:
        name = self._organizer.managedGame().gameShortName().lower()
        return {
            "oblivion": ["Oblivion.ini", "OblivionPrefs.ini"],
            "fallout3": ["Fallout.ini", "FalloutPrefs.ini"],
            "falloutnv": ["Fallout.ini", "FalloutPrefs.ini"],
            "ttw": ["Fallout.ini", "FalloutPrefs.ini"],
            "fallout4": ["Fallout4.ini", "Fallout4Prefs.ini", "Fallout4Custom.ini"],
            "fallout4vr": ["Fallout4.ini", "Fallout4Prefs.ini", "Fallout4Custom.ini"],
            "skyrim": ["Skyrim.ini", "SkyrimPrefs.ini"],
            "skyrimse": ["Skyrim.ini", "SkyrimPrefs.ini"],
            "skyrimvr": ["SkyrimVR.ini", "SkyrimPrefs.ini"],
        }.get(name, [])

    def display(self) -> None:
        profile = self._organizer.profile()
        game = self._organizer.managedGame()
        if profile.localSettingsEnabled():
            folder = profile.absolutePath()
            where = tr("The INI files of the profile \"{profile}\"").format(profile=profile.name())
        else:
            folder = game.documentsDirectory().absolutePath()
            where = tr("The game's INI files in Documents")
        try:
            model = Model(self._database, self._game(), self._ini_files(), folder)
        except OSError as error:
            QMessageBox.warning(self._parent, tr("Configurator"),
                                tr("The INI files could not be read:\n{error}").format(error=error))
            return
        ConfiguratorDialog(self._parent, model, game.gameName(), where).exec()


def createPlugin() -> mobase.IPlugin:
    return IniEdit()
