"""Per-profile experimental startup and performance options for Elden Ring in MO2."""

from __future__ import annotations

import configparser
import os
from pathlib import Path
import tempfile

import mobase
from PyQt6.QtGui import QIcon
from PyQt6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QGroupBox,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QMessageBox,
    QPushButton,
    QVBoxLayout,
)


class EldenRingMo2StartupTool(mobase.IPluginTool):
    ConfigFilename = "eldenring_mo2_startup.ini"
    Cpu0DelayChoices = (15, 30, 60)

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
        return (
            "Experimental Elden Ring options. Behavior can vary by system and "
            "some options may not work in every setup."
        )

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(0, 5, 0, 56)

    def requirements(self):
        return [mobase.PluginRequirementFactory.gameDependency({"ELDEN RING"})]

    def settings(self) -> list[mobase.PluginSetting]:
        return []

    def enabledByDefault(self) -> bool:
        return True

    def displayName(self) -> str:
        return "Experimental / Elden Ring Startup Options"

    def tooltip(self) -> str:
        return (
            "Experimental Elden Ring options for the active profile. "
            "Behavior can vary and may fail in some setups."
        )

    def icon(self) -> QIcon:
        return QIcon()

    def setParentWidget(self, widget) -> None:
        self.__parent_widget = widget

    def display(self) -> None:
        try:
            game = self.__organizer.managedGame()
            if game is None or game.gameShortName().casefold() != "eldenring":
                return
        except Exception:
            return

        profile = self.__organizer.profile()
        if profile is None:
            QMessageBox.warning(
                self.__parent_widget,
                "No active Elden Ring profile",
                "Select an Elden Ring profile in MO2 before changing startup options.",
            )
            return

        profile_path = Path(profile.absolutePath())
        config_path = profile_path / self.ConfigFilename
        config = configparser.ConfigParser()
        try:
            config.read(config_path, encoding="utf-8")
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
            exclude_cpu0 = config.getboolean(
                "Performance", "exclude_cpu0_after_start", fallback=False
            )
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
        except (OSError, UnicodeError, configparser.Error, ValueError) as error:
            QMessageBox.warning(
                self.__parent_widget,
                "Could not read profile options",
                f"The profile settings file could not be read. Defaults will be shown.\n\n{error}",
            )
            config = configparser.ConfigParser()
            start_minimized = False
            black_startup_background = False
            exclude_cpu0 = False
            cpu0_delay = 30
            clear_overwrite_after_game = False
            clear_overwrite_logs_before_game = False

        dialog = QDialog(self.__parent_widget)
        dialog.setWindowTitle("Experimental Elden Ring options")
        dialog.setMinimumWidth(720)
        dialog.setSizeGripEnabled(True)
        layout = QVBoxLayout(dialog)
        layout.setContentsMargins(20, 18, 20, 16)
        layout.setSpacing(12)

        def add_option(
            section_layout: QVBoxLayout, checkbox: QCheckBox, description: str
        ) -> None:
            section_layout.addWidget(checkbox)
            detail = QLabel(description)
            detail.setWordWrap(True)
            detail.setContentsMargins(24, 0, 4, 4)
            section_layout.addWidget(detail)

        profile_group = QGroupBox("Profile and status")
        profile_layout = QVBoxLayout(profile_group)
        profile_layout.setContentsMargins(14, 12, 14, 12)
        profile_layout.setSpacing(8)
        profile_label = QLabel(f"<b>Active MO2 profile:</b> {profile.name()}")
        profile_layout.addWidget(profile_label)
        profile_note = QLabel(
            "Settings are saved separately for each MO2 profile and loaded "
            "when Elden Ring starts. They do not change character save files."
        )
        profile_note.setWordWrap(True)
        profile_layout.addWidget(profile_note)
        experimental_note = QLabel(
            "These options are experimental; behavior can vary by system and "
            "game setup. A minimized game can be restored with Alt+Tab, "
            "Win+Tab, or its main taskbar icon. The thumbnail preview may "
            "leave it behind other windows."
        )
        experimental_note.setWordWrap(True)
        profile_layout.addWidget(experimental_note)

        options_grid = QGridLayout()
        options_grid.setContentsMargins(0, 0, 0, 0)
        options_grid.setHorizontalSpacing(12)
        options_grid.setVerticalSpacing(12)
        options_grid.setColumnStretch(0, 1)
        options_grid.setColumnStretch(1, 1)

        startup_group = QGroupBox("Startup")
        startup_layout = QVBoxLayout(startup_group)
        startup_layout.setContentsMargins(14, 12, 14, 12)
        startup_layout.setSpacing(6)
        minimized_box = QCheckBox("Start Elden Ring minimized")
        minimized_box.setChecked(start_minimized)
        add_option(
            startup_layout,
            minimized_box,
            "Starts the game minimized so it does not cover other windows. "
            "Use Alt+Tab, Win+Tab, or the game's main taskbar icon to restore it.",
        )
        black_background_box = QCheckBox("Use a black startup background")
        black_background_box.setChecked(black_startup_background)
        add_option(
            startup_layout,
            black_background_box,
            "Sets the game window background to black before it is shown. "
            "This may cover a white window flash; graphics or display-mode "
            "transitions can still appear.",
        )
        options_grid.addWidget(startup_group, 0, 0)

        cleanup_group = QGroupBox("Overwrite cleanup")
        cleanup_layout = QVBoxLayout(cleanup_group)
        cleanup_layout.setContentsMargins(14, 12, 14, 12)
        cleanup_layout.setSpacing(6)
        clear_overwrite_logs_box = QCheckBox(
            "Clear previous mod logs before launch"
        )
        clear_overwrite_logs_box.setChecked(clear_overwrite_logs_before_game)
        add_option(
            cleanup_layout,
            clear_overwrite_logs_box,
            "At launch, removes existing .log files from Overwrite and its "
            "subfolders. Other files are kept, and logs created during the "
            "new session remain available.",
        )
        clear_overwrite_box = QCheckBox(
            "Delete all Overwrite contents after the game closes"
        )
        clear_overwrite_box.setChecked(clear_overwrite_after_game)
        add_option(
            cleanup_layout,
            clear_overwrite_box,
            "After eldenring.exe exits, removes all files and subfolders in "
            "Overwrite, including new logs and output from other mods or tools. "
            "The Overwrite folder itself remains. Enable only if you do not "
            "need any of its contents.",
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
            "Only one cleanup option can be active at a time. Both are off by default."
        )
        cleanup_note.setWordWrap(True)
        cleanup_layout.addWidget(cleanup_note)
        options_grid.addWidget(cleanup_group, 0, 1)

        performance_group = QGroupBox("Performance")
        performance_layout = QVBoxLayout(performance_group)
        performance_layout.setContentsMargins(14, 12, 14, 12)
        performance_layout.setSpacing(6)
        cpu0_box = QCheckBox("Exclude logical CPU 0 after startup")
        cpu0_box.setChecked(exclude_cpu0)
        add_option(
            performance_layout,
            cpu0_box,
            "Changes the Elden Ring process affinity after the selected delay. "
            "Results depend on the processor and setup; Windows system settings "
            "are not changed.",
        )
        delay_row = QHBoxLayout()
        delay_row.addWidget(QLabel("Delay after bridge loads:"))
        delay_combo = QComboBox()
        for seconds in self.Cpu0DelayChoices:
            delay_combo.addItem(f"{seconds} seconds", seconds)
        delay_index = delay_combo.findData(cpu0_delay)
        delay_combo.setCurrentIndex(delay_index if delay_index >= 0 else 1)
        delay_combo.setEnabled(exclude_cpu0)
        cpu0_box.toggled.connect(delay_combo.setEnabled)
        delay_row.addWidget(delay_combo)
        delay_row.addStretch(1)
        performance_layout.addLayout(delay_row)
        options_grid.addWidget(performance_group, 1, 0)

        bridge_group = QGroupBox("Bridge log")
        bridge_layout = QVBoxLayout(bridge_group)
        bridge_layout.setContentsMargins(14, 12, 14, 12)
        bridge_layout.setSpacing(8)
        bridge_note = QLabel(
            "MO2 keeps the current bridge log and one previous-session copy. "
            "The current log is recreated when the bridge loads."
        )
        bridge_note.setWordWrap(True)
        bridge_layout.addWidget(bridge_note)
        bridge_buttons = QHBoxLayout()
        log_button = QPushButton("Open last bridge log")
        log_button.setMinimumHeight(34)
        log_button.clicked.connect(
            lambda _checked=False: self._open_latest_bridge_log()
        )
        previous_log_button = QPushButton("Open previous bridge log")
        previous_log_button.setMinimumHeight(34)
        previous_log_button.clicked.connect(
            lambda _checked=False: self._open_previous_bridge_log()
        )
        bridge_buttons.addWidget(log_button)
        bridge_buttons.addWidget(previous_log_button)
        bridge_layout.addLayout(bridge_buttons)
        options_grid.addWidget(bridge_group, 1, 1)

        layout.addWidget(profile_group)
        layout.addLayout(options_grid)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Save
            | QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(dialog.accept)
        buttons.rejected.connect(dialog.reject)
        layout.addWidget(buttons)

        layout.activate()
        dialog.ensurePolished()
        dialog.adjustSize()

        if dialog.exec() != QDialog.DialogCode.Accepted:
            return

        if not config.has_section("Startup"):
            config.add_section("Startup")
        config["Startup"].update({
            "start_minimized": "1" if minimized_box.isChecked() else "0",
            "black_startup_background": (
                "1" if black_background_box.isChecked() else "0"
            ),
        })
        config["Startup"].pop("prevent_focus_steal", None)
        config["Startup"].pop("start_in_background", None)
        if not config.has_section("Performance"):
            config.add_section("Performance")
        config["Performance"].update({
            "exclude_cpu0_after_start": "1" if cpu0_box.isChecked() else "0",
            "cpu0_delay_seconds": str(delay_combo.currentData()),
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
        if config.has_section("Installation"):
            config["Installation"].pop("skip_txt_files", None)
            config["Installation"].pop("skip_md_files", None)
            if not config["Installation"]:
                config.remove_section("Installation")
        try:
            profile_path.mkdir(parents=True, exist_ok=True)
            with config_path.open("w", encoding="utf-8", newline="\n") as stream:
                config.write(stream)
        except OSError as error:
            QMessageBox.critical(
                self.__parent_widget,
                "Could not save profile options",
                f"MO2 could not save the settings for this profile.\n\n{error}",
            )
            return

        QMessageBox.information(
            self.__parent_widget,
            "Elden Ring experimental options saved",
            f"The options were saved for profile '{profile.name()}'.",
        )

    def _open_latest_bridge_log(self) -> None:
        log_path = Path(tempfile.gettempdir()) / "EldenRingMO2Bridge.log"
        if not log_path.is_file():
            QMessageBox.information(
                self.__parent_widget,
                "Bridge log not found",
                "Start Elden Ring through MO2 once; the bridge creates its log when it loads.",
            )
            return
        try:
            os.startfile(str(log_path))
        except OSError as error:
            QMessageBox.warning(
                self.__parent_widget,
                "Could not open bridge log",
                f"MO2 could not open the log file.\n\n{error}",
            )

    def _open_previous_bridge_log(self) -> None:
        log_path = Path(tempfile.gettempdir()) / "EldenRingMO2Bridge.previous.log"
        if not log_path.is_file():
            QMessageBox.information(
                self.__parent_widget,
                "Previous bridge log not found",
                "MO2 creates this copy before the next Elden Ring launch, "
                "when a current bridge log is available.",
            )
            return
        try:
            os.startfile(str(log_path))
        except OSError as error:
            QMessageBox.warning(
                self.__parent_widget,
                "Could not open previous bridge log",
                f"MO2 could not open the log file.\n\n{error}",
            )


def createPlugin():
    return EldenRingMo2StartupTool()
