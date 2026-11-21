"""MO2 Revamped help, credits, and runtime information."""

from __future__ import annotations

import os
import platform
import re
import sys
import tempfile
import traceback
import zipfile
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import quote

import mobase
from PyQt6 import QtCore
from PyQt6.QtCore import QCoreApplication, QEvent, QObject, QRectF, QSize, Qt, QUrl, qCritical
from PyQt6.QtGui import (
    QAction,
    QColor,
    QDesktopServices,
    QFont,
    QFontMetrics,
    QIcon,
    QFontDatabase,
    QPainter,
    QPalette,
    QPixmap,
)
from PyQt6.QtWidgets import (
    QApplication,
    QDialog,
    QDialogButtonBox,
    QFileDialog,
    QFrame,
    QHBoxLayout,
    QLabel,
    QListWidget,
    QListWidgetItem,
    QMenu,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QScrollArea,
    QStackedWidget,
    QStyle,
    QStyledItemDelegate,
    QToolButton,
    QVBoxLayout,
    QWidget,
)

# MO2 installs translations/revamped_<language>.qm; the source text is English.
_TR_CONTEXT = "Mo2RevampedInfo"


def _tr(text: str) -> str:
    return QCoreApplication.translate(_TR_CONTEXT, text)


def _named_for_nightreign(text: str) -> str:
    """A translated text that names Elden Ring, for a Nightreign instance.
    German joins the name with hyphens; French elides before it."""
    for old, new in (
        ("d'Elden Ring", "de Nightreign"),
        ("d\u2019Elden Ring", "de Nightreign"),
        ("qu'Elden Ring", "que Nightreign"),
        ("qu\u2019Elden Ring", "que Nightreign"),
        ("Elden-Ring", "Nightreign"),
        ("Elden Ring", "Nightreign"),
    ):
        text = text.replace(old, new)
    return text


_GLYPH_FAMILIES = ["Segoe Fluent Icons", "Segoe MDL2 Assets"]


def _theme_kind() -> str:
    application = QApplication.instance()
    if application is not None and application.palette().window().color().lightness() < 128:
        return "dark"
    return "light"


def _accent_color() -> QColor:
    return QColor({"dark": "#4CC2FF"}.get(_theme_kind(), "#005FB8"))


def _glyph_font(pixels: int) -> QFont:
    font = QFont()
    font.setFamilies(_GLYPH_FAMILIES)
    font.setPixelSize(pixels)
    return font


def _glyph_label(glyph: int, parent) -> QLabel:
    label = QLabel(chr(glyph), parent)
    label.setObjectName("instanceRowGlyph")
    label.setFont(_glyph_font(16))
    label.setFixedWidth(24)
    label.setAlignment(Qt.AlignmentFlag.AlignCenter)
    return label


def _glyph_icon(glyph: int, muted: bool = False) -> QIcon:
    application = QApplication.instance()
    palette = application.palette() if application is not None else None
    color = (
        palette.color(QPalette.ColorGroup.Disabled if muted else QPalette.ColorGroup.Active,
                      QPalette.ColorRole.WindowText)
        if palette is not None
        else QColor("#808080")
    )
    pixmap = QPixmap(32, 32)
    pixmap.setDevicePixelRatio(2.0)
    pixmap.fill(Qt.GlobalColor.transparent)
    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    painter.setFont(_glyph_font(16))
    painter.setPen(color)
    painter.drawText(QRectF(0, 0, 16, 16), Qt.AlignmentFlag.AlignCenter, chr(glyph))
    painter.end()
    return QIcon(pixmap)


class _NavigationDelegate(QStyledItemDelegate):
    """The page list of MO2's Settings window: a soft fill under the hovered
    and selected page, an accent pill beside the selected one, a glyph and
    the page's name (the glyph is the item's UserRole data)."""

    ItemHeight = 36

    def sizeHint(self, option, index) -> QSize:
        return QSize(super().sizeHint(option, index).width(), self.ItemHeight + 4)

    def paint(self, painter, option, index) -> None:
        selected = bool(option.state & QStyle.StateFlag.State_Selected)
        hovered = bool(option.state & QStyle.StateFlag.State_MouseOver)
        text = option.palette.color(QPalette.ColorGroup.Active, QPalette.ColorRole.WindowText)
        painter.save()
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        row = QRectF(option.rect).adjusted(2, 2, -2, -2)
        alpha = 26 if selected else (15 if hovered else 0)
        if alpha:
            fill = QColor(text)
            fill.setAlpha(alpha)
            painter.setPen(Qt.PenStyle.NoPen)
            painter.setBrush(fill)
            painter.drawRoundedRect(row, 4, 4)
        if selected:
            painter.setPen(Qt.PenStyle.NoPen)
            painter.setBrush(_accent_color())
            painter.drawRoundedRect(QRectF(row.left(), row.center().y() - 8, 3, 16), 1.5, 1.5)
        painter.setFont(_glyph_font(16))
        painter.setPen(text)
        painter.drawText(
            QRectF(row.left() + 14, row.top(), 20, row.height()),
            Qt.AlignmentFlag.AlignCenter,
            str(index.data(Qt.ItemDataRole.UserRole) or ""),
        )
        label = QFont(option.font)
        label.setWeight(QFont.Weight.DemiBold if selected else QFont.Weight.Normal)
        painter.setFont(label)
        text_rect = row.adjusted(46, 0, -8, 0)
        painter.drawText(
            text_rect,
            Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignLeft,
            QFontMetrics(label).elidedText(
                str(index.data() or ""), Qt.TextElideMode.ElideRight, int(text_rect.width())
            ),
        )
        painter.restore()


class _DeveloperTaps(QObject):
    """Seven taps on the MO2 version turn the developer mode on, as on a
    phone's build number. MO2 keeps it from the application's
    revampedDeveloperMode property (see MOApplication::event)."""

    Needed = 7

    def __init__(self, targets: list, status: QLabel):
        super().__init__(status)
        self._taps = 0
        self._last = None
        self._status = status
        for target in targets:
            target.installEventFilter(self)

    def eventFilter(self, watched, event) -> bool:
        if event.type() != QEvent.Type.MouseButtonRelease:
            return False
        # a click a label lets through reaches its row as well: count it once
        if event.timestamp() == self._last:
            return False
        self._last = event.timestamp()
        application = QApplication.instance()
        if application is None:
            return False
        if application.property("revampedDeveloperMode"):
            self._show(_tr("The developer mode is already on: Settings > Developer."))
            return False
        self._taps += 1
        left = self.Needed - self._taps
        if left <= 0:
            application.setProperty("revampedDeveloperMode", True)
            self._show(_tr("You are now a developer. Open Settings again to see "
                           "Settings > Developer."))
        elif left <= 4:
            self._show(
                (_tr("1 more tap to turn the developer mode on.") if left == 1
                 else _tr("{0} more taps to turn the developer mode on.").format(left))
            )
        return False

    def _show(self, text: str) -> None:
        self._status.setText(text)
        self._status.show()


class Mo2RevampedInfo(mobase.IPluginTool):
    """Add an About entry and clearer help for MO2's existing toolbar buttons."""

    def __init__(self):
        super().__init__()
        self._organizer = None
        self._parent_widget = None
        self._about_action = None
        self._diagnostics_action = None

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        organizer.onUserInterfaceInitialized(self._initialize_main_window)
        return True

    def name(self) -> str:
        return "MO2 Revamped Information"

    def localizedName(self) -> str:
        # Settings > Plugins lists it by the name its menu shows
        return self.displayName()

    def author(self) -> str:
        return "MO2 Revamped"

    def description(self) -> str:
        return _tr("About, credits, component versions, and clearer help for MO2 Revamped.")

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(1, 0, 0, 0)

    def requirements(self):
        return []

    def settings(self) -> list[mobase.PluginSetting]:
        return []

    def enabledByDefault(self) -> bool:
        return True

    def displayName(self) -> str:
        return _tr("About MO2 Revamped")

    def tooltip(self) -> str:
        return _tr("View MO2 Revamped features, component versions, credits, and licenses.")

    def icon(self) -> QIcon:
        return QIcon()

    def setParentWidget(self, widget) -> None:
        self._parent_widget = widget

    def display(self) -> None:
        self._show_about(self._parent_widget)

    def _initialize_main_window(self, main_window) -> None:
        self._parent_widget = main_window
        # the command bar's tooltips are the C++ ones: they follow a language
        # change and the endorsement state, which replacing them here broke
        help_menu = main_window.findChild(QMenu, "menuHelp")
        if help_menu is None:
            return
        about_found = False
        diagnostics_found = False
        # the C++ Help menu ("More") already has "About MO2 Revamped"; looking
        # only in the hidden menu bar added a second one nobody could reach
        existing_about = main_window.findChild(QAction, "mo2RevampedAboutAction")
        if existing_about is not None:
            self._about_action = existing_about
            about_found = True
        for action in help_menu.actions():
            if action.objectName() == "mo2RevampedDiagnosticsAction":
                self._diagnostics_action = action
                diagnostics_found = True
        if not about_found:
            help_menu.addSeparator()
            self._about_action = QAction(_tr("About MO2 Revamped…"), main_window)
            self._about_action.setObjectName("mo2RevampedAboutAction")
            self._about_action.setToolTip(
                _tr("MO2 Revamped information, features, components and credits.")
            )
            self._about_action.triggered.connect(
                lambda _checked=False: self._show_about(main_window)
            )
            help_menu.addAction(self._about_action)
        if not diagnostics_found:
            self._diagnostics_action = QAction(
                _tr("Create MO2 Revamped bug report…"), main_window
            )
            self._diagnostics_action.setObjectName("mo2RevampedDiagnosticsAction")
            self._diagnostics_action.setToolTip(
                self._game_tr("Saves a report with the MO2, USVFS and Elden Ring bridge logs.")
                if self._has_elden_ring_tools()
                else _tr("Saves a report with the MO2 and USVFS logs.")
            )
            self._diagnostics_action.triggered.connect(
                lambda _checked=False: self._create_diagnostics_bundle(main_window)
            )
            help_menu.addAction(self._diagnostics_action)

    def _managed_game_key(self) -> str:
        try:
            game = self._organizer.managedGame()
            return game.gameShortName().casefold() if game is not None else ""
        except Exception:
            return ""

    def _has_elden_ring_tools(self) -> bool:
        """Whether this instance manages a game with the Elden Ring tools."""
        return self._managed_game_key() in {"eldenring", "nightreign"}

    def _game_tr(self, text: str) -> str:
        """A translated text that names Elden Ring, for the managed game."""
        translated = _tr(text)
        if self._managed_game_key() == "nightreign":
            return _named_for_nightreign(translated)
        return translated

    def _context_text(self) -> str:
        game_name = _tr("Not detected")
        profile_name = _tr("Not selected")
        try:
            game = self._organizer.managedGame()
            if game is not None:
                game_name = game.gameName()
        except Exception:
            pass
        try:
            profile = self._organizer.profile()
            if profile is not None:
                profile_name = profile.name()
        except Exception:
            pass
        return _tr("Active game: {game}<br>Active profile: {profile}").format(
            game=game_name, profile=profile_name
        )

    def _component_versions(self) -> dict:
        """The versions this copy really has: read from the plugins next to
        this one and from the running program, so the page does not go stale
        when a plugin is updated."""
        plugins_dir = Path(__file__).resolve().parent

        def read(relative: str, pattern: str, shape, default: str) -> str:
            try:
                text = (plugins_dir / relative).read_text(encoding="utf-8")
            except OSError:
                return default
            match = re.search(pattern, text, re.MULTILINE)
            return shape(match.group(1)) if match else default

        game = "basic_games/games/game_eldenring_mo2.py"
        versions = {
            "plugin": read(game, r'^\s*Version = "([^"]+)"', str, "0.5.0-alpha.84"),
            "bridge": read(game, r'NativeBridgeVersion = "([^"]+)"', str, "0.5.0-alpha.38"),
            "installer": read(
                "eldenring_mo2_archive_installer/installer.py",
                r"VersionInfo\(0, 5, 0, (\d+)\)", lambda n: f"alpha.{n}", "alpha.76",
            ),
            "saves": read(
                "eldenring_mo2_tools/save_isolation.py",
                r"VersionInfo\(0, 5, 0, (\d+)\)", lambda n: f"alpha.{n}", "alpha.84",
            ),
            "startup": read(
                "eldenring_mo2_tools/startup_tool.py",
                r"VersionInfo\(0, 5, 0, (\d+)\)", lambda n: f"0.5.0.{n}", "0.5.0.64",
            ),
            "mo2": "2.5.2",
            "qt": QtCore.qVersion(),
            "python": platform.python_version(),
            "openssl": "3.5.9",
        }
        try:
            versions["mo2"] = self._organizer.appVersion().displayString(3)
        except Exception:
            pass
        # the MO2 Revamped release; appVersion() is the MO2 it is based on
        app = QCoreApplication.instance()
        revamped = app.property("revampedVersion") if app is not None else None
        versions["revamped"] = str(revamped) if revamped else ""
        try:
            from PyQt6.QtNetwork import QSslSocket

            library = QSslSocket.sslLibraryVersionString()
            if library:
                versions["openssl"] = library.replace("OpenSSL", "").split()[0]
        except Exception:
            pass
        return versions

    def _show_about(self, parent) -> None:
        # Laid out like MO2's Settings window: the program's card above the
        # pages on the left, cards of rows on the right; the themes' global
        # Fluent rules (fluentNav, fluentCard, fluentSettingRow) style it
        dialog = QDialog(parent)
        dialog.setObjectName("MO2RevampedAboutDialog")
        dialog.setWindowTitle(_tr("About MO2 Revamped"))
        dialog.setMinimumSize(860, 580)
        dialog.resize(960, 660)
        dialog.setWindowFlag(Qt.WindowType.WindowContextHelpButtonHint, False)

        # no right margin: the pages scroll at the window's edge
        layout = QVBoxLayout(dialog)
        layout.setContentsMargins(16, 16, 0, 12)
        layout.setSpacing(12)

        body = QHBoxLayout()
        body.setSpacing(16)
        side = QVBoxLayout()
        side.setSpacing(8)

        program_card = QFrame(dialog)
        program_card.setProperty("fluentCard", True)
        program_card.setFixedWidth(232)
        program_layout = QVBoxLayout(program_card)
        program_layout.setContentsMargins(14, 14, 14, 12)
        program_layout.setSpacing(4)
        logo = QLabel(program_card)
        try:
            # the large logo built into ModOrganizer.exe first, so it always
            # matches the program; the loose files are older builds' fallback
            pixmap = QPixmap(":/MO/gui/app_icon")
            app_dir = QApplication.applicationDirPath()
            if pixmap.isNull():
                pixmap = QPixmap(os.path.join(app_dir, "resources", "mo_icon.png"))
            if pixmap.isNull():
                pixmap = QPixmap(os.path.join(app_dir, "splash.png"))
            if not pixmap.isNull():
                logo.setPixmap(pixmap.scaled(
                    40, 40,
                    Qt.AspectRatioMode.KeepAspectRatio,
                    Qt.TransformationMode.SmoothTransformation,
                ))
        except Exception:
            pass
        program_layout.addWidget(logo)
        title = QLabel("Mod Organizer 2: Revamped", program_card)
        title.setObjectName("instanceDetailName")
        title.setWordWrap(True)
        program_layout.addWidget(title)
        revamped = self._component_versions().get("revamped", "")
        edition = QLabel(
            _tr("Version {version}  ·  ArialSenki").format(version=revamped)
            if revamped
            else _tr("Revamped edition  ·  ArialSenki"),
            program_card,
        )
        edition.setObjectName("instanceDetailCaption")
        edition.setWordWrap(True)
        program_layout.addWidget(edition)
        context = QLabel(self._context_text().replace("<br>", "\n"), program_card)
        context.setObjectName("instanceDetailCaption")
        context.setTextFormat(Qt.TextFormat.PlainText)
        context.setWordWrap(True)
        program_layout.addWidget(context)
        side.addWidget(program_card)

        nav = QListWidget(dialog)
        nav.setObjectName("fluentNav")
        nav.setItemDelegate(_NavigationDelegate(nav))
        nav.setFrameShape(QFrame.Shape.NoFrame)
        nav.setMouseTracking(True)
        nav.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        nav.setFixedWidth(232)
        nav.setAccessibleName(_tr("Pages"))
        side.addWidget(nav, 1)
        body.addLayout(side)

        page_column = QVBoxLayout()
        page_column.setSpacing(4)
        page_title = QLabel(dialog)
        page_title.setProperty("wizardPageTitle", True)
        page_subtitle = QLabel(dialog)
        page_subtitle.setProperty("wizardPageText", True)
        page_subtitle.setWordWrap(True)
        page_subtitle.setContentsMargins(0, 0, 16, 0)
        # room for two lines on every page, from the top: the cards below
        # start at the same height on all of them
        page_subtitle.setAlignment(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop)
        page_subtitle.setMinimumHeight(2 * page_subtitle.fontMetrics().lineSpacing())
        page_column.addWidget(page_title)
        page_column.addWidget(page_subtitle)
        page_column.addSpacing(8)
        pages = QStackedWidget(dialog)
        page_column.addWidget(pages, 1)
        body.addLayout(page_column, 1)
        layout.addLayout(body, 1)

        page_texts: list[tuple[str, str]] = []

        def add_page(glyph: int, name: str, heading: str, subtitle: str) -> QVBoxLayout:
            scroll = QScrollArea(pages)
            scroll.setObjectName("instanceDetailsScroll")
            scroll.setFrameShape(QFrame.Shape.NoFrame)
            scroll.setWidgetResizable(True)
            scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
            content = QWidget(scroll)
            content.setObjectName("fluentPageContent")
            content_layout = QVBoxLayout(content)
            # the gap to the window's edge, with the scroll bar inside it
            content_layout.setContentsMargins(0, 0, 16, 0)
            content_layout.setSpacing(12)
            scroll.setWidget(content)
            pages.addWidget(scroll)
            item = QListWidgetItem(name, nav)
            item.setData(Qt.ItemDataRole.UserRole, chr(glyph))
            page_texts.append((heading, subtitle))
            return content_layout

        def add_card(page: QVBoxLayout) -> QFrame:
            card = QFrame()
            card.setProperty("fluentCard", True)
            card_layout = QVBoxLayout(card)
            card_layout.setContentsMargins(0, 0, 0, 0)
            card_layout.setSpacing(0)
            page.addWidget(card)
            return card

        def add_row(card: QFrame, glyph: int, title: str, description: str = "",
                    widgets: tuple = ()) -> QFrame:
            row = QFrame(card)
            row.setObjectName("fluentSettingRow")
            row.setMinimumHeight(62)
            row_layout = QHBoxLayout(row)
            row_layout.setContentsMargins(18, 10, 12, 10)
            row_layout.setSpacing(14)
            row_layout.addWidget(_glyph_label(glyph, row), 0, Qt.AlignmentFlag.AlignVCenter)
            texts = QVBoxLayout()
            texts.setSpacing(2)
            # the lines keep together and centred, however tall the row
            texts.addStretch(1)
            title_label = QLabel(title, row)
            title_label.setObjectName("fluentSettingTitle")
            title_label.setWordWrap(True)
            texts.addWidget(title_label)
            if description:
                description_label = QLabel(description, row)
                description_label.setObjectName("fluentSettingDescription")
                description_label.setWordWrap(True)
                description_label.setTextInteractionFlags(
                    Qt.TextInteractionFlag.TextSelectableByMouse
                )
                texts.addWidget(description_label)
            texts.addStretch(1)
            row_layout.addLayout(texts, 1)
            for widget in widgets:
                widget.setParent(row)
                row_layout.addWidget(widget, 0, Qt.AlignmentFlag.AlignVCenter)
                widget.show()
            card.layout().addWidget(row)
            return row

        def link_button(glyph: int, tip: str, url: str) -> QToolButton:
            button = QToolButton()
            button.setObjectName("instanceIconButton")
            button.setIcon(_glyph_icon(glyph))
            button.setAutoRaise(True)
            button.setToolTip(tip)
            button.setAccessibleName(tip)
            button.setCursor(Qt.CursorShape.PointingHandCursor)
            button.clicked.connect(lambda _checked=False: QDesktopServices.openUrl(QUrl(url)))
            return button

        # --- Revamped --------------------------------------------------------
        overview = add_page(
            0xE946, _tr("Revamped"), _tr("What Revamped adds"),
            _tr("Keeps the Mod Organizer 2 workflow and adds tools to install "
                "mods, separate setups and recognize each environment more easily."),
        )
        features = add_card(overview)
        elden_ring_tools = self._has_elden_ring_tools()
        if elden_ring_tools:
            add_row(features, 0xE896, _tr("Mod installation"),
                    self._game_tr("Detects Elden Ring paths and recognizes nested folders to "
                                  "install mods and native add-ons."))
            add_row(features, 0xE77B, _tr("Profiles and saves"),
                    self._game_tr("Organizes profiles and, for Elden Ring, can route saves per "
                                  "profile."))
        add_row(features, 0xE8B7, _tr("Independent instances"),
                self._game_tr("Choose global or portable setups. You can also create an "
                              "isolated MO2 copy for Elden Ring.")
                if elden_ring_tools
                else _tr("Choose global or portable setups."))
        add_row(features, 0xE8F1, _tr("Nexus collections"),
                _tr("Browse and install Nexus Mods collections from MO2."))
        add_row(features, 0xE7FC, _tr("Supported games"),
                _tr("Skyrim Special Edition, Fallout 4, Fallout 3, Fallout: New Vegas, "
                    "Baldur's Gate 3, Cyberpunk 2077, Elden Ring and Elden Ring "
                    "Nightreign, plus the other games Mod Organizer 2 supports."))
        add_row(features, 0xE790, _tr("Interface and diagnostics"),
                _tr("Includes Windows 11 style themes (Light and Dark) "
                    "with Fluent icons, plus tools to prepare diagnostic reports."))
        if elden_ring_tools:
            note = QLabel(
                self._game_tr("Elden Ring-specific features are only enabled in an instance "
                              "for that game."),
            )
            note.setObjectName("fluentSettingDescription")
            note.setWordWrap(True)
            note.setContentsMargins(0, 0, 4, 0)
            overview.addWidget(note)
        overview.addStretch(1)

        # --- Components ------------------------------------------------------
        versions = self._component_versions()
        components = add_page(
            0xE950, _tr("Components"), _tr("Included components"),
            _tr("These are the versions included in this build; they are not "
                "necessarily the latest versions available."),
        )
        parts = add_card(components)
        if versions.get("revamped"):
            add_row(parts, 0xE946, "MO2 Revamped", versions["revamped"])
        base_row = add_row(parts, 0xE737, _tr("MO2 base"),
                           _tr("Mod Organizer 2 · {mo2}\nQt and PyQt · {qt}\nPython · {python}\n"
                               "OpenSSL · {openssl}").format(**versions))
        add_row(parts, 0xE8B7, _tr("Virtual file system"),
                "USVFS · 0.5.7.2\n"
                + _tr("Deploys mods virtually without changing the game's "
                      "original files."))
        if elden_ring_tools:
            add_row(parts, 0xE7FC, self._game_tr("Elden Ring tools"),
                    _tr("Game plugin · {plugin}\nNative bridge · {bridge}\n"
                        "Archive installer · {installer}").format(**versions))
        add_row(parts, 0xE771, _tr("Profiles and interface"),
                _tr("Save isolation · {saves}\nStartup options · {startup}\n"
                    "Fluent themes · Light and Dark").format(**versions)
                if elden_ring_tools
                else _tr("Fluent themes · Light and Dark"))
        developer_status = QLabel()
        developer_status.setObjectName("fluentSettingDescription")
        developer_status.setWordWrap(True)
        developer_status.setContentsMargins(0, 0, 4, 0)
        developer_status.hide()
        components.addWidget(developer_status)
        # the version's row, and its description where the version is shown
        _DeveloperTaps([base_row, *base_row.findChildren(QLabel)], developer_status)
        components.addStretch(1)

        # --- Credits and licenses -------------------------------------------
        credits = add_page(
            0xE716, _tr("Credits and licenses"), _tr("Credits and licenses"),
            _tr("Information about this community edition, the base project and its legal notices."),
        )
        people = add_card(credits)
        add_row(people, 0xE77B, _tr("Revamped authorship"),
                "MO2 Revamped · ArialSenki\n" + _tr("Independent community edition."),
                (link_button(0xE774, _tr("MO2 Revamped on Nexus Mods"),
                             "https://www.nexusmods.com/site/mods/2612"),
                 link_button(0xE8A7, _tr("MO2 Revamped on GitHub"),
                             "https://github.com/ArialSenki/MO2-Revamped")))
        add_row(people, 0xE8F1, _tr("Base project"),
                _tr("Based on Mod Organizer 2 and its contributions. Revamped is not "
                    "an official release from the MO2 team."))
        licenses_folder = os.path.join(QApplication.applicationDirPath(), "licenses")
        folder_button = QToolButton()
        folder_button.setObjectName("instanceIconButton")
        folder_button.setIcon(_glyph_icon(0xE838))
        folder_button.setAutoRaise(True)
        folder_button.setToolTip(_tr("Open the licenses folder"))
        folder_button.setAccessibleName(folder_button.toolTip())
        folder_button.setCursor(Qt.CursorShape.PointingHandCursor)
        folder_button.setEnabled(os.path.isdir(licenses_folder))
        folder_button.clicked.connect(
            lambda _checked=False: QDesktopServices.openUrl(QUrl.fromLocalFile(licenses_folder))
        )
        add_row(people, 0xE8A5, _tr("Licenses and source code"),
                _tr("The authorship notices and licenses of MO2 and its dependencies are "
                    "kept. See the licenses folder next to the program. For the source "
                    "code, see docs/UPSTREAMS.md in the MO2 Revamped repository."),
                (folder_button,))
        add_row(people, 0xE8B7, _tr("Virtual file system"),
                _tr("USVFS, from the Mod Organizer 2 project. For its source code and "
                    "license, see the licenses folder and docs/UPSTREAMS.md."))
        credits.addStretch(1)

        # --- Diagnostics -----------------------------------------------------
        diagnostics = add_page(
            0xE9D9, _tr("Diagnostics"), _tr("Diagnostics"),
            _tr("Collects the logs needed to help locate MO2 Revamped problems."),
        )
        report = add_card(diagnostics)
        add_row(report, 0xE8A5, _tr("Report contents"),
                self._game_tr("The end of the MO2 log, recent USVFS logs and the Elden Ring "
                              "bridge logs, with the versions of MO2, Windows, the GPU, the "
                              "game and the plugins, and the instance and mod counts.")
                if elden_ring_tools
                else _tr("The end of the MO2 log and recent USVFS logs, with the versions "
                         "of MO2, Windows, the GPU, the game and the plugins, and the "
                         "instance and mod counts."))
        add_row(report, 0xE72E, _tr("Privacy"),
                _tr("Your Windows user name, the PC name, e-mail addresses, Nexus keys, "
                    "tokens and cookies are replaced. Mods, downloads, saves and crash "
                    "dumps are not included. A preview shows every file before you save "
                    "or copy it."))
        add_row(report, 0xE7BA, _tr("If MO2 does not start"),
                _tr("Attach the latest installer log shown in its error message. "
                    "If MO2 closes unexpectedly, also keep the file from crashDumps."))
        actions = QFrame(report)
        actions.setObjectName("fluentSettingRow")
        actions_layout = QHBoxLayout(actions)
        actions_layout.setContentsMargins(12, 12, 12, 12)
        create_report_button = QPushButton(
            _glyph_icon(0xEBE8), _tr("Create MO2 Revamped bug report…"), actions
        )
        create_report_button.setObjectName("mo2RevampedAboutDiagnosticsButton")
        create_report_button.setAutoDefault(False)
        create_report_button.clicked.connect(
            lambda _checked=False: self._create_diagnostics_bundle(dialog)
        )
        actions_layout.addWidget(create_report_button)
        actions_layout.addStretch(1)
        report.layout().addWidget(actions)
        diagnostics.addStretch(1)

        def show_page(index: int) -> None:
            if 0 <= index < len(page_texts):
                pages.setCurrentIndex(index)
                page_title.setText(page_texts[index][0])
                page_subtitle.setText(page_texts[index][1])

        nav.currentRowChanged.connect(show_page)
        # accessibility tools select a page without moving the current row
        nav.itemSelectionChanged.connect(
            lambda: show_page(nav.row(nav.selectedItems()[0])) if nav.selectedItems() else None
        )
        nav.setCurrentRow(0)

        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Close, dialog)
        buttons.setContentsMargins(0, 0, 16, 0)
        buttons.rejected.connect(dialog.reject)
        buttons.accepted.connect(dialog.accept)
        buttons.button(QDialogButtonBox.StandardButton.Close).clicked.connect(
            dialog.accept
        )
        layout.addWidget(buttons)
        dialog.exec()

    @staticmethod
    def _safe_text(value) -> str:
        try:
            return str(value)
        except Exception:
            return "unavailable"

    # --- the bug report ------------------------------------------------------
    # Every text of the report goes through _Redactor before it is shown or
    # saved; the preview shows exactly the texts the ZIP and the clipboard get.

    # how many of the last lines of each log go into the report
    LogLines = {"logs/mo_interface.log": 3000}
    DefaultLogLines = 800

    def _game_facts(self) -> list[str]:
        lines = []
        try:
            game = self._organizer.managedGame()
        except Exception:
            game = None
        if game is None:
            return ["Game: not detected"]

        def ask(method: str) -> str:
            try:
                value = getattr(game, method)()
                if hasattr(value, "absolutePath"):
                    value = value.absolutePath()
                return self._safe_text(value) or "-"
            except Exception:
                return "unavailable"

        lines.append(f"Game: {ask('gameName')} ({ask('gameShortName')})")
        lines.append(f"Game version: {ask('gameVersion')}")
        try:
            variants = list(game.gameVariants())
            if len(variants) > 1:
                lines.append("Game variants: " + ", ".join(map(str, variants)))
        except Exception:
            pass
        lines.append(f"Game directory: {ask('gameDirectory')}")
        lines.append(f"Game data directory: {ask('dataDirectory')}")
        return lines

    def _instance_facts(self) -> list[str]:
        app_dir = Path(QApplication.applicationDirPath())
        if (app_dir / "eldenring-only.portable").is_file():
            mode = "Isolated portable (Elden Ring only)"
        elif (app_dir / "portable.txt").is_file():
            mode = "Portable"
        else:
            mode = "Global"
        base = self._organizer_path("basePath")
        return [
            f"Instance type: {mode}",
            f"Instance name: {Path(base).name if base != 'Not available' else base}",
            f"Application directory: {app_dir}",
            f"Instance directory: {base}",
            f"Mods directory: {self._organizer_path('modsPath')}",
            f"Downloads directory: {self._organizer_path('downloadsPath')}",
            f"Overwrite directory: {self._organizer_path('overwritePath')}",
        ]

    def _mod_facts(self) -> list[str]:
        """The profile, its mod counts and the Root folders the VFS deploys."""
        lines = []
        try:
            profile = self._organizer.profile()
            lines.append(f"Active profile: {self._safe_text(profile.name())}")
            for label, method in (
                ("local saves", "localSavesEnabled"),
                ("local game settings", "localSettingsEnabled"),
            ):
                try:
                    lines.append(
                        f"Profile {label}: {'yes' if getattr(profile, method)() else 'no'}"
                    )
                except Exception:
                    pass
        except Exception:
            lines.append("Active profile: not selected")
        try:
            mod_list = self._organizer.modList()
            names = list(mod_list.allModsByProfilePriority())
            active_flag = mobase.ModState.ACTIVE

            def kind(name: str) -> str:
                # the game's own DLC and Creation Club files show as mods too
                try:
                    mod = mod_list.getMod(name)
                    if mod.isOverwrite():
                        return "overwrite"
                    if mod.isForeign():
                        return "game"
                    if mod.isSeparator():
                        return "separator"
                except Exception:
                    if name.endswith("_separator"):
                        return "separator"
                return "mod"

            kinds = {n: kind(n) for n in names}
            mods = [n for n in names if kinds[n] == "mod"]
            active = [n for n in mods if mod_list.state(n) & active_flag]
            lines.append(
                f"Mods: {len(active)} enabled of {len(mods)} "
                f"({sum(k == 'separator' for k in kinds.values())} separators, "
                f"{sum(k == 'game' for k in kinds.values())} game files listed as mods)"
            )
            mods_dir = Path(self._organizer.modsPath())
            with_root = sum(1 for n in active if (mods_dir / n / "Root").is_dir())
            lines.append(f"Enabled mods with a Root folder: {with_root}")
        except Exception as error:
            lines.append(f"Mods: unavailable ({error})")
        try:
            plugins = self._organizer.pluginList()
            plugin_names = list(plugins.pluginNames())
            active_plugins = sum(
                1 for n in plugin_names
                if plugins.state(n) == mobase.PluginState.ACTIVE
            )
            if plugin_names:
                lines.append(f"Game plugins: {active_plugins} active of {len(plugin_names)}")
        except Exception:
            pass
        try:
            short = self._organizer.managedGame().gameShortName()
            state = Path(self._organizer.basePath()) / "rootcopy-backup" / short / "manifest.json"
            lines.append(
                "Root copies waiting to be restored: "
                + ("yes" if state.is_file() else "no")
            )
        except Exception:
            pass
        lines.append("Virtual file system: USVFS 0.5.7.2")
        return lines

    @staticmethod
    def _windows_facts() -> list[str]:
        lines = []
        try:
            import winreg

            with winreg.OpenKey(
                winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows NT\CurrentVersion"
            ) as key:
                def value(name: str):
                    try:
                        return winreg.QueryValueEx(key, name)[0]
                    except OSError:
                        return ""

                build = str(value("CurrentBuild") or sys.getwindowsversion().build)
                product = str(value("ProductName") or "Windows")
                # Windows 11 still says "Windows 10" in ProductName
                if build.isdigit() and int(build) >= 22000:
                    product = product.replace("Windows 10", "Windows 11")
                ubr = value("UBR")
                display = value("DisplayVersion") or value("ReleaseId")
                lines.append(
                    f"Windows: {product} {display} (build {build}"
                    + (f".{ubr}" if ubr != "" else "") + ")"
                )
        except Exception:
            lines.append(f"Windows: {platform.platform()}")
        lines.append(f"Architecture: {platform.machine()}")
        try:
            import ctypes

            class _Memory(ctypes.Structure):
                _fields_ = [
                    ("dwLength", ctypes.c_ulong), ("dwMemoryLoad", ctypes.c_ulong),
                    ("ullTotalPhys", ctypes.c_ulonglong), ("ullAvailPhys", ctypes.c_ulonglong),
                    ("ullTotalPageFile", ctypes.c_ulonglong),
                    ("ullAvailPageFile", ctypes.c_ulonglong),
                    ("ullTotalVirtual", ctypes.c_ulonglong),
                    ("ullAvailVirtual", ctypes.c_ulonglong),
                    ("ullAvailExtendedVirtual", ctypes.c_ulonglong),
                ]

            memory = _Memory()
            memory.dwLength = ctypes.sizeof(_Memory)
            if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(memory)):
                lines.append(
                    f"Memory: {memory.ullTotalPhys / 2**30:.1f} GiB, "
                    f"{memory.ullAvailPhys / 2**30:.1f} GiB free"
                )
        except Exception:
            pass
        # the display adapters as Windows lists them, with their drivers
        try:
            import winreg

            gpus = []
            adapters = (
                r"SYSTEM\CurrentControlSet\Control\Class"
                r"\{4d36e968-e325-11ce-bfc1-08002be10318}"
            )
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, adapters) as root:
                for index in range(64):
                    try:
                        sub = winreg.EnumKey(root, index)
                    except OSError:
                        break
                    if not sub.isdigit():
                        continue
                    try:
                        with winreg.OpenKey(root, sub) as key:
                            name = winreg.QueryValueEx(key, "DriverDesc")[0]
                            try:
                                driver = winreg.QueryValueEx(key, "DriverVersion")[0]
                            except OSError:
                                driver = "?"
                    except OSError:
                        continue
                    entry = f"{name} (driver {driver})"
                    if entry not in gpus:
                        gpus.append(entry)
            lines.extend(f"GPU: {gpu}" for gpu in gpus or ["not found"])
        except Exception:
            lines.append("GPU: unavailable")
        return lines

    @staticmethod
    def _file_version(path: Path) -> str:
        """The version resource of a DLL, as Explorer shows it."""
        try:
            import ctypes
            from ctypes import wintypes

            version = ctypes.windll.version
            size = version.GetFileVersionInfoSizeW(str(path), None)
            if not size:
                return ""
            buffer = ctypes.create_string_buffer(size)
            if not version.GetFileVersionInfoW(str(path), 0, size, buffer):
                return ""
            pointer = ctypes.c_void_p()
            length = wintypes.UINT()
            if not version.VerQueryValueW(
                buffer, "\\", ctypes.byref(pointer), ctypes.byref(length)
            ):
                return ""
            fixed = ctypes.cast(pointer, ctypes.POINTER(ctypes.c_uint32 * 13)).contents
            ms, ls = fixed[2], fixed[3]
            return f"{ms >> 16}.{ms & 0xFFFF}.{ls >> 16}.{ls & 0xFFFF}"
        except Exception:
            return ""

    def _plugin_facts(self) -> list[str]:
        """MO2's plugins in the plugins folder, with the versions they carry."""
        plugins_dir = Path(QApplication.applicationDirPath()) / "plugins"
        pattern = re.compile(
            r"VersionInfo\(\s*(\d+)\s*,\s*(\d+)\s*(?:,\s*(\d+))?(?:\s*,\s*(\d+))?"
            r"|^\s*Version\s*=\s*\"([^\"]+)\"",
            re.MULTILINE,
        )

        def python_version(*files: Path) -> str:
            for file in files:
                try:
                    match = pattern.search(file.read_text(encoding="utf-8", errors="replace"))
                except OSError:
                    continue
                if match:
                    if match.group(5):
                        return match.group(5)
                    return ".".join(g for g in match.groups()[:4] if g is not None)
            return ""

        lines = []
        try:
            entries = sorted(plugins_dir.iterdir(), key=lambda p: p.name.casefold())
        except OSError:
            return ["Plugins: the plugins folder could not be read"]
        for entry in entries:
            name = entry.name
            if name.startswith((".", "__")) or name.endswith((".bak", ".pyc")) or ".bak-" in name:
                continue
            version = ""
            if entry.is_file() and name.casefold().endswith(".dll"):
                # MO2's own plugin DLLs carry no version: their date tells
                # which build they came with
                version = self._file_version(entry)
                if not version:
                    try:
                        version = "built " + datetime.fromtimestamp(
                            entry.stat().st_mtime, timezone.utc
                        ).strftime("%Y-%m-%d")
                    except OSError:
                        # removed or locked since the folder was listed
                        version = ""
            elif entry.is_file() and name.casefold().endswith(".py"):
                version = python_version(entry)
            elif entry.is_dir() and (entry / "__init__.py").is_file():
                version = python_version(
                    entry / "__init__.py", *sorted(entry.glob("*.py"))[:8]
                )
            elif entry.is_dir():
                # data, plugin_python's own folder and other helpers
                continue
            else:
                continue
            lines.append(f"  {name}: {version or 'no version'}")
        return [f"Plugins ({len(lines)}):", *lines]

    def _crash_dump_facts(self) -> list[str]:
        """Where the last crash dump is; the dump itself is never included."""
        try:
            folder = Path(self._organizer.basePath()) / "crashDumps"
            candidates = list(folder.glob("*.dmp"))
        except Exception:
            return ["Last crash dump: unavailable"]
        # each dump's facts read once: one removed or locked meanwhile (MO2
        # deletes the oldest) is left out instead of failing the whole report
        dumps = []
        for dump in candidates:
            try:
                dumps.append((dump, dump.stat()))
            except OSError:
                continue
        if not dumps:
            return ["Last crash dump: none"]
        dumps.sort(key=lambda item: item[1].st_mtime, reverse=True)
        last, info = dumps[0]
        when = datetime.fromtimestamp(info.st_mtime, timezone.utc).isoformat()
        return [
            f"Crash dumps: {len(dumps)}",
            f"Last crash dump: {last} (modified UTC {when}, "
            f"{info.st_size // 1024} KiB, not included)",
        ]

    def _report_summary(self) -> str:
        versions = self._component_versions()
        try:
            pyqt_version = QtCore.PYQT_VERSION_STR
        except Exception:
            pyqt_version = "unavailable"
        revamped = versions.get("revamped") or "unknown"
        channel = "stable"
        lowered = revamped.casefold()
        if "-beta" in lowered or "-rc" in lowered:
            channel = "beta"
        elif any(tag in lowered for tag in ("-alpha", "-dev", "-test", "-preview")):
            channel = "development"
        app = QCoreApplication.instance()
        build = app.property("revampedBuild") if app is not None else None
        developer = bool(app.property("revampedDeveloperMode")) if app is not None else False

        sections = [
            ("MO2 Revamped", [
                f"Version: {revamped}",
                f"Channel: {channel}",
                f"Build: {build or 'unknown'}",
                f"Base: Mod Organizer {versions['mo2']}",
                f"Qt: {versions['qt']}",
                f"PyQt: {pyqt_version}",
                f"Python: {platform.python_version()}",
                f"OpenSSL: {versions['openssl']}",
                f"Developer mode: {'on' if developer else 'off'}",
            ]),
            ("System", self._windows_facts()),
            ("Game", self._game_facts()),
            ("Instance", self._instance_facts()),
            ("Profile and mods", self._mod_facts()),
            ("Crashes", self._crash_dump_facts()),
        ]
        if self._has_elden_ring_tools():
            sections.append(("Elden Ring tools", [
                f"Game plugin: {versions['plugin']}",
                f"Native bridge: {versions['bridge']}",
                f"Archive layout installer: {versions['installer']}",
                f"Save isolation: {versions['saves']}",
                f"Startup options: {versions['startup']}",
            ]))
        sections.append(("Plugins", self._plugin_facts()))

        out = [
            "MO2 Revamped bug report",
            f"Created UTC: {datetime.now(timezone.utc).isoformat(timespec='seconds')}",
            "Private data was replaced: the Windows user name (%USERNAME%), the PC "
            "name (<pc>), e-mail addresses (<email>), Nexus keys, tokens and cookies "
            "(<removed>).",
        ]
        for title, lines in sections:
            out.append("")
            out.append(f"[{title}]")
            out.extend(lines)
        return "\n".join(out) + "\n"

    def _organizer_path(self, method_name: str) -> str:
        try:
            value = getattr(self._organizer, method_name)()
            if hasattr(value, "absolutePath"):
                value = value.absolutePath()
            return self._safe_text(value)
        except Exception:
            return "Not available"

    def _diagnostic_log_dir(self) -> Path:
        # MO2 writes its logs into the instance folder: beside the program for
        # a portable instance, under %LOCALAPPDATA%\ModOrganizer for a global one
        try:
            log_dir = Path(self._organizer.basePath()) / "logs"
            if log_dir.is_dir():
                return log_dir
        except Exception:
            pass
        return Path(QApplication.applicationDirPath()) / "logs"

    def _diagnostic_log_files(self) -> list[tuple[str, Path]]:
        log_dir = self._diagnostic_log_dir()
        candidates: list[tuple[str, Path]] = []
        main_log = log_dir / "mo_interface.log"
        if main_log.is_file():
            candidates.append(("logs/mo_interface.log", main_log))

        if log_dir.is_dir():
            def modified(path: Path) -> float:
                # a log removed meanwhile sorts last instead of failing the report
                try:
                    return path.stat().st_mtime
                except OSError:
                    return 0.0

            usvfs_logs = sorted(
                log_dir.glob("usvfs-*.log"),
                key=modified,
                reverse=True,
            )
            candidates.extend(
                (f"logs/{path.name}", path) for path in usvfs_logs[:3]
            )
            memory_log = log_dir / "memory_diagnostics.log"
            if memory_log.is_file():
                candidates.append(("logs/memory_diagnostics.log", memory_log))

        # the bridge runs only with these games: another instance's report
        # must not pick up a log left by an Elden Ring run
        if not self._has_elden_ring_tools():
            return candidates
        temp_dir = Path(tempfile.gettempdir())
        # the bridge names its log after the game it runs in
        log_name = (
            "NightreignMO2Bridge"
            if self._managed_game_key() == "nightreign"
            else "EldenRingMO2Bridge"
        )
        compat_name = (
            "NightreignMO2Me3Compat"
            if self._managed_game_key() == "nightreign"
            else "EldenRingMO2Me3Compat"
        )
        for archive_name, filename in (
            ("bridge/current.log", f"{log_name}.log"),
            ("bridge/previous.log", f"{log_name}.previous.log"),
            ("bridge/me3_compat.log", f"{compat_name}.log"),
        ):
            path = temp_dir / filename
            if path.is_file():
                candidates.append((archive_name, path))
        return candidates

    @staticmethod
    def _log_tail(path: Path, lines: int) -> tuple[str, int, bool]:
        """The last lines of a log, its size, and whether lines were left out.
        Only the end of the file is read: mo_interface.log can grow to
        hundreds of MiB in a long session."""
        with path.open("rb") as stream:
            size = os.fstat(stream.fileno()).st_size
            window = min(size, max(256 * 1024, lines * 400))
            stream.seek(size - window)
            data = stream.read(window)
        text = data.decode("utf-8", errors="replace")
        parts = text.splitlines()
        # the first line of a window that starts mid-file is cut
        if window < size and parts:
            parts = parts[1:]
        cut = window < size or len(parts) > lines
        return "\n".join(parts[-lines:]) + "\n", size, cut

    def _build_report(self) -> list[tuple[str, str]]:
        """Every file of the report with its anonymized text, in order."""
        redact = _Redactor()
        files = [("report.txt", redact(self._report_summary()))]
        included = []
        errors = []
        for archive_name, source in self._diagnostic_log_files():
            limit = self.LogLines.get(archive_name, self.DefaultLogLines)
            try:
                text, size, cut = self._log_tail(source, limit)
                modified = datetime.fromtimestamp(
                    source.stat().st_mtime, timezone.utc
                ).isoformat(timespec="seconds")
            except OSError as error:
                errors.append(f"Could not read {archive_name}: {error}")
                continue
            if cut:
                text = f"[MO2 Revamped report: only the last {limit} lines]\n" + text
            files.append((archive_name, redact(text)))
            included.append(
                f"{archive_name}\tmodified UTC {modified}\t{size} bytes"
                + (f"\tlast {limit} lines" if cut else "")
            )
        files.append((
            "included-files.txt",
            redact("\n".join(included + errors) or "No log files were available.") + "\n",
        ))
        return files

    @staticmethod
    def _show_report_notice(
        parent, object_name: str, title: str, heading: str, details: str
    ) -> None:
        message = QMessageBox(parent)
        message.setObjectName(object_name)
        message.setOption(QMessageBox.Option.DontUseNativeDialog, True)
        message.setWindowTitle(title)
        message.setIcon(QMessageBox.Icon.NoIcon)
        message.setText(heading)
        message.setInformativeText(details)
        message.setStandardButtons(QMessageBox.StandardButton.Ok)
        message.setDefaultButton(QMessageBox.StandardButton.Ok)
        message.setEscapeButton(QMessageBox.StandardButton.Ok)
        accept_button = message.button(QMessageBox.StandardButton.Ok)
        if accept_button is not None:
            accept_button.setText(_tr("OK"))
            accept_button.setObjectName("mo2RevampedReportAcceptButton")
        for label_name in ("qt_msgbox_label", "qt_msgbox_informativelabel"):
            label = message.findChild(QLabel, label_name)
            if label is not None:
                label.setWordWrap(True)
        message.exec()

    def _create_diagnostics_bundle(self, parent) -> None:
        QApplication.setOverrideCursor(Qt.CursorShape.WaitCursor)
        try:
            files = self._build_report()
        except Exception as error:
            QApplication.restoreOverrideCursor()
            self._report_failed(parent, error, None)
            return
        QApplication.restoreOverrideCursor()
        _ReportPreview(parent, files, lambda dialog: self._save_report(dialog, files)).exec()

    def _save_report(self, parent, files: list[tuple[str, str]]) -> bool:
        filename = "MO2-Revamped-Report-" + datetime.now().strftime(
            "%Y%m%d-%H%M%S"
        ) + ".zip"
        initial_path = Path.home() / "Desktop" / filename
        save_dialog = QFileDialog(parent)
        save_dialog.setObjectName("MO2RevampedReportSaveDialog")
        save_dialog.setWindowTitle(_tr("Save MO2 Revamped report"))
        save_dialog.setAcceptMode(QFileDialog.AcceptMode.AcceptSave)
        save_dialog.setFileMode(QFileDialog.FileMode.AnyFile)
        save_dialog.setNameFilters([_tr("ZIP archive (*.zip)")])
        save_dialog.setDefaultSuffix("zip")
        save_dialog.setDirectory(str(initial_path.parent))
        save_dialog.selectFile(initial_path.name)
        save_label = _tr("Save")
        save_dialog.setLabelText(QFileDialog.DialogLabel.LookIn, _tr("Save in:"))
        save_dialog.setLabelText(QFileDialog.DialogLabel.FileName, _tr("File name:"))
        save_dialog.setLabelText(QFileDialog.DialogLabel.FileType, _tr("File type:"))
        save_dialog.setLabelText(QFileDialog.DialogLabel.Accept, save_label)
        save_dialog.setLabelText(QFileDialog.DialogLabel.Reject, _tr("Cancel"))
        for button in save_dialog.findChildren(QPushButton):
            if button.text().replace("&", "") == save_label:
                button.setObjectName("mo2RevampedReportSaveButton")
        save_dialog.setMinimumSize(760, 500)
        save_dialog.resize(860, 560)
        while True:
            if save_dialog.exec() != QDialog.DialogCode.Accepted:
                return False
            selected_files = save_dialog.selectedFiles()
            destination = selected_files[0] if selected_files else ""
            if not destination:
                return False
            if destination.casefold().endswith(".zip"):
                break
            # a name with another extension ("report.txt") gets .zip here, after
            # the dialog asked about overwriting the name as typed: ask again
            # for the file that will really be written
            destination += ".zip"
            if not Path(destination).exists():
                break
            answer = QMessageBox.question(
                parent,
                _tr("Save MO2 Revamped report"),
                _tr("{name} already exists. Do you want to replace it?").format(
                    name=Path(destination).name
                ),
            )
            if answer == QMessageBox.StandardButton.Yes:
                break
            save_dialog.selectFile(Path(destination).name)

        archive_started = False
        try:
            with zipfile.ZipFile(
                destination, "w", compression=zipfile.ZIP_DEFLATED
            ) as archive:
                archive_started = True
                for name, text in files:
                    archive.writestr(name, text.encode("utf-8"))
        except Exception as error:
            self._report_failed(parent, error, destination if archive_started else None)
            return False
        self._show_report_notice(
            parent,
            "MO2RevampedReportSuccessDialog",
            _tr("Report created"),
            _tr("The report was saved"),
            _tr("Location:\n{path}\n\nIt holds exactly what the preview showed.").format(
                path=destination
            ),
        )
        return True

    def _report_failed(self, parent, error: Exception, unfinished: str | None) -> None:
        # PyQt passes Qt's log functions ASCII only; a path with an accent
        # in the traceback must not stop this handler
        qCritical(
            (
                "MO2 Revamped: could not create diagnostic report.\n"
                + traceback.format_exc()
            ).encode("ascii", "backslashreplace").decode("ascii")
        )
        # an unfinished ZIP is not left behind to be shared by mistake
        # (a file MO2 could not even open is left alone)
        if unfinished:
            try:
                Path(unfinished).unlink(missing_ok=True)
            except OSError:
                pass
        self._show_report_notice(
            parent,
            "MO2RevampedReportErrorDialog",
            _tr("Could not create the report"),
            _tr("MO2 Revamped could not save the bug report"),
            _tr("{error}\n\nCheck the MO2 log for details.").format(error=error),
        )


class _Redactor:
    """Takes private data out of a report's text: the Windows user name and
    the PC name, e-mail addresses, and Nexus keys, tokens and cookies."""

    Removed = "<removed>"

    def __init__(self):
        users = {
            os.environ.get("USERNAME", ""),
            Path.home().name,
            Path(os.environ.get("USERPROFILE", "")).name,
        }
        try:
            import getpass

            users.add(getpass.getuser())
        except Exception:
            pass
        # the profile folder's 8.3 name ("JANEDO~1") shows in Temp paths
        try:
            import ctypes

            profile = os.environ.get("USERPROFILE", "")
            if profile:
                buffer = ctypes.create_unicode_buffer(1024)
                if ctypes.windll.kernel32.GetShortPathNameW(profile, buffer, 1024):
                    users.add(Path(buffer.value).name)
        except Exception:
            pass
        computers = {
            os.environ.get("COMPUTERNAME", ""),
            platform.node(),
            os.environ.get("USERDOMAIN", ""),
        }
        # "a" or "pc" would wipe half the text: names that short are left
        self._users = sorted({u for u in users if len(u) >= 2}, key=len, reverse=True)
        self._computers = sorted(
            {c for c in computers if len(c) >= 2 and c.casefold() != "workgroup"},
            key=len, reverse=True,
        )
        secret = r"[^\s\"'&;,<>]+"
        self._secrets = [
            # Nexus links and API requests: the download key and the account
            (re.compile(r"(?i)(\b(?:key|apikey|api_key|api-key|access_token|"
                        r"refresh_token|id_token|token|password|cooppassword|secret|session|"
                        r"user_id|userid)\b[\"']?\s*[=:]\s*[\"']?)" + secret), r"\1" + self.Removed),
            # a Nexus API key: base64 pieces joined by "--" ("...==--...--...=="),
            # which can hold "/" and so isn't caught below
            (re.compile(r"(?<![A-Za-z0-9+/=])[A-Za-z0-9+/]{8,}={0,2}--[A-Za-z0-9+/]{8,}={0,2}"
                        r"(?:--[A-Za-z0-9+/]{8,}={0,2})+(?![A-Za-z0-9+/=])"), self.Removed),
            # headers with keys and cookies, to the end of the line
            (re.compile(r"(?im)^(.*?\b(?:cookie|set-cookie|authorization|apikey)\s*:\s*).*$"),
             r"\1" + self.Removed),
            (re.compile(r"(?i)\b(bearer|basic)\s+[A-Za-z0-9._~+/=-]{8,}"), r"\1 " + self.Removed),
            # JSON web tokens
            (re.compile(r"\beyJ[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]*"),
             self.Removed),
            # a long run of mixed letters and digits is a key (a Nexus API key
            # is over a hundred): hashes are hexadecimal, paths have
            # separators and archive names have underscores or dots
            (re.compile(r"(?<![A-Za-z0-9+=_./\\-])(?=[A-Za-z0-9+=-]*[a-z])"
                        r"(?=[A-Za-z0-9+=-]*[A-Z])(?=[A-Za-z0-9+=-]*\d)"
                        r"(?!(?:[A-Za-z0-9+=]*-){5})"
                        r"[A-Za-z0-9+=-]{64,}(?![A-Za-z0-9+=_./\\-])"), self.Removed),
        ]
        # from the start of a run only, and at most 64 characters before the @
        # (the limit for an address): a long run without @ (base64, hex) is
        # tried once instead of from each of its characters
        self._email = re.compile(
            r"(?<![A-Za-z0-9._%+-])[A-Za-z0-9._%+-]{1,64}@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+"
        )
        # a SteamID64 (in Elden Ring's and other games' save paths) is the
        # player's public Steam profile
        self._steam = re.compile(r"(?<!\d)7656119\d{10}(?!\d)")
        # the same account by Steam's 32-bit id: Steam\userdata\<id> and
        # SteamID3 ("[U:1:<id>]")
        self._steam_folder = re.compile(r"(?i)(userdata[\\/]+)\d{3,}(?!\d)")
        self._steam3 = re.compile(r"\[U:1:\d+\]")

    def __call__(self, text: str) -> str:
        for pattern, replacement in self._secrets:
            text = pattern.sub(replacement, text)
        text = self._email.sub("<email>", text)
        text = self._steam.sub("<steamid>", text)
        text = self._steam_folder.sub(r"\1<steamid>", text)
        text = self._steam3.sub("[U:1:<steamid>]", text)
        for user in self._users:
            # file:/// links spell "Jane Doe" as "Jane%20Doe"
            encoded = quote(user)
            if encoded != user:
                text = re.sub(r"(?i)(?<![A-Za-z0-9_])" + re.escape(encoded)
                              + r"(?![A-Za-z0-9_])", "%USERNAME%", text)
            name = re.escape(user)
            # C:\Users\<name> and /Users/<name>, as the task's paths say
            text = re.sub(r"(?i)([\\/]Users[\\/]+)" + name + r"(?![A-Za-z0-9_])",
                          r"\1<user>", text)
            text = re.sub(r"(?i)(?<![A-Za-z0-9_])" + name + r"(?![A-Za-z0-9_])",
                          "%USERNAME%", text)
        for computer in self._computers:
            text = re.sub(r"(?i)(?<![A-Za-z0-9_%])" + re.escape(computer)
                          + r"(?![A-Za-z0-9_%])", "<pc>", text)
        return text


class _ReportPreview(QDialog):
    """Shows every file of the report as it will be saved or copied."""

    def __init__(self, parent, files: list[tuple[str, str]], save):
        super().__init__(parent)
        self.setObjectName("MO2RevampedReportPreview")
        self.setWindowTitle(_tr("Create bug report"))
        self.setWindowFlag(Qt.WindowType.WindowContextHelpButtonHint, False)
        self.setMinimumSize(820, 540)
        self.resize(980, 660)
        self._files = files

        layout = QVBoxLayout(self)
        layout.setContentsMargins(16, 16, 16, 12)
        layout.setSpacing(10)
        heading = QLabel(_tr("This is exactly what the report will share"), self)
        heading.setProperty("wizardPageTitle", True)
        layout.addWidget(heading)
        intro = QLabel(
            _tr("Your Windows user name, the PC name, e-mail addresses, Nexus keys, "
                "tokens and cookies were replaced. Mods, downloads, saves and crash "
                "dumps are not included. Read the files before you share them."),
            self,
        )
        intro.setProperty("wizardPageText", True)
        intro.setWordWrap(True)
        layout.addWidget(intro)

        body = QHBoxLayout()
        body.setSpacing(12)
        self._list = QListWidget(self)
        self._list.setObjectName("mo2RevampedReportFiles")
        self._list.setAccessibleName(_tr("Files in the report"))
        self._list.setFixedWidth(240)
        for name, text in files:
            item = QListWidgetItem(name, self._list)
            item.setToolTip(_tr("{lines} lines").format(lines=text.count("\n")))
        body.addWidget(self._list)
        self._text = QPlainTextEdit(self)
        self._text.setObjectName("mo2RevampedReportText")
        self._text.setReadOnly(True)
        self._text.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)
        self._text.setFont(QFontDatabase.systemFont(QFontDatabase.SystemFont.FixedFont))
        self._text.setAccessibleName(_tr("Contents of the selected file"))
        body.addWidget(self._text, 1)
        layout.addLayout(body, 1)
        self._list.currentRowChanged.connect(self._show_file)
        # screen readers select a file without moving the current row
        self._list.itemSelectionChanged.connect(
            lambda: self._show_file(self._list.row(self._list.selectedItems()[0]))
            if self._list.selectedItems() else None
        )
        self._list.setCurrentRow(0)

        buttons = QDialogButtonBox(self)
        save_button = buttons.addButton(_tr("Save ZIP…"), QDialogButtonBox.ButtonRole.AcceptRole)
        save_button.setObjectName("mo2RevampedReportSaveZipButton")
        save_button.setToolTip(_tr("Save every file of the list in a ZIP to attach to your report."))
        self._copy_button = buttons.addButton(
            _tr("Copy summary"), QDialogButtonBox.ButtonRole.ActionRole
        )
        self._copy_button.setObjectName("mo2RevampedReportCopyButton")
        self._copy_button.setToolTip(
            _tr("Copy report.txt, to paste it in an issue or a post.")
        )
        close_button = buttons.addButton(_tr("Close"), QDialogButtonBox.ButtonRole.RejectRole)
        close_button.setObjectName("mo2RevampedReportCloseButton")
        save_button.setDefault(True)
        save_button.clicked.connect(lambda _checked=False: save(self) and self.accept())
        self._copy_button.clicked.connect(self._copy_summary)
        close_button.clicked.connect(self.reject)
        layout.addWidget(buttons)

    def _show_file(self, row: int) -> None:
        if 0 <= row < len(self._files):
            self._text.setPlainText(self._files[row][1])

    def _copy_summary(self) -> None:
        QApplication.clipboard().setText(self._files[0][1])
        idle = _tr("Copy summary")
        self._copy_button.setText(_tr("Copied"))

        def restore() -> None:
            try:
                self._copy_button.setText(idle)
            except RuntimeError:
                pass  # the preview was closed meanwhile

        QtCore.QTimer.singleShot(2000, restore)


def createPlugin() -> Mo2RevampedInfo:
    return Mo2RevampedInfo()
