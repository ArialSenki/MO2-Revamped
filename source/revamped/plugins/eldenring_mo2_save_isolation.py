"""Per-profile Elden Ring save routing and safe save-copy controls for MO2."""

from __future__ import annotations

import configparser
from datetime import datetime
import json
import os
from pathlib import Path
import shutil
import stat
import tempfile
import traceback
import uuid

import mobase
from PyQt6.QtCore import QEvent, QObject, QUrl, Qt, QTimer, qInfo
from PyQt6.QtGui import QDesktopServices, QIcon
from PyQt6.QtWidgets import (
    QApplication,
    QButtonGroup,
    QComboBox,
    QCheckBox,
    QDialog,
    QDialogButtonBox,
    QFrame,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMessageBox,
    QCommandLinkButton,
    QPushButton,
    QRadioButton,
    QScrollArea,
    QSizePolicy,
    QVBoxLayout,
    QWidget,
)


class _Mo2DialogShowEventFilter(QObject):
    """Qt event-filter object that forwards dialog events to the MO2 plugin."""

    def __init__(self, plugin, parent=None):
        super().__init__(parent)
        self.__plugin = plugin

    def eventFilter(self, watched, event) -> bool:
        return self.__plugin._handle_dialog_show_event(watched, event)


class EldenRingMo2SaveIsolation(mobase.IPluginTool):
    ConfigFilename = "eldenring_mo2_saves.ini"
    ProfileIsolated = "profile"
    InstanceShared = "instance_shared"
    GlobalShared = "global"
    DialogUiIntegrationSetting = "Enable Elden Ring save dialog integration"
    DialogUiIntegrationMigrationKey = "save dialog integration alpha.62 default migration"
    ProfilesDialogCardName = "eldenRingSaveIsolationCard"
    ProfilesDialogFooterName = "eldenRingSaveIsolationFooter"
    SafeTransferButtonName = "eldenRingSafeSaveTransferButton"
    ProfilesPanelDesign = "below"
    CreateInstanceCardName = "eldenRingSaveModeCard"
    PendingModesFilename = "pending_instance_modes.json"
    CreateInstanceSaveCopyCheckboxName = "eldenRingCreateInstanceSaveCopy"
    CreateInstanceSaveSourceComboName = "eldenRingCreateInstanceSaveSource"
    CreateInstanceSaveStatusName = "eldenRingCreateInstanceSaveStatus"
    CreateInstanceGameSaveProfileCheckboxName = "eldenRingCreateInstanceGameSaveProfile"
    CreateInstanceGameSaveProfileNameEditName = "eldenRingCreateInstanceGameSaveProfileName"
    CreateInstanceGameSaveProfileNoteName = "eldenRingCreateInstanceGameSaveProfileNote"
    SaveProfilesDirectoryName = "save_profiles"
    ProfileSaveProfileKey = "profile_save_profile"
    InstanceSaveProfileKey = "instance_save_profile"
    ProfilesSaveProfileComboName = "eldenRingSaveProfileSelector"
    ProfilesSaveProfileCreateButtonName = "eldenRingCreateSaveProfile"

    ModeDescriptions = {
        ProfileIsolated: "Separate saves for each MO2 profile. Recommended for modded play.",
        InstanceShared: "Share modded saves across profiles in this MO2 instance.",
        GlobalShared: "Use the Steam save in Roaming. This shares saves outside MO2.",
    }

    def __init__(self):
        super().__init__()
        self.__organizer = None
        self.__parent_widget = None
        self.__ui_timer = None
        self.__ui_event_filter = None
        self.__refreshing_dialogs = False
        self.__pending_error_reported = set()
        self.__pending_applied_paths = set()
        self.__pending_cleanup_failed_paths = set()

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self.__organizer = organizer
        # Plugin init is called while MO2 is discovering plugins, including in
        # non-Elden-Ring instances. Defer all UI hooks and settings migration
        # until the managed game is known, and leave them dormant elsewhere.
        organizer.onUserInterfaceInitialized(self._initialize_for_managed_game)
        return True

    def _initialize_for_managed_game(self, _main_window) -> None:
        if not self._active_instance_is_elden_ring():
            return

        self._migrate_dialog_integration_default()
        application = QApplication.instance()
        if application is not None:
            self.__ui_event_filter = _Mo2DialogShowEventFilter(self, application)
            application.installEventFilter(self.__ui_event_filter)
            self.__ui_timer = QTimer(application)
            self.__ui_timer.setInterval(400)
            self.__ui_timer.timeout.connect(self._refresh_mo2_dialogs)
            self.__ui_timer.start()
        qInfo("Elden Ring MO2: save isolation alpha.68 initialized.")

    def _migrate_dialog_integration_default(self) -> None:
        """Enable the redesigned Elden Ring dialogs once for existing installs.

        MO2 persists plugin settings, so changing the default alone does not
        update users who previously saved the old default-off value. The
        migration marker preserves a later user opt-out across restarts.
        """
        try:
            if self.__organizer.persistent(
                self.name(), self.DialogUiIntegrationMigrationKey, False
            ):
                return
            self.__organizer.setPluginSetting(
                self.name(), self.DialogUiIntegrationSetting, True
            )
            self.__organizer.setPersistent(
                self.name(), self.DialogUiIntegrationMigrationKey, True
            )
            qInfo(
                "Elden Ring MO2 Save Isolation: enabled the Elden Ring save "
                "dialog integration once for this existing MO2 instance."
            )
        except Exception as error:
            qInfo(
                "Elden Ring MO2 Save Isolation: could not migrate the saved "
                f"dialog integration setting: {error}"
            )

    def _handle_dialog_show_event(self, watched, event) -> bool:
        if (
            event.type() in (QEvent.Type.Show, QEvent.Type.ShowToParent)
            and isinstance(watched, QDialog)
            and not self.__refreshing_dialogs
        ):
            self._refresh_mo2_dialogs(watched)
        return False

    def name(self) -> str:
        return "Elden Ring MO2 Save Isolation"

    def localizedName(self) -> str:
        return self.name()

    def author(self) -> str:
        return "ArialSenki"

    def description(self) -> str:
        return (
            "Choose Elden Ring save routes and named game save profiles, inspect "
            "the active save path, and import or transfer .sl2 saves."
        )

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(0, 5, 0, 68)

    def requirements(self):
        return [mobase.PluginRequirementFactory.gameDependency({"ELDEN RING"})]

    def settings(self) -> list[mobase.PluginSetting]:
        return [
            mobase.PluginSetting(
                self.DialogUiIntegrationSetting,
                (
                    "Show Elden Ring save modes in MO2 2.5.2's instance and "
                    "profile dialogs. Other games keep their native controls. "
                    "Enabled by default; disable to restore MO2's native controls."
                ),
                True,
            )
        ]

    def enabledByDefault(self) -> bool:
        return True

    def displayName(self) -> str:
        return "Save Isolation"

    def tooltip(self) -> str:
        return "Select the Elden Ring save route and named game save for this profile."

    def icon(self) -> QIcon:
        return QIcon()

    def setParentWidget(self, widget) -> None:
        self.__parent_widget = widget

    def _dialog_ui_enabled(self) -> bool:
        try:
            return bool(
                self.__organizer.pluginSetting(
                    self.name(), self.DialogUiIntegrationSetting
                )
            )
        except Exception:
            return False

    @staticmethod
    def _dialog_mode_card(
        parent,
        object_name: str,
        include_apply: bool = False,
        profile_options: tuple[QCheckBox, ...] = (),
        compact: bool = False,
        title: str = "Elden Ring profile options",
    ):
        card = QWidget(parent) if compact else QGroupBox(title, parent)
        card.setObjectName(object_name)
        layout = QVBoxLayout(card)
        if compact:
            layout.setContentsMargins(0, 0, 0, 0)
            layout.setSpacing(8)
            card.setSizePolicy(
                QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Maximum
            )
            card.setMinimumWidth(380)
            card.setMaximumWidth(440)
        else:
            layout.setContentsMargins(12, 10, 12, 10)
            layout.setSpacing(7)

        mode_combo = QComboBox(card)
        mode_combo.setObjectName(f"{object_name}_mode")
        mode_combo.setMaxVisibleItems(8)
        mode_combo.view().setMinimumWidth(max(320, mode_combo.width()))
        if not compact:
            selection_row = QHBoxLayout()
            selection_row.setSpacing(8)
            selection_row.addWidget(QLabel("Save mode", card))
            mode_combo.setMinimumContentsLength(24)
            mode_combo.setSizeAdjustPolicy(
                QComboBox.SizeAdjustPolicy.AdjustToMinimumContentsLengthWithIcon
            )
            selection_row.addWidget(mode_combo, 1)
            layout.addLayout(selection_row)
        else:
            # Keep one mode model for the existing profile/save callbacks, but
            # present the choices as the same readable radio rows used by the
            # Create Instance wizard.
            mode_combo.hide()
            heading = QLabel("Elden Ring save isolation", card)
            heading.setObjectName(f"{object_name}_heading")
            heading.setStyleSheet("font-weight: 600;")
            layout.addWidget(heading)

        choices = (
            (EldenRingMo2SaveIsolation.ProfileIsolated, "Separate saves per MO2 profile"),
            (EldenRingMo2SaveIsolation.InstanceShared, "Share modded saves in this instance"),
            (EldenRingMo2SaveIsolation.GlobalShared, "Use the global Steam save"),
        )
        radio_group = None
        radio_buttons = {}
        if compact:
            radio_group = QButtonGroup(card)
            radio_group.setExclusive(True)
        for mode, title in choices:
            mode_combo.addItem(title, mode)
            if compact:
                option = QWidget(card)
                option_layout = QVBoxLayout(option)
                option_layout.setContentsMargins(0, 0, 0, 0)
                option_layout.setSpacing(2)
                radio = QRadioButton(title, option)
                radio.setProperty("eldenRingSaveMode", mode)
                radio_group.addButton(radio)
                radio_buttons[mode] = radio
                option_layout.addWidget(radio)
                description = QLabel(
                    EldenRingMo2SaveIsolation.ModeDescriptions[mode], option
                )
                description.setWordWrap(True)
                description.setContentsMargins(22, 0, 0, 0)
                option_layout.addWidget(description)
                layout.addWidget(option)

                def select_mode(checked: bool, selected_mode=mode) -> None:
                    if checked:
                        mode_combo.setCurrentIndex(mode_combo.findData(selected_mode))

                radio.toggled.connect(select_mode)

        detail = QLabel(card)
        detail.setObjectName(f"{object_name}_description")
        detail.setWordWrap(True)
        detail.setContentsMargins(0 if compact else 4, 0, 0 if compact else 4, 0)
        if compact:
            # The radio rows each show their own explanation, so this former
            # single active-mode label is intentionally hidden.
            detail.hide()
        else:
            layout.addWidget(detail)

        for checkbox in profile_options:
            old_parent = checkbox.parentWidget()
            old_layout = old_parent.layout() if old_parent is not None else None
            if old_layout is not None:
                old_layout.removeWidget(checkbox)
            checkbox.setParent(card)
            checkbox.show()
            layout.addWidget(checkbox)

        status = QLabel(card)
        status.setObjectName(f"{object_name}_status")
        status.setWordWrap(True)
        layout.addWidget(status)

        apply_button = None
        if include_apply:
            button_row = QHBoxLayout()
            button_row.addStretch(1)
            apply_button = QPushButton("Apply to selected profile", card)
            apply_button.setObjectName(f"{object_name}_apply")
            button_row.addWidget(apply_button)
            layout.addLayout(button_row)

        def update_description(index: int) -> None:
            mode = mode_combo.itemData(index)
            if not compact:
                detail.setText(
                    EldenRingMo2SaveIsolation.ModeDescriptions.get(mode, "")
                )

        mode_combo.currentIndexChanged.connect(update_description)
        update_description(mode_combo.currentIndex())
        if compact and mode_combo.currentData() in radio_buttons:
            radio_buttons[mode_combo.currentData()].setChecked(True)
        return card, mode_combo, detail, status, apply_button

    def _profiles_full_width_panel(self, parent):
        """Build the full-width save route and named game save selector."""
        card = QWidget(parent)
        card.setObjectName(self.ProfilesDialogCardName)
        card.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Maximum)
        card.setMinimumWidth(820)
        layout = QVBoxLayout(card)
        layout.setContentsMargins(0, 4, 0, 0)
        layout.setSpacing(6)

        heading = QLabel("Elden Ring save isolation", card)
        heading.setObjectName(f"{self.ProfilesDialogCardName}_heading")
        heading.setStyleSheet("font-weight: 600;")
        layout.addWidget(heading)

        mode_combo = QComboBox(card)
        mode_combo.setObjectName(f"{self.ProfilesDialogCardName}_mode")
        mode_combo.hide()
        radio_group = QButtonGroup(card)
        radio_group.setExclusive(True)
        options_row = QHBoxLayout()
        options_row.setSpacing(12)
        choices = (
            (self.ProfileIsolated, "Separate per profile"),
            (self.InstanceShared, "Share within this instance"),
            (self.GlobalShared, "Use global Steam save"),
        )
        for mode, title in choices:
            mode_combo.addItem(title, mode)
            option = QWidget(card)
            option.setObjectName("eldenRingSaveModeOptionCard")
            option.setSizePolicy(
                QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred
            )
            option_layout = QVBoxLayout(option)
            option_layout.setContentsMargins(9, 7, 9, 7)
            option_layout.setSpacing(4)
            radio = QRadioButton(title, option)
            radio.setProperty("eldenRingSaveMode", mode)
            radio_group.addButton(radio)
            option_layout.addWidget(radio)
            description = QLabel(self.ModeDescriptions[mode], option)
            description.setWordWrap(True)
            description.setContentsMargins(22, 0, 0, 0)
            option_layout.addWidget(description)
            options_row.addWidget(option, 1)
            radio.toggled.connect(
                lambda checked, selected_mode=mode: checked
                and mode_combo.setCurrentIndex(mode_combo.findData(selected_mode))
            )
        layout.addLayout(options_row)

        save_profile_heading = QLabel("Game save profile", card)
        save_profile_heading.setStyleSheet("font-weight: 600;")
        layout.addWidget(save_profile_heading)
        save_profile_row = QHBoxLayout()
        save_profile_combo = QComboBox(card)
        save_profile_combo.setObjectName(self.ProfilesSaveProfileComboName)
        save_profile_combo.setMinimumContentsLength(30)
        save_profile_combo.setSizeAdjustPolicy(
            QComboBox.SizeAdjustPolicy.AdjustToMinimumContentsLengthWithIcon
        )
        save_profile_combo.setMaxVisibleItems(10)
        create_save_profile_button = QPushButton("Create or import…", card)
        create_save_profile_button.setObjectName(
            self.ProfilesSaveProfileCreateButtonName
        )
        apply_button = QPushButton("Apply to selected profile", card)
        apply_button.setObjectName(f"{self.ProfilesDialogCardName}_apply")
        save_profile_row.addWidget(save_profile_combo, 1)
        save_profile_row.addWidget(create_save_profile_button)
        save_profile_row.addWidget(apply_button)
        layout.addLayout(save_profile_row)
        route_note = QLabel(
            "Each choice keeps its own save. MO2 gives Elden Ring the selected file "
            "as ER0000.sl2. Apply saves the route and choice for this profile; restart "
            "MO2 before launching after a change.",
            card,
        )
        route_note.setObjectName(f"{self.ProfilesDialogCardName}_mode_note")
        route_note.setWordWrap(True)
        layout.addWidget(route_note)

        status = QLabel(card)
        status.setObjectName(f"{self.ProfilesDialogCardName}_status")
        status.setWordWrap(True)
        layout.addWidget(status)
        status.hide()

        detail = QLabel(card)
        detail.setObjectName(f"{self.ProfilesDialogCardName}_description")
        detail.hide()
        mode_combo.currentIndexChanged.connect(
            lambda index: detail.setText(
                self.ModeDescriptions.get(mode_combo.itemData(index), "")
            )
        )
        first_radio = radio_group.buttons()[0] if radio_group.buttons() else None
        if first_radio is not None:
            first_radio.setChecked(True)
        return (
            card,
            mode_combo,
            detail,
            status,
            apply_button,
            save_profile_combo,
            create_save_profile_button,
        )

    @staticmethod
    def _select_dialog_mode(mode_combo, mode: str) -> None:
        index = mode_combo.findData(mode)
        if index < 0:
            index = mode_combo.findData(EldenRingMo2SaveIsolation.ProfileIsolated)
        mode_combo.blockSignals(True)
        mode_combo.setCurrentIndex(index)
        mode_combo.blockSignals(False)
        for radio in mode_combo.parentWidget().findChildren(QRadioButton):
            if radio.property("eldenRingSaveMode") is not None:
                radio.blockSignals(True)
                radio.setChecked(radio.property("eldenRingSaveMode") == mode_combo.currentData())
                radio.blockSignals(False)
        detail = mode_combo.parentWidget().findChild(
            QLabel, f"{mode_combo.objectName().removesuffix('_mode')}_description"
        )
        if detail is not None:
            detail.setText(
                EldenRingMo2SaveIsolation.ModeDescriptions.get(
                    mode_combo.currentData(), ""
                )
            )

    @staticmethod
    def _selected_dialog_mode(mode_combo) -> str:
        mode = mode_combo.currentData()
        if mode not in {
            EldenRingMo2SaveIsolation.ProfileIsolated,
            EldenRingMo2SaveIsolation.InstanceShared,
            EldenRingMo2SaveIsolation.GlobalShared,
        }:
            return EldenRingMo2SaveIsolation.ProfileIsolated
        return mode

    @staticmethod
    def _hide_native_checkbox(checkbox) -> None:
        if checkbox is None:
            return
        checkbox.setProperty("_eldenRingSaveUiHidden", True)
        checkbox.hide()

    @classmethod
    def _set_create_checkbox(cls, checkbox, checked: bool) -> None:
        if checkbox is None:
            return
        if checkbox.property("_eldenRingSaveUiOriginalChecked") is None:
            checkbox.setProperty(
                "_eldenRingSaveUiOriginalChecked", checkbox.isChecked()
            )
        checkbox.setChecked(checked)
        cls._hide_native_checkbox(checkbox)

    @staticmethod
    def _profiles_dialog_layouts(dialog):
        """Return MO2's profile row and its profile/action columns."""
        row_layout = dialog.findChild(QHBoxLayout, "horizontalLayout_3")
        if row_layout is None or row_layout.count() < 2:
            return None, None, None
        profile_column = row_layout.itemAt(0).layout()
        actions_column = next(
            (
                row_layout.itemAt(index).layout()
                for index in range(1, row_layout.count())
                if row_layout.itemAt(index).layout() is not None
            ),
            None,
        )
        profile_list = dialog.findChild(QListWidget, "profilesList")
        if (
            profile_column is None
            or actions_column is None
            or profile_list is None
            or profile_column.indexOf(profile_list) < 0
        ):
            return None, None, None
        return row_layout, profile_column, actions_column

    @staticmethod
    def _insert_profiles_panel_below_row(dialog, root_layout, row_layout, panel) -> bool:
        """Place the save panel after MO2's profile row, allowing a page header."""
        row_host = row_layout.parentWidget()
        row_index = root_layout.indexOf(row_host) if row_host is not None else -1
        if row_index < 0:
            return False
        if root_layout.indexOf(panel) < 0:
            root_layout.insertWidget(row_index + 1, panel)
        return True

    def _hide_create_instance_options(
        self, dialog, save_checkbox, selected_mode: str
    ) -> None:
        self._set_create_checkbox(
            save_checkbox, selected_mode != self.GlobalShared
        )
        self._set_create_checkbox(
            dialog.findChild(QCheckBox, "profileInisCheckbox"), False
        )
        self._set_create_checkbox(
            dialog.findChild(QCheckBox, "archiveInvalidationCheckbox"), False
        )

    def _install_safe_transfer_button(self, dialog, actions_column) -> None:
        native_button = dialog.findChild(QPushButton, "transferButton")
        profile_list = dialog.findChild(QListWidget, "profilesList")
        if native_button is None or actions_column is None:
            return

        if native_button.property("_eldenRingOriginalTransferVisible") is None:
            native_button.setProperty(
                "_eldenRingOriginalTransferVisible", not native_button.isHidden()
            )
            native_button.setProperty(
                "_eldenRingOriginalTransferEnabled", native_button.isEnabled()
            )

        safe_button = dialog.findChild(QPushButton, self.SafeTransferButtonName)
        if safe_button is None:
            safe_button = QPushButton(native_button.text(), dialog)
            safe_button.setObjectName(self.SafeTransferButtonName)
            safe_button.setToolTip(
                "Choose one Elden Ring save file and its source and destination "
                "routes."
            )
            safe_button.clicked.connect(
                lambda _checked=False, target=dialog: self._open_safe_transfer_from_profiles(
                    target
                )
            )

        native_index = actions_column.indexOf(native_button)
        if actions_column.indexOf(safe_button) < 0:
            if native_index >= 0:
                actions_column.insertWidget(native_index, safe_button)
            else:
                actions_column.addWidget(safe_button)
        if profile_list is not None and not safe_button.property(
            "_eldenRingTransferSelectionSync"
        ):
            safe_button.setProperty("_eldenRingTransferSelectionSync", True)
            def sync_transfer_enabled(_current=None, _previous=None) -> None:
                safe_button.setEnabled(profile_list.currentItem() is not None)
            profile_list.currentItemChanged.connect(sync_transfer_enabled)
        if actions_column.indexOf(safe_button) < 0:
            qInfo(
                "Elden Ring MO2 Save Isolation: could not place the route-aware "
                "Transfer Saves button; MO2's native action remains available."
            )
            return
        safe_button.setEnabled(
            profile_list is None or profile_list.currentItem() is not None
        )
        safe_button.show()
        if safe_button.isHidden():
            qInfo(
                "Elden Ring MO2 Save Isolation: route-aware Transfer Saves "
                "button did not become visible; MO2's native action remains available."
            )
            return
        native_button.hide()
        if not dialog.property("_eldenRingSafeTransferIntegrated"):
            dialog.setProperty("_eldenRingSafeTransferIntegrated", True)
            qInfo(
                "Elden Ring MO2 Save Isolation: replaced MO2's native "
                "Transfer Saves action with the route-aware save-copy dialog."
            )

    def _open_safe_transfer_from_profiles(self, dialog) -> None:
        profile_list = dialog.findChild(QListWidget, "profilesList")
        active_profile = self.__organizer.profile()
        selected_item = profile_list.currentItem() if profile_list is not None else None
        if selected_item is None:
            QMessageBox.warning(
                dialog,
                "Select a profile",
                "Select the MO2 profile whose save routes you want to manage.",
            )
            return

        if active_profile is None:
            QMessageBox.warning(dialog, "No active profile", "MO2 has no active profile.")
            return

        selected_path = Path(active_profile.absolutePath()).parent / selected_item.text()
        self._display_transfer_dialog(selected_path, selected_item.text(), dialog)

    @staticmethod
    def _profile_route_id(profile_directory: Path) -> str:
        return "profile:" + os.path.normcase(os.path.abspath(str(profile_directory)))

    @classmethod
    def _save_transfer_routes(
        cls, current_profile_directory: Path
    ) -> tuple[list[tuple[str, str, Path]], str]:
        profiles_root = current_profile_directory.parent
        try:
            profile_directories = [
                entry
                for entry in profiles_root.iterdir()
                if entry.is_dir() and (entry / "modlist.txt").is_file()
            ]
        except OSError:
            profile_directories = []

        current_id = cls._profile_route_id(current_profile_directory)
        if all(
            cls._profile_route_id(path) != current_id for path in profile_directories
        ):
            profile_directories.append(current_profile_directory)
        profile_directories.sort(
            key=lambda path: (
                path.name.casefold() != current_profile_directory.name.casefold(),
                path.name.casefold(),
            )
        )

        routes = []
        for profile_directory in profile_directories:
            profile_route = cls._profile_route_id(profile_directory)
            routes.append(
                (
                    f"MO2 profile saves — {profile_directory.name} (standard)",
                    profile_route,
                    profile_directory / "saves",
                )
            )
            for profile_id, profile_name in cls._read_named_save_profiles(
                profile_directory, cls.ProfileIsolated
            ):
                routes.append(
                    (
                        f"MO2 game save profile — {profile_directory.name}: {profile_name}",
                        f"{profile_route}:save:{profile_id}",
                        profile_directory / cls.SaveProfilesDirectoryName / profile_id,
                    )
                )

        shared_root = profiles_root.parent / "Elden Ring Shared Saves"
        routes.append(
            (
                "Shared saves for this MO2 instance (standard)",
                cls.InstanceShared,
                shared_root,
            )
        )
        for profile_id, profile_name in cls._read_named_save_profiles(
            current_profile_directory, cls.InstanceShared
        ):
            routes.append(
                (
                    f"Shared game save profile — {profile_name}",
                    f"{cls.InstanceShared}:save:{profile_id}",
                    shared_root / cls.SaveProfilesDirectoryName / profile_id,
                )
            )
        routes.append(
            (
                "Global Elden Ring / Steam saves",
                cls.GlobalShared,
                cls._global_save_directory(),
            )
        )
        return routes, current_id
    @staticmethod
    def _is_reparse_point(path: Path) -> bool:
        try:
            item_stat = os.lstat(path)
        except OSError:
            return False
        reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
        return bool(
            getattr(item_stat, "st_file_attributes", 0) & reparse_flag
        ) or stat.S_ISLNK(item_stat.st_mode)

    @classmethod
    def _list_transferable_saves(cls, root: Path) -> list[Path]:
        if not root.is_dir():
            return []

        saves = []
        seen = set()
        for current, directories, filenames in os.walk(root, followlinks=False):
            current_path = Path(current)
            directories[:] = [
                name
                for name in directories
                if not cls._is_reparse_point(current_path / name)
            ]
            for filename in filenames:
                if Path(filename).suffix.casefold() != ".sl2":
                    continue
                save_path = current_path / filename
                if cls._is_reparse_point(save_path):
                    continue
                try:
                    if not stat.S_ISREG(os.lstat(save_path).st_mode):
                        continue
                    key = os.path.normcase(os.path.abspath(str(save_path)))
                except OSError:
                    continue
                if key in seen:
                    continue
                seen.add(key)
                saves.append(save_path)

        try:
            saves.sort(key=lambda path: path.relative_to(root).as_posix().casefold())
        except ValueError:
            saves.sort(key=lambda path: str(path).casefold())
        return saves

    @classmethod
    def _configured_instance_profiles_root(cls, ini_path: Path) -> Path | None:
        """Resolve an instance's profiles folder from its ModOrganizer.ini."""
        config = configparser.ConfigParser(interpolation=None)
        try:
            config.read(ini_path, encoding="utf-8")
            base_text = config.get(
                "Settings", "base_directory", fallback=str(ini_path.parent)
            ).strip()
            base_text = base_text.replace("%BASE_DIR%", str(ini_path.parent))
            base_path = Path(os.path.expandvars(base_text))
            if not base_path.is_absolute():
                base_path = ini_path.parent / base_path

            profiles_text = config.get(
                "Settings", "profiles_directory", fallback="%BASE_DIR%/profiles"
            ).strip()
            profiles_text = profiles_text.replace("%BASE_DIR%", str(base_path))
            profiles_path = Path(os.path.expandvars(profiles_text))
            if not profiles_path.is_absolute():
                profiles_path = base_path / profiles_path
            return profiles_path
        except (OSError, UnicodeError, configparser.Error, ValueError):
            return None

    def _create_instance_save_sources(self) -> list[tuple[str, Path, str]]:
        """Find individual Elden Ring saves in Steam and known MO2 instances.

        MO2 has a standard global-instance directory and a portable-instance
        marker in the current application directory. The active profile's
        configured profile root is included too, covering custom roots for the
        instance that is currently running.
        """
        discovered_roots: list[tuple[str, Path]] = []
        instance_ini_paths: dict[str, Path] = {}

        local_app_data = os.environ.get("LOCALAPPDATA")
        if not local_app_data:
            local_app_data = str(Path.home() / "AppData" / "Local")
        global_instances_root = Path(local_app_data) / "ModOrganizer"
        try:
            for instance_directory in global_instances_root.iterdir():
                if not instance_directory.is_dir():
                    continue
                ini_path = instance_directory / "ModOrganizer.ini"
                if ini_path.is_file():
                    instance_ini_paths[self._normalized_path(ini_path)] = ini_path
        except OSError:
            pass

        try:
            mo2_directory = Path(__file__).resolve().parents[1]
            portable_ini = mo2_directory / "ModOrganizer.ini"
            if portable_ini.is_file():
                instance_ini_paths[self._normalized_path(portable_ini)] = portable_ini
        except (OSError, RuntimeError, IndexError):
            pass

        for ini_path in instance_ini_paths.values():
            config = configparser.ConfigParser(interpolation=None)
            try:
                config.read(ini_path, encoding="utf-8")
                game_name = config.get("General", "gameName", fallback="")
            except (OSError, UnicodeError, configparser.Error, ValueError):
                continue
            if "elden ring" not in game_name.casefold():
                continue
            profiles_root = self._configured_instance_profiles_root(ini_path)
            if profiles_root is None or not profiles_root.is_dir():
                continue
            discovered_roots.append((ini_path.parent.name, profiles_root))

        try:
            active_profile = self.__organizer.profile()
            if active_profile is not None and self._active_instance_is_elden_ring():
                active_profiles_root = Path(active_profile.absolutePath()).parent
                if all(
                    self._normalized_path(active_profiles_root)
                    != self._normalized_path(root)
                    for _name, root in discovered_roots
                ):
                    discovered_roots.append(
                        ("Current MO2 instance", active_profiles_root)
                    )
        except (AttributeError, OSError, RuntimeError):
            pass

        sources: list[tuple[str, Path, str]] = []
        seen_files: set[str] = set()

        def add_save_files(label: str, root: Path) -> None:
            for save_path in self._list_transferable_saves(root):
                try:
                    relative_path = save_path.relative_to(root)
                except ValueError:
                    continue
                file_key = self._normalized_path(save_path)
                if file_key in seen_files:
                    continue
                seen_files.add(file_key)
                relative_parts = relative_path.as_posix().split("/")
                display_parts = list(relative_parts)
                for index, part in enumerate(relative_parts[:-1]):
                    if part.isdigit() and len(part) > 12:
                        display_parts[index] = f"…{part[-6:]}"
                display_path = "/".join(display_parts)
                sources.append(
                    (
                        f"{label} — {display_path}",
                        save_path,
                        relative_path.as_posix(),
                    )
                )

        add_save_files("Steam / Elden Ring", self._global_save_directory())
        discovered_roots.sort(key=lambda item: item[0].casefold())
        for instance_name, profiles_root in discovered_roots:
            display_instance = instance_name.partition(" — ")[0].strip()
            if len(display_instance) > 24:
                display_instance = display_instance[:21].rstrip() + "…"
            try:
                profile_directories = sorted(
                    (entry for entry in profiles_root.iterdir() if entry.is_dir()),
                    key=lambda entry: entry.name.casefold(),
                )
            except OSError:
                continue
            for profile_directory in profile_directories:
                display_profile = f"{profile_directory.name} ({display_instance})"
                add_save_files(
                    f"MO2 — {display_profile} / standard saves",
                    profile_directory / "saves",
                )
                for profile_id, profile_name in self._read_named_save_profiles(
                    profile_directory, self.ProfileIsolated
                ):
                    add_save_files(
                        f"MO2 — {display_profile} / {profile_name}",
                        profile_directory / self.SaveProfilesDirectoryName / profile_id,
                    )

            shared_profile_directory = profiles_root / "__EldenRingSharedSaves__"
            shared_root = profiles_root.parent / "Elden Ring Shared Saves"
            add_save_files(f"MO2 — {display_instance} / shared saves", shared_root)
            for profile_id, profile_name in self._read_named_save_profiles(
                shared_profile_directory, self.InstanceShared
            ):
                add_save_files(
                    f"MO2 — {display_instance} / shared / {profile_name}",
                    shared_root / self.SaveProfilesDirectoryName / profile_id,
                )

        return sources

    def _refresh_create_instance_save_sources(self, dialog) -> None:
        card = dialog.findChild(QGroupBox, self.CreateInstanceCardName)
        if card is None:
            return
        source_combo = card.findChild(
            QComboBox, self.CreateInstanceSaveSourceComboName
        )
        copy_checkbox = card.findChild(
            QCheckBox, self.CreateInstanceSaveCopyCheckboxName
        )
        status_label = card.findChild(QLabel, self.CreateInstanceSaveStatusName)
        if source_combo is None or copy_checkbox is None or status_label is None:
            return

        selected_data = source_combo.currentData()
        selected_source = (
            selected_data[0]
            if isinstance(selected_data, (tuple, list)) and selected_data
            else None
        )
        source_combo.blockSignals(True)
        source_combo.clear()
        for label, source_path, relative_path in self._create_instance_save_sources():
            index = source_combo.count()
            source_combo.addItem(label, (str(source_path), relative_path))
            source_combo.setItemData(
                index,
                f"{label}\n{source_path}",
                Qt.ItemDataRole.ToolTipRole,
            )
        restore_index = -1
        if selected_source:
            for index in range(source_combo.count()):
                data = source_combo.itemData(index)
                if isinstance(data, (tuple, list)) and data[0] == selected_source:
                    restore_index = index
                    break
        if restore_index >= 0:
            source_combo.setCurrentIndex(restore_index)
        source_combo.blockSignals(False)

        found_saves = source_combo.count()
        copy_checkbox.setEnabled(found_saves > 0)
        source_combo.setEnabled(found_saves > 0 and copy_checkbox.isChecked())
        if found_saves:
            status_label.setText(
                f"{found_saves} .sl2 save(s) found. The selected file will be "
                "copied after the new instance opens. Existing destination files "
                "are never replaced."
            )
        else:
            copy_checkbox.setChecked(False)
            status_label.setText(
                "No .sl2 saves were found in Steam or the detected Elden Ring "
                "MO2 profiles."
            )
        self._sync_create_instance_game_save_profile_controls(dialog)

    def _on_create_instance_game_save_profile_toggled(
        self, dialog, checked: bool
    ) -> None:
        card = dialog.findChild(QGroupBox, self.CreateInstanceCardName)
        name_edit = (
            card.findChild(QLineEdit, self.CreateInstanceGameSaveProfileNameEditName)
            if card is not None
            else None
        )
        if checked and name_edit is not None and not name_edit.text().strip():
            name_edit.setText("Elden Ring Save")
            name_edit.selectAll()
        self._sync_create_instance_game_save_profile_controls(dialog)

    def _sync_create_instance_game_save_profile_controls(self, dialog) -> None:
        card = dialog.findChild(QGroupBox, self.CreateInstanceCardName)
        if card is None:
            return

        checkbox = card.findChild(
            QCheckBox, self.CreateInstanceGameSaveProfileCheckboxName
        )
        name_edit = card.findChild(
            QLineEdit, self.CreateInstanceGameSaveProfileNameEditName
        )
        note = card.findChild(
            QLabel, self.CreateInstanceGameSaveProfileNoteName
        )
        copy_checkbox = card.findChild(
            QCheckBox, self.CreateInstanceSaveCopyCheckboxName
        )
        source_combo = card.findChild(
            QComboBox, self.CreateInstanceSaveSourceComboName
        )
        status_label = card.findChild(QLabel, self.CreateInstanceSaveStatusName)
        if checkbox is None or name_edit is None:
            return

        mode = dialog.property("_eldenRingSaveMode")
        supports_named_profiles = mode in {
            self.ProfileIsolated,
            self.InstanceShared,
        }
        if not supports_named_profiles and checkbox.isChecked():
            checkbox.blockSignals(True)
            checkbox.setChecked(False)
            checkbox.blockSignals(False)

        checkbox.setEnabled(supports_named_profiles)
        name_edit.setEnabled(supports_named_profiles and checkbox.isChecked())

        if copy_checkbox is not None:
            if checkbox.isChecked():
                copy_checkbox.setText(
                    "Copy an existing .sl2 save into this named profile"
                )
            else:
                copy_checkbox.setText(
                    "Copy an existing .sl2 save into the new instance"
                )
            copy_checkbox.setEnabled(
                source_combo is not None and source_combo.count() > 0
            )
            if source_combo is not None:
                source_combo.setEnabled(
                    copy_checkbox.isChecked() and copy_checkbox.isEnabled()
                )

        if note is not None:
            if not supports_named_profiles:
                note.setText(
                    "Named game save profiles require profile-isolated or "
                    "instance-shared saves. Global mode uses the Steam save folder."
                )
            elif checkbox.isChecked():
                note.setText(
                    "This name appears in MO2's Profiles section. If you choose "
                    "a starting save below, it will be copied into this profile. "
                    "Elden Ring loads the selected file as ER0000.sl2."
                )
            else:
                note.setText(
                    "Optional. Leave this unchecked to use standard saves. "
                    "You can create named game save profiles later in Profiles."
                )

        if status_label is not None and source_combo is not None:
            if source_combo.count():
                destination = (
                    "the named game save profile"
                    if checkbox.isChecked()
                    else "the new instance's standard save folder"
                )
                status_label.setText(
                    f"{source_combo.count()} .sl2 save(s) found. The selected file "
                    f"will be copied to {destination} after the new instance opens. "
                    "Existing destination files are never replaced."
                )
            else:
                status_label.setText(
                    "No .sl2 saves were found in Steam or the detected Elden Ring "
                    "MO2 profiles."
                )

    @staticmethod
    def _same_file_contents(first: Path, second: Path) -> bool:
        if first.stat().st_size != second.stat().st_size:
            return False
        with first.open("rb") as first_stream, second.open("rb") as second_stream:
            while True:
                first_chunk = first_stream.read(1024 * 1024)
                second_chunk = second_stream.read(1024 * 1024)
                if first_chunk != second_chunk:
                    return False
                if not first_chunk:
                    return True

    def _display_transfer_dialog(
        self, profile_directory: Path, profile_name: str, parent=None
    ) -> None:
        dialog = QDialog(parent or self.__parent_widget)
        dialog.setObjectName("EldenRingSaveTransferDialog")
        dialog.setWindowTitle("Elden Ring Save Transfer")
        dialog.setMinimumSize(760, 500)
        dialog.resize(820, 560)
        dialog.setSizeGripEnabled(True)

        layout = QVBoxLayout(dialog)
        layout.setContentsMargins(18, 16, 18, 14)
        layout.setSpacing(10)

        header = QWidget(dialog)
        header_layout = QVBoxLayout(header)
        header_layout.setContentsMargins(2, 0, 2, 0)
        header_layout.setSpacing(2)
        heading = QLabel("Transfer an Elden Ring save", header)
        heading.setObjectName("saveTransferTitle")
        header_layout.addWidget(heading)
        intro = QLabel(
            "Copy one .sl2 save between storage routes. "
            f"The selected MO2 profile is {profile_name}. Its account folder is preserved, "
            "and the copy is named ER0000.sl2 at the destination.",
            header,
        )
        intro.setObjectName("saveTransferSubtitle")
        intro.setWordWrap(True)
        header_layout.addWidget(intro)
        layout.addWidget(header)

        routes, current_profile_route = self._save_transfer_routes(profile_directory)
        route_paths = {route_id: path for _label, route_id, path in routes}

        route_card = QFrame(dialog)
        route_card.setObjectName("saveTransferRouteCard")
        route_card.setFrameShape(QFrame.Shape.StyledPanel)
        route_layout = QVBoxLayout(route_card)
        route_layout.setContentsMargins(12, 9, 12, 10)
        route_layout.setSpacing(7)
        route_heading = QLabel("Save routes", route_card)
        route_heading.setObjectName("saveTransferSectionHeading")
        route_layout.addWidget(route_heading)

        source_row = QHBoxLayout()
        source_row.setSpacing(10)
        source_row.addWidget(QLabel("Copy from:", route_card))
        source_combo = QComboBox(route_card)
        source_combo.setObjectName("saveTransferSourceRoute")
        for label, route_id, _path in routes:
            source_combo.addItem(label, route_id)
        source_row.addWidget(source_combo, 1)
        route_layout.addLayout(source_row)

        destination_row = QHBoxLayout()
        destination_row.setSpacing(10)
        destination_row.addWidget(QLabel("Copy to:", route_card))
        destination_combo = QComboBox(route_card)
        destination_combo.setObjectName("saveTransferDestinationRoute")
        destination_combo.addItem("Choose a destination…", "")
        for label, route_id, _path in routes:
            destination_combo.addItem(label, route_id)
        destination_row.addWidget(destination_combo, 1)
        route_layout.addLayout(destination_row)
        layout.addWidget(route_card)

        path_card = QFrame(dialog)
        path_card.setObjectName("saveTransferPathCard")
        path_card.setFrameShape(QFrame.Shape.StyledPanel)
        path_layout = QVBoxLayout(path_card)
        path_layout.setContentsMargins(12, 8, 12, 8)
        path_layout.setSpacing(4)
        source_folder_label = QLabel(path_card)
        source_folder_label.setObjectName("saveTransferSourcePath")
        source_folder_label.setWordWrap(True)
        source_folder_label.setTextInteractionFlags(
            Qt.TextInteractionFlag.TextSelectableByMouse
        )
        path_layout.addWidget(source_folder_label)
        destination_folder_label = QLabel(
            "Destination folder: choose a route above.", path_card
        )
        destination_folder_label.setObjectName("saveTransferDestinationPath")
        destination_folder_label.setWordWrap(True)
        destination_folder_label.setTextInteractionFlags(
            Qt.TextInteractionFlag.TextSelectableByMouse
        )
        path_layout.addWidget(destination_folder_label)
        layout.addWidget(path_card)

        list_heading = QLabel("Save files in the selected source", dialog)
        list_heading.setObjectName("saveTransferListHeading")
        layout.addWidget(list_heading)

        save_list = QListWidget(dialog)
        save_list.setObjectName("saveTransferList")
        save_list.setSelectionMode(QListWidget.SelectionMode.SingleSelection)
        save_list.setMinimumHeight(150)
        layout.addWidget(save_list, 1)

        status_card = QFrame(dialog)
        status_card.setObjectName("saveTransferStatusCard")
        status_card.setFrameShape(QFrame.Shape.StyledPanel)
        status_layout = QVBoxLayout(status_card)
        status_layout.setContentsMargins(12, 7, 12, 7)
        status_label = QLabel(status_card)
        status_label.setObjectName("saveTransferStatus")
        status_label.setWordWrap(True)
        status_layout.addWidget(status_label)
        layout.addWidget(status_card)

        button_row = QHBoxLayout()
        button_row.addStretch(1)
        copy_button = QPushButton("Copy selected save")
        copy_button.setObjectName("saveTransferCopyButton")
        close_button = QPushButton("Close")
        close_button.setObjectName("saveTransferCloseButton")
        button_row.addWidget(copy_button)
        button_row.addWidget(close_button)
        layout.addLayout(button_row)

        active_mode = self._read_mode_directory(profile_directory)
        active_path = self._active_save_directory_directory(profile_directory, active_mode)
        source_index = 0
        for index in range(source_combo.count()):
            route_path = route_paths.get(source_combo.itemData(index))
            if route_path is not None and self._normalized_path(route_path) == self._normalized_path(active_path):
                source_index = index
                break
        source_combo.setCurrentIndex(source_index)

        def transfer_is_available() -> bool:
            source_root = route_paths.get(source_combo.currentData())
            destination_root = route_paths.get(destination_combo.currentData())
            if (
                save_list.currentItem() is None
                or source_root is None
                or destination_root is None
            ):
                return False
            try:
                return source_root.resolve() != destination_root.resolve()
            except OSError:
                return os.path.normcase(os.path.abspath(str(source_root))) != os.path.normcase(
                    os.path.abspath(str(destination_root))
                )

        def refresh_transfer_choices(*_args) -> None:
            source_id = source_combo.currentData()
            destination_id = destination_combo.currentData()
            source_root = route_paths.get(source_id, Path())
            destination_root = route_paths.get(destination_id)
            source_folder_label.setText(f"Source folder: {source_root}")
            destination_folder_label.setText(
                f"Destination folder: {destination_root}"
                if destination_root is not None
                else "Destination folder: choose a route above."
            )

            save_list.clear()
            files = self._list_transferable_saves(source_root)
            for save_path in files:
                try:
                    relative_path = save_path.relative_to(source_root)
                    display_path = relative_path.as_posix()
                except ValueError:
                    display_path = save_path.name
                item = QListWidgetItem(display_path)
                item.setData(Qt.ItemDataRole.UserRole, str(save_path))
                save_list.addItem(item)

            same_route = False
            if destination_root is not None:
                try:
                    same_route = source_root.resolve() == destination_root.resolve()
                except OSError:
                    same_route = os.path.normcase(os.path.abspath(str(source_root))) == os.path.normcase(
                        os.path.abspath(str(destination_root))
                    )

            if same_route:
                status_label.setText(
                    "Source and destination are the same folder. Nothing will be copied."
                )
            elif not files:
                status_label.setText("No .sl2 save files were found in this source folder.")
            else:
                status_label.setText(
                    f"{len(files)} save file(s) found. Select exactly one file to copy."
                )
            copy_button.setEnabled(transfer_is_available())

        def copy_selected_save() -> None:
            item = save_list.currentItem()
            source_id = source_combo.currentData()
            destination_id = destination_combo.currentData()
            source_root = route_paths.get(source_id)
            destination_root = route_paths.get(destination_id)
            if item is None or source_root is None or destination_root is None:
                return

            source_path = Path(item.data(Qt.ItemDataRole.UserRole))
            if source_path.suffix.casefold() != ".sl2" or self._is_reparse_point(source_path):
                QMessageBox.warning(dialog, "Invalid save file", "Choose a normal .sl2 save file.")
                return
            try:
                source_root_resolved = source_root.resolve()
                source_path_resolved = source_path.resolve(strict=True)
                relative_path = source_path_resolved.relative_to(source_root_resolved)
                destination_root_resolved = destination_root.resolve()
            except (OSError, ValueError) as error:
                QMessageBox.warning(
                    dialog,
                    "Save path could not be resolved",
                    f"The selected save is no longer inside its source folder. Nothing was copied.\n\n{error}",
                )
                return

            destination_relative_path = relative_path.parent / "ER0000.sl2"
            destination_path = destination_root_resolved / destination_relative_path
            if source_path_resolved == destination_path.resolve():
                QMessageBox.information(
                    dialog, "Same save file", "Source and destination point to the same file. Nothing was copied."
                )
                return
            if destination_path.exists():
                if not destination_path.is_file() or self._is_reparse_point(destination_path):
                    QMessageBox.warning(
                        dialog,
                        "Destination is not a save file",
                        f"The destination cannot be replaced safely:\n\n{destination_path}",
                    )
                    return
                try:
                    if self._same_file_contents(source_path_resolved, destination_path):
                        QMessageBox.information(
                            dialog,
                            "Save already matches",
                            "The destination already contains an identical save. No second copy was made.",
                        )
                        return
                except OSError as error:
                    QMessageBox.warning(dialog, "Could not compare saves", str(error))
                    return
                prompt = (
                    "The destination already has a different save. Replace that file?"
                )
            else:
                prompt = (
                    "Copy this one save file to the selected route? Any missing ID folder will be created."
                )
            answer = QMessageBox.question(
                dialog,
                "Copy one save file",
                f"{prompt}\n\nFrom:\n{source_path_resolved}\n\nTo:\n{destination_path}\n\n"
                "Close Elden Ring before copying. This operation copies only the selected .sl2 file.",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                QMessageBox.StandardButton.No,
            )
            if answer != QMessageBox.StandardButton.Yes:
                return

            temporary_path = None
            backup_path = None
            try:
                destination_path.parent.mkdir(parents=True, exist_ok=True)
                if destination_path.exists():
                    backup_root = destination_root_resolved.parent / (
                        destination_root_resolved.name + " Backups"
                    )
                    backup_path = backup_root / destination_relative_path
                    backup_path.parent.mkdir(parents=True, exist_ok=True)
                    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")
                    original_backup_path = backup_path.with_name(
                        f"{backup_path.stem}.{timestamp}{backup_path.suffix}"
                    )
                    backup_path = original_backup_path
                    suffix = 2
                    while os.path.lexists(backup_path):
                        backup_path = original_backup_path.with_name(
                            f"{original_backup_path.stem}-{suffix}"
                            f"{original_backup_path.suffix}"
                        )
                        suffix += 1
                    os.replace(destination_path, backup_path)
                with tempfile.NamedTemporaryFile(
                    prefix=f".{destination_path.name}.",
                    suffix=".copying",
                    dir=destination_path.parent,
                    delete=False,
                ) as temporary_file:
                    temporary_path = Path(temporary_file.name)
                shutil.copy2(source_path_resolved, temporary_path)
                os.replace(temporary_path, destination_path)
                temporary_path = None
            except OSError as error:
                recovery_note = ""
                if (
                    backup_path is not None
                    and backup_path.exists()
                    and not destination_path.exists()
                ):
                    try:
                        destination_path.parent.mkdir(parents=True, exist_ok=True)
                        os.replace(backup_path, destination_path)
                        backup_path = None
                    except OSError as restore_error:
                        recovery_note = (
                            "\n\nThe previous destination is preserved at:\n"
                            f"{backup_path}\n\nRestore error: {restore_error}"
                        )
                QMessageBox.warning(
                    dialog,
                    "Save copy failed",
                    f"The selected file could not be copied.\n\n{error}"
                    f"{recovery_note}",
                )
                return
            finally:
                if temporary_path is not None:
                    try:
                        temporary_path.unlink(missing_ok=True)
                    except OSError:
                        pass

            backup_note = (
                f"\n\nThe previous destination was moved to:\n{backup_path}"
                if backup_path is not None
                else ""
            )
            QMessageBox.information(
                dialog,
                "Save copied",
                f"Copied and prepared the game save:\n{destination_path}{backup_note}",
            )
            refresh_transfer_choices()

        source_combo.currentIndexChanged.connect(refresh_transfer_choices)
        destination_combo.currentIndexChanged.connect(refresh_transfer_choices)
        save_list.currentRowChanged.connect(
            lambda _row: copy_button.setEnabled(transfer_is_available())
        )
        copy_button.clicked.connect(copy_selected_save)
        close_button.clicked.connect(dialog.accept)
        refresh_transfer_choices()
        dialog.exec()

    def _restore_native_dialog(self, dialog) -> None:
        profile_card = dialog.findChild(QWidget, self.ProfilesDialogCardName)
        if profile_card is not None:
            profile_list = dialog.findChild(QListWidget, "profilesList")
            row_layout, profile_column, actions_column = (
                self._profiles_dialog_layouts(dialog)
            )
            root_layout = dialog.findChild(QVBoxLayout, "verticalLayout_3")
            card_placement = dialog.property("_eldenRingSaveUiCardPlacement")
            native_transfer = dialog.findChild(QPushButton, "transferButton")
            safe_transfer = dialog.findChild(
                QPushButton, self.SafeTransferButtonName
            )
            if safe_transfer is not None:
                safe_transfer.hide()
            if native_transfer is not None:
                original_visible = native_transfer.property(
                    "_eldenRingOriginalTransferVisible"
                )
                original_enabled = native_transfer.property(
                    "_eldenRingOriginalTransferEnabled"
                )
                if original_enabled is not None:
                    native_transfer.setEnabled(bool(original_enabled))
                if original_visible is not None:
                    native_transfer.setVisible(bool(original_visible))
                native_transfer.setProperty(
                    "_eldenRingOriginalTransferVisible", None
                )
                native_transfer.setProperty(
                    "_eldenRingOriginalTransferEnabled", None
                )
            dialog.setProperty("_eldenRingSafeTransferIntegrated", False)
            if profile_column is not None and profile_list is not None:
                local_saves = dialog.findChild(QCheckBox, "localSavesBox")
                insert_at = profile_column.indexOf(local_saves) + 1 if local_saves else 1
                for object_name in ("localIniFilesBox", "invalidationBox"):
                    checkbox = dialog.findChild(QCheckBox, object_name)
                    if checkbox is not None and checkbox.parentWidget() is profile_card:
                        profile_card.layout().removeWidget(checkbox)
                        checkbox.setParent(profile_list.parentWidget())
                        profile_column.insertWidget(insert_at, checkbox)
                        checkbox.show()
                        insert_at += 1

            if card_placement == "row" and row_layout is not None:
                row_layout.removeWidget(profile_card)
                for index in range(row_layout.count()):
                    if row_layout.itemAt(index).spacerItem() is not None:
                        row_layout.takeAt(index)
                        break
                original_left_stretch = dialog.property(
                    "_eldenRingSaveUiOriginalLeftStretch"
                )
                original_action_stretch = dialog.property(
                    "_eldenRingSaveUiOriginalActionStretch"
                )
                if original_left_stretch is not None:
                    row_layout.setStretch(0, int(original_left_stretch))
                if original_action_stretch is not None:
                    row_layout.setStretch(1, int(original_action_stretch))
            elif card_placement == "below" and root_layout is not None:
                root_layout.removeWidget(profile_card)

            footer_layout = dialog.findChild(QHBoxLayout, "horizontalLayout")
            footer = dialog.findChild(QWidget, self.ProfilesDialogFooterName)
            if footer_layout is not None and footer is not None:
                footer_layout.removeWidget(footer)
                footer.hide()
            if footer_layout is not None:
                for object_name, property_name in (
                    (
                        "select",
                        "_eldenRingSaveUiOriginalSelectButtonAlignment",
                    ),
                    (
                        "close",
                        "_eldenRingSaveUiOriginalCloseButtonAlignment",
                    ),
                ):
                    button = dialog.findChild(QPushButton, object_name)
                    alignment = dialog.property(property_name)
                    if button is not None and alignment is not None:
                        footer_layout.setAlignment(
                            button, Qt.AlignmentFlag(int(alignment))
                        )

            profile_card.hide()
            if profile_list is not None:
                original_min_width = dialog.property(
                    "_eldenRingSaveUiOriginalProfileMinWidth"
                )
                if original_min_width is not None:
                    profile_list.setMinimumWidth(int(original_min_width))
                original_list_max_width = dialog.property(
                    "_eldenRingSaveUiOriginalProfileListMaxWidth"
                )
                if original_list_max_width is not None:
                    profile_list.setMaximumWidth(int(original_list_max_width))
                for size_kind in ("MinimumHeight", "MaximumHeight"):
                    original = dialog.property(
                        f"_eldenRingSaveUiOriginalProfile{size_kind}"
                    )
                    if original is not None:
                        getattr(profile_list, f"set{size_kind}")(int(original))
                original_alignment = dialog.property(
                    "_eldenRingSaveUiOriginalProfileListAlignment"
                )
                if original_alignment is not None and profile_column is not None:
                    profile_column.setAlignment(
                        profile_list, Qt.AlignmentFlag(int(original_alignment))
                    )
            original_column_spacing = dialog.property(
                "_eldenRingSaveUiOriginalProfileColumnSpacing"
            )
            if profile_column is not None and original_column_spacing is not None:
                profile_column.setSpacing(int(original_column_spacing))
            original_spacer_index = dialog.property(
                "_eldenRingSaveUiOriginalActionSpacerIndex"
            )
            if actions_column is not None and original_spacer_index is not None:
                spacer_item = actions_column.itemAt(int(original_spacer_index))
                spacer = spacer_item.spacerItem() if spacer_item is not None else None
                original_values = (
                    dialog.property("_eldenRingSaveUiOriginalSpacerWidth"),
                    dialog.property("_eldenRingSaveUiOriginalSpacerHeight"),
                    dialog.property("_eldenRingSaveUiOriginalSpacerHPolicy"),
                    dialog.property("_eldenRingSaveUiOriginalSpacerVPolicy"),
                )
                if spacer is not None and all(value is not None for value in original_values):
                    spacer.changeSize(
                        int(original_values[0]),
                        int(original_values[1]),
                        QSizePolicy.Policy(int(original_values[2])),
                        QSizePolicy.Policy(int(original_values[3])),
                    )
                    actions_column.invalidate()
            original_row_spacing = dialog.property(
                "_eldenRingSaveUiOriginalProfileRowSpacing"
            )
            if row_layout is not None and original_row_spacing is not None:
                row_layout.setSpacing(int(original_row_spacing))
            original_width = dialog.property("_eldenRingSaveUiOriginalWidth")
            original_height = dialog.property("_eldenRingSaveUiOriginalHeight")
            if original_width and original_height:
                if dialog.layout() is not None:
                    dialog.layout().activate()
                dialog.resize(int(original_width), int(original_height))
            for property_name in (
                "_eldenRingSaveUiOriginalProfileMinWidth",
                "_eldenRingSaveUiOriginalProfileListMaxWidth",
                "_eldenRingSaveUiOriginalProfileMinimumHeight",
                "_eldenRingSaveUiOriginalProfileMaximumHeight",
                "_eldenRingSaveUiOriginalProfileColumnSpacing",
                "_eldenRingSaveUiOriginalProfileListAlignment",
                "_eldenRingSaveUiOriginalSelectButtonAlignment",
                "_eldenRingSaveUiOriginalCloseButtonAlignment",
                "_eldenRingSaveUiOriginalActionSpacerIndex",
                "_eldenRingSaveUiOriginalSpacerWidth",
                "_eldenRingSaveUiOriginalSpacerHeight",
                "_eldenRingSaveUiOriginalSpacerHPolicy",
                "_eldenRingSaveUiOriginalSpacerVPolicy",
                "_eldenRingSaveUiOriginalProfileRowSpacing",
                "_eldenRingSaveUiOriginalLeftStretch",
                "_eldenRingSaveUiOriginalActionStretch",
                "_eldenRingSaveUiProfileSpacerInserted",
                "_eldenRingSaveUiCardPlacement",
                "_eldenRingSaveUiOriginalWidth",
                "_eldenRingSaveUiOriginalHeight",
            ):
                dialog.setProperty(property_name, None)

        create_card = dialog.findChild(QGroupBox, self.CreateInstanceCardName)
        if create_card is not None:
            create_card.hide()

        for object_name in (
            "localSavesBox",
            "localIniFilesBox",
            "invalidationBox",
            "profileSavesCheckbox",
            "profileInisCheckbox",
            "archiveInvalidationCheckbox",
        ):
            checkbox = dialog.findChild(QCheckBox, object_name)
            if checkbox is None:
                continue
            original_checked = checkbox.property(
                "_eldenRingSaveUiOriginalChecked"
            )
            if original_checked is not None:
                checkbox.setChecked(bool(original_checked))
                checkbox.setProperty("_eldenRingSaveUiOriginalChecked", None)
            if checkbox.property("_eldenRingSaveUiHidden"):
                checkbox.setProperty("_eldenRingSaveUiHidden", False)
                checkbox.show()

        dialog.setProperty("_eldenRingSaveUiInjected", False)

    def _is_elden_ring_create_dialog(self, dialog) -> bool:
        games_container = dialog.findChild(QWidget, "games")
        if games_container is not None:
            game_buttons = [
                button
                for button in games_container.findChildren(QCommandLinkButton)
                if button.isCheckable() and button.isChecked()
            ]
            return any(
                "elden ring" in button.text().casefold()
                for button in game_buttons
            )

        # Older MO2 variants may not expose the game picker widget.
        try:
            game = self.__organizer.managedGame()
            return bool(game and game.gameShortName().casefold() == "eldenring")
        except Exception:
            return False

    def _active_instance_is_elden_ring(self) -> bool:
        try:
            game = self.__organizer.managedGame()
            return bool(game and game.gameShortName().casefold() == "eldenring")
        except Exception:
            return False

    def _inject_create_instance_dialog(self, dialog, save_checkbox) -> None:
        existing_card = dialog.findChild(QGroupBox, self.CreateInstanceCardName)
        if existing_card is not None:
            existing_card.show()
            mode = dialog.property("_eldenRingSaveMode")
            if mode not in {self.ProfileIsolated, self.InstanceShared, self.GlobalShared}:
                mode = self.ProfileIsolated
            mode_combo = existing_card.findChild(QComboBox, f"{self.CreateInstanceCardName}_mode")
            if mode_combo is not None:
                self._select_dialog_mode(mode_combo, mode)
            self._sync_native_create_instance_mode(dialog, mode)
            copy_checkbox = existing_card.findChild(
                QCheckBox, self.CreateInstanceSaveCopyCheckboxName
            )
            source_combo = existing_card.findChild(
                QComboBox, self.CreateInstanceSaveSourceComboName
            )
            if copy_checkbox is not None and source_combo is not None:
                source_combo.setEnabled(
                    copy_checkbox.isChecked() and copy_checkbox.isEnabled()
                )
            self._hide_create_instance_options(dialog, save_checkbox, mode)
            self._sync_create_instance_game_save_profile_controls(dialog)
            dialog.setProperty("_eldenRingSaveUiInjected", True)
            return

        container = save_checkbox.parentWidget()
        layout = container.layout() if container is not None else None
        if layout is None:
            return

        card, mode_combo, _detail, status, _ = self._dialog_mode_card(
            container,
            self.CreateInstanceCardName,
            title="Elden Ring Save Isolation",
        )
        card.setSizePolicy(
            QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Maximum
        )
        if card.layout() is not None:
            card.layout().setContentsMargins(10, 6, 10, 4)
            card.layout().setSpacing(5)
        insertion_index = layout.indexOf(save_checkbox)
        if insertion_index < 0:
            layout.addWidget(card)
        else:
            layout.insertWidget(insertion_index, card)

        scope = QLabel(
            "Applies only to Elden Ring profiles in this new instance.", card
        )
        scope.setObjectName("eldenRingWizardScopeNote")
        scope.setWordWrap(True)
        card.layout().insertWidget(0, scope)

        dialog.setProperty("_eldenRingSaveUiInjected", True)
        dialog.setProperty("_eldenRingSaveMode", self.ProfileIsolated)
        self._hide_create_instance_options(
            dialog, save_checkbox, self.ProfileIsolated
        )
        status.setText(
            "Saved for the new instance's Default profile. Restart MO2 before launch."
        )

        game_save_profile_heading = QLabel("Game save profile", card)
        game_save_profile_heading.setObjectName("eldenRingWizardGameSaveProfileHeading")
        game_save_profile_heading.setStyleSheet("font-weight: 600;")
        card.layout().addWidget(game_save_profile_heading)

        game_save_profile_row = QHBoxLayout()
        game_save_profile_checkbox = QCheckBox(
            "Create a named game save profile", card
        )
        game_save_profile_checkbox.setObjectName(
            self.CreateInstanceGameSaveProfileCheckboxName
        )
        game_save_profile_checkbox.setToolTip(
            "Create a separate named save choice that will appear in MO2's Profiles section."
        )
        game_save_profile_name = QLineEdit(card)
        game_save_profile_name.setObjectName(
            self.CreateInstanceGameSaveProfileNameEditName
        )
        game_save_profile_name.setPlaceholderText("For example: Main playthrough")
        game_save_profile_name.setMaxLength(64)
        game_save_profile_name.setToolTip(
            "This name distinguishes the Elden Ring save profile in MO2. "
            "The game loads the selected file as ER0000.sl2."
        )
        game_save_profile_name.setEnabled(False)
        game_save_profile_row.addWidget(game_save_profile_checkbox)
        game_save_profile_row.addWidget(game_save_profile_name, 1)
        card.layout().addLayout(game_save_profile_row)

        game_save_profile_note = QLabel(card)
        game_save_profile_note.setObjectName(
            self.CreateInstanceGameSaveProfileNoteName
        )
        game_save_profile_note.setWordWrap(True)
        card.layout().addWidget(game_save_profile_note)

        starting_save_heading = QLabel("Optional starting save", card)
        starting_save_heading.setObjectName("eldenRingStartingSaveHeading")
        card.layout().addWidget(starting_save_heading)

        copy_checkbox = QCheckBox(
            "Copy an existing .sl2 save into the new instance", card
        )
        copy_checkbox.setObjectName(self.CreateInstanceSaveCopyCheckboxName)
        copy_checkbox.setToolTip(
            "Choose a save from Steam or a detected Elden Ring MO2 profile. "
            "The file is copied as ER0000.sl2 after the new instance is opened."
        )
        card.layout().addWidget(copy_checkbox)

        source_row = QHBoxLayout()
        source_combo = QComboBox(card)
        source_combo.setObjectName(self.CreateInstanceSaveSourceComboName)
        source_combo.setSizeAdjustPolicy(
            QComboBox.SizeAdjustPolicy.AdjustToMinimumContentsLengthWithIcon
        )
        source_combo.setMinimumContentsLength(28)
        source_combo.setMaxVisibleItems(8)
        source_combo.view().setMinimumWidth(max(360, source_combo.width()))
        refresh_button = QPushButton("Refresh saves", card)
        refresh_button.setToolTip(
            "Search Steam and detected Elden Ring MO2 profiles again."
        )
        source_row.addWidget(source_combo, 1)
        source_row.addWidget(refresh_button)
        card.layout().addLayout(source_row)

        source_status = QLabel(card)
        source_status.setObjectName(self.CreateInstanceSaveStatusName)
        source_status.setWordWrap(True)
        card.layout().addWidget(source_status)

        copy_checkbox.toggled.connect(
            lambda checked, combo=source_combo: combo.setEnabled(
                checked and copy_checkbox.isEnabled()
            )
        )
        copy_checkbox.toggled.connect(
            lambda _checked, target=dialog: self._sync_create_instance_game_save_profile_controls(
                target
            )
        )
        game_save_profile_checkbox.toggled.connect(
            lambda checked, target=dialog: self._on_create_instance_game_save_profile_toggled(
                target, checked
            )
        )
        refresh_button.clicked.connect(
            lambda _checked=False, target=dialog: self._refresh_create_instance_save_sources(
                target
            )
        )
        self._refresh_create_instance_save_sources(dialog)

        def choose_mode(mode: str) -> None:
            previous_mode = dialog.property("_eldenRingSaveMode")
            if mode == self.GlobalShared and previous_mode != self.GlobalShared:
                answer = QMessageBox.warning(
                    dialog,
                    "Use the global Steam save?",
                    "This mode shares the save with Steam and every Elden Ring "
                    "instance using the global path. Modded play can change the "
                    "vanilla character. Continue?",
                    QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                    QMessageBox.StandardButton.No,
                )
                if answer != QMessageBox.StandardButton.Yes:
                    self._select_dialog_mode(mode_combo, previous_mode)
                    return
            self._hide_create_instance_options(dialog, save_checkbox, mode)
            dialog.setProperty("_eldenRingSaveMode", mode)
            self._sync_native_create_instance_mode(dialog, mode)
            self._sync_create_instance_game_save_profile_controls(dialog)

        def mode_changed(index: int) -> None:
            mode = mode_combo.itemData(index)
            if mode:
                choose_mode(mode)

        mode_combo.currentIndexChanged.connect(mode_changed)

        self._select_dialog_mode(mode_combo, self.ProfileIsolated)
        self._sync_native_create_instance_mode(dialog, self.ProfileIsolated)
        self._sync_create_instance_game_save_profile_controls(dialog)
        dialog.adjustSize()
        dialog.resize(max(dialog.width(), 540), max(dialog.height(), 480))
        qInfo(
            "Elden Ring MO2 Save Isolation: added save-mode options to the "
            "MO2 2.5.2 instance dialog."
        )

        if not dialog.property("_eldenRingSaveUiAcceptedConnected"):
            dialog.setProperty("_eldenRingSaveUiAcceptedConnected", True)
            dialog.accepted.connect(
                lambda target=dialog: self._queue_created_instance_mode(target)
            )

    @staticmethod
    def _sync_native_create_instance_mode(dialog, mode: str) -> None:
        """Keep the fork's profile settings in step with the visible plugin card."""
        native_group = dialog.findChild(QGroupBox, "eldenRingSaveIsolationGroup")
        if native_group is not None:
            native_group.hide()

        native_combo = dialog.findChild(QComboBox, "eldenRingSaveMode")
        if native_combo is None:
            return
        index = native_combo.findData(mode)
        if index >= 0 and native_combo.currentIndex() != index:
            native_combo.setCurrentIndex(index)

    def _queue_created_instance_mode(self, dialog) -> None:
        if not self._is_elden_ring_create_dialog(dialog):
            return
        mode = dialog.property("_eldenRingSaveMode")
        if mode not in {self.ProfileIsolated, self.InstanceShared, self.GlobalShared}:
            return

        profiles_edit = dialog.findChild(QLineEdit, "profiles")
        base_edit = dialog.findChild(QLineEdit, "base")
        if profiles_edit is None:
            qInfo(
                "Elden Ring MO2 Save Isolation: could not read the new instance's "
                "profiles path; configure its mode in the Profiles dialog."
            )
            return

        base_text = base_edit.text().strip() if base_edit is not None else ""
        profiles_text = profiles_edit.text().strip()
        if not profiles_text:
            profiles_text = "%BASE_DIR%/profiles"
        profiles_text = profiles_text.replace("%BASE_DIR%", base_text)
        profiles_path = Path(profiles_text)
        if not profiles_path.is_absolute() and base_text:
            profiles_path = Path(base_text) / profiles_path
        if not profiles_path.is_absolute():
            qInfo(
                "Elden Ring MO2 Save Isolation: the new instance's profile path "
                "could not be resolved; configure its mode in the Profiles dialog."
            )
            return

        default_profile_path = profiles_path / "Default"
        source_save_path = None
        relative_save_path = None
        card = dialog.findChild(QGroupBox, self.CreateInstanceCardName)
        copy_checkbox = (
            card.findChild(QCheckBox, self.CreateInstanceSaveCopyCheckboxName)
            if card is not None
            else None
        )
        source_combo = (
            card.findChild(QComboBox, self.CreateInstanceSaveSourceComboName)
            if card is not None
            else None
        )
        game_save_profile_name = None
        game_save_profile_checkbox = (
            card.findChild(
                QCheckBox, self.CreateInstanceGameSaveProfileCheckboxName
            )
            if card is not None
            else None
        )
        game_save_profile_name_edit = (
            card.findChild(
                QLineEdit, self.CreateInstanceGameSaveProfileNameEditName
            )
            if card is not None
            else None
        )
        if (
            game_save_profile_checkbox is not None
            and game_save_profile_checkbox.isChecked()
            and mode in {self.ProfileIsolated, self.InstanceShared}
        ):
            requested_name = (
                game_save_profile_name_edit.text()
                if game_save_profile_name_edit is not None
                else ""
            )
            try:
                game_save_profile_name = self._validated_save_profile_name(
                    requested_name
                )
            except ValueError:
                game_save_profile_name = "Elden Ring Save"
                qInfo(
                    "Elden Ring MO2 Save Isolation: the named game save profile "
                    "had no valid name; using 'Elden Ring Save'."
                )

        if copy_checkbox is not None and copy_checkbox.isChecked():
            selected_data = (
                source_combo.currentData() if source_combo is not None else None
            )
            if isinstance(selected_data, (tuple, list)) and len(selected_data) == 2:
                candidate_path, candidate_relative = selected_data
                relative_path = Path(str(candidate_relative))
                if (
                    isinstance(candidate_path, str)
                    and candidate_path
                    and not relative_path.is_absolute()
                    and ".." not in relative_path.parts
                    and relative_path.suffix.casefold() == ".sl2"
                ):
                    source_save_path = candidate_path
                    relative_save_path = relative_path.as_posix()
                else:
                    qInfo(
                        "Elden Ring MO2 Save Isolation: the selected save path "
                        "was invalid; the new profile will be created without "
                        "copying a save."
                    )
            else:
                qInfo(
                    "Elden Ring MO2 Save Isolation: no save was selected; the "
                    "new profile will be created without copying a save."
                )
        try:
            self._queue_pending_mode(
                default_profile_path,
                mode,
                source_save_path,
                relative_save_path,
                game_save_profile_name,
            )
        except (OSError, UnicodeError, ValueError) as error:
            qInfo(
                "Elden Ring MO2 Save Isolation: could not queue the new instance "
                f"save mode: {error}"
            )
            return

        qInfo(
            "Elden Ring MO2 Save Isolation: queued "
            f"'{mode}' for the new instance profile at '{default_profile_path}'."
        )
        if game_save_profile_name:
            qInfo(
                "Elden Ring MO2 Save Isolation: queued named game save profile "
                f"'{game_save_profile_name}' for the new instance."
            )
        if source_save_path is not None:
            qInfo(
                "Elden Ring MO2 Save Isolation: queued one selected .sl2 save "
                f"from '{source_save_path}' for the new instance."
            )

    @staticmethod
    def _remember_profiles_dialog_layout(
        dialog, row_layout, profile_list, profile_column, actions_column
    ) -> None:
        if dialog.property("_eldenRingSaveUiOriginalWidth") is not None:
            return
        dialog.setProperty("_eldenRingSaveUiOriginalWidth", dialog.width())
        dialog.setProperty("_eldenRingSaveUiOriginalHeight", dialog.height())
        dialog.setProperty(
            "_eldenRingSaveUiOriginalProfileRowSpacing", row_layout.spacing()
        )
        dialog.setProperty(
            "_eldenRingSaveUiOriginalLeftStretch", row_layout.stretch(0)
        )
        dialog.setProperty(
            "_eldenRingSaveUiOriginalActionStretch", row_layout.stretch(1)
        )
        dialog.setProperty(
            "_eldenRingSaveUiOriginalProfileMinWidth", profile_list.minimumWidth()
        )
        dialog.setProperty(
            "_eldenRingSaveUiOriginalProfileListMaxWidth",
            profile_list.maximumWidth(),
        )
        dialog.setProperty(
            "_eldenRingSaveUiOriginalProfileMinimumHeight",
            profile_list.minimumHeight(),
        )
        dialog.setProperty(
            "_eldenRingSaveUiOriginalProfileMaximumHeight",
            profile_list.maximumHeight(),
        )
        dialog.setProperty(
            "_eldenRingSaveUiOriginalProfileColumnSpacing",
            profile_column.spacing(),
        )
        profile_list_index = profile_column.indexOf(profile_list)
        profile_list_item = (
            profile_column.itemAt(profile_list_index)
            if profile_list_index >= 0
            else None
        )
        alignment = (
            profile_list_item.alignment()
            if profile_list_item is not None
            else Qt.AlignmentFlag(0)
        )
        dialog.setProperty(
            "_eldenRingSaveUiOriginalProfileListAlignment",
            int(alignment.value),
        )
        footer_layout = dialog.findChild(QHBoxLayout, "horizontalLayout")
        if footer_layout is not None:
            for object_name, property_name in (
                (
                    "select",
                    "_eldenRingSaveUiOriginalSelectButtonAlignment",
                ),
                (
                    "close",
                    "_eldenRingSaveUiOriginalCloseButtonAlignment",
                ),
            ):
                button = dialog.findChild(QPushButton, object_name)
                index = footer_layout.indexOf(button) if button is not None else -1
                item = footer_layout.itemAt(index) if index >= 0 else None
                button_alignment = (
                    item.alignment()
                    if item is not None
                    else Qt.AlignmentFlag(0)
                )
                dialog.setProperty(property_name, int(button_alignment.value))
        for index in range(actions_column.count() - 1, -1, -1):
            item = actions_column.itemAt(index)
            spacer = item.spacerItem() if item is not None else None
            if spacer is None:
                continue
            size = spacer.sizeHint()
            policy = spacer.sizePolicy()
            dialog.setProperty("_eldenRingSaveUiOriginalActionSpacerIndex", index)
            dialog.setProperty("_eldenRingSaveUiOriginalSpacerWidth", size.width())
            dialog.setProperty("_eldenRingSaveUiOriginalSpacerHeight", size.height())
            dialog.setProperty(
                "_eldenRingSaveUiOriginalSpacerHPolicy",
                int(policy.horizontalPolicy().value),
            )
            dialog.setProperty(
                "_eldenRingSaveUiOriginalSpacerVPolicy",
                int(policy.verticalPolicy().value),
            )
            break

    @staticmethod
    def _apply_profiles_dialog_layout(
        dialog, row_layout, profile_list, profile_column, actions_column
    ) -> None:
        header = dialog.findChild(QWidget, "profilesHeader")
        if header is not None:
            header.setSizePolicy(
                QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Maximum
            )
            header_layout = header.layout()
            if header_layout is not None:
                header_layout.setContentsMargins(12, 4, 12, 4)
                header_layout.setSpacing(2)
        for object_name in ("profilesPageTitle", "profilesPageSubtitle"):
            label = dialog.findChild(QLabel, object_name)
            if label is not None:
                label.setSizePolicy(
                    QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Fixed
                )

        row_host = row_layout.parentWidget()
        if row_host is not None:
            row_host.setSizePolicy(
                QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Maximum
            )
        footer_host = dialog.findChild(QWidget, "widget_2")
        if footer_host is not None:
            footer_host.setSizePolicy(
                QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Maximum
            )

        if EldenRingMo2SaveIsolation.ProfilesPanelDesign == "below":
            action_buttons = [
                actions_column.itemAt(index).widget()
                for index in range(actions_column.count())
                if actions_column.itemAt(index) is not None
                and actions_column.itemAt(index).widget() is not None
                and not actions_column.itemAt(index).widget().isHidden()
            ]
            action_spacing = max(0, actions_column.spacing())
            margins = actions_column.contentsMargins()
            list_height = (
                sum(button.sizeHint().height() for button in action_buttons)
                + action_spacing * max(0, len(action_buttons) - 1)
                + margins.top()
                + margins.bottom()
            )
            for index in range(actions_column.count()):
                item = actions_column.itemAt(index)
                spacer = item.spacerItem() if item is not None else None
                if spacer is not None:
                    spacer_policy = spacer.sizePolicy()
                    spacer.changeSize(
                        0,
                        0,
                        spacer_policy.horizontalPolicy(),
                        QSizePolicy.Policy.Fixed,
                    )
            actions_column.invalidate()
            profile_column.setSpacing(0)
            profile_column.setAlignment(profile_list, Qt.AlignmentFlag.AlignTop)
            profile_list.setFixedHeight(max(100, list_height))
            root_layout = dialog.findChild(QVBoxLayout, "verticalLayout_3")
            if root_layout is not None:
                root_layout.activate()
        else:
            if not dialog.property("_eldenRingSaveUiProfileSpacerInserted"):
                row_layout.insertStretch(2, 1)
                dialog.setProperty("_eldenRingSaveUiProfileSpacerInserted", True)
            # MO2's row contains two nested layouts: profiles/settings first,
            # actions second. The center panel is inserted between them; only
            # the spacer absorbs extra width.
            row_layout.setStretch(0, 0)
            row_layout.setStretch(1, 0)
            row_layout.setStretch(2, 1)
            row_layout.setStretch(3, 0)
            profile_list.setMinimumWidth(240)
            profile_list.setMaximumWidth(280)
            row_layout.setSpacing(14)

        if dialog.layout() is not None:
            dialog.layout().activate()

        # This method can run from QDialog's Show event. Resizing there races
        # the native window's first layout pass and asks Windows for a height
        # that its window procedure immediately clamps, producing
        # QWindowsWindow::setGeometry warnings. Measure and size the completed
        # layout on the next event-loop turn instead.
        if not dialog.property("_eldenRingSaveUiProfilesResizePending"):
            dialog.setProperty("_eldenRingSaveUiProfilesResizePending", True)
            QTimer.singleShot(
                0,
                lambda dialog=dialog: EldenRingMo2SaveIsolation._finish_profiles_dialog_layout(
                    dialog
                ),
            )

    @staticmethod
    def _finish_profiles_dialog_layout(dialog) -> None:
        try:
            dialog.setProperty("_eldenRingSaveUiProfilesResizePending", False)
            if not dialog.property("_eldenRingSaveUiInjected"):
                return
            if dialog.layout() is not None:
                dialog.layout().activate()
            minimum_size = dialog.minimumSizeHint()
            preferred_size = dialog.sizeHint()
            screen = dialog.screen()
            target_width = max(
                1020, minimum_size.width(), preferred_size.width()
            )
            minimum_height = (
                410
                if EldenRingMo2SaveIsolation.ProfilesPanelDesign == "below"
                else 420
            )
            target_height = max(
                minimum_height, minimum_size.height(), preferred_size.height()
            )
            if screen is not None:
                available = screen.availableGeometry()
                target_width = min(
                    target_width, max(minimum_size.width(), available.width() - 80)
                )
                target_height = min(
                    target_height,
                    max(minimum_height, available.height() - 80),
                )
            if (
                not dialog.isMaximized()
                and (dialog.width(), dialog.height())
                != (target_width, target_height)
            ):
                dialog.resize(target_width, target_height)
        except RuntimeError:
            # The dialog may have been closed before the queued resize runs.
            return

    def _inject_profiles_dialog(self, dialog, profile_list, local_saves_checkbox) -> None:
        existing_card = dialog.findChild(QWidget, self.ProfilesDialogCardName)
        row_layout, profile_column, actions_column = (
            self._profiles_dialog_layouts(dialog)
        )
        if row_layout is None or profile_column is None or actions_column is None:
            qInfo(
                "Elden Ring MO2 Save Isolation: MO2's profile/action columns were "
                "not recognized; native profile settings were left unchanged."
            )
            return
        self._install_safe_transfer_button(dialog, actions_column)
        if existing_card is not None:
            if dialog.property("_eldenRingSaveUiInjected"):
                existing_card.show()
                self._hide_native_checkbox(local_saves_checkbox)
                self._hide_native_checkbox(
                    dialog.findChild(QCheckBox, "localIniFilesBox")
                )
                self._hide_native_checkbox(
                    dialog.findChild(QCheckBox, "invalidationBox")
                )
                return
            existing_card.show()
            self._remember_profiles_dialog_layout(
                dialog, row_layout, profile_list, profile_column, actions_column
            )
            if self.ProfilesPanelDesign == "below":
                root_layout = dialog.findChild(QVBoxLayout, "verticalLayout_3")
                if root_layout is None:
                    return
                if not self._insert_profiles_panel_below_row(
                    dialog, root_layout, row_layout, existing_card
                ):
                    qInfo(
                        "Elden Ring MO2 Save Isolation: the profile row could not "
                        "be located for save-panel placement."
                    )
                    return
                dialog.setProperty("_eldenRingSaveUiCardPlacement", "below")
            else:
                if row_layout.indexOf(existing_card) < 0:
                    row_layout.insertWidget(
                        1, existing_card, 0, Qt.AlignmentFlag.AlignTop
                    )
                dialog.setProperty("_eldenRingSaveUiCardPlacement", "row")
            self._apply_profiles_dialog_layout(
                dialog, row_layout, profile_list, profile_column, actions_column
            )
            self._hide_native_checkbox(local_saves_checkbox)
            self._hide_native_checkbox(
                dialog.findChild(QCheckBox, "localIniFilesBox")
            )
            self._hide_native_checkbox(
                dialog.findChild(QCheckBox, "invalidationBox")
            )
            dialog.setProperty("_eldenRingSaveUiInjected", True)
            return

        self._remember_profiles_dialog_layout(
            dialog, row_layout, profile_list, profile_column, actions_column
        )

        profile_options = ()
        save_profile_combo = None
        create_save_profile_button = None
        if self.ProfilesPanelDesign == "below":
            root_layout = dialog.findChild(QVBoxLayout, "verticalLayout_3")
            if root_layout is None:
                qInfo(
                    "Elden Ring MO2 Save Isolation: the Profiles dialog's main "
                    "layout was not recognized; native settings were left intact."
                )
                return
            (
                card,
                mode_combo,
                _detail,
                status,
                apply_button,
                save_profile_combo,
                create_save_profile_button,
            ) = self._profiles_full_width_panel(dialog)
            if not self._insert_profiles_panel_below_row(
                dialog, root_layout, row_layout, card
            ):
                card.deleteLater()
                qInfo(
                    "Elden Ring MO2 Save Isolation: the profile row could not "
                    "be located for save-panel placement; native settings were "
                    "left unchanged."
                )
                return
            dialog.setProperty("_eldenRingSaveUiCardPlacement", "below")
        else:
            card, mode_combo, _detail, status, apply_button = self._dialog_mode_card(
                dialog,
                self.ProfilesDialogCardName,
                include_apply=True,
                profile_options=profile_options,
                compact=True,
            )
            row_layout.insertWidget(1, card, 0, Qt.AlignmentFlag.AlignTop)
            dialog.setProperty("_eldenRingSaveUiCardPlacement", "row")
        self._apply_profiles_dialog_layout(
            dialog, row_layout, profile_list, profile_column, actions_column
        )
        self._hide_native_checkbox(local_saves_checkbox)
        self._hide_native_checkbox(dialog.findChild(QCheckBox, "localIniFilesBox"))
        self._hide_native_checkbox(dialog.findChild(QCheckBox, "invalidationBox"))

        dialog.setProperty("_eldenRingSaveUiInjected", True)
        dialog.setProperty("_eldenRingSaveUiProfileKey", "")

        def selected_profile_directory() -> Path | None:
            item = profile_list.currentItem()
            active_profile = self.__organizer.profile()
            if item is None or active_profile is None:
                return None
            profile_name = item.text().strip()
            if not profile_name:
                return None
            return Path(active_profile.absolutePath()).parent / profile_name

        def refresh_selection() -> None:
            directory = selected_profile_directory()
            if directory is None:
                status.setText("Select a profile to view its Elden Ring save settings.")
                status.show()
                apply_button.setEnabled(False)
                if create_save_profile_button is not None:
                    create_save_profile_button.setEnabled(False)
                return

            profile_key = self._normalized_path(directory)
            if dialog.property("_eldenRingSaveUiProfileKey") != profile_key:
                current_mode = self._read_mode_directory(directory)
                self._select_dialog_mode(mode_combo, current_mode)
                dialog.setProperty("_eldenRingSaveUiProfileKey", profile_key)
                dialog.setProperty("_eldenRingSaveUiAppliedMode", current_mode)

            current_mode = self._selected_dialog_mode(mode_combo)
            route_key = f"{profile_key}|{current_mode}"
            if save_profile_combo is not None and dialog.property(
                "_eldenRingSaveUiSaveProfileRouteKey"
            ) != route_key:
                selected_id = self._read_save_profile_id_directory(
                    directory, current_mode
                )
                self._populate_save_profile_combo(
                    save_profile_combo, directory, current_mode, selected_id
                )
                dialog.setProperty("_eldenRingSaveUiSaveProfileRouteKey", route_key)

            target_local_saves = current_mode != self.GlobalShared
            local_saves_state = local_saves_checkbox.isChecked()
            applied_mode = dialog.property("_eldenRingSaveUiAppliedMode")
            selected_save_id = (
                str(save_profile_combo.currentData() or "")
                if save_profile_combo is not None
                else ""
            )
            saved_save_id = self._read_save_profile_id_directory(
                directory, current_mode
            )
            save_profile_changed = (
                current_mode != self.GlobalShared
                and selected_save_id != saved_save_id
            )
            missing_save_profile = bool(
                save_profile_combo is not None
                and save_profile_combo.property("_eldenRingMissingSaveProfile")
            )
            if create_save_profile_button is not None:
                create_save_profile_button.setEnabled(
                    current_mode in {self.ProfileIsolated, self.InstanceShared}
                )
            if missing_save_profile:
                status.setText(
                    "The saved game profile is unavailable. Choose an existing profile "
                    "or the standard saves before applying."
                )
                status.show()
            elif (
                current_mode == applied_mode
                and local_saves_state == target_local_saves
                and not save_profile_changed
            ):
                status.clear()
                status.hide()
            else:
                status.setText(
                    "Unsaved changes. Apply the save route and selected game save profile."
                )
                status.show()
            apply_button.setEnabled(True)

        def create_save_profile_for_selection() -> None:
            directory = selected_profile_directory()
            item = profile_list.currentItem()
            mode = self._selected_dialog_mode(mode_combo)
            if directory is None or item is None or save_profile_combo is None:
                return
            if mode == self.GlobalShared:
                QMessageBox.information(
                    dialog,
                    "Global Steam save selected",
                    "Named game save profiles are available with profile-isolated "
                    "or instance-shared saves.",
                )
                return
            profile_id = self._display_create_save_profile_dialog(
                directory, mode, dialog
            )
            if profile_id is None:
                return
            self._populate_save_profile_combo(
                save_profile_combo, directory, mode, profile_id
            )
            dialog.setProperty(
                "_eldenRingSaveUiSaveProfileRouteKey",
                f"{self._normalized_path(directory)}|{mode}",
            )
            status.setText(
                f"Created '{save_profile_combo.currentText()}'. Apply to select it for "
                f"MO2 profile '{item.text()}'."
            )
            status.show()
            refresh_selection()

        def apply_selected_mode() -> None:
            directory = selected_profile_directory()
            item = profile_list.currentItem()
            if directory is None or item is None:
                return

            mode = self._selected_dialog_mode(mode_combo)
            previous_mode = self._read_mode_directory(directory)
            previous_save_id = self._read_save_profile_id_directory(
                directory, previous_mode
            )
            selected_save_id = (
                str(save_profile_combo.currentData() or "")
                if save_profile_combo is not None
                else ""
            )
            if mode != self.GlobalShared:
                available_ids = {
                    profile_id
                    for profile_id, _name in self._read_named_save_profiles(
                        directory, mode
                    )
                }
                if (
                    save_profile_combo is not None
                    and save_profile_combo.property("_eldenRingMissingSaveProfile")
                ) or (selected_save_id and selected_save_id not in available_ids):
                    QMessageBox.warning(
                        dialog,
                        "Save profile unavailable",
                        "Choose an available named save profile before applying.",
                    )
                    refresh_selection()
                    return

            if mode == self.GlobalShared and previous_mode != self.GlobalShared:
                answer = QMessageBox.warning(
                    dialog,
                    "Use the global Steam save?",
                    "This shares saves with Steam and other Elden Ring instances "
                    "using the global path. Modded play can change the vanilla "
                    "character. Continue?",
                    QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                    QMessageBox.StandardButton.No,
                )
                if answer != QMessageBox.StandardButton.Yes:
                    self._select_dialog_mode(mode_combo, previous_mode)
                    refresh_selection()
                    return

            should_enable_local_saves = mode != self.GlobalShared
            try:
                self._save_mode_directory(
                    directory,
                    mode,
                    selected_save_id if mode != self.GlobalShared else None,
                )
            except (OSError, UnicodeError, configparser.Error, ValueError) as error:
                self._select_dialog_mode(mode_combo, previous_mode)
                status.setText(f"Could not save the selected route: {error}")
                status.show()
                QMessageBox.warning(dialog, "Could not save route", str(error))
                return

            if local_saves_checkbox.isChecked() != should_enable_local_saves:
                local_saves_checkbox.click()

            if local_saves_checkbox.isChecked() != should_enable_local_saves:
                rollback_succeeded = True
                try:
                    self._save_mode_directory(
                        directory, previous_mode, previous_save_id
                    )
                except (OSError, UnicodeError, configparser.Error, ValueError) as error:
                    rollback_succeeded = False
                    qInfo(
                        "Elden Ring MO2 Save Isolation: could not restore the "
                        f"previous save route after MO2 rejected the change: {error}"
                    )
                self._select_dialog_mode(mode_combo, previous_mode)
                dialog.setProperty("_eldenRingSaveUiProfileKey", "")
                if rollback_succeeded:
                    status.setText(
                        "MO2 did not change its save setting. The previous route "
                        "and named save selection were restored."
                    )
                else:
                    status.setText(
                        "MO2 did not change its save setting, and the previous "
                        "route could not be restored. Do not launch until you check "
                        "Elden Ring / Save Isolation."
                    )
                status.show()
                return

            dialog.setProperty(
                "_eldenRingSaveUiProfileKey", self._normalized_path(directory)
            )
            dialog.setProperty("_eldenRingSaveUiAppliedMode", mode)
            dialog.setProperty(
                "_eldenRingSaveUiSaveProfileRouteKey",
                f"{self._normalized_path(directory)}|{mode}",
            )
            selected_name = (
                save_profile_combo.currentText()
                if save_profile_combo is not None
                else "standard saves"
            )
            status.setText(
                f"'{selected_name}' saved for '{item.text()}'. Restart MO2 before "
                "launching Elden Ring."
            )
            status.show()
            qInfo(
                "Elden Ring MO2 Save Isolation: saved route "
                f"'{mode}' and save profile '{selected_save_id or 'default'}' "
                f"for profile '{item.text()}'."
            )
        if apply_button is not None:
            apply_button.clicked.connect(apply_selected_mode)

        def refresh_after_selection(*_args) -> None:
            QTimer.singleShot(0, refresh_selection)

        profile_list.currentItemChanged.connect(refresh_after_selection)
        mode_combo.currentIndexChanged.connect(lambda _index: refresh_selection())
        if save_profile_combo is not None:
            save_profile_combo.currentIndexChanged.connect(lambda _index: refresh_selection())
        if create_save_profile_button is not None:
            create_save_profile_button.clicked.connect(create_save_profile_for_selection)
        refresh_selection()
        qInfo(
            "Elden Ring MO2 Save Isolation: integrated a full-width save section "
            "below MO2's profile list and action columns."
        )

    @staticmethod
    def _normalized_path(path: Path) -> str:
        try:
            return os.path.normcase(os.path.normpath(str(path.resolve())))
        except (OSError, RuntimeError):
            return os.path.normcase(os.path.abspath(str(path)))

    @classmethod
    def _pending_modes_path(cls) -> Path:
        local_app_data = os.environ.get("LOCALAPPDATA")
        if not local_app_data:
            local_app_data = str(Path.home() / "AppData" / "Local")
        return (
            Path(local_app_data)
            / "ModOrganizer"
            / "EldenRingMO2SaveIsolation"
            / cls.PendingModesFilename
        )

    def _read_pending_modes(self) -> list[dict[str, str]]:
        pending_path = self._pending_modes_path()
        try:
            data = json.loads(pending_path.read_text(encoding="utf-8"))
        except FileNotFoundError:
            return []
        except (OSError, UnicodeError, json.JSONDecodeError):
            return []
        if not isinstance(data, list):
            return []
        return [
            item
            for item in data
            if isinstance(item, dict)
            and isinstance(item.get("profile_path"), str)
            and item.get("mode") in {self.ProfileIsolated, self.InstanceShared, self.GlobalShared}
        ]

    def _write_pending_modes(self, entries: list[dict[str, str]]) -> None:
        pending_path = self._pending_modes_path()
        pending_path.parent.mkdir(parents=True, exist_ok=True)
        temporary_path = None
        try:
            with tempfile.NamedTemporaryFile(
                mode="w",
                encoding="utf-8",
                newline="",
                prefix=f".{pending_path.name}.",
                suffix=".tmp",
                dir=pending_path.parent,
                delete=False,
            ) as stream:
                temporary_path = Path(stream.name)
                json.dump(entries, stream, indent=2)
                stream.write("\n")
            os.replace(temporary_path, pending_path)
        finally:
            if temporary_path is not None and temporary_path.exists():
                temporary_path.unlink()

    def _queue_pending_mode(
        self,
        profile_directory: Path,
        mode: str,
        source_save_path: str | None = None,
        relative_save_path: str | None = None,
        game_save_profile_name: str | None = None,
    ) -> None:
        if mode not in {self.ProfileIsolated, self.InstanceShared, self.GlobalShared}:
            raise ValueError("Unknown Elden Ring save mode.")
        key = self._normalized_path(profile_directory)
        entries = [
            item
            for item in self._read_pending_modes()
            if self._normalized_path(Path(item["profile_path"])) != key
        ]
        entry = {"profile_path": str(profile_directory), "mode": mode}
        if source_save_path and relative_save_path:
            entry["save_source_path"] = source_save_path
            entry["save_relative_path"] = relative_save_path
        if game_save_profile_name:
            entry["game_save_profile_name"] = self._validated_save_profile_name(
                game_save_profile_name
            )
        entries.append(entry)
        self._write_pending_modes(entries)

    @classmethod
    def _queued_save_source_root(cls, pending_item: dict[str, str]) -> Path:
        source_path = Path(pending_item["save_source_path"])
        relative_path = Path(pending_item["save_relative_path"])
        if (
            relative_path.is_absolute()
            or not relative_path.parts
            or ".." in relative_path.parts
            or relative_path.suffix.casefold() != ".sl2"
        ):
            raise ValueError("The queued save's relative path is invalid.")
        if (
            source_path.suffix.casefold() != ".sl2"
            or cls._is_reparse_point(source_path)
            or not source_path.is_file()
        ):
            raise FileNotFoundError(
                f"The selected .sl2 save is no longer available: {source_path}"
            )
        resolved_source = source_path.resolve(strict=True)
        if len(relative_path.parts) > len(resolved_source.parts) - 1:
            raise ValueError("The queued save's source folder could not be resolved.")
        source_root = resolved_source.parents[len(relative_path.parts) - 1]
        try:
            actual_relative_path = resolved_source.relative_to(source_root)
        except ValueError as error:
            raise ValueError("The queued save is outside its source folder.") from error
        if actual_relative_path.as_posix().casefold() != relative_path.as_posix().casefold():
            raise ValueError("The queued save's source path no longer matches.")
        return source_root

    def _copy_queued_save_to_profile(
        self, profile_directory: Path, mode: str, pending_item: dict[str, str]
    ) -> str:
        source_path = Path(pending_item["save_source_path"])
        relative_path = Path(pending_item["save_relative_path"])
        if (
            relative_path.is_absolute()
            or ".." in relative_path.parts
            or relative_path.suffix.casefold() != ".sl2"
        ):
            raise ValueError("The queued save's relative path is invalid.")
        if (
            source_path.suffix.casefold() != ".sl2"
            or self._is_reparse_point(source_path)
            or not source_path.is_file()
        ):
            raise FileNotFoundError(
                f"The selected .sl2 save is no longer available: {source_path}"
            )
        source_path = source_path.resolve(strict=True)

        if mode == self.InstanceShared:
            destination_root = profile_directory.parent.parent / "Elden Ring Shared Saves"
        elif mode == self.GlobalShared:
            destination_root = self._global_save_directory()
        else:
            destination_root = profile_directory / "saves"
        destination_path = destination_root / relative_path.parent / "ER0000.sl2"

        if self._normalized_path(source_path) == self._normalized_path(destination_path):
            return "same"
        if os.path.lexists(destination_path):
            if (
                self._is_reparse_point(destination_path)
                or not destination_path.is_file()
            ):
                raise FileExistsError(
                    f"The destination exists and is not a regular save file: "
                    f"{destination_path}"
                )
            if self._same_file_contents(source_path, destination_path):
                return "identical"
            raise FileExistsError(
                f"A different save already exists at the destination; it was "
                f"left unchanged: {destination_path}"
            )

        parent_path = destination_path.parent
        while parent_path == destination_root or destination_root in parent_path.parents:
            if os.path.lexists(parent_path) and self._is_reparse_point(parent_path):
                raise OSError(
                    f"The destination save folder contains a link or reparse point: "
                    f"{parent_path}"
                )
            if parent_path == destination_root:
                break
            parent_path = parent_path.parent
        destination_path.parent.mkdir(parents=True, exist_ok=True)
        temporary_path = None
        try:
            with tempfile.NamedTemporaryFile(
                prefix=f".{destination_path.name}.",
                suffix=".copying",
                dir=destination_path.parent,
                delete=False,
            ) as temporary_file:
                temporary_path = Path(temporary_file.name)
            shutil.copy2(source_path, temporary_path)
            try:
                # Windows rename fails if the destination appeared after the
                # first check. On other platforms, a hard link gives the same
                # no-overwrite publication behavior.
                if os.name == "nt":
                    os.rename(temporary_path, destination_path)
                else:
                    os.link(temporary_path, destination_path)
            except FileExistsError:
                if (
                    destination_path.is_file()
                    and not self._is_reparse_point(destination_path)
                    and self._same_file_contents(source_path, destination_path)
                ):
                    return "identical"
                raise FileExistsError(
                    f"A save appeared at the destination during copying and was "
                    f"left unchanged: {destination_path}"
                )
        finally:
            if temporary_path is not None:
                try:
                    temporary_path.unlink(missing_ok=True)
                except OSError:
                    pass
        return "copied"

    def _apply_pending_mode_to_active_profile(self) -> None:
        profile = self.__organizer.profile()
        if profile is None:
            return

        active_path = self._normalized_path(Path(profile.absolutePath()))
        pending = self._read_pending_modes()
        remaining = []
        changed = False
        for item in pending:
            if self._normalized_path(Path(item["profile_path"])) != active_path:
                remaining.append(item)
                continue
            if active_path in self.__pending_applied_paths:
                changed = True
                continue
            try:
                profile_directory = Path(profile.absolutePath())
                mode = item["mode"]
                save_profile_name = self._validated_save_profile_name(
                    item.get("game_save_profile_name", "")
                ) if item.get("game_save_profile_name") else ""
                selected_save_profile_id = ""

                if save_profile_name and mode in {
                    self.ProfileIsolated,
                    self.InstanceShared,
                }:
                    existing_profile = next(
                        (
                            (profile_id, name)
                            for profile_id, name in self._read_named_save_profiles(
                                profile_directory, mode
                            )
                            if name.casefold() == save_profile_name.casefold()
                        ),
                        None,
                    )
                    if existing_profile is not None:
                        selected_save_profile_id = existing_profile[0]
                        if item.get("save_source_path"):
                            qInfo(
                                "Elden Ring MO2 Save Isolation: a game save profile "
                                f"named '{save_profile_name}' already exists; it was "
                                "kept unchanged and the selected source save was not imported."
                            )
                    else:
                        source_path = None
                        source_root = None
                        if item.get("save_source_path") and item.get(
                            "save_relative_path"
                        ):
                            source_path = Path(item["save_source_path"])
                            source_root = self._queued_save_source_root(item)
                        selected_save_profile_id = self._create_named_save_profile(
                            profile_directory,
                            mode,
                            save_profile_name,
                            source_path,
                            source_root,
                        )
                        qInfo(
                            "Elden Ring MO2 Save Isolation: created named game save "
                            f"profile '{save_profile_name}' for the new instance."
                        )

                    self._save_mode_directory(
                        profile_directory, mode, selected_save_profile_id
                    )
                else:
                    if save_profile_name and mode == self.GlobalShared:
                        qInfo(
                            "Elden Ring MO2 Save Isolation: named game save profiles "
                            "are unavailable in global Steam mode; using the standard "
                            "global save route."
                        )
                    self._save_mode_directory(profile_directory, mode)

                changed = True
                self.__pending_applied_paths.add(active_path)
                self.__pending_error_reported.discard(active_path)
                qInfo(
                    "Elden Ring MO2 Save Isolation: applied the selected save "
                    f"mode to new profile '{profile.name()}'."
                )

                if (
                    not save_profile_name
                    and item.get("save_source_path")
                    and item.get("save_relative_path")
                ):
                    try:
                        result = self._copy_queued_save_to_profile(
                            profile_directory, mode, item
                        )
                        if result == "copied":
                            qInfo(
                                "Elden Ring MO2 Save Isolation: copied one "
                                "selected .sl2 save into the new instance."
                            )
                        elif result == "identical":
                            qInfo(
                                "Elden Ring MO2 Save Isolation: the destination "
                                "already has the same .sl2 save; no duplicate "
                                "copy was made."
                            )
                        else:
                            qInfo(
                                "Elden Ring MO2 Save Isolation: the selected "
                                "save already points to the new instance's "
                                "destination; no copy was needed."
                            )
                    except (OSError, UnicodeError, ValueError) as error:
                        qInfo(
                            "Elden Ring MO2 Save Isolation: the new instance "
                            f"was created, but its selected save was not copied: {error}"
                        )
            except (OSError, UnicodeError, configparser.Error, ValueError) as error:
                if active_path not in self.__pending_error_reported:
                    qInfo(
                        "Elden Ring MO2 Save Isolation: could not apply the "
                        f"pending save mode: {error}"
                    )
                    self.__pending_error_reported.add(active_path)
                remaining.append(item)

        if changed or len(remaining) != len(pending):
            if active_path in self.__pending_cleanup_failed_paths:
                return
            try:
                self._write_pending_modes(remaining)
            except (OSError, UnicodeError, ValueError) as error:
                self.__pending_cleanup_failed_paths.add(active_path)
                qInfo(
                    "Elden Ring MO2 Save Isolation: could not update pending "
                    f"instance choices: {error}"
                )

    def _watch_create_game_buttons(self, dialog) -> None:
        games_container = dialog.findChild(QWidget, "games")
        if games_container is None:
            return
        for button in games_container.findChildren(QCommandLinkButton):
            if not button.isCheckable() or button.property(
                "_eldenRingSaveUiWatchConnected"
            ):
                continue
            button.setProperty("_eldenRingSaveUiWatchConnected", True)
            button.clicked.connect(
                lambda _checked=False, target=dialog: QTimer.singleShot(
                    0, lambda: self._sync_create_instance_dialog(target)
                )
            )

    def _sync_profile_input_dialog(self, dialog) -> bool:
        default_ini_checkbox = dialog.findChild(QCheckBox, "defaultSettingsBox")
        if default_ini_checkbox is None:
            return False

        should_hide = (
            self._dialog_ui_enabled() and self._active_instance_is_elden_ring()
        )
        original_visible = default_ini_checkbox.property(
            "_eldenRingOriginalDefaultIniVisible"
        )
        if should_hide:
            if original_visible is None:
                default_ini_checkbox.setProperty(
                    "_eldenRingOriginalDefaultIniVisible",
                    not default_ini_checkbox.isHidden(),
                )
            if not default_ini_checkbox.isHidden():
                default_ini_checkbox.hide()
                layout = dialog.layout()
                if layout is not None:
                    layout.invalidate()
                dialog.adjustSize()
        elif original_visible is not None:
            default_ini_checkbox.setVisible(bool(original_visible))
            default_ini_checkbox.setProperty(
                "_eldenRingOriginalDefaultIniVisible", None
            )
            layout = dialog.layout()
            if layout is not None:
                layout.invalidate()
            dialog.adjustSize()
        return True

    def _sync_create_instance_dialog(self, dialog) -> None:
        save_checkbox = dialog.findChild(QCheckBox, "profileSavesCheckbox")
        if save_checkbox is None:
            return
        enabled = self._dialog_ui_enabled()
        is_elden_ring_create = self._is_elden_ring_create_dialog(dialog)
        if enabled and is_elden_ring_create:
            self._inject_create_instance_dialog(dialog, save_checkbox)
        elif dialog.property("_eldenRingSaveUiInjected"):
            self._restore_native_dialog(dialog)

    def _refresh_mo2_dialogs(self, preferred_dialog=None) -> None:
        if self.__refreshing_dialogs:
            return
        self.__refreshing_dialogs = True
        try:
            if preferred_dialog is None:
                self._apply_pending_mode_to_active_profile()
            application = QApplication.instance()
            if application is None:
                return

            enabled = self._dialog_ui_enabled()
            dialogs = (
                [preferred_dialog]
                if isinstance(preferred_dialog, QDialog)
                else application.topLevelWidgets()
            )
            for dialog in dialogs:
                if not isinstance(dialog, QDialog):
                    continue
                if preferred_dialog is None and not dialog.isVisible():
                    continue

                if self._sync_profile_input_dialog(dialog):
                    continue

                profile_list = dialog.findChild(QListWidget, "profilesList")
                local_saves_checkbox = dialog.findChild(QCheckBox, "localSavesBox")
                if profile_list is not None and local_saves_checkbox is not None:
                    if enabled and self._active_instance_is_elden_ring():
                        if not dialog.property("_eldenRingSaveUiInjected"):
                            self._inject_profiles_dialog(
                                dialog, profile_list, local_saves_checkbox
                            )
                    elif dialog.property("_eldenRingSaveUiInjected"):
                        self._restore_native_dialog(dialog)
                    continue

                self._watch_create_game_buttons(dialog)
                save_checkbox = dialog.findChild(QCheckBox, "profileSavesCheckbox")
                if save_checkbox is None:
                    continue

                # When Elden Ring is selected this native widget is deliberately
                # hidden. Keep checking the wizard so changing to another game
                # restores MO2's own controls straight away.
                if (
                    not save_checkbox.isVisible()
                    and not dialog.property("_eldenRingSaveUiInjected")
                    and preferred_dialog is None
                ):
                    continue
                self._sync_create_instance_dialog(dialog)
        except Exception:
            qInfo(
                "Elden Ring MO2 Save Isolation: dialog refresh failed:\n"
                + traceback.format_exc()
            )
        finally:
            self.__refreshing_dialogs = False

    @staticmethod
    def _profile_directory(profile) -> Path:
        return Path(profile.absolutePath())

    @classmethod
    def _profile_save_directory(cls, profile) -> Path:
        return cls._profile_directory(profile) / "saves"

    @classmethod
    def _instance_shared_directory_from_path(cls, profile_directory: Path) -> Path:
        return profile_directory.parent.parent / "Elden Ring Shared Saves"

    @classmethod
    def _instance_shared_directory(cls, profile) -> Path:
        return cls._instance_shared_directory_from_path(
            cls._profile_directory(profile)
        )

    @staticmethod
    def _global_save_directory() -> Path:
        appdata = os.environ.get("APPDATA")
        if not appdata:
            appdata = str(Path.home() / "AppData" / "Roaming")
        return Path(appdata) / "EldenRing"

    @classmethod
    def _read_mode(cls, profile) -> str:
        return cls._read_mode_directory(cls._profile_directory(profile))

    @classmethod
    def _read_mode_directory(cls, profile_directory: Path) -> str:
        config_path = profile_directory / cls.ConfigFilename
        config = configparser.ConfigParser(interpolation=None)
        try:
            config.read(config_path, encoding="utf-8")
            mode = config.get("Saves", "mode", fallback=cls.ProfileIsolated)
        except (OSError, UnicodeError, configparser.Error, ValueError):
            mode = cls.ProfileIsolated
        allowed = {cls.ProfileIsolated, cls.InstanceShared, cls.GlobalShared}
        return mode if mode in allowed else cls.ProfileIsolated

    @classmethod
    def _save_mode(cls, profile, mode: str, save_profile_id: str | None = None) -> None:
        cls._save_mode_directory(cls._profile_directory(profile), mode, save_profile_id)

    @classmethod
    def _save_mode_directory(
        cls,
        profile_directory: Path,
        mode: str,
        save_profile_id: str | None = None,
    ) -> None:
        if mode not in {cls.ProfileIsolated, cls.InstanceShared, cls.GlobalShared}:
            raise ValueError("Unknown Elden Ring save mode.")
        if not profile_directory.is_dir():
            raise FileNotFoundError(
                f"The selected MO2 profile folder was not found:\n{profile_directory}"
            )

        config = configparser.ConfigParser(interpolation=None)
        config_path = profile_directory / cls.ConfigFilename
        try:
            config.read(config_path, encoding="utf-8")
        except (OSError, UnicodeError, configparser.Error, ValueError):
            raise
        if not config.has_section("Saves"):
            config.add_section("Saves")
        config.set("Saves", "mode", mode)
        if save_profile_id is not None:
            key = cls._save_profile_key(mode)
            if key is not None:
                normalized_id = cls._valid_save_profile_id(save_profile_id)
                if save_profile_id and not normalized_id:
                    raise ValueError("The selected named save profile is invalid.")
                config.set("Saves", key, normalized_id)

        temporary_path = None
        try:
            with tempfile.NamedTemporaryFile(
                mode="w",
                encoding="utf-8",
                newline="",
                prefix=f".{config_path.name}.",
                suffix=".tmp",
                dir=config_path.parent,
                delete=False,
            ) as stream:
                temporary_path = Path(stream.name)
                config.write(stream)
            os.replace(temporary_path, config_path)
        finally:
            if temporary_path is not None and temporary_path.exists():
                temporary_path.unlink()
    @classmethod
    def _valid_save_profile_id(cls, profile_id: str) -> str:
        normalized = str(profile_id).strip().lower()
        if len(normalized) != 32 or any(
            character not in "0123456789abcdef" for character in normalized
        ):
            return ""
        return normalized

    @classmethod
    def _save_profile_key(cls, mode: str) -> str | None:
        if mode == cls.ProfileIsolated:
            return cls.ProfileSaveProfileKey
        if mode == cls.InstanceShared:
            return cls.InstanceSaveProfileKey
        return None

    @classmethod
    def _read_save_profile_id_directory(
        cls, profile_directory: Path, mode: str
    ) -> str:
        key = cls._save_profile_key(mode)
        if key is None:
            return ""
        config = configparser.ConfigParser(interpolation=None)
        try:
            config.read(profile_directory / cls.ConfigFilename, encoding="utf-8")
            value = config.get("Saves", key, fallback="")
        except (OSError, UnicodeError, configparser.Error, ValueError):
            return ""
        return cls._valid_save_profile_id(value)

    @classmethod
    def _save_profiles_root_directory(
        cls, profile_directory: Path, mode: str
    ) -> Path | None:
        if mode == cls.ProfileIsolated:
            return profile_directory / cls.SaveProfilesDirectoryName
        if mode == cls.InstanceShared:
            return (
                cls._instance_shared_directory_from_path(profile_directory)
                / cls.SaveProfilesDirectoryName
            )
        return None

    @classmethod
    def _read_named_save_profiles(
        cls, profile_directory: Path, mode: str
    ) -> list[tuple[str, str]]:
        root = cls._save_profiles_root_directory(profile_directory, mode)
        if root is None or not root.is_dir() or cls._is_reparse_point(root):
            return []
        profiles = []
        try:
            candidates = sorted(root.iterdir(), key=lambda path: path.name.casefold())
        except OSError:
            return []
        for directory in candidates:
            profile_id = cls._valid_save_profile_id(directory.name)
            if (
                not profile_id
                or profile_id != directory.name
                or not directory.is_dir()
                or cls._is_reparse_point(directory)
            ):
                continue
            metadata_path = root / f"{profile_id}.json"
            if not metadata_path.is_file() or cls._is_reparse_point(metadata_path):
                continue
            try:
                metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
                name = cls._validated_save_profile_name(metadata.get("name"))
                if metadata.get("id") != profile_id:
                    continue
            except (OSError, UnicodeError, json.JSONDecodeError, ValueError, AttributeError):
                continue
            profiles.append((profile_id, name))
        return sorted(profiles, key=lambda item: item[1].casefold())

    @staticmethod
    def _validated_save_profile_name(name: str) -> str:
        if not isinstance(name, str):
            raise ValueError("Enter a name for this game save profile.")
        cleaned = name.strip()
        if not cleaned or len(cleaned) > 64:
            raise ValueError("Use a name between 1 and 64 characters.")
        if any(ord(character) < 32 or ord(character) == 127 for character in cleaned):
            raise ValueError("The name cannot contain control characters.")
        return cleaned

    @classmethod
    def _save_directory_for_selection(
        cls, profile_directory: Path, mode: str, selected_id: str | None
    ) -> Path:
        if mode == cls.GlobalShared:
            return cls._global_save_directory()
        profile_id = (
            cls._valid_save_profile_id(selected_id)
            if selected_id is not None
            else cls._read_save_profile_id_directory(profile_directory, mode)
        )
        if mode == cls.InstanceShared:
            shared_root = cls._instance_shared_directory_from_path(profile_directory)
            if profile_id:
                return shared_root / cls.SaveProfilesDirectoryName / profile_id
            return shared_root
        if profile_id:
            return profile_directory / cls.SaveProfilesDirectoryName / profile_id
        return profile_directory / "saves"

    @classmethod
    def _active_save_directory_directory(
        cls, profile_directory: Path, mode: str
    ) -> Path:
        return cls._save_directory_for_selection(profile_directory, mode, None)

    @classmethod
    def _active_save_directory(cls, profile, mode: str) -> Path:
        return cls._active_save_directory_directory(cls._profile_directory(profile), mode)

    @classmethod
    def _populate_save_profile_combo(
        cls, combo: QComboBox, profile_directory: Path, mode: str, selected_id: str
    ) -> None:
        combo.blockSignals(True)
        combo.clear()
        default_label = (
            "Standard saves for this MO2 profile"
            if mode == cls.ProfileIsolated
            else "Default saves shared in this MO2 instance"
        )
        combo.addItem(default_label, "")
        profiles = cls._read_named_save_profiles(profile_directory, mode)
        for profile_id, name in profiles:
            combo.addItem(name, profile_id)
        normalized_id = cls._valid_save_profile_id(selected_id)
        index = combo.findData(normalized_id)
        if normalized_id and index < 0:
            index = combo.count()
            combo.addItem(f"Missing save profile ({normalized_id[:8]})", normalized_id)
            combo.setItemData(
                index,
                "This saved selection has no matching profile folder. Choose an available profile.",
                Qt.ItemDataRole.ToolTipRole,
            )
        combo.setCurrentIndex(max(0, index))
        combo.setProperty("_eldenRingMissingSaveProfile", bool(normalized_id and not profiles and index > 0))
        if normalized_id and index > 0:
            combo.setProperty(
                "_eldenRingMissingSaveProfile",
                not any(profile_id == normalized_id for profile_id, _name in profiles),
            )
        combo.blockSignals(False)
        combo.setEnabled(mode in {cls.ProfileIsolated, cls.InstanceShared})

    def _create_named_save_profile(
        self,
        profile_directory: Path,
        mode: str,
        name: str,
        source_path: Path | None = None,
        source_root: Path | None = None,
    ) -> str:
        clean_name = self._validated_save_profile_name(name)
        root = self._save_profiles_root_directory(profile_directory, mode)
        if root is None:
            raise ValueError("Named save profiles are unavailable in global Steam mode.")
        existing_names = {
            existing_name.casefold()
            for _profile_id, existing_name in self._read_named_save_profiles(
                profile_directory, mode
            )
        }
        if clean_name.casefold() in existing_names:
            raise ValueError("A save profile with this name already exists here.")

        root.mkdir(parents=True, exist_ok=True)
        if self._is_reparse_point(root):
            raise OSError(f"The save profile folder is a link and cannot be used: {root}")
        profile_id = uuid.uuid4().hex
        destination = root / profile_id
        if os.path.lexists(destination) or os.path.lexists(root / f"{profile_id}.json"):
            raise FileExistsError("A save profile with this identifier already exists.")

        temporary_directory = Path(tempfile.mkdtemp(prefix=f".{profile_id}.", dir=root))
        created_directory = False
        metadata_temporary = None
        try:
            if source_path is not None:
                if source_root is None:
                    raise ValueError("The selected save source has no known folder.")
                if (
                    source_path.suffix.casefold() != ".sl2"
                    or self._is_reparse_point(source_path)
                    or not source_path.is_file()
                ):
                    raise FileNotFoundError(
                        f"The selected .sl2 save is no longer available: {source_path}"
                    )
                source_root_resolved = source_root.resolve(strict=True)
                source_path_resolved = source_path.resolve(strict=True)
                relative_path = source_path_resolved.relative_to(source_root_resolved)
                if (
                    relative_path.is_absolute()
                    or ".." in relative_path.parts
                    or relative_path.suffix.casefold() != ".sl2"
                ):
                    raise ValueError("The selected save path is outside its source folder.")
                source_parent = source_path.parent
                while source_parent == source_root or source_root in source_parent.parents:
                    if self._is_reparse_point(source_parent):
                        raise OSError(
                            f"The selected save folder contains a link: {source_parent}"
                        )
                    if source_parent == source_root:
                        break
                    source_parent = source_parent.parent

                canonical_relative = relative_path.parent / "ER0000.sl2"
                canonical_path = temporary_directory / canonical_relative
                canonical_path.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source_path_resolved, canonical_path)

                backup_source = source_path_resolved.with_name(
                    source_path_resolved.name + ".bak"
                )
                if backup_source.is_file() and not self._is_reparse_point(backup_source):
                    backup_relative = relative_path.parent / "ER0000.sl2.bak"
                    backup_destination = temporary_directory / backup_relative
                    backup_destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(backup_source, backup_destination)

            os.rename(temporary_directory, destination)
            created_directory = True
            with tempfile.NamedTemporaryFile(
                mode="w",
                encoding="utf-8",
                newline="",
                prefix=f".{profile_id}.",
                suffix=".tmp",
                dir=root,
                delete=False,
            ) as stream:
                metadata_temporary = Path(stream.name)
                json.dump({"id": profile_id, "name": clean_name}, stream, ensure_ascii=False)
                stream.write("\n")
            os.replace(metadata_temporary, root / f"{profile_id}.json")
            metadata_temporary = None
        except Exception:
            if created_directory:
                shutil.rmtree(destination, ignore_errors=True)
            raise
        finally:
            if temporary_directory.exists():
                shutil.rmtree(temporary_directory, ignore_errors=True)
            if metadata_temporary is not None:
                try:
                    metadata_temporary.unlink(missing_ok=True)
                except OSError:
                    pass
        return profile_id

    def _display_create_save_profile_dialog(
        self, profile_directory: Path, mode: str, parent=None
    ) -> str | None:
        if mode not in {self.ProfileIsolated, self.InstanceShared}:
            return None

        dialog = QDialog(parent or self.__parent_widget)
        dialog.setObjectName("EldenRingCreateSaveProfileDialog")
        dialog.setWindowTitle("Create Elden Ring save profile")
        dialog.setMinimumSize(700, 460)
        dialog.resize(780, 500)
        dialog.setSizeGripEnabled(True)
        layout = QVBoxLayout(dialog)
        layout.setContentsMargins(18, 16, 18, 14)
        layout.setSpacing(10)

        header = QFrame(dialog)
        header.setObjectName("saveProfileHeader")
        header_layout = QVBoxLayout(header)
        header_layout.setContentsMargins(14, 10, 14, 10)
        header_layout.setSpacing(3)
        heading = QLabel("Create a save profile", header)
        heading.setObjectName("saveProfileTitle")
        intro = QLabel(
            "Start with a fresh save or copy an existing save into a separate profile.",
            header,
        )
        intro.setObjectName("saveProfileSubtitle")
        intro.setWordWrap(True)
        header_layout.addWidget(heading)
        header_layout.addWidget(intro)
        layout.addWidget(header)

        details_group = QGroupBox("Profile name", dialog)
        details_group.setObjectName("saveProfileDetailsCard")
        details_layout = QVBoxLayout(details_group)
        details_layout.setContentsMargins(14, 12, 14, 12)
        details_layout.setSpacing(6)
        name_row = QHBoxLayout()
        name_label = QLabel("Save profile name:", details_group)
        name_edit = QLineEdit(details_group)
        name_edit.setObjectName("saveProfileNameEdit")
        name_edit.setPlaceholderText("For example: Modded journey")
        name_edit.setMaxLength(64)
        name_row.addWidget(name_edit, 1)
        details_layout.addWidget(name_label)
        details_layout.addLayout(name_row)
        layout.addWidget(details_group)

        source_group = QGroupBox("Starting save", dialog)
        source_group.setObjectName("saveProfileStartingSaveCard")
        source_layout = QVBoxLayout(source_group)
        source_layout.setContentsMargins(14, 12, 14, 12)
        source_layout.setSpacing(7)
        source_note = QLabel(
            "Choose a new empty save or import an existing .sl2 file. Imports are "
            "copied as ER0000.sl2; a matching .bak is copied as ER0000.sl2.bak. "
            "The source is left unchanged. Close Elden Ring before importing.",
            source_group,
        )
        source_note.setObjectName("saveProfileSourceNote")
        source_note.setWordWrap(True)
        source_layout.addWidget(source_note)
        source_row = QHBoxLayout()
        source_label = QLabel("Start from:", source_group)
        source_combo = QComboBox(source_group)
        source_combo.setObjectName("saveProfileSourceCombo")
        source_combo.addItem("Start a new game with an empty save profile", None)
        source_combo.setMaxVisibleItems(10)
        source_combo.setSizeAdjustPolicy(
            QComboBox.SizeAdjustPolicy.AdjustToMinimumContentsLengthWithIcon
        )
        source_combo.setMinimumContentsLength(34)
        source_combo.view().setMinimumWidth(480)
        try:
            sources = self._create_instance_save_sources()
        except (OSError, RuntimeError, ValueError):
            sources = []
        for label, source_path, relative_path in sources:
            relative = Path(relative_path)
            if not relative.parts:
                continue
            try:
                source_root = source_path.parents[len(relative.parts) - 1]
            except IndexError:
                continue
            index = source_combo.count()
            source_combo.addItem(label, (str(source_path), str(source_root)))
            source_combo.setItemData(
                index,
                f"{label}\n{source_path}",
                Qt.ItemDataRole.ToolTipRole,
            )
        source_row.addWidget(source_label)
        source_row.addWidget(source_combo, 1)
        source_layout.addLayout(source_row)
        layout.addWidget(source_group)

        location_card = QFrame(dialog)
        location_card.setObjectName("saveProfileLocationCard")
        location_layout = QVBoxLayout(location_card)
        location_layout.setContentsMargins(12, 8, 12, 8)
        location_layout.setSpacing(3)
        location_caption = QLabel("Save profile folder", location_card)
        location_caption.setObjectName("saveProfileLocationCaption")
        location_label = QLabel(
            str(self._save_profiles_root_directory(profile_directory, mode)),
            location_card,
        )
        location_label.setObjectName("saveProfileLocationPath")
        location_label.setWordWrap(True)
        location_label.setTextInteractionFlags(
            Qt.TextInteractionFlag.TextSelectableByMouse
        )
        location_layout.addWidget(location_caption)
        location_layout.addWidget(location_label)
        layout.addWidget(location_card)

        status = QLabel(dialog)
        status.setObjectName("saveProfileStatus")
        status.setWordWrap(True)
        layout.addWidget(status)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Save
            | QDialogButtonBox.StandardButton.Cancel,
            parent=dialog,
        )
        buttons.setObjectName("saveProfileButtons")
        create_button = buttons.button(QDialogButtonBox.StandardButton.Save)
        if create_button is not None:
            create_button.setText("Create save profile")
            existing_names = {
                existing_name.casefold()
                for _profile_id, existing_name in self._read_named_save_profiles(
                    profile_directory, mode
                )
            }

            def update_create_button(name: str) -> None:
                try:
                    clean_name = self._validated_save_profile_name(name)
                except ValueError:
                    create_button.setEnabled(False)
                    status.clear()
                    return
                duplicate = clean_name.casefold() in existing_names
                create_button.setEnabled(not duplicate)
                status.setText(
                    "A save profile with this name already exists here."
                    if duplicate
                    else ""
                )

            name_edit.textChanged.connect(update_create_button)
            update_create_button(name_edit.text())
        layout.addWidget(buttons)

        def create_profile() -> None:
            try:
                clean_name = self._validated_save_profile_name(name_edit.text())
                selected_source = source_combo.currentData()
                source_path = Path(selected_source[0]) if selected_source else None
                source_root = Path(selected_source[1]) if selected_source else None
                profile_id = self._create_named_save_profile(
                    profile_directory,
                    mode,
                    clean_name,
                    source_path,
                    source_root,
                )
            except (OSError, UnicodeError, ValueError, configparser.Error) as error:
                status.setText(str(error))
                QMessageBox.warning(dialog, "Could not create save profile", str(error))
                return
            dialog.setProperty("_eldenRingCreatedSaveProfileId", profile_id)
            dialog.accept()

        buttons.accepted.connect(create_profile)
        buttons.rejected.connect(dialog.reject)
        dialog.exec()
        if dialog.result() != QDialog.DialogCode.Accepted:
            return None
        return str(dialog.property("_eldenRingCreatedSaveProfileId") or "") or None

    def display(self) -> None:
        if not self._active_instance_is_elden_ring():
            return

        profile = self.__organizer.profile()
        if profile is None:
            QMessageBox.warning(
                self.__parent_widget,
                "No active Elden Ring profile",
                "Select an Elden Ring profile in MO2 before changing save options.",
            )
            return

        dialog = QDialog(self.__parent_widget)
        dialog.setObjectName("EldenRingSaveIsolationDialog")
        dialog.setWindowTitle("Elden Ring Save Isolation")
        dialog.setMinimumSize(720, 540)
        dialog.resize(820, 680)
        dialog.setSizeGripEnabled(True)
        layout = QVBoxLayout(dialog)
        layout.setContentsMargins(16, 14, 16, 12)
        layout.setSpacing(9)

        header = QFrame(dialog)
        header.setObjectName("saveIsolationHeaderPanel")
        header_layout = QVBoxLayout(header)
        header_layout.setContentsMargins(14, 10, 14, 10)
        header_layout.setSpacing(3)
        heading = QLabel("Save routing and profiles", header)
        heading.setObjectName("saveIsolationPageTitle")
        subtitle = QLabel(
            "Choose where Elden Ring loads this MO2 profile's save. Restart MO2 "
            "before launching after you save a route.",
            header,
        )
        subtitle.setObjectName("saveIsolationPageSubtitle")
        subtitle.setWordWrap(True)
        header_layout.addWidget(heading)
        header_layout.addWidget(subtitle)
        layout.addWidget(header)

        scroll_area = QScrollArea(dialog)
        scroll_area.setObjectName("saveIsolationBody")
        scroll_area.setFrameShape(QFrame.Shape.NoFrame)
        scroll_area.setWidgetResizable(True)
        scroll_area.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        content = QWidget(scroll_area)
        content.setObjectName("saveIsolationContent")
        content_layout = QVBoxLayout(content)
        content_layout.setContentsMargins(2, 2, 8, 2)
        content_layout.setSpacing(8)
        scroll_area.setWidget(content)
        layout.addWidget(scroll_area, 1)

        profile_group = QGroupBox("Active MO2 profile", content)
        profile_group.setObjectName("saveIsolationProfileCard")
        profile_layout = QVBoxLayout(profile_group)
        profile_layout.setContentsMargins(12, 10, 12, 10)
        profile_label = QLabel(f"<b>Profile:</b> {profile.name()}")
        profile_label.setObjectName("saveIsolationProfileName")
        profile_layout.addWidget(profile_label)
        profile_path_label = QLabel(str(self._profile_directory(profile)))
        profile_path_label.setObjectName("saveIsolationProfilePath")
        profile_path_label.setWordWrap(True)
        profile_path_label.setTextInteractionFlags(
            Qt.TextInteractionFlag.TextSelectableByMouse
        )
        profile_layout.addWidget(profile_path_label)
        profile_note = QLabel(
            "The selected mode is stored with this profile and checked before "
            "each Elden Ring launch. Restart MO2 after changing the mode so its "
            "save mapping is reloaded before you launch the game."
        )
        profile_note.setObjectName("saveIsolationSectionNote")
        profile_note.setWordWrap(True)
        profile_layout.addWidget(profile_note)
        content_layout.addWidget(profile_group)

        mode_group = QGroupBox("Save mode for this profile", content)
        mode_group.setObjectName("saveIsolationModeCard")
        mode_layout = QVBoxLayout(mode_group)
        mode_layout.setContentsMargins(12, 10, 12, 10)
        mode_layout.setSpacing(6)
        mode_box = QComboBox(mode_group)
        mode_box.setObjectName("saveIsolationModeCombo")
        mode_box.addItem("Isolate saves for this profile (recommended)", self.ProfileIsolated)
        mode_box.addItem("Share modded saves within this MO2 instance", self.InstanceShared)
        mode_box.addItem("Use global Steam / Elden Ring saves", self.GlobalShared)
        current_mode = self._read_mode(profile)
        mode_box.setCurrentIndex(max(0, mode_box.findData(current_mode)))
        mode_layout.addWidget(mode_box)
        mode_description = QLabel()
        mode_description.setObjectName("saveIsolationModeDescription")
        mode_description.setWordWrap(True)
        mode_layout.addWidget(mode_description)
        content_layout.addWidget(mode_group)
        save_profile_group = QGroupBox("Game save profile", content)
        save_profile_group.setObjectName("saveIsolationGameProfileCard")
        save_profile_layout = QVBoxLayout(save_profile_group)
        save_profile_layout.setContentsMargins(12, 10, 12, 10)
        save_profile_layout.setSpacing(6)
        save_profile_row = QHBoxLayout()
        save_profile_combo = QComboBox(save_profile_group)
        save_profile_combo.setObjectName("saveIsolationGameProfileCombo")
        save_profile_combo.setMinimumContentsLength(30)
        save_profile_combo.setSizeAdjustPolicy(
            QComboBox.SizeAdjustPolicy.AdjustToMinimumContentsLengthWithIcon
        )
        save_profile_combo.setMaxVisibleItems(10)
        create_save_profile_button = QPushButton("Create or import…", save_profile_group)
        create_save_profile_button.setObjectName("saveIsolationCreateProfileButton")
        save_profile_row.addWidget(save_profile_combo, 1)
        save_profile_row.addWidget(create_save_profile_button)
        save_profile_layout.addLayout(save_profile_row)
        save_profile_note = QLabel(
            "Every choice has its own save folder. MO2 presents the selected save "
            "to Elden Ring as ER0000.sl2; importing a save leaves its source intact.",
            save_profile_group,
        )
        save_profile_note.setObjectName("saveIsolationSectionNote")
        save_profile_note.setWordWrap(True)
        save_profile_layout.addWidget(save_profile_note)
        content_layout.addWidget(save_profile_group)

        status_group = QGroupBox("Routing status", content)
        status_group.setObjectName("saveIsolationRoutingCard")
        status_layout = QVBoxLayout(status_group)
        status_layout.setContentsMargins(12, 10, 12, 10)
        status_layout.setSpacing(5)
        status_label = QLabel()
        status_label.setObjectName("saveIsolationRoutingStatus")
        status_label.setWordWrap(True)
        status_layout.addWidget(status_label)
        active_path_label = QLabel()
        active_path_label.setObjectName("saveIsolationSelectedPath")
        active_path_label.setWordWrap(True)
        active_path_label.setTextInteractionFlags(
            Qt.TextInteractionFlag.TextSelectableByMouse
        )
        status_layout.addWidget(active_path_label)
        mo2_saves_state = bool(profile.localSavesEnabled())
        mo2_state_label = QLabel(
            "MO2 profile-specific saves: "
            + ("enabled" if mo2_saves_state else "disabled")
        )
        mo2_state_label.setObjectName("saveIsolationMo2SaveState")
        status_layout.addWidget(mo2_state_label)
        content_layout.addWidget(status_group)

        safety_note = QLabel(
            "This only routes save files. It does not change Easy Anti-Cheat or "
            "network mode, and it cannot make modded saves safe for online play. "
            "The global folder can contain separate subfolders for multiple "
            "Steam IDs; identical ER0000.sl2 names under different IDs are "
            "separate account saves."
        )
        safety_note.setObjectName("saveIsolationSafetyNote")
        safety_note.setWordWrap(True)
        content_layout.addWidget(safety_note)

        actions_group = QGroupBox("Folders and tools", content)
        actions_group.setObjectName("saveIsolationActionsCard")
        actions_layout = QVBoxLayout(actions_group)
        actions_layout.setContentsMargins(12, 10, 12, 10)
        actions_layout.setSpacing(6)
        folder_buttons = QHBoxLayout()
        folder_buttons.setSpacing(8)
        open_active_button = QPushButton("Open selected save folder", actions_group)
        open_active_button.setObjectName("saveIsolationOpenSelectedButton")
        open_global_button = QPushButton("Open global save folder", actions_group)
        open_global_button.setObjectName("saveIsolationOpenGlobalButton")
        folder_buttons.addWidget(open_active_button)
        folder_buttons.addWidget(open_global_button)
        actions_layout.addLayout(folder_buttons)

        tool_buttons = QHBoxLayout()
        tool_buttons.setSpacing(8)
        copy_report_button = QPushButton("Copy save-routing diagnostics", actions_group)
        copy_report_button.setObjectName("saveIsolationCopyDiagnosticsButton")
        tool_buttons.addWidget(copy_report_button)

        transfer_button = QPushButton("Transfer one save file…", actions_group)
        transfer_button.setObjectName("saveIsolationTransferButton")
        transfer_button.setToolTip(
            "Choose one .sl2 file and copy it once between MO2 profile, "
            "instance-shared, or global save routes."
        )
        tool_buttons.addWidget(transfer_button)
        actions_layout.addLayout(tool_buttons)
        content_layout.addWidget(actions_group)
        content_layout.addStretch(1)

        button_box = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Save
            | QDialogButtonBox.StandardButton.Close
        )
        button_box.setObjectName("saveIsolationDialogButtons")
        save_button = button_box.button(QDialogButtonBox.StandardButton.Save)
        if save_button is not None:
            save_button.setText("Save route and game profile")
        layout.addWidget(button_box)

        def refresh_details() -> None:
            mode = mode_box.currentData()
            profile_directory = self._profile_directory(profile)
            route_key = f"{self._normalized_path(profile_directory)}|{mode}"
            if dialog.property("_eldenRingSaveUiSaveProfileRouteKey") != route_key:
                selected_id = self._read_save_profile_id_directory(
                    profile_directory, mode
                )
                self._populate_save_profile_combo(
                    save_profile_combo, profile_directory, mode, selected_id
                )
                dialog.setProperty("_eldenRingSaveUiSaveProfileRouteKey", route_key)

            selected_save_id = str(save_profile_combo.currentData() or "")
            selected_save_name = save_profile_combo.currentText()
            local_saves_on = bool(profile.localSavesEnabled())
            active_path = self._save_directory_for_selection(
                profile_directory, mode, selected_save_id
            )
            mode_description.setText(self.ModeDescriptions[mode])
            active_path_label.setText(
                f"<b>Selected save folder:</b><br>{active_path}"
            )
            create_save_profile_button.setEnabled(
                mode in {self.ProfileIsolated, self.InstanceShared}
            )

            if save_profile_combo.property("_eldenRingMissingSaveProfile"):
                status_label.setText(
                    "<b>Save profile unavailable.</b> Choose an available profile "
                    "or the standard saves before saving."
                )
            elif mode in {self.ProfileIsolated, self.InstanceShared} and not local_saves_on:
                status_label.setText(
                    "<b>Launch blocked:</b> enable 'Use profile-specific Save Games' "
                    "for this profile in MO2's Profiles dialog. This prevents a "
                    "modded profile from silently using the global Steam save."
                )
            elif mode == self.GlobalShared and local_saves_on:
                status_label.setText(
                    "<b>Launch blocked:</b> this mode requires profile-specific "
                    "saves to be disabled in MO2 for the active profile."
                )
            elif mode == self.GlobalShared:
                status_label.setText(
                    "<b>Global save selected.</b> This is shared with Steam and "
                    "any other instance using the global save path."
                )
            elif mode == self.InstanceShared:
                status_label.setText(
                    f"<b>Instance-shared modded save selected.</b> "
                    f"Game save profile: {selected_save_name}. Profiles in this "
                    "instance can choose the same named profile to share it."
                )
            else:
                status_label.setText(
                    f"<b>Profile-isolated save selected.</b> "
                    f"Game save profile: {selected_save_name}."
                )

        def open_folder(path: Path) -> None:
            if not path.is_dir():
                QMessageBox.information(
                    dialog,
                    "Save folder not found",
                    f"This folder does not exist yet:\n\n{path}",
                )
                return
            QDesktopServices.openUrl(QUrl.fromLocalFile(str(path)))

        def copy_diagnostics() -> None:
            selected_mode = mode_box.currentData()
            selected_path = self._active_save_directory(profile, selected_mode)
            saved_mode = self._read_mode(profile)
            report = "\n".join(
                (
                    "Elden Ring MO2 save-routing diagnostics",
                    f"Profile: {profile.name()}",
                    f"Profile path: {self._profile_directory(profile)}",
                    f"Saved mode: {saved_mode}",
                    f"Selected mode: {selected_mode}",
                    "MO2 profile-specific saves: "
                    + ("enabled" if bool(profile.localSavesEnabled()) else "disabled"),
                    f"Selected save folder: {selected_path}",
                    f"Profile save folder: {self._profile_save_directory(profile)}",
                    f"Instance-shared folder: {self._instance_shared_directory(profile)}",
                    f"Global save folder: {self._global_save_directory()}",
                )
            )
            QApplication.clipboard().setText(report)
            QMessageBox.information(
                dialog,
                "Diagnostics copied",
                "Save-routing information was copied to the clipboard. It includes "
                "local paths and may include your Windows user name; review it "
                "before sharing.",
            )

        def create_save_profile() -> None:
            mode = mode_box.currentData()
            if mode == self.GlobalShared:
                QMessageBox.information(
                    dialog,
                    "Global Steam save selected",
                    "Named game save profiles are available with profile-isolated "
                    "or instance-shared saves.",
                )
                return
            profile_id = self._display_create_save_profile_dialog(
                self._profile_directory(profile), mode, dialog
            )
            if profile_id is None:
                return
            self._populate_save_profile_combo(
                save_profile_combo,
                self._profile_directory(profile),
                mode,
                profile_id,
            )
            dialog.setProperty(
                "_eldenRingSaveUiSaveProfileRouteKey",
                f"{self._normalized_path(self._profile_directory(profile))}|{mode}",
            )
            refresh_details()
            QMessageBox.information(
                dialog,
                "Save profile created",
                f"'{save_profile_combo.currentText()}' is ready. Select 'Save route "
                "and game profile' to use it for this MO2 profile.",
            )

        def save_mode() -> None:
            mode = mode_box.currentData()
            selected_save_id = str(save_profile_combo.currentData() or "")
            if (
                mode != self.GlobalShared
                and save_profile_combo.property("_eldenRingMissingSaveProfile")
            ):
                QMessageBox.warning(
                    dialog,
                    "Save profile unavailable",
                    "Choose an available named save profile before saving.",
                )
                return
            if mode != self.GlobalShared and selected_save_id:
                available_ids = {
                    profile_id
                    for profile_id, _name in self._read_named_save_profiles(
                        self._profile_directory(profile), mode
                    )
                }
                if selected_save_id not in available_ids:
                    QMessageBox.warning(
                        dialog,
                        "Save profile unavailable",
                        "Choose an available named save profile before saving.",
                    )
                    return
            if mode == self.GlobalShared:
                answer = QMessageBox.warning(
                    dialog,
                    "Use the global Steam save?",
                    "This shares the same save folder with Steam and all MO2 "
                    "instances that use the global path. Modded play can alter "
                    "the vanilla character.\n\n"
                    "Select this only if you deliberately want to share that save. "
                    "Continue?",
                    QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                    QMessageBox.StandardButton.No,
                )
                if answer != QMessageBox.StandardButton.Yes:
                    return
            try:
                self._save_mode(
                    profile,
                    mode,
                    selected_save_id if mode != self.GlobalShared else None,
                )
            except (OSError, UnicodeError, configparser.Error, ValueError) as error:
                QMessageBox.warning(dialog, "Could not save route", str(error))
                return
            refresh_details()
            QMessageBox.information(
                dialog,
                "Save settings saved",
                f"The route and game save profile were saved for MO2 profile "
                f"'{profile.name()}'. Restart MO2 before launching so the new "
                "save mapping takes effect. If the status says launch blocked, "
                "adjust MO2's profile-specific save setting first.",
            )
        mode_box.currentIndexChanged.connect(refresh_details)
        save_profile_combo.currentIndexChanged.connect(refresh_details)
        create_save_profile_button.clicked.connect(create_save_profile)
        open_active_button.clicked.connect(
            lambda: open_folder(
                self._save_directory_for_selection(
                    self._profile_directory(profile),
                    mode_box.currentData(),
                    str(save_profile_combo.currentData() or ""),
                )
            )
        )
        open_global_button.clicked.connect(lambda: open_folder(self._global_save_directory()))
        copy_report_button.clicked.connect(copy_diagnostics)
        transfer_button.clicked.connect(
            lambda: self._display_transfer_dialog(
                self._profile_directory(profile), profile.name(), dialog
            )
        )
        button_box.accepted.connect(save_mode)
        button_box.rejected.connect(dialog.reject)

        refresh_details()
        dialog.exec()


def createPlugin() -> EldenRingMo2SaveIsolation:
    """Return the save-isolation tool instance expected by MO2."""
    return EldenRingMo2SaveIsolation()
