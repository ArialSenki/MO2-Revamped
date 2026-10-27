"""Per-profile experimental startup and performance options for Elden Ring in MO2."""

from __future__ import annotations

import configparser
import os
from pathlib import Path
import tempfile

import mobase
from PyQt6.QtCore import QCoreApplication, QRectF, QSize, Qt
from PyQt6.QtGui import QColor, QFont, QFontMetrics, QIcon, QPainter, QPalette, QPixmap
from PyQt6.QtWidgets import (
    QApplication,
    QCheckBox,
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QFrame,
    QHBoxLayout,
    QLabel,
    QListWidget,
    QListWidgetItem,
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
_TR_CONTEXT = "EldenRingStartupOptions"
# the managed game's name, for texts that name Elden Ring; set when the tool opens
_game_name = "Elden Ring"


def _tr(text: str) -> str:
    text = QCoreApplication.translate(_TR_CONTEXT, text)
    if _game_name != "Elden Ring":
        text = text.replace("Elden Ring", _game_name).replace("eldenring.exe", "nightreign.exe")
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


def _make_switch(box: QCheckBox, title: str, description: str) -> None:
    box.setText("")
    box.setAccessibleName(title)
    box.setAccessibleDescription(description)
    box.setProperty("fluentSwitch", True)
    box.setCursor(Qt.CursorShape.PointingHandCursor)
    box.style().unpolish(box)
    box.style().polish(box)


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


class EldenRingMo2StartupTool(mobase.IPluginTool):
    ConfigFilename = "eldenring_mo2_startup.ini"
    Cpu0DelayChoices = (15, 30, 60)
    BridgeLogHistoryChoices = (1, 3, 5)
    ProcessPriorityChoices = (0, 1)

    def __init__(self):
        super().__init__()
        self.__organizer = None
        self.__parent_widget = None

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self.__organizer = organizer
        return True

    def name(self) -> str:
        return "Elden Ring MO2 Startup Options"

    def localizedName(self) -> str:
        return self.name()

    def author(self) -> str:
        return "ArialSenki"

    def description(self) -> str:
        return _tr("Per-profile Elden Ring startup, cleanup, and performance options.")

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(0, 5, 0, 64)

    def requirements(self):
        return [mobase.PluginRequirementFactory.gameDependency({"ELDEN RING", "ELDEN RING NIGHTREIGN"})]

    def settings(self) -> list[mobase.PluginSetting]:
        return []

    def enabledByDefault(self) -> bool:
        return True

    def displayName(self) -> str:
        return _tr("Startup Options")

    def tooltip(self) -> str:
        return _tr("Startup, cleanup, and performance options for the active Elden Ring profile.")

    def icon(self) -> QIcon:
        return QIcon()

    def setParentWidget(self, widget) -> None:
        self.__parent_widget = widget

    def display(self) -> None:
        try:
            game = self.__organizer.managedGame()
            if game is None or game.gameShortName().casefold() not in {
                "eldenring",
                "nightreign",
            }:
                return
            global _game_name
            _game_name = (
                "Nightreign"
                if game.gameShortName().casefold() == "nightreign"
                else "Elden Ring"
            )
            nightreign = game.gameShortName().casefold() == "nightreign"
        except Exception:
            return

        profile = self.__organizer.profile()
        if profile is None:
            QMessageBox.warning(
                self.__parent_widget,
                _tr("No active Elden Ring profile"),
                _tr("Select an Elden Ring profile in MO2 before changing startup options."),
            )
            return

        profile_path = Path(profile.absolutePath())
        config_path = profile_path / self.ConfigFilename
        config = configparser.ConfigParser()
        # the file also holds the native DLL profile: if it cannot be read,
        # nothing is written back, so those sections are never lost
        read_error = None
        try:
            with config_path.open("r", encoding="utf-8-sig") as stream:
                config.read_file(stream)
        except FileNotFoundError:
            pass
        except (OSError, UnicodeError, configparser.Error) as error:
            read_error = error
            config = configparser.ConfigParser()
        try:
            start_minimized = config.getboolean(
                "Startup", "start_minimized", fallback=None
            )
            if start_minimized is None:
                start_minimized = config.getboolean(
                    "Startup", "prevent_focus_steal", fallback=None
                )
            if start_minimized is None:
                # Preserve the earlier background-start profile choice.
                start_minimized = config.getboolean(
                    "Startup", "start_in_background", fallback=False
                )
            black_startup_background = config.getboolean(
                "Startup", "black_startup_background", fallback=False
            )
            skip_intro_logos = config.getboolean(
                "Startup", "skip_intro_logos", fallback=False
            )
            exclude_cpu0 = config.getboolean(
                "Performance", "exclude_cpu0_after_start", fallback=False
            )
            process_priority = config.getint(
                "Performance", "process_priority", fallback=0
            )
            if process_priority not in self.ProcessPriorityChoices:
                process_priority = 0
            bridge_log_history = config.getint(
                "Diagnostics", "previous_bridge_sessions", fallback=1
            )
            if bridge_log_history not in self.BridgeLogHistoryChoices:
                bridge_log_history = 1
            clear_overwrite_after_game = config.getboolean(
                "Cleanup", "clear_overwrite_after_game", fallback=False
            )
            clear_overwrite_logs_before_game = config.getboolean(
                "Cleanup", "clear_overwrite_logs_before_game", fallback=False
            )
            if clear_overwrite_after_game:
                clear_overwrite_logs_before_game = False
            cpu0_delay = config.getint(
                "Performance", "cpu0_delay_seconds", fallback=30
            )
            if cpu0_delay not in self.Cpu0DelayChoices:
                cpu0_delay = 30
        except (configparser.Error, ValueError) as error:
            # a bad value: defaults are shown, and saving rewrites only this
            # dialog's own keys
            QMessageBox.warning(
                self.__parent_widget,
                _tr("Could not read profile options"),
                _tr("The profile settings file could not be read. Defaults will be shown.\n\n{error}").format(error=error),
            )
            start_minimized = False
            black_startup_background = False
            skip_intro_logos = False
            exclude_cpu0 = False
            process_priority = 0
            bridge_log_history = 1
            cpu0_delay = 30
            clear_overwrite_after_game = False
            clear_overwrite_logs_before_game = False

        if read_error is not None:
            QMessageBox.warning(
                self.__parent_widget,
                _tr("Could not read profile options"),
                _tr("The profile settings file could not be read. Defaults will be "
                    "shown, and changes cannot be saved until the file is fixed or "
                    "removed:\n{path}\n\n{error}").format(path=config_path, error=read_error),
            )

        # Laid out like MO2's Settings window and the Saves dialog: the pages
        # on the left, cards of setting rows on the right, the themes' global
        # Fluent rules (fluentNav, fluentCard, fluentSettingRow, fluentSwitch)
        dialog = QDialog(self.__parent_widget)
        dialog.setObjectName("EldenRingStartupOptionsDialog")
        dialog.setWindowTitle(_tr("Elden Ring startup options"))
        dialog.setMinimumSize(860, 560)
        dialog.resize(980, 640)
        dialog.setSizeGripEnabled(True)
        # no right margin: the pages scroll at the window's edge, as in
        # Windows 11, and keep their gap from it inside the scroll area
        root = QVBoxLayout(dialog)
        root.setContentsMargins(16, 16, 0, 12)
        root.setSpacing(12)

        body = QHBoxLayout()
        body.setSpacing(16)
        side = QVBoxLayout()
        side.setSpacing(8)
        profile_card = QFrame(dialog)
        profile_card.setProperty("fluentCard", True)
        profile_card.setFixedWidth(232)
        profile_card_layout = QVBoxLayout(profile_card)
        profile_card_layout.setContentsMargins(14, 12, 14, 12)
        profile_card_layout.setSpacing(2)
        profile_name = QLabel(profile.name(), profile_card)
        profile_name.setObjectName("instanceDetailName")
        profile_name.setWordWrap(True)
        profile_caption = QLabel(_tr("Options for this MO2 profile"), profile_card)
        profile_caption.setObjectName("instanceDetailCaption")
        profile_caption.setWordWrap(True)
        profile_card_layout.addWidget(profile_name)
        profile_card_layout.addWidget(profile_caption)
        side.addWidget(profile_card)
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
        # the themes' generic page title and text, as in the New instance wizard
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
        root.addLayout(body, 1)

        page_texts: list[tuple[str, str]] = []

        def add_page(glyph: int, name: str, subtitle: str) -> QVBoxLayout:
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
            page_texts.append((name, subtitle))
            return content_layout

        def add_card(page: QVBoxLayout, title: str) -> QFrame:
            card = QFrame()
            card.setProperty("fluentCard", True)
            card_layout = QVBoxLayout(card)
            card_layout.setContentsMargins(0, 10, 0, 0)
            card_layout.setSpacing(0)
            heading = QLabel(title, card)
            heading.setObjectName("fluentCardTitle")
            heading.setContentsMargins(18, 0, 18, 8)
            card_layout.addWidget(heading)
            page.addWidget(card)
            return card

        def add_row(card: QFrame, glyph: int, title: str, description: str = "",
                    widgets: tuple = ()) -> QFrame:
            row = QFrame(card)
            row.setObjectName("fluentSettingRow")
            row.setMinimumHeight(62)
            row_layout = QHBoxLayout(row)
            row_layout.setContentsMargins(18, 8, 12, 8)
            row_layout.setSpacing(14)
            row_layout.addWidget(_glyph_label(glyph, row))
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
                texts.addWidget(description_label)
            row_layout.addLayout(texts, 1)
            for widget in widgets:
                widget.setParent(row)
                if isinstance(widget, QCheckBox):
                    _make_switch(widget, title, description)
                    row_layout.addWidget(
                        widget, 0, Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignRight
                    )
                else:
                    if isinstance(widget, QComboBox):
                        widget.setMinimumWidth(200)
                    row_layout.addWidget(widget, 0, Qt.AlignmentFlag.AlignVCenter)
                widget.show()
            card.layout().addWidget(row)
            return row

        def icon_button(glyph: int, tip: str) -> QToolButton:
            button = QToolButton()
            button.setObjectName("instanceIconButton")
            button.setIcon(_glyph_icon(glyph))
            button.setAutoRaise(True)
            button.setToolTip(tip)
            button.setAccessibleName(tip)
            button.setCursor(Qt.CursorShape.PointingHandCursor)
            return button

        # --- Launch ----------------------------------------------------------
        launch_page = add_page(
            0xE768, _tr("Launch"),
            _tr("How Elden Ring appears when MO2 starts it. Character save files are "
                "not changed."),
        )
        window_card = add_card(launch_page, _tr("Window"))
        minimized_box = QCheckBox()
        minimized_box.setChecked(start_minimized)
        add_row(
            window_card, 0xE921, _tr("Start Elden Ring minimized"),
            _tr("Defers the first window display and starts Elden Ring minimized. "
                "Use Alt+Tab, Win+Tab, or the game's main taskbar icon to restore it."),
            (minimized_box,),
        )
        black_background_box = QCheckBox()
        black_background_box.setChecked(black_startup_background)
        add_row(
            window_card, 0xE7B3, _tr("Use a black startup background"),
            _tr("Sets the Windows window-class background to black before the first "
                "visible display. This covers a blank client area; it cannot cover "
                "game-rendered frames, videos, or graphics-mode transitions."),
            (black_background_box,),
        )
        add_row(
            window_card, 0xE946, _tr("Good to know"),
            _tr("Window behavior depends on Windows and the game's display mode. "
                "The taskbar thumbnail may leave the game behind other windows."),
        )
        intro_card = add_card(launch_page, _tr("Intro"))
        skip_logos_box = QCheckBox()
        skip_logos_box.setChecked(skip_intro_logos and not nightreign)
        skip_logos_row = add_row(
            intro_card, 0xE893, _tr("Skip the intro logo screens"),
            _tr("Goes straight to the title screen. If the game version is not "
                "recognized, the logos play normally. To avoid the online connection "
                "messages, set Launch Setting to Play Offline in the game's "
                "System > Network options."),
            (skip_logos_box,),
        )
        if nightreign:
            # the patch matches Elden Ring's game code only
            skip_logos_box.setEnabled(False)
            skip_logos_row.setToolTip(_tr("Not available for Nightreign yet."))
        launch_page.addStretch(1)

        # --- Performance ---------------------------------------------------
        performance_page = add_page(
            0xEC4A, _tr("Performance"),
            _tr("Only affects Elden Ring launched through MO2; resets when the game closes."),
        )
        processor_card = add_card(performance_page, _tr("Processor"))
        cpu0_box = QCheckBox()
        cpu0_box.setChecked(exclude_cpu0)
        add_row(
            processor_card, 0xE950, _tr("Exclude logical CPU 0 after startup"),
            _tr("After the selected delay from bridge loading, excludes logical "
                "processor 0 from Elden Ring's current process affinity. This can "
                "help or hurt performance depending on the CPU and system; compare "
                "with the option off if you notice stutter."),
            (cpu0_box,),
        )
        delay_combo = QComboBox()
        for seconds in self.Cpu0DelayChoices:
            delay_combo.addItem(_tr("{count} seconds").format(count=seconds), seconds)
        delay_index = delay_combo.findData(cpu0_delay)
        delay_combo.setCurrentIndex(delay_index if delay_index >= 0 else 1)
        delay_combo.setEnabled(exclude_cpu0)
        cpu0_box.toggled.connect(delay_combo.setEnabled)
        add_row(
            processor_card, 0xE916, _tr("Delay after bridge loads"),
            _tr("How long the game runs on every processor first"),
            (delay_combo,),
        )
        logical_processors = os.cpu_count()
        task_manager_button = QPushButton(_tr("Open Task Manager"))
        task_manager_button.setObjectName("openTaskManager")
        task_manager_button.setAutoDefault(False)
        task_manager_button.clicked.connect(
            lambda _checked=False: self._open_task_manager()
        )
        add_row(
            processor_card, 0xE9D9, _tr("Processors"),
            _tr("The affinity change applies only to Elden Ring and resets when "
                "the game closes.")
            if logical_processors is None
            else _tr("Windows reports {count} logical processors. "
                     "The affinity change applies only to Elden Ring and resets "
                     "when the game closes.").format(count=logical_processors),
            (task_manager_button,),
        )
        priority_card = add_card(performance_page, _tr("Priority"))
        priority_combo = QComboBox()
        priority_combo.addItem(_tr("System default"), 0)
        priority_combo.addItem(_tr("Above normal"), 1)
        priority_index = priority_combo.findData(process_priority)
        priority_combo.setCurrentIndex(priority_index if priority_index >= 0 else 0)
        add_row(
            priority_card, 0xE7C1, _tr("Process priority"),
            _tr("Above normal may not improve performance and can make other apps "
                "less responsive while Elden Ring is running."),
            (priority_combo,),
        )
        performance_page.addStretch(1)

        # --- Overwrite -----------------------------------------------------
        cleanup_page = add_page(
            0xE74D, _tr("Overwrite cleanup"),
            _tr("Only one cleanup option can be active at a time. Both are off by default."),
        )
        cleanup_card = add_card(cleanup_page, _tr("Cleanup"))
        clear_overwrite_logs_box = QCheckBox()
        clear_overwrite_logs_box.setObjectName("clearOverwriteLogsBeforeLaunch")
        clear_overwrite_logs_box.setChecked(clear_overwrite_logs_before_game)
        add_row(
            cleanup_card, 0xE8A5, _tr("Clear previous mod logs before launch"),
            _tr("At launch, removes existing .log files from Overwrite and its "
                "subfolders. Other files are kept, and logs created during the "
                "new session remain available."),
            (clear_overwrite_logs_box,),
        )
        clear_overwrite_box = QCheckBox()
        clear_overwrite_box.setObjectName("clearOverwriteAfterGame")
        clear_overwrite_box.setChecked(clear_overwrite_after_game)
        add_row(
            cleanup_card, 0xE74D, _tr("Delete all Overwrite contents after the game closes"),
            _tr("After eldenring.exe exits, removes all files and subfolders in "
                "Overwrite, including new logs and output from other mods or tools. "
                "The Overwrite folder itself remains. Enable only if you do not "
                "need any of its contents."),
            (clear_overwrite_box,),
        )

        def update_overwrite_cleanup_controls() -> None:
            if clear_overwrite_box.isChecked():
                clear_overwrite_logs_box.setChecked(False)
                clear_overwrite_logs_box.setEnabled(False)
            elif clear_overwrite_logs_box.isChecked():
                clear_overwrite_box.setEnabled(False)
            else:
                clear_overwrite_box.setEnabled(True)
                clear_overwrite_logs_box.setEnabled(True)

        clear_overwrite_box.toggled.connect(update_overwrite_cleanup_controls)
        clear_overwrite_logs_box.toggled.connect(update_overwrite_cleanup_controls)
        update_overwrite_cleanup_controls()
        cleanup_page.addStretch(1)

        # --- Logs ----------------------------------------------------------
        logs_page = add_page(
            0xE9F9, _tr("Bridge logs"),
            _tr("Choose how many previous bridge sessions to rotate. Older copies "
                "stay in the log folder; the current log is recreated when the "
                "bridge loads."),
        )
        logs_card = add_card(logs_page, _tr("Bridge logs"))
        bridge_history_combo = QComboBox()
        for sessions in self.BridgeLogHistoryChoices:
            bridge_history_combo.addItem(
                _tr("{count} sessions").format(count=sessions)
                if sessions > 1
                else _tr("1 session"),
                sessions,
            )
        history_index = bridge_history_combo.findData(bridge_log_history)
        bridge_history_combo.setCurrentIndex(history_index if history_index >= 0 else 0)
        add_row(
            logs_card, 0xE81C, _tr("Previous sessions to keep"),
            _tr("Copies of earlier launches kept next to the current log"),
            (bridge_history_combo,),
        )
        log_button = icon_button(0xE8A7, _tr("Open last bridge log"))
        log_button.setObjectName("openCurrentBridgeLog")
        log_button.clicked.connect(lambda _checked=False: self._open_latest_bridge_log())
        add_row(
            logs_card, 0xE8A5, _tr("Last bridge log"),
            _tr("Written while the game ran last"), (log_button,),
        )
        previous_log_button = icon_button(0xE8A7, _tr("Open previous bridge log"))
        previous_log_button.setObjectName("openPreviousBridgeLog")
        previous_log_button.clicked.connect(
            lambda _checked=False: self._open_previous_bridge_log()
        )
        add_row(
            logs_card, 0xE81C, _tr("Previous bridge log"),
            _tr("The launch before that one"), (previous_log_button,),
        )
        log_folder_button = icon_button(0xE838, _tr("Open log folder"))
        log_folder_button.setObjectName("openBridgeLogFolder")
        log_folder_button.clicked.connect(
            lambda _checked=False: self._open_bridge_log_folder()
        )
        add_row(
            logs_card, 0xE8B7, _tr("Log folder"),
            str(Path(tempfile.gettempdir())), (log_folder_button,),
        )
        logs_page.addStretch(1)

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

        # defaults on the left, Save and Cancel on the right
        footer = QHBoxLayout()
        footer.setSpacing(8)
        footer.setContentsMargins(0, 0, 16, 0)
        defaults_button = QPushButton(_glyph_icon(0xE777), _tr("Restore defaults"))
        defaults_button.setObjectName("restoreStartupDefaults")
        defaults_button.setAutoDefault(False)
        defaults_button.setToolTip(_tr("Turns every option off again; nothing changes until you save"))

        def restore_defaults() -> None:
            for box in (minimized_box, black_background_box, skip_logos_box, cpu0_box,
                        clear_overwrite_logs_box, clear_overwrite_box):
                box.setChecked(False)
            delay_combo.setCurrentIndex(max(0, delay_combo.findData(30)))
            priority_combo.setCurrentIndex(0)
            bridge_history_combo.setCurrentIndex(0)

        defaults_button.clicked.connect(lambda _checked=False: restore_defaults())
        footer.addWidget(defaults_button)
        footer.addStretch(1)
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Save
            | QDialogButtonBox.StandardButton.Cancel
        )
        buttons.setObjectName("dialogButtons")
        buttons.accepted.connect(dialog.accept)
        buttons.rejected.connect(dialog.reject)
        footer.addWidget(buttons)
        root.addLayout(footer)

        accepted = dialog.exec() == QDialog.DialogCode.Accepted
        # a child of MO2's window: freed once this call returns to MO2's event
        # loop (the choices below are read before that), not kept per opening
        dialog.deleteLater()
        if not accepted:
            return
        if read_error is not None:
            QMessageBox.warning(
                self.__parent_widget,
                _tr("Could not save profile options"),
                _tr("The profile settings file could not be read, so it was left "
                    "unchanged:\n{path}").format(path=config_path),
            )
            return

        if not config.has_section("Startup"):
            config.add_section("Startup")
        config["Startup"].update({
            "start_minimized": "1" if minimized_box.isChecked() else "0",
            "black_startup_background": (
                "1" if black_background_box.isChecked() else "0"
            ),
            "skip_intro_logos": "1" if skip_logos_box.isChecked() else "0",
        })
        config["Startup"].pop("prevent_focus_steal", None)
        config["Startup"].pop("start_in_background", None)
        if not config.has_section("Performance"):
            config.add_section("Performance")
        config["Performance"].update({
            "exclude_cpu0_after_start": "1" if cpu0_box.isChecked() else "0",
            "cpu0_delay_seconds": str(delay_combo.currentData()),
            "process_priority": str(priority_combo.currentData()),
        })
        if not config.has_section("Cleanup"):
            config.add_section("Cleanup")
        config["Cleanup"].update({
            "clear_overwrite_logs_before_game": (
                "1" if clear_overwrite_logs_box.isChecked() else "0"
            ),
            "clear_overwrite_after_game": (
                "1" if clear_overwrite_box.isChecked() else "0"
            ),
        })
        if not config.has_section("Diagnostics"):
            config.add_section("Diagnostics")
        config["Diagnostics"]["previous_bridge_sessions"] = str(
            bridge_history_combo.currentData()
        )
        if config.has_section("Installation"):
            config["Installation"].pop("skip_txt_files", None)
            config["Installation"].pop("skip_md_files", None)
            if not config["Installation"]:
                config.remove_section("Installation")
        temporary_path = config_path.with_name(f".{config_path.name}.saving")
        try:
            profile_path.mkdir(parents=True, exist_ok=True)
            with temporary_path.open("w", encoding="utf-8", newline="\n") as stream:
                config.write(stream)
            os.replace(temporary_path, config_path)
        except OSError as error:
            # a half-written copy is not left in the profile folder
            try:
                temporary_path.unlink(missing_ok=True)
            except OSError:
                pass
            QMessageBox.critical(
                self.__parent_widget,
                _tr("Could not save profile options"),
                _tr("MO2 could not save the settings for this profile.\n\n{error}").format(error=error),
            )
            return

        QMessageBox.information(
            self.__parent_widget,
            _tr("Elden Ring startup options saved"),
            _tr("The options were saved for profile '{profile}'.").format(profile=profile.name()),
        )

    def _open_latest_bridge_log(self) -> None:
        log_path = Path(tempfile.gettempdir()) / "EldenRingMO2Bridge.log"
        if not log_path.is_file():
            QMessageBox.information(
                self.__parent_widget,
                _tr("Bridge log not found"),
                _tr("Start Elden Ring through MO2 once; the bridge creates its log when it loads."),
            )
            return
        try:
            os.startfile(str(log_path))
        except OSError as error:
            QMessageBox.warning(
                self.__parent_widget,
                _tr("Could not open bridge log"),
                _tr("MO2 could not open the log file.\n\n{error}").format(error=error),
            )

    def _open_previous_bridge_log(self) -> None:
        log_path = Path(tempfile.gettempdir()) / "EldenRingMO2Bridge.previous.log"
        if not log_path.is_file():
            QMessageBox.information(
                self.__parent_widget,
                _tr("Previous bridge log not found"),
                _tr("MO2 creates this copy before the next Elden Ring launch, "
                "when a current bridge log is available."),
            )
            return
        try:
            os.startfile(str(log_path))
        except OSError as error:
            QMessageBox.warning(
                self.__parent_widget,
                _tr("Could not open previous bridge log"),
                _tr("MO2 could not open the log file.\n\n{error}").format(error=error),
            )

    def _open_bridge_log_folder(self) -> None:
        log_directory = Path(tempfile.gettempdir())
        try:
            os.startfile(str(log_directory))
        except OSError as error:
            QMessageBox.warning(
                self.__parent_widget,
                _tr("Could not open bridge log folder"),
                _tr("MO2 could not open the log folder.\n\n{error}").format(error=error),
            )

    def _open_task_manager(self) -> None:
        system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
        task_manager = system_root / "System32" / "Taskmgr.exe"
        if not task_manager.is_file():
            QMessageBox.information(
                self.__parent_widget,
                _tr("Task Manager not found"),
                _tr("Windows Task Manager could not be found on this system."),
            )
            return
        try:
            os.startfile(str(task_manager))
        except OSError as error:
            QMessageBox.warning(
                self.__parent_widget,
                _tr("Could not open Task Manager"),
                _tr("MO2 could not open Task Manager.\n\n{error}").format(error=error),
            )


def createPlugin():
    return EldenRingMo2StartupTool()
