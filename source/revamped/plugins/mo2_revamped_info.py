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
            self._show(_tr("You are now a developer. Settings > Developer has the beta "
                           "features; open Settings again to see it."))
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
        return "ArialSenki"

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
        for object_name, tooltip, whats_this in (
            (
                "actionNexus",
                _tr("Nexus Mods: open the website to browse mods for the active game."),
                _tr("Opens Nexus Mods in your browser. The content depends on the active game and your account."),
            ),
            (
                "actionNotifications",
                _tr("MO2 notifications: review detected errors and setup recommendations."),
                _tr("Opens MO2 notifications. The icon can be highlighted when there are problems worth reviewing."),
            ),
            (
                "actionEndorseMO",
                _tr("Support MO2 Revamped on Nexus Mods."),
                _tr("Opens the options to endorse or not endorse MO2 Revamped on its Nexus Mods page."),
            ),
            (
                "actionHelp",
                _tr("MO2 help: guides and support links. Use \"About MO2 Revamped\" to see this edition, its components and credits."),
                _tr("Opens the help options. \"About MO2 Revamped\" describes this edition, its features, components and licenses."),
            ),
        ):
            action = main_window.findChild(QAction, object_name)
            if action is not None:
                action.setToolTip(tooltip)
                action.setStatusTip(tooltip)
                action.setWhatsThis(whats_this)

        help_menu = main_window.findChild(QMenu, "menuHelp")
        if help_menu is None:
            return
        about_found = False
        diagnostics_found = False
        for action in help_menu.actions():
            if action.objectName() == "mo2RevampedAboutAction":
                self._about_action = action
                about_found = True
            elif action.objectName() == "mo2RevampedDiagnosticsAction":
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
                _tr("Saves a report with the MO2, USVFS and Elden Ring bridge logs.")
            )
            self._diagnostics_action.triggered.connect(
                lambda _checked=False: self._create_diagnostics_bundle(main_window)
            )
            help_menu.addAction(self._diagnostics_action)

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
            "plugin": read(game, r'^\s*Version = "([^"]+)"', str, "0.5.0-alpha.75"),
            "bridge": read(game, r'NativeBridgeVersion = "([^"]+)"', str, "0.5.0-alpha.36"),
            "installer": read(
                "eldenring_mo2_archive_installer/installer.py",
                r"VersionInfo\(0, 5, 0, (\d+)\)", lambda n: f"alpha.{n}", "alpha.72",
            ),
            "saves": read(
                "eldenring_mo2_save_isolation.py",
                r"VersionInfo\(0, 5, 0, (\d+)\)", lambda n: f"alpha.{n}", "alpha.77",
            ),
            "startup": read(
                "eldenring_mo2_startup_tool.py",
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
        edition = QLabel(_tr("Revamped edition  ·  ArialSenki"), program_card)
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
            row_layout.addWidget(_glyph_label(glyph, row), 0, Qt.AlignmentFlag.AlignTop)
            texts = QVBoxLayout()
            texts.setSpacing(2)
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
        add_row(features, 0xE896, _tr("Mod installation"),
                _tr("Detects Elden Ring paths and recognizes nested folders to "
                    "install mods and native add-ons."))
        add_row(features, 0xE77B, _tr("Profiles and saves"),
                _tr("Organizes profiles and, for Elden Ring, can route saves per "
                    "profile."))
        add_row(features, 0xE8B7, _tr("Independent instances"),
                _tr("Choose global or portable setups. You can also create an "
                    "isolated MO2 copy for Elden Ring."))
        add_row(features, 0xE790, _tr("Interface and diagnostics"),
                _tr("Includes Windows 11 style themes (Light and Dark) "
                    "with Fluent icons, plus tools to prepare diagnostic reports."))
        note = QLabel(
            _tr("Elden Ring-specific features are only enabled in an instance "
                "for that game."),
        )
        note.setObjectName("fluentSettingDescription")
        note.setWordWrap(True)
        note.setContentsMargins(4, 0, 4, 0)
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
        base_row = add_row(parts, 0xE737, _tr("MO2 base"),
                           _tr("Mod Organizer 2 · {mo2}\nQt and PyQt · {qt}\nPython · {python}\n"
                               "OpenSSL · {openssl}").format(**versions))
        add_row(parts, 0xE8B7, _tr("Virtual file system"),
                "USVFS · 0.5.7.2\n"
                + _tr("Deploys mods virtually without changing the game's "
                      "original files."))
        add_row(parts, 0xE7FC, _tr("Elden Ring tools"),
                _tr("Game plugin · {plugin}\nNative bridge · {bridge}\n"
                    "Archive installer · {installer}").format(**versions))
        add_row(parts, 0xE771, _tr("Profiles and interface"),
                _tr("Save isolation · {saves}\nStartup options · {startup}\n"
                    "Fluent themes · Light and Dark").format(**versions))
        developer_status = QLabel()
        developer_status.setObjectName("fluentSettingDescription")
        developer_status.setWordWrap(True)
        developer_status.setContentsMargins(4, 0, 4, 0)
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
                (link_button(0xE8A7, _tr("MO2 Revamped on GitHub"),
                             "https://github.com/ArialSenki/MO2-Revamped"),))
        add_row(people, 0xE8F1, _tr("Base project"),
                _tr("Based on Mod Organizer 2 and its contributions. Revamped is not "
                    "an official release from the MO2 team."),
                (link_button(0xE8A7, _tr("Original Mod Organizer 2 project and source code"),
                             "https://github.com/ModOrganizer2/modorganizer"),))
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
                    "kept. See the licenses folder next to the program."),
                (folder_button,))
        add_row(people, 0xE8B7, _tr("USVFS source code"), "github.com/ModOrganizer2/usvfs",
                (link_button(0xE8A7, _tr("USVFS source code"),
                             "https://github.com/ModOrganizer2/usvfs"),))
        credits.addStretch(1)

        # --- Diagnostics -----------------------------------------------------
        diagnostics = add_page(
            0xE9D9, _tr("Diagnostics"), _tr("Bug reports"),
            _tr("Collects the logs needed to help locate MO2 Revamped problems."),
        )
        report = add_card(diagnostics)
        add_row(report, 0xE8A5, _tr("Report contents"),
                _tr("Includes the main MO2 log, recent USVFS logs and the current and "
                    "previous Elden Ring bridge logs when available, along with program, "
                    "system, game and instance versions."))
        add_row(report, 0xE72E, _tr("Privacy"),
                _tr("Mods, downloads, saves, Nexus keys and memory dumps are not included. "
                    "Logs can contain Windows paths and profile names; review them "
                    "before sharing."))
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

    def _diagnostics_context(self) -> str:
        app_dir = Path(QApplication.applicationDirPath())
        game_name = "Not detected"
        profile_name = "Not selected"
        profile_path = "Not available"
        game_path = "Not available"
        game_data_path = "Not available"
        try:
            game = self._organizer.managedGame()
            if game is not None:
                game_name = self._safe_text(game.gameName())
                try:
                    game_path = self._safe_text(game.gameDirectory().absolutePath())
                except Exception:
                    pass
                try:
                    game_data_path = self._safe_text(game.dataDirectory().absolutePath())
                except Exception:
                    pass
        except Exception:
            pass
        try:
            profile = self._organizer.profile()
            if profile is not None:
                profile_name = self._safe_text(profile.name())
                profile_path = self._safe_text(profile.absolutePath())
        except Exception:
            pass

        mode = "Standard (global instance)"
        if (app_dir / "eldenring-only.portable").is_file():
            mode = "Isolated portable - Elden Ring only"
        elif (app_dir / "portable.txt").is_file():
            mode = "Portable"

        try:
            pyqt_version = QtCore.PYQT_VERSION_STR
        except Exception:
            pyqt_version = "Unavailable"
        try:
            qt_version = QtCore.qVersion()
        except Exception:
            qt_version = "Unavailable"
        try:
            import ssl
            openssl_version = ssl.OPENSSL_VERSION
        except Exception:
            openssl_version = "Unavailable"

        return "\n".join(
            (
                "MO2 Revamped diagnostic report",
                f"Created UTC: {datetime.now(timezone.utc).isoformat()}",
                "Package: MO2 Revamped 1.1.0-beta.1 (based on MO2 2.5.2)",
                "Base: Mod Organizer 2.5.2",
                "USVFS: 0.5.7.2",
                f"Windows: {platform.platform()}",
                f"Architecture: {platform.machine()}",
                f"Python: {sys.version.replace(os.linesep, ' ')}",
                f"OpenSSL: {openssl_version}",
                f"Qt: {qt_version}",
                f"PyQt: {pyqt_version}",
                f"Application directory: {app_dir}",
                f"Instance directory: {self._organizer_path('basePath')}",
                f"Mods directory: {self._organizer_path('modsPath')}",
                f"Downloads directory: {self._organizer_path('downloadsPath')}",
                f"Overwrite directory: {self._organizer_path('overwritePath')}",
                f"Instance mode: {mode}",
                f"Active game: {game_name}",
                f"Game directory: {game_path}",
                f"Game data directory: {game_data_path}",
                f"Active profile: {profile_name}",
                f"Profile directory: {profile_path}",
                "Elden Ring game plugin: 0.5.0-alpha.75",
                "Native bridge: 0.5.0-alpha.35",
                "Archive layout installer: alpha.72",
                "Save isolation: alpha.76",
                "Startup options: 0.5.0.64",
                "",
                "Review this report and the included logs before sharing; paths and profile names may identify your Windows account.",
            )
        )

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

        temp_dir = Path(tempfile.gettempdir())
        for archive_name, filename in (
            ("bridge/current.log", "EldenRingMO2Bridge.log"),
            ("bridge/previous.log", "EldenRingMO2Bridge.previous.log"),
        ):
            path = temp_dir / filename
            if path.is_file():
                candidates.append((archive_name, path))
        return candidates

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
        confirmation = QMessageBox(parent)
        confirmation.setObjectName("MO2RevampedReportConfirmDialog")
        confirmation.setOption(QMessageBox.Option.DontUseNativeDialog, True)
        confirmation.setWindowTitle(_tr("Create bug report"))
        confirmation.setIcon(QMessageBox.Icon.NoIcon)
        confirmation.setText(_tr("Review the report before sharing it"))
        confirmation.setInformativeText(
            _tr("Includes MO2, USVFS and Elden Ring bridge logs. They can contain "
                "Windows paths and profile names. The ZIP does not include mods, "
                "downloads, saves, Nexus keys or memory dumps.")
        )
        create_button = confirmation.addButton(
            _tr("Create report"), QMessageBox.ButtonRole.AcceptRole
        )
        create_button.setObjectName("mo2RevampedReportCreateButton")
        cancel_button = confirmation.addButton(
            _tr("Cancel"), QMessageBox.ButtonRole.RejectRole
        )
        cancel_button.setObjectName("mo2RevampedReportCancelButton")
        confirmation.setDefaultButton(cancel_button)
        confirmation.setEscapeButton(cancel_button)
        for label_name in ("qt_msgbox_label", "qt_msgbox_informativelabel"):
            label = confirmation.findChild(QLabel, label_name)
            if label is not None:
                label.setWordWrap(True)
        confirmation.exec()
        if confirmation.clickedButton() is not create_button:
            return

        filename = "MO2-Revamped-Diagnostics-" + datetime.now().strftime(
            "%Y%m%d-%H%M%S"
        ) + ".zip"
        initial_path = Path.home() / "Desktop" / filename
        save_dialog = QFileDialog(parent)
        save_dialog.setObjectName("MO2RevampedReportSaveDialog")
        save_dialog.setOption(QFileDialog.Option.DontUseNativeDialog, True)
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
        if save_dialog.exec() != QDialog.DialogCode.Accepted:
            return
        selected_files = save_dialog.selectedFiles()
        destination = selected_files[0] if selected_files else ""
        if not destination:
            return
        if not destination.casefold().endswith(".zip"):
            destination += ".zip"

        archive_started = False
        try:
            included = []
            collection_errors = []
            with zipfile.ZipFile(
                destination, "w", compression=zipfile.ZIP_DEFLATED
            ) as archive:
                archive_started = True
                archive.writestr("diagnostics.txt", self._diagnostics_context())
                for archive_name, source_path in self._diagnostic_log_files():
                    try:
                        limit = 2 * 1024 * 1024
                        # only the end of a large log is read: mo_interface.log
                        # can grow to hundreds of MiB in a long session
                        with source_path.open("rb") as stream:
                            metadata = os.fstat(stream.fileno())
                            original_size = metadata.st_size
                            truncated = original_size > limit
                            if truncated:
                                stream.seek(original_size - limit)
                            data = stream.read(limit)
                        if truncated:
                            data = (
                                b"[MO2 Revamped report: only the last 2 MiB are included]\n"
                                + data
                            )
                        archive.writestr(archive_name, data)
                        modified = datetime.fromtimestamp(
                            metadata.st_mtime, timezone.utc
                        ).isoformat()
                        included.append(
                            f"{archive_name}\tmodified UTC={modified}\tsize={original_size} bytes"
                            + ("\tcontent limited to last 2 MiB" if truncated else "")
                        )
                    except OSError as error:
                        collection_errors.append(
                            f"Could not read {archive_name}: {error}"
                        )
                archive.writestr(
                    "included-files.txt",
                    "\n".join(included) if included else "No application log files were available.",
                )
                if collection_errors:
                    archive.writestr(
                        "collection-errors.txt",
                        "\n".join(collection_errors),
                    )
            self._show_report_notice(
                parent,
                "MO2RevampedReportSuccessDialog",
                _tr("Report created"),
                _tr("The report was saved"),
                _tr("Location:\n{path}\n\n"
                    "Review the ZIP and remove private paths before sharing it.").format(
                    path=destination
                ),
            )
        except Exception as error:
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
            if archive_started:
                try:
                    Path(destination).unlink(missing_ok=True)
                except OSError:
                    pass
            self._show_report_notice(
                parent,
                "MO2RevampedReportErrorDialog",
                _tr("Could not create the report"),
                _tr("MO2 Revamped could not save the bug report"),
                _tr("{error}\n\nCheck the MO2 log for details.").format(error=error),
            )


def createPlugin() -> Mo2RevampedInfo:
    return Mo2RevampedInfo()
