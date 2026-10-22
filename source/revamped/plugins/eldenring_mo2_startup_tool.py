"""Per-profile experimental startup and performance options for Elden Ring in MO2."""

from __future__ import annotations

import configparser
import os
from pathlib import Path
import tempfile

import mobase
from PyQt6.QtCore import QCoreApplication, Qt, QTimer
from PyQt6.QtGui import QIcon
from PyQt6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QFrame,
    QHBoxLayout,
    QLabel,
    QMessageBox,
    QPushButton,
    QScrollArea,
    QSizePolicy,
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


def _make_circular_icon(parent, object_name: str, icon_path: str, size: int = 26,
                        fallback: str = "•") -> QLabel:
    icon = QLabel(parent)
    icon.setObjectName(object_name)
    icon.setAlignment(Qt.AlignmentFlag.AlignCenter)
    icon.setFixedSize(size, size)
    pixmap = QIcon(icon_path).pixmap(max(12, size // 2), max(12, size // 2))
    if pixmap.isNull():
        icon.setText(fallback)
    else:
        icon.setPixmap(pixmap)
    return icon


def _add_circular_heading(layout, parent, text: str, title_object_name: str,
                          icon_path: str, icon_object_name: str,
                          size: int = 26, fallback: str = "•") -> QLabel:
    row = QHBoxLayout()
    row.setContentsMargins(0, 0, 0, 0)
    row.setSpacing(8)
    row.addWidget(
        _make_circular_icon(parent, icon_object_name, icon_path, size, fallback),
        0,
        Qt.AlignmentFlag.AlignVCenter,
    )
    heading = QLabel(text, parent)
    heading.setObjectName(title_object_name)
    row.addWidget(heading, 1, Qt.AlignmentFlag.AlignVCenter)
    layout.addLayout(row)
    return heading


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

        dialog = QDialog(self.__parent_widget)
        dialog.setObjectName("EldenRingStartupOptionsDialog")
        dialog.setWindowTitle(_tr("Elden Ring startup options"))
        screen = dialog.screen()
        available_geometry = (
            screen.availableGeometry() if screen is not None else None
        )
        available_width = (
            available_geometry.width() if available_geometry is not None else 1048
        )
        preferred_dialog_width = min(1000, max(520, available_width - 48))
        dialog.setMinimumWidth(min(760, preferred_dialog_width))
        dialog.setSizeGripEnabled(True)
        layout = QVBoxLayout(dialog)
        layout.setContentsMargins(16, 16, 16, 14)
        layout.setSpacing(12)

        def compact_note(label: QLabel) -> None:
            label.setWordWrap(True)
            label.setAlignment(
                Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop
            )
            label.setSizePolicy(
                QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Maximum
            )

        header_panel = QFrame(dialog)
        header_panel.setObjectName("startupHeaderPanel")
        header_panel.setSizePolicy(
            QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed
        )
        header_layout = QVBoxLayout(header_panel)
        header_layout.setContentsMargins(14, 10, 14, 10)
        header_layout.setSpacing(3)
        _add_circular_heading(
            header_layout,
            header_panel,
            _tr("Elden Ring startup options"),
            "startupPageTitle",
            ":/MO/gui/mainwindow/executables.svg",
            "startupHeaderIcon",
            30,
            "S",
        )
        header_subtitle = QLabel(
            _tr("Configure launch behavior for the active MO2 profile."), header_panel
        )
        header_subtitle.setObjectName("startupPageSubtitle")
        header_subtitle.setWordWrap(True)
        header_layout.addWidget(header_subtitle)
        layout.addWidget(header_panel, 0)

        def add_option(
            section_layout: QVBoxLayout, checkbox: QCheckBox, description: str
        ) -> None:
            section_layout.addWidget(checkbox)
            detail = QLabel(description)
            compact_note(detail)
            detail.setContentsMargins(24, 0, 4, 4)
            section_layout.addWidget(detail)

        profile_group = QFrame(dialog)
        profile_group.setObjectName("profileCard")
        profile_group.setSizePolicy(
            QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed
        )
        profile_layout = QVBoxLayout(profile_group)
        profile_layout.setContentsMargins(13, 10, 13, 10)
        profile_layout.setSpacing(7)
        _add_circular_heading(
            profile_layout,
            profile_group,
            _tr("Active profile"),
            "startupCardTitle",
            ":/MO/gui/mainwindow/profile.svg",
            "startupProfileIcon",
            26,
            "P",
        )
        profile_details = QHBoxLayout()
        profile_details.setContentsMargins(0, 0, 0, 0)
        profile_details.setSpacing(16)
        profile_label = QLabel(_tr("MO2 profile: {profile}").format(profile=profile.name()))
        profile_label.setObjectName("activeProfileName")
        profile_details.addWidget(profile_label)
        profile_note = QLabel(
            _tr("Options are stored per profile and apply to Elden Ring launched "
            "through MO2. Character save files are not changed.")
        )
        profile_note.setObjectName("profileNote")
        profile_note.setWordWrap(True)
        profile_details.addWidget(profile_note, 1)
        profile_layout.addLayout(profile_details)

        options_scroll = QScrollArea(dialog)
        options_scroll.setObjectName("startupOptionsScrollArea")
        options_scroll.setFrameShape(QFrame.Shape.NoFrame)
        options_scroll.setWidgetResizable(True)
        options_scroll.setHorizontalScrollBarPolicy(
            Qt.ScrollBarPolicy.ScrollBarAlwaysOff
        )
        options_content = QWidget(options_scroll)
        options_content.setObjectName("startupOptionsContent")
        options_scroll.setWidget(options_content)

        options_columns = QHBoxLayout(options_content)
        options_columns.setContentsMargins(0, 0, 0, 0)
        options_columns.setSpacing(12)
        left_options_column = QVBoxLayout()
        left_options_column.setSpacing(12)
        right_options_column = QVBoxLayout()
        right_options_column.setSpacing(12)

        startup_group = QFrame(options_content)
        startup_group.setObjectName("startupCard")
        startup_group.setSizePolicy(
            QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Maximum
        )
        startup_layout = QVBoxLayout(startup_group)
        startup_layout.setContentsMargins(13, 11, 13, 11)
        startup_layout.setSpacing(6)
        _add_circular_heading(
            startup_layout,
            startup_group,
            _tr("Launch behavior"),
            "startupCardTitle",
            ":/MO/gui/mainwindow/files/executable.svg",
            "startupLaunchIcon",
            26,
            "L",
        )
        minimized_box = QCheckBox(_tr("Start Elden Ring minimized"))
        minimized_box.setChecked(start_minimized)
        add_option(
            startup_layout,
            minimized_box,
            _tr("Defers the first window display and starts Elden Ring minimized. "
            "Use Alt+Tab, Win+Tab, or the game's main taskbar icon to restore it."),
        )
        options_note = QLabel(
            _tr("Window behavior depends on Windows and the game's display mode. "
            "The taskbar thumbnail may leave the game behind other windows.")
        )
        options_note.setObjectName("startupCompatibilityNote")
        compact_note(options_note)
        options_note.setContentsMargins(24, 4, 4, 4)
        startup_layout.addWidget(options_note)
        black_background_box = QCheckBox(_tr("Use a black startup background"))
        black_background_box.setChecked(black_startup_background)
        add_option(
            startup_layout,
            black_background_box,
            _tr("Sets the Windows window-class background to black before the first "
            "visible display. This covers a blank client area; it cannot cover "
            "game-rendered frames, videos, or graphics-mode transitions."),
        )
        skip_logos_box = QCheckBox(_tr("Skip the intro logo screens"))
        skip_logos_box.setChecked(skip_intro_logos and not nightreign)
        if nightreign:
            # the patch matches Elden Ring's game code only
            skip_logos_box.setEnabled(False)
            skip_logos_box.setToolTip(_tr("Not available for Nightreign yet."))
        add_option(
            startup_layout,
            skip_logos_box,
            _tr("Goes straight to the title screen. If the game version is not "
            "recognized, the logos play normally. To avoid the online connection "
            "messages, set Launch Setting to Play Offline in the game's "
            "System > Network options."),
        )
        left_options_column.addWidget(startup_group)

        cleanup_group = QFrame(options_content)
        cleanup_group.setObjectName("cleanupCard")
        cleanup_group.setSizePolicy(
            QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Maximum
        )
        cleanup_layout = QVBoxLayout(cleanup_group)
        cleanup_layout.setContentsMargins(13, 11, 13, 11)
        cleanup_layout.setSpacing(6)
        _add_circular_heading(
            cleanup_layout,
            cleanup_group,
            _tr("Overwrite cleanup"),
            "startupCardTitle",
            ":/MO/gui/mainwindow/status/cleanup.svg",
            "startupCleanupIcon",
            26,
            "C",
        )
        clear_overwrite_logs_box = QCheckBox(
            _tr("Clear previous mod logs before launch")
        )
        clear_overwrite_logs_box.setObjectName("clearOverwriteLogsBeforeLaunch")
        clear_overwrite_logs_box.setChecked(clear_overwrite_logs_before_game)
        add_option(
            cleanup_layout,
            clear_overwrite_logs_box,
            _tr("At launch, removes existing .log files from Overwrite and its "
            "subfolders. Other files are kept, and logs created during the "
            "new session remain available."),
        )
        clear_overwrite_box = QCheckBox(
            _tr("Delete all Overwrite contents after the game closes")
        )
        clear_overwrite_box.setObjectName("clearOverwriteAfterGame")
        clear_overwrite_box.setChecked(clear_overwrite_after_game)
        add_option(
            cleanup_layout,
            clear_overwrite_box,
            _tr("After eldenring.exe exits, removes all files and subfolders in "
            "Overwrite, including new logs and output from other mods or tools. "
            "The Overwrite folder itself remains. Enable only if you do not "
            "need any of its contents."),
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
        cleanup_note = QLabel(
            _tr("Only one cleanup option can be active at a time. Both are off by default.")
        )
        cleanup_note.setObjectName("cleanupNote")
        compact_note(cleanup_note)
        cleanup_layout.addWidget(cleanup_note)
        right_options_column.addWidget(cleanup_group)

        performance_group = QFrame(options_content)
        performance_group.setObjectName("performanceCard")
        performance_group.setSizePolicy(
            QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Maximum
        )
        performance_layout = QVBoxLayout(performance_group)
        performance_layout.setContentsMargins(13, 11, 13, 11)
        performance_layout.setSpacing(6)
        _add_circular_heading(
            performance_layout,
            performance_group,
            _tr("Performance"),
            "startupCardTitle",
            ":/MO/gui/mainwindow/settings.svg",
            "startupPerformanceIcon",
            26,
            "P",
        )
        cpu0_box = QCheckBox(_tr("Exclude logical CPU 0 after startup"))
        cpu0_box.setChecked(exclude_cpu0)
        add_option(
            performance_layout,
            cpu0_box,
            _tr("After the selected delay from bridge loading, excludes logical "
            "processor 0 from Elden Ring's current process affinity. This can "
            "help or hurt performance depending on the CPU and system; compare "
            "with the option off if you notice stutter."),
        )
        delay_row = QHBoxLayout()
        delay_row.addWidget(QLabel(_tr("Delay after bridge loads:")))
        delay_combo = QComboBox()
        for seconds in self.Cpu0DelayChoices:
            delay_combo.addItem(_tr("{count} seconds").format(count=seconds), seconds)
        delay_index = delay_combo.findData(cpu0_delay)
        delay_combo.setCurrentIndex(delay_index if delay_index >= 0 else 1)
        delay_combo.setEnabled(exclude_cpu0)
        cpu0_box.toggled.connect(delay_combo.setEnabled)
        delay_row.addWidget(delay_combo)
        task_manager_button = QPushButton(_tr("Open Task Manager"))
        task_manager_button.setObjectName("openTaskManager")
        task_manager_button.setMinimumHeight(30)
        task_manager_button.clicked.connect(
            lambda _checked=False: self._open_task_manager()
        )
        delay_row.addWidget(task_manager_button)
        delay_row.addStretch(1)
        performance_layout.addLayout(delay_row)
        priority_row = QHBoxLayout()
        priority_row.addWidget(QLabel(_tr("Process priority:")))
        priority_combo = QComboBox()
        priority_combo.addItem(_tr("System default"), 0)
        priority_combo.addItem(_tr("Above normal"), 1)
        priority_index = priority_combo.findData(process_priority)
        priority_combo.setCurrentIndex(priority_index if priority_index >= 0 else 0)
        priority_combo.setToolTip(
            _tr("Only affects Elden Ring launched through MO2; resets when the game closes.")
        )
        priority_row.addWidget(priority_combo)
        priority_row.addStretch(1)
        performance_layout.addLayout(priority_row)
        priority_note = QLabel(
            _tr("Above normal may not improve performance and can make other apps "
            "less responsive while Elden Ring is running.")
        )
        compact_note(priority_note)
        priority_note.setContentsMargins(24, 0, 4, 4)
        performance_layout.addWidget(priority_note)
        logical_processors = os.cpu_count()
        if logical_processors is None:
            processor_note_text = (
                _tr("The affinity change applies only to Elden Ring and resets when "
                "the game closes.")
            )
        else:
            processor_note_text = (
                _tr("Windows reports {count} logical processors. "
                "The affinity change applies only to Elden Ring and resets "
                "when the game closes.").format(count=logical_processors)
            )
        processor_note = QLabel(processor_note_text)
        compact_note(processor_note)
        processor_note.setContentsMargins(24, 0, 4, 4)
        performance_layout.addWidget(processor_note)
        left_options_column.addWidget(performance_group)

        bridge_group = QFrame(options_content)
        bridge_group.setObjectName("bridgeLogCard")
        bridge_group.setSizePolicy(
            QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Maximum
        )
        bridge_layout = QVBoxLayout(bridge_group)
        bridge_layout.setContentsMargins(13, 11, 13, 11)
        bridge_layout.setSpacing(8)
        _add_circular_heading(
            bridge_layout,
            bridge_group,
            _tr("Bridge logs"),
            "startupCardTitle",
            ":/MO/gui/mainwindow/files/text.svg",
            "startupBridgeIcon",
            26,
            "B",
        )
        bridge_note = QLabel(
            _tr("Choose how many previous bridge sessions to rotate. Older copies "
            "stay in the log folder; the current log is recreated when the "
            "bridge loads.")
        )
        compact_note(bridge_note)
        bridge_layout.addWidget(bridge_note)
        history_row = QHBoxLayout()
        history_row.addWidget(QLabel(_tr("Previous sessions to keep:")))
        bridge_history_combo = QComboBox()
        for sessions in self.BridgeLogHistoryChoices:
            bridge_history_combo.addItem(
                _tr("{count} sessions").format(count=sessions)
                if sessions > 1
                else _tr("1 session"),
                sessions,
            )
        history_index = bridge_history_combo.findData(bridge_log_history)
        bridge_history_combo.setCurrentIndex(
            history_index if history_index >= 0 else 0
        )
        history_row.addWidget(bridge_history_combo)
        history_row.addStretch(1)
        bridge_layout.addLayout(history_row)
        bridge_buttons = QHBoxLayout()
        log_button = QPushButton(_tr("Open last bridge log"))
        log_button.setObjectName("openCurrentBridgeLog")
        log_button.setMinimumHeight(34)
        log_button.clicked.connect(
            lambda _checked=False: self._open_latest_bridge_log()
        )
        previous_log_button = QPushButton(_tr("Open previous bridge log"))
        previous_log_button.setObjectName("openPreviousBridgeLog")
        previous_log_button.setMinimumHeight(34)
        previous_log_button.clicked.connect(
            lambda _checked=False: self._open_previous_bridge_log()
        )
        bridge_buttons.addWidget(log_button)
        bridge_buttons.addWidget(previous_log_button)
        bridge_layout.addLayout(bridge_buttons)
        log_folder_button = QPushButton(_tr("Open log folder"))
        log_folder_button.setObjectName("openBridgeLogFolder")
        log_folder_button.setMinimumHeight(34)
        log_folder_button.clicked.connect(
            lambda _checked=False: self._open_bridge_log_folder()
        )
        bridge_layout.addWidget(log_folder_button)
        right_options_column.addWidget(bridge_group)

        left_options_column.addStretch(1)
        right_options_column.addStretch(1)
        options_columns.addLayout(left_options_column, 1)
        options_columns.addLayout(right_options_column, 1)

        layout.addWidget(profile_group, 0)
        layout.addWidget(options_scroll, 1)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Save
            | QDialogButtonBox.StandardButton.Cancel
        )
        buttons.setObjectName("dialogButtons")
        buttons.accepted.connect(dialog.accept)
        buttons.rejected.connect(dialog.reject)
        layout.addWidget(buttons)

        layout.activate()
        dialog.ensurePolished()
        dialog.adjustSize()
        if available_geometry is not None:
            available_height = available_geometry.height()
            max_dialog_height = max(420, available_height - 40)
            options_scroll.setMinimumHeight(
                max(160, min(500, max_dialog_height - 300))
            )
            dialog.adjustSize()
            dialog.resize(
                preferred_dialog_width,
                min(dialog.height(), max_dialog_height),
            )
        else:
            dialog.resize(preferred_dialog_width, dialog.height())

        def grow_past_short_scroll() -> None:
            # once shown, the wrapped text has its real height: grow the
            # dialog instead of leaving a short scroll, as far as the screen
            # allows
            if available_geometry is None:
                return
            viewport = options_scroll.viewport()
            needed = max(
                options_content.height(),
                options_content.minimumSizeHint().height(),
                options_content.heightForWidth(viewport.width())
                if options_content.hasHeightForWidth()
                else 0,
            )
            extra = needed - viewport.height()
            if extra <= 0:
                return
            height = min(dialog.height() + extra, available_geometry.height() - 40)
            if height <= dialog.height():
                return
            frame = dialog.frameGeometry()
            top = min(frame.top(), available_geometry.bottom() - (frame.height() - dialog.height() + height))
            dialog.resize(dialog.width(), height)
            dialog.move(frame.left(), max(available_geometry.top(), top))

        QTimer.singleShot(50, grow_past_short_scroll)
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
