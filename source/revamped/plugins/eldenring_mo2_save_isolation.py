"""Per-profile Elden Ring save routing and safe save-copy controls for MO2."""

from __future__ import annotations

import configparser
import ctypes
from ctypes import wintypes
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import traceback
import uuid

import mobase
from PyQt6.QtCore import QCoreApplication, QEvent, QMetaObject, QObject, QTimer, QUrl, Qt, qInfo
from PyQt6.QtGui import QDesktopServices, QIcon
from PyQt6.QtWidgets import (
    QApplication,
    QComboBox,
    QCheckBox,
    QDialog,
    QDialogButtonBox,
    QFrame,
    QGroupBox,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QCommandLinkButton,
    QPushButton,
    QScrollArea,
    QSizePolicy,
    QStackedWidget,
    QVBoxLayout,
    QWidget,
)


# MO2 installs translations/revamped_<language>.qm; the source text is English.
_TR_CONTEXT = "EldenRingSaveIsolation"


def _tr(text: str) -> str:
    return QCoreApplication.translate(_TR_CONTEXT, text)


class _ProcessEntry32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD),
        ("cntUsage", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD),
        ("th32DefaultHeapID", ctypes.c_size_t),
        ("th32ModuleID", wintypes.DWORD),
        ("cntThreads", wintypes.DWORD),
        ("th32ParentProcessID", wintypes.DWORD),
        ("pcPriClassBase", ctypes.c_long),
        ("dwFlags", wintypes.DWORD),
        ("szExeFile", ctypes.c_wchar * 260),
    ]


def _running_process_names() -> set[str] | None:
    """The lowercased executable names of every running process, read from
    Windows directly (a few milliseconds, no console process), or None when
    the list can't be read."""
    try:
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        snapshot_fn = kernel32.CreateToolhelp32Snapshot
        snapshot_fn.argtypes = (wintypes.DWORD, wintypes.DWORD)
        snapshot_fn.restype = wintypes.HANDLE
        first = kernel32.Process32FirstW
        first.argtypes = (wintypes.HANDLE, ctypes.POINTER(_ProcessEntry32W))
        first.restype = wintypes.BOOL
        following = kernel32.Process32NextW
        following.argtypes = (wintypes.HANDLE, ctypes.POINTER(_ProcessEntry32W))
        following.restype = wintypes.BOOL
        close = kernel32.CloseHandle
        close.argtypes = (wintypes.HANDLE,)
        snapshot = snapshot_fn(0x00000002, 0)  # TH32CS_SNAPPROCESS
        if not snapshot or snapshot == wintypes.HANDLE(-1).value:
            return None
        try:
            entry = _ProcessEntry32W()
            entry.dwSize = ctypes.sizeof(_ProcessEntry32W)
            names: set[str] = set()
            ok = first(snapshot, ctypes.byref(entry))
            while ok:
                names.add(entry.szExeFile.casefold())
                ok = following(snapshot, ctypes.byref(entry))
            # the walk ends with ERROR_NO_MORE_FILES; anything else cut it short
            if ctypes.get_last_error() != 18:
                return None
            return names or None
        finally:
            close(snapshot)
    except (OSError, AttributeError, ValueError):
        return None


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

    @staticmethod
    def mode_description(mode) -> str:
        # Translated on each call: the translator is not loaded yet when the class is defined.
        return {
            EldenRingMo2SaveIsolation.ProfileIsolated: _tr("Separate saves for each MO2 profile. Recommended for modded play."),
            EldenRingMo2SaveIsolation.InstanceShared: _tr("Share modded saves across profiles in this MO2 instance."),
            EldenRingMo2SaveIsolation.GlobalShared: _tr("Use the Steam save in Roaming. This shares saves outside MO2."),
        }.get(mode, "")

    def __init__(self):
        super().__init__()
        self.__organizer = None
        self.__parent_widget = None
        self.__ui_event_filter = None
        self.__refreshing_dialogs = False
        self.__pending_error_reported = set()
        self.__pending_applied_paths = set()
        self.__pending_cleanup_failed_paths = set()

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self.__organizer = organizer
        # Plugin init is called while MO2 is discovering plugins, including in
        # non-Elden-Ring instances. Defer UI hooks until the managed game is
        # known. Profile changes are event-driven so queued choices can be
        # applied without polling the widget tree.
        organizer.onUserInterfaceInitialized(self._initialize_for_managed_game)
        organizer.onProfileChanged(self._on_profile_changed)
        return True

    def _on_profile_changed(self, _old_profile, _new_profile) -> None:
        if self._active_instance_is_elden_ring():
            QTimer.singleShot(0, self._apply_pending_mode_to_active_profile)

    def _initialize_for_managed_game(self, _main_window) -> None:
        if not self._active_instance_is_elden_ring():
            return

        application = QApplication.instance()
        if application is not None:
            self.__ui_event_filter = _Mo2DialogShowEventFilter(self, application)
            application.installEventFilter(self.__ui_event_filter)
            # Handle any dialogs already visible when the plugin finishes loading.
            QTimer.singleShot(0, self._refresh_mo2_dialogs)
        qInfo("Elden Ring MO2: save isolation alpha.77 initialized.")

    def _request_mo2_restart(self) -> None:
        main_window = self.__parent_widget
        if main_window is None or not QMetaObject.invokeMethod(
            main_window,
            "requestRestartFromPlugin",
            Qt.ConnectionType.QueuedConnection,
        ):
            qInfo(
                "Elden Ring MO2 Save Isolation: MO2 restart request was not "
                "accepted; the saved route needs a manual restart."
            )
            self._show_notice(
                main_window,
                _tr("Restart unavailable"),
                _tr("The save route is saved, but MO2 could not start its restart. "
                "Restart MO2 manually before launching Elden Ring."),
                "warning",
            )

    def _handle_dialog_show_event(self, watched, event) -> bool:
        if (
            event.type() in (QEvent.Type.Show, QEvent.Type.ShowToParent)
            and isinstance(watched, QDialog)
            and not self.__refreshing_dialogs
        ):
            # Run after Qt has completed this dialog's first layout pass. The
            # event already identifies the dialog, so avoid rescanning every
            # top-level widget after each dialog opens.
            QTimer.singleShot(
                0, lambda target=watched: self._refresh_mo2_dialogs(target)
            )
        return False

    def name(self) -> str:
        return "Elden Ring MO2 Save Isolation"

    def localizedName(self) -> str:
        return self.name()

    def author(self) -> str:
        return "ArialSenki"

    def description(self) -> str:
        return (
            _tr("Choose Elden Ring save routes and named game save profiles, inspect "
            "the active save path, and import or transfer .sl2 saves.")
        )

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(0, 5, 0, 77)

    def requirements(self):
        return [mobase.PluginRequirementFactory.gameDependency({"ELDEN RING"})]

    def settings(self) -> list[mobase.PluginSetting]:
        return []

    def enabledByDefault(self) -> bool:
        return True

    def displayName(self) -> str:
        return _tr("Save Isolation")

    def tooltip(self) -> str:
        return _tr("Select the Elden Ring save route and named game save for this profile.")

    def icon(self) -> QIcon:
        return QIcon()

    def setParentWidget(self, widget) -> None:
        self.__parent_widget = widget

    def _show_notice(
        self, parent, title: str, message: str, kind: str = "info"
    ) -> None:
        dialog = QDialog(parent or self.__parent_widget)
        dialog.setObjectName("EldenRingSaveIsolationNoticeDialog")
        dialog.setWindowTitle(title)
        dialog.setMinimumWidth(440)
        dialog.setMaximumWidth(660)
        layout = QVBoxLayout(dialog)
        layout.setContentsMargins(16, 16, 16, 14)
        layout.setSpacing(12)

        header = QFrame(dialog)
        header.setObjectName("saveIsolationPromptHeader")
        header_layout = QHBoxLayout(header)
        header_layout.setContentsMargins(12, 10, 12, 10)
        header_layout.setSpacing(10)
        icon_text = {
            "info": "i",
            "warning": "!",
            "error": "×",
            "success": "✓",
        }.get(kind, "i")
        icon = QLabel(icon_text, header)
        icon.setObjectName("saveIsolationPromptIcon")
        icon.setProperty("promptKind", kind)
        icon.setAlignment(Qt.AlignmentFlag.AlignCenter)
        icon.setFixedSize(28, 28)
        heading = QLabel(title, header)
        heading.setObjectName("saveIsolationPromptTitle")
        heading.setWordWrap(True)
        header_layout.addWidget(icon, 0, Qt.AlignmentFlag.AlignTop)
        header_layout.addWidget(heading, 1)
        layout.addWidget(header)

        body = QLabel(message, dialog)
        body.setObjectName("saveIsolationPromptMessage")
        body.setWordWrap(True)
        body.setTextFormat(Qt.TextFormat.PlainText)
        body.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        layout.addWidget(body)

        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok, parent=dialog)
        buttons.setObjectName("saveIsolationPromptButtons")
        ok_button = buttons.button(QDialogButtonBox.StandardButton.Ok)
        if ok_button is not None:
            ok_button.setObjectName("saveIsolationPromptOkButton")
            ok_button.setDefault(True)
        buttons.accepted.connect(dialog.accept)
        layout.addWidget(buttons)
        dialog.adjustSize()
        dialog.exec()

    def _ask_confirmation(
        self,
        parent,
        title: str,
        message: str,
        accept_text: str,
        reject_text: str,
        kind: str = "warning",
        default_accept: bool = True,
    ) -> bool:
        dialog = QDialog(parent or self.__parent_widget)
        dialog.setObjectName("EldenRingSaveIsolationConfirmDialog")
        dialog.setWindowTitle(title)
        dialog.setMinimumWidth(500)
        dialog.setMaximumWidth(720)
        layout = QVBoxLayout(dialog)
        layout.setContentsMargins(16, 16, 16, 14)
        layout.setSpacing(12)

        header = QFrame(dialog)
        header.setObjectName("saveIsolationPromptHeader")
        header_layout = QHBoxLayout(header)
        header_layout.setContentsMargins(12, 10, 12, 10)
        header_layout.setSpacing(10)
        icon_text = {"info": "i", "warning": "!", "error": "×"}.get(kind, "!")
        icon = QLabel(icon_text, header)
        icon.setObjectName("saveIsolationPromptIcon")
        icon.setProperty("promptKind", kind)
        icon.setAlignment(Qt.AlignmentFlag.AlignCenter)
        icon.setFixedSize(28, 28)
        heading = QLabel(title, header)
        heading.setObjectName("saveIsolationPromptTitle")
        heading.setWordWrap(True)
        header_layout.addWidget(icon, 0, Qt.AlignmentFlag.AlignTop)
        header_layout.addWidget(heading, 1)
        layout.addWidget(header)

        body = QLabel(message, dialog)
        body.setObjectName("saveIsolationPromptMessage")
        body.setWordWrap(True)
        body.setTextFormat(Qt.TextFormat.PlainText)
        body.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        layout.addWidget(body)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.NoButton, parent=dialog
        )
        buttons.setObjectName("saveIsolationConfirmButtons")
        accept_button = buttons.addButton(
            accept_text, QDialogButtonBox.ButtonRole.AcceptRole
        )
        accept_button.setObjectName("saveIsolationConfirmAcceptButton")
        accept_button.setDefault(default_accept)
        reject_button = buttons.addButton(
            reject_text, QDialogButtonBox.ButtonRole.RejectRole
        )
        reject_button.setObjectName("saveIsolationConfirmRejectButton")
        reject_button.setDefault(not default_accept)
        reject_button.setAutoDefault(False)
        accept_button.clicked.connect(dialog.accept)
        reject_button.clicked.connect(dialog.reject)
        layout.addWidget(buttons)
        dialog.adjustSize()
        return dialog.exec() == QDialog.DialogCode.Accepted

    def _dialog_ui_enabled(self) -> bool:
        return self._active_instance_is_elden_ring()

    @staticmethod
    def _dialog_mode_card(
        parent,
        object_name: str,
        title: str = _tr("Elden Ring profile options"),
    ):
        card = QGroupBox(title, parent)
        card.setObjectName(object_name)
        layout = QVBoxLayout(card)
        layout.setContentsMargins(12, 10, 12, 10)
        layout.setSpacing(7)

        mode_combo = QComboBox(card)
        mode_combo.setObjectName(f"{object_name}_mode")
        mode_combo.setMaxVisibleItems(8)
        mode_combo.view().setMinimumWidth(max(320, mode_combo.width()))
        selection_row = QHBoxLayout()
        selection_row.setSpacing(8)
        selection_row.addWidget(QLabel(_tr("Save mode"), card))
        mode_combo.setMinimumContentsLength(24)
        mode_combo.setSizeAdjustPolicy(
            QComboBox.SizeAdjustPolicy.AdjustToMinimumContentsLengthWithIcon
        )
        selection_row.addWidget(mode_combo, 1)
        layout.addLayout(selection_row)

        choices = (
            (EldenRingMo2SaveIsolation.ProfileIsolated, _tr("Separate saves per MO2 profile")),
            (EldenRingMo2SaveIsolation.InstanceShared, _tr("Share modded saves in this instance")),
            (EldenRingMo2SaveIsolation.GlobalShared, _tr("Use the global Steam save")),
        )
        for mode, title in choices:
            mode_combo.addItem(title, mode)

        detail = QLabel(card)
        detail.setObjectName(f"{object_name}_description")
        detail.setWordWrap(True)
        detail.setContentsMargins(4, 0, 4, 0)
        layout.addWidget(detail)

        status = QLabel(card)
        status.setObjectName(f"{object_name}_status")
        status.setWordWrap(True)
        layout.addWidget(status)

        def update_description(index: int) -> None:
            mode = mode_combo.itemData(index)
            detail.setText(EldenRingMo2SaveIsolation.mode_description(mode))

        mode_combo.currentIndexChanged.connect(update_description)
        update_description(mode_combo.currentIndex())
        return card, mode_combo, detail, status, None

    @staticmethod
    def _select_dialog_mode(mode_combo, mode: str) -> None:
        index = mode_combo.findData(mode)
        if index < 0:
            index = mode_combo.findData(EldenRingMo2SaveIsolation.ProfileIsolated)
        mode_combo.blockSignals(True)
        mode_combo.setCurrentIndex(index)
        mode_combo.blockSignals(False)
        detail = mode_combo.parentWidget().findChild(
            QLabel, f"{mode_combo.objectName().removesuffix('_mode')}_description"
        )
        if detail is not None:
            detail.setText(
                EldenRingMo2SaveIsolation.mode_description(mode_combo.currentData())
            )

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
                    _tr("MO2 profile saves — {profile} (standard)").format(profile=profile_directory.name),
                    profile_route,
                    profile_directory / "saves",
                )
            )
            for profile_id, profile_name in cls._read_named_save_profiles(
                profile_directory, cls.ProfileIsolated
            ):
                routes.append(
                    (
                        _tr("MO2 game save profile — {profile}: {save}").format(profile=profile_directory.name, save=profile_name),
                        f"{profile_route}:save:{profile_id}",
                        profile_directory / cls.SaveProfilesDirectoryName / profile_id,
                    )
                )

        shared_root = profiles_root.parent / "Elden Ring Shared Saves"
        routes.append(
            (
                _tr("Shared saves for this MO2 instance (standard)"),
                cls.InstanceShared,
                shared_root,
            )
        )
        for profile_id, profile_name in cls._read_named_save_profiles(
            current_profile_directory, cls.InstanceShared
        ):
            routes.append(
                (
                    _tr("Shared game save profile — {save}").format(save=profile_name),
                    f"{cls.InstanceShared}:save:{profile_id}",
                    shared_root / cls.SaveProfilesDirectoryName / profile_id,
                )
            )
        routes.append(
            (
                _tr("Global Elden Ring / Steam saves"),
                cls.GlobalShared,
                cls._global_save_directory(),
            )
        )
        return routes, current_id
    @staticmethod
    def _transfer_blocked_reason() -> str:
        names = _running_process_names()
        if names is not None:
            running = "eldenring.exe" in names
        else:
            try:
                result = subprocess.run(
                    ["tasklist", "/FI", "IMAGENAME eq eldenring.exe", "/FO", "CSV", "/NH"],
                    capture_output=True,
                    text=True,
                    encoding="utf-8",
                    errors="replace",
                    timeout=5,
                    check=False,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
                )
                running = result.returncode != 0 or any(
                    line.lstrip().casefold().startswith('"eldenring.exe"')
                    for line in result.stdout.splitlines()
                )
            except (OSError, subprocess.SubprocessError):
                running = True
        if running:
            return _tr("Elden Ring is running, or MO2 could not confirm that it is "
                       "closed. Close the game and try again; nothing was copied.")

        appdata = os.environ.get("APPDATA")
        recovery = Path(appdata) / ".MO2RevampedSaveRecovery" if appdata else None
        try:
            # a session still hiding account folders is pending; one that only
            # kept unrouted output for review is not
            pending = recovery is not None and recovery.is_dir() and any(
                session.is_dir()
                and any(
                    child.name not in ("unrouted-global", "session.json")
                    for child in session.iterdir()
                )
                for instance in recovery.iterdir()
                if instance.is_dir()
                for session in instance.iterdir()
            )
        except OSError:
            pending = True
        if pending:
            return _tr("The Steam saves of an earlier launch are still waiting to be "
                       "restored. Start MO2 again with Elden Ring closed so it can "
                       "restore them, then copy the save; nothing was copied.")
        return ""

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
    def _list_transferable_saves(
        cls, root: Path, suffixes: tuple[str, ...] = (".sl2",)
    ) -> list[Path]:
        if not root.is_dir():
            return []

        saves = []
        seen = set()
        for current, directories, filenames in os.walk(root, followlinks=False):
            current_path = Path(current)
            # the game reads <account>/ER0000.* (or loose files in the root);
            # deeper files belong to named save profiles or backups kept
            # inside the shared route, which are routes of their own
            if current_path != root:
                directories[:] = []
            directories[:] = [
                name
                for name in directories
                if not cls._is_reparse_point(current_path / name)
                and name.casefold() != cls.SaveProfilesDirectoryName
                and not name.startswith(".")
            ]
            for filename in filenames:
                if Path(filename).suffix.casefold() not in suffixes:
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

    def _create_instance_save_sources(
        self, suffixes: tuple[str, ...] = (".sl2",)
    ) -> list[tuple[str, Path, str]]:
        """Find Elden Ring saves in Steam and this MO2 instance only."""
        discovered_roots: list[tuple[str, Path]] = []

        try:
            active_profile = self.__organizer.profile()
            if active_profile is not None and self._active_instance_is_elden_ring():
                active_profiles_root = Path(active_profile.absolutePath()).parent
                if active_profiles_root.is_dir():
                    discovered_roots.append(
                        (_tr("Current MO2 instance"), active_profiles_root)
                    )
        except (AttributeError, OSError, RuntimeError):
            pass

        sources: list[tuple[str, Path, str]] = []
        seen_files: set[str] = set()

        def add_save_files(label: str, root: Path) -> None:
            for save_path in self._list_transferable_saves(root, suffixes):
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
                    _tr("MO2 — {profile} / standard saves").format(profile=display_profile),
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
            add_save_files(_tr("MO2 — {instance} / shared saves").format(instance=display_instance), shared_root)
            for profile_id, profile_name in self._read_named_save_profiles(
                shared_profile_directory, self.InstanceShared
            ):
                add_save_files(
                    _tr("MO2 — {instance} / shared / {save}").format(
                        instance=display_instance, save=profile_name
                    ),
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
                _tr("{count} .sl2 save(s) found. The selected file will be "
                "copied after the new instance opens. Existing destination files "
                "are never replaced.").format(count=found_saves)
            )
        else:
            copy_checkbox.setChecked(False)
            status_label.setText(
                _tr("No .sl2 saves were found in Steam or the detected Elden Ring "
                "MO2 profiles.")
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
                    _tr("Copy an existing .sl2 save into this named profile")
                )
            else:
                copy_checkbox.setText(
                    _tr("Copy an existing .sl2 save into the new instance")
                )
            # Global mode plays from the Steam folder; nothing is copied into it
            copy_checkbox.setEnabled(
                mode != self.GlobalShared
                and source_combo is not None
                and source_combo.count() > 0
            )
            if source_combo is not None:
                source_combo.setEnabled(
                    copy_checkbox.isChecked() and copy_checkbox.isEnabled()
                )

        if note is not None:
            if not supports_named_profiles:
                note.setText(
                    _tr("Named game save profiles require profile-isolated or "
                    "instance-shared saves. Global mode uses the Steam save folder.")
                )
            elif checkbox.isChecked():
                note.setText(
                    _tr("This name appears in Tools > Elden Ring Save Isolation. If "
                    "you choose a starting save below, it will be copied into "
                    "this named save profile. "
                    "Elden Ring loads the selected file as ER0000.sl2.")
                )
            else:
                note.setText(
                    _tr("Optional. Leave this unchecked to use standard saves. "
                    "You can create named game save profiles later from Tools > "
                    "Elden Ring Save Isolation.")
                )

        if status_label is not None and source_combo is not None:
            if source_combo.count():
                destination = (
                    _tr("the named game save profile")
                    if checkbox.isChecked()
                    else _tr("the new instance's standard save folder")
                )
                status_label.setText(
                    _tr("{count} .sl2 save(s) found. The selected file "
                    "will be copied to {destination} after the new instance opens. "
                    "Existing destination files are never replaced.").format(
                        count=source_combo.count(), destination=destination
                    )
                )
            else:
                status_label.setText(
                    _tr("No .sl2 saves were found in Steam or the detected Elden Ring "
                    "MO2 profiles.")
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

    def _refresh_save_checks(self) -> None:
        diagnose = getattr(self, "_save_diagnose", None)
        if diagnose is not None:
            try:
                diagnose._invalidate()
            except (AttributeError, RuntimeError):
                pass

    def _instance_base(self) -> Path | None:
        try:
            return Path(self.__organizer.basePath())
        except (AttributeError, RuntimeError, TypeError):
            return None

    @staticmethod
    def _auto_backup_root(route_root: Path, instance_base: Path, is_global: bool) -> Path:
        # the same place the game plugin writes its automatic backups to
        if is_global:
            return instance_base / "Elden Ring Global Save Backups" / "auto"
        return route_root.parent / f"{route_root.name} Backups" / "auto"

    @staticmethod
    def _backup_label(backup: Path) -> str:
        try:
            stamp = datetime.strptime(backup.name[:15], "%Y%m%d-%H%M%S")
            when = stamp.strftime("%Y-%m-%d %H:%M:%S")
        except ValueError:
            when = backup.name
        files = [file for file in backup.rglob("*") if file.is_file()]
        accounts = sorted({file.relative_to(backup).parts[0] for file in files if len(file.relative_to(backup).parts) > 1})
        size = sum(file.stat().st_size for file in files)
        return _tr("{when} — {count} file(s), {size} MB, account {accounts}").format(
            when=when,
            count=len(files),
            size=f"{size / (1024 * 1024):.1f}",
            accounts=", ".join(accounts) or "—",
        )

    def _restore_backup(self, backup: Path, route_root: Path, replaced_root: Path) -> Path | None:
        """Puts a backup's saves into the route as one step: the backup's files
        are copied beside their destinations first, then the route's files of
        the same save families move to replaced_root/<now> (also across
        drives) and the copies take their place. If anything fails, the moved
        files go back and the copies are removed. Returns the folder holding
        the replaced files, or None when nothing was replaced."""
        backup_files = [file for file in backup.rglob("*") if file.is_file()]
        if not backup_files:
            raise OSError(_tr("The backup is empty."))

        # a save and its .bak are one family (ER0000.sl2, ER0000.co2, a
        # Convergence ER0000.cnv...); only the families the backup holds are
        # replaced, in the account folders it has, so a backup of the vanilla
        # save leaves a Seamless Co-op or Convergence save where it is
        def family(name: str) -> str:
            name = name.casefold()
            return name[:-4] if name.endswith(".bak") else name

        families: dict[Path, set[str]] = {}
        for file in backup_files:
            relative = file.relative_to(backup)
            families.setdefault(relative.parent, set()).add(family(relative.name))

        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        replaced = replaced_root / stamp
        suffix = 2
        while replaced.exists():
            replaced = replaced_root / f"{stamp}-{suffix}"
            suffix += 1

        staged: list[tuple[Path, Path]] = []  # (temporary copy, destination)
        moved: list[tuple[Path, Path]] = []  # (original place, place in replaced)
        try:
            for file in backup_files:
                destination = route_root / file.relative_to(backup)
                destination.parent.mkdir(parents=True, exist_ok=True)
                temporary = destination.with_name(f".{destination.name}.restoring")
                shutil.copy2(file, temporary)
                staged.append((temporary, destination))

            for account, account_families in families.items():
                folder = route_root / account
                if not folder.is_dir():
                    continue
                for current in list(folder.iterdir()):
                    if (
                        current.is_file()
                        and not self._is_reparse_point(current)
                        and current.name.casefold().startswith("er0000.")
                        and family(current.name) in account_families
                    ):
                        target = replaced / account / current.name
                        target.parent.mkdir(parents=True, exist_ok=True)
                        shutil.move(str(current), str(target))
                        moved.append((current, target))

            for temporary, destination in staged:
                os.replace(temporary, destination)
            staged = []
        except OSError as error:
            rollback_problems = []
            for temporary, _destination in staged:
                try:
                    temporary.unlink(missing_ok=True)
                except OSError as cleanup_error:
                    rollback_problems.append(str(cleanup_error))
            for original, target in reversed(moved):
                try:
                    if not original.exists():
                        shutil.move(str(target), str(original))
                except OSError as move_error:
                    rollback_problems.append(str(move_error))
            if rollback_problems:
                raise OSError(
                    _tr("{error}\n\nThe route could not be put back as it was. The "
                        "saves that were set aside are in:\n{path}").format(
                        error=error, path=replaced
                    )
                ) from error
            raise
        return replaced if moved else None

    @staticmethod
    def _prune_replaced(replaced_root: Path, keep: int = 10) -> None:
        # the saves a restore set aside: the newest ones are kept
        try:
            folders = sorted(
                (entry for entry in replaced_root.iterdir() if entry.is_dir()),
                key=lambda entry: entry.name,
            )
        except OSError:
            return
        for old in folders[:-keep]:
            shutil.rmtree(old, ignore_errors=True)

    def _display_restore_dialog(self, profile_directory: Path, parent=None) -> None:
        base = self._instance_base()
        if base is None:
            return
        dialog = QDialog(parent or self.__parent_widget)
        dialog.setObjectName("EldenRingSaveRestoreDialog")
        dialog.setWindowTitle(_tr("Restore an Elden Ring save backup"))
        dialog.setMinimumSize(720, 460)
        dialog.resize(820, 520)
        layout = QVBoxLayout(dialog)
        layout.setContentsMargins(16, 16, 16, 14)
        layout.setSpacing(9)

        intro = QLabel(
            _tr("Before each launch MO2 copies the saves of the route the game "
            "will use, when they changed since the last copy. Choose a route and "
            "a copy to bring back; the saves it replaces are kept in the "
            "backups' \"replaced\" folder."),
            dialog,
        )
        intro.setWordWrap(True)
        layout.addWidget(intro)

        routes, current_route = self._save_transfer_routes(profile_directory)
        route_row = QHBoxLayout()
        route_row.addWidget(QLabel(_tr("Save route:"), dialog))
        route_combo = QComboBox(dialog)
        for label, route_id, route_path in routes:
            route_combo.addItem(label, (route_id, str(route_path)))
            if route_id == current_route:
                route_combo.setCurrentIndex(route_combo.count() - 1)
        route_row.addWidget(route_combo, 1)
        layout.addLayout(route_row)

        backup_list = QListWidget(dialog)
        backup_list.setObjectName("saveRestoreBackupList")
        layout.addWidget(backup_list, 1)
        status = QLabel(dialog)
        status.setWordWrap(True)
        layout.addWidget(status)

        buttons = QHBoxLayout()
        open_button = QPushButton(_tr("Open backups folder"), dialog)
        restore_button = QPushButton(_tr("Restore selected backup"), dialog)
        close_button = QPushButton(_tr("Close"), dialog)
        buttons.addWidget(open_button)
        buttons.addStretch(1)
        buttons.addWidget(restore_button)
        buttons.addWidget(close_button)
        layout.addLayout(buttons)

        def selected_route() -> tuple[str, Path, Path]:
            route_id, route_path = route_combo.currentData()
            route_root = Path(route_path)
            root = self._auto_backup_root(route_root, base, route_id == self.GlobalShared)
            return route_id, route_root, root

        def refresh() -> None:
            backup_list.clear()
            _route_id, _route_root, root = selected_route()
            backups = []
            if root.is_dir():
                backups = sorted(
                    (entry for entry in root.iterdir() if entry.is_dir() and not entry.name.startswith(".")),
                    key=lambda entry: entry.name,
                    reverse=True,
                )
            for backup in backups:
                try:
                    label = self._backup_label(backup)
                except OSError:
                    continue
                item = QListWidgetItem(label, backup_list)
                item.setData(Qt.ItemDataRole.UserRole, str(backup))
                item.setToolTip(str(backup))
            if backups:
                backup_list.setCurrentRow(0)
                status.setText(_tr("{count} backup(s), newest first.").format(count=len(backups)))
            else:
                status.setText(_tr("No automatic backups of this route yet. MO2 makes one "
                                   "before the next launch that uses it."))
            restore_button.setEnabled(bool(backups))
            open_button.setEnabled(root.is_dir())

        def restore() -> None:
            item = backup_list.currentItem()
            if item is None:
                return
            busy_reason = self._transfer_blocked_reason()
            if busy_reason:
                self._show_notice(dialog, _tr("Backup not restored"), busy_reason, "warning")
                return
            route_id, route_root, root = selected_route()
            backup = Path(item.data(Qt.ItemDataRole.UserRole))
            if not self._ask_confirmation(
                dialog,
                _tr("Restore this backup?"),
                _tr("{backup}\n\nThe current saves of this route are moved to the "
                "backups' \"replaced\" folder first, so nothing is lost.\n\n"
                "Route:\n{route}").format(backup=item.text(), route=route_root),
                _tr("Restore"),
                _tr("Cancel"),
                "warning",
                default_accept=False,
            ):
                return
            try:
                replaced = self._restore_backup(backup, route_root, root.parent / "replaced")
                self._prune_replaced(root.parent / "replaced")
                self._refresh_save_checks()
            except OSError as error:
                self._show_notice(
                    dialog,
                    _tr("Backup not restored"),
                    _tr("The backup could not be restored.\n\n{error}").format(error=error),
                    "warning",
                )
                return
            qInfo(
                f"Elden Ring MO2 Save Isolation: restored backup '{backup}' into '{route_root}'."
            )
            note = (
                _tr("\n\nThe saves it replaced are in:\n{path}").format(path=replaced)
                if replaced is not None
                else ""
            )
            self._show_notice(
                dialog,
                _tr("Backup restored"),
                _tr("The save route now holds the selected backup.") + note,
                "success",
            )
            refresh()

        route_combo.currentIndexChanged.connect(lambda _index: refresh())
        restore_button.clicked.connect(restore)
        open_button.clicked.connect(
            lambda: QDesktopServices.openUrl(QUrl.fromLocalFile(str(selected_route()[2])))
        )
        close_button.clicked.connect(dialog.accept)
        refresh()
        dialog.exec()

    def _display_transfer_dialog(
        self, profile_directory: Path, profile_name: str, parent=None
    ) -> None:
        dialog = QDialog(parent or self.__parent_widget)
        dialog.setObjectName("EldenRingSaveTransferDialog")
        dialog.setWindowTitle(_tr("Elden Ring Save Transfer"))
        dialog.setMinimumSize(780, 520)
        dialog.resize(900, 600)
        dialog.setSizeGripEnabled(True)

        layout = QVBoxLayout(dialog)
        layout.setContentsMargins(16, 16, 16, 14)
        layout.setSpacing(9)

        header = QFrame(dialog)
        header.setObjectName("saveTransferHeader")
        header_layout = QVBoxLayout(header)
        header_layout.setContentsMargins(13, 10, 13, 10)
        header_layout.setSpacing(3)
        _add_circular_heading(
            header_layout,
            header,
            _tr("Transfer an Elden Ring save"),
            "saveTransferTitle",
            ":/MO/gui/mainwindow/files/save.svg",
            "saveTransferHeaderIcon",
            30,
            "S",
        )
        intro = QLabel(
            _tr("Copy one save between storage routes: a vanilla .sl2 or a Seamless "
            "Co-op .co2. The selected MO2 profile is {profile}. Its account folder is "
            "preserved, and the copy is named ER0000 at the destination with the "
            "same extension.").format(profile=profile_name),
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
        route_heading = QLabel(_tr("Save routes"), route_card)
        route_heading.setObjectName("saveTransferSectionHeading")
        route_layout.addWidget(route_heading)

        source_row = QHBoxLayout()
        source_row.setSpacing(10)
        source_row.addWidget(QLabel(_tr("Copy from:"), route_card))
        source_combo = QComboBox(route_card)
        source_combo.setObjectName("saveTransferSourceRoute")
        for label, route_id, _path in routes:
            source_combo.addItem(label, route_id)
        source_row.addWidget(source_combo, 1)
        route_layout.addLayout(source_row)

        destination_row = QHBoxLayout()
        destination_row.setSpacing(10)
        destination_row.addWidget(QLabel(_tr("Copy to:"), route_card))
        destination_combo = QComboBox(route_card)
        destination_combo.setObjectName("saveTransferDestinationRoute")
        destination_combo.addItem(_tr("Choose a destination…"), "")
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
            _tr("Destination folder: choose a route above."), path_card
        )
        destination_folder_label.setObjectName("saveTransferDestinationPath")
        destination_folder_label.setWordWrap(True)
        destination_folder_label.setTextInteractionFlags(
            Qt.TextInteractionFlag.TextSelectableByMouse
        )
        path_layout.addWidget(destination_folder_label)
        layout.addWidget(path_card)

        list_heading = QLabel(_tr("Save files in the selected source"), dialog)
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
        copy_button = QPushButton(_tr("Copy selected save"))
        copy_button.setObjectName("saveTransferCopyButton")
        close_button = QPushButton(_tr("Close"))
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
            source_folder_label.setText(_tr("Source folder: {path}").format(path=source_root))
            destination_folder_label.setText(
                _tr("Destination folder: {path}").format(path=destination_root)
                if destination_root is not None
                else _tr("Destination folder: choose a route above.")
            )

            save_list.clear()
            files = self._list_transferable_saves(source_root, (".sl2", ".co2"))
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
                    _tr("Source and destination are the same folder. Nothing will be copied.")
                )
            elif not files:
                status_label.setText(_tr("No .sl2 or .co2 save files were found in this source folder."))
            else:
                status_label.setText(
                    _tr("{count} save file(s) found. Select exactly one file to copy.").format(count=len(files))
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
            if source_path.suffix.casefold() not in (".sl2", ".co2") or self._is_reparse_point(
                source_path
            ):
                self._show_notice(
                    dialog,
                    _tr("Invalid save file"),
                    _tr("Choose a regular .sl2 or .co2 save file."),
                    "warning",
                )
                return
            try:
                source_root_resolved = source_root.resolve()
                source_path_resolved = source_path.resolve(strict=True)
                relative_path = source_path_resolved.relative_to(source_root_resolved)
                destination_root_resolved = destination_root.resolve()
            except (OSError, ValueError) as error:
                self._show_notice(
                    dialog,
                    _tr("Save path could not be resolved"),
                    _tr("The selected save is no longer inside its source folder. Nothing was copied.\n\n{error}").format(error=error),
                    "warning",
                )
                return

            # a running game, or Steam saves still hidden by an unfinished
            # launch, would undo or misplace the copy
            busy_reason = self._transfer_blocked_reason()
            if busy_reason:
                self._show_notice(dialog, _tr("Save not copied"), busy_reason, "warning")
                return

            destination_relative_path = (
                relative_path.parent / f"ER0000{source_path_resolved.suffix.lower()}"
            )
            destination_path = destination_root_resolved / destination_relative_path
            if source_path_resolved == destination_path.resolve():
                self._show_notice(
                    dialog,
                    _tr("Same save file"),
                    _tr("Source and destination point to the same file. Nothing was copied."),
                    "info",
                )
                return
            if destination_path.exists():
                if not destination_path.is_file() or self._is_reparse_point(destination_path):
                    self._show_notice(
                        dialog,
                        _tr("Destination is not a save file"),
                        _tr("The destination cannot be replaced safely:\n\n{path}").format(path=destination_path),
                        "warning",
                    )
                    return
                try:
                    if self._same_file_contents(source_path_resolved, destination_path):
                        self._show_notice(
                            dialog,
                            _tr("Save already matches"),
                            _tr("The destination already contains an identical save. No second copy was made."),
                            "info",
                        )
                        return
                except OSError as error:
                    self._show_notice(
                        dialog, _tr("Could not compare saves"), str(error), "warning"
                    )
                    return
                prompt = (
                    _tr("The destination already has a different save. Replace that file?")
                )
            else:
                prompt = (
                    _tr("Copy this one save file to the selected route? Any missing ID folder will be created.")
                )
            accepted = self._ask_confirmation(
                dialog,
                _tr("Copy one save file"),
                _tr("{prompt}\n\nFrom:\n{source}\n\n"
                "To:\n{destination}\n\n"
                "Close Elden Ring before copying. This operation copies only "
                "the selected save and its .bak, if it has one.").format(
                    prompt=prompt, source=source_path_resolved, destination=destination_path
                ),
                _tr("Replace save") if destination_path.exists() else _tr("Copy save"),
                _tr("Cancel"),
                "warning",
                default_accept=False,
            )
            if not accepted:
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
                            _tr("\n\nThe previous destination is preserved at:\n"
                            "{path}\n\nRestore error: {error}").format(path=backup_path, error=restore_error)
                        )
                self._show_notice(
                    dialog,
                    _tr("Save copy failed"),
                    _tr("The selected file could not be copied.\n\n{error}").format(error=error)
                    + recovery_note,
                    "warning",
                )
                return
            finally:
                if temporary_path is not None:
                    try:
                        temporary_path.unlink(missing_ok=True)
                    except OSError:
                        pass

            # the destination's old .bak belongs to the replaced character: it
            # goes to the backups, and the source's .bak comes along
            bak_note = ""
            try:
                destination_bak = destination_path.with_name(destination_path.name + ".bak")
                source_bak = source_path_resolved.with_name(source_path_resolved.name + ".bak")
                bak_is_plain = not os.path.lexists(destination_bak) or (
                    destination_bak.is_file() and not self._is_reparse_point(destination_bak)
                )
                if bak_is_plain and destination_bak.is_file():
                    bak_backup = (
                        backup_path.with_name(backup_path.name + ".bak")
                        if backup_path is not None
                        else destination_root_resolved.parent
                        / (destination_root_resolved.name + " Backups")
                        / destination_relative_path.parent
                        / f"ER0000.{datetime.now().strftime('%Y%m%d-%H%M%S-%f')}{destination_path.suffix}.bak"
                    )
                    bak_backup.parent.mkdir(parents=True, exist_ok=True)
                    os.replace(destination_bak, bak_backup)
                if (
                    bak_is_plain
                    and source_bak.is_file()
                    and not self._is_reparse_point(source_bak)
                ):
                    shutil.copy2(source_bak, destination_bak)
            except OSError as error:
                bak_note = _tr("\n\nThe save was copied, but its .bak could not be "
                               "updated: {error}").format(error=error)

            backup_note = (
                _tr("\n\nThe previous destination was moved to:\n{path}").format(path=backup_path)
                if backup_path is not None
                else ""
            )
            self._show_notice(
                dialog,
                _tr("Save copied"),
                _tr("Copied and prepared the game save:\n{path}").format(path=destination_path)
                + backup_note
                + bak_note,
                "success",
            )
            self._refresh_save_checks()
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

    def _set_native_save_group_plugin_owned(
        self, dialog, plugin_controls_active: bool
    ) -> None:
        # The native group is part of MO2's new-instance wizard. Profiles no
        # longer has a save-mode panel; all post-creation routing lives in Tools.
        native_save_group = dialog.findChild(QGroupBox, "eldenRingSaveIsolationGroup")
        if native_save_group is not None:
            native_save_group.setProperty(
                "_eldenRingSaveUiPluginControlsActive",
                bool(plugin_controls_active),
            )
            native_save_group.setVisible(not plugin_controls_active)

    def _restore_native_dialog(self, dialog) -> None:
        self._set_native_save_group_plugin_owned(dialog, False)

        native_transfer = dialog.findChild(QPushButton, "transferButton")
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
            native_transfer.setProperty("_eldenRingOriginalTransferVisible", None)
            native_transfer.setProperty("_eldenRingOriginalTransferEnabled", None)
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
            title="",
        )
        card.setSizePolicy(
            QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Maximum
        )
        if card.layout() is not None:
            card.layout().setContentsMargins(0, 0, 0, 0)
            card.layout().setSpacing(5)
        insertion_index = layout.indexOf(save_checkbox)
        if insertion_index < 0:
            layout.addWidget(card)
        else:
            layout.insertWidget(insertion_index, card)

        scope = QLabel(
            _tr("Applies only to Elden Ring profiles in this new instance."), card
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
            _tr("Saved for the new instance's Default profile. Local routes apply when "
            "that instance starts; restart MO2 only when switching to or from the "
            "global Steam save.")
        )

        game_save_profile_heading = QLabel(_tr("Game save profile"), card)
        game_save_profile_heading.setObjectName("eldenRingWizardGameSaveProfileHeading")
        game_save_profile_heading.setStyleSheet("font-weight: 600;")
        card.layout().addWidget(game_save_profile_heading)

        game_save_profile_row = QHBoxLayout()
        game_save_profile_checkbox = QCheckBox(
            _tr("Create a named game save profile"), card
        )
        game_save_profile_checkbox.setObjectName(
            self.CreateInstanceGameSaveProfileCheckboxName
        )
        game_save_profile_checkbox.setToolTip(
            _tr("Create a separate named save choice managed in Tools > "
            "Elden Ring Save Isolation.")
        )
        game_save_profile_name = QLineEdit(card)
        game_save_profile_name.setObjectName(
            self.CreateInstanceGameSaveProfileNameEditName
        )
        game_save_profile_name.setPlaceholderText(_tr("For example: Main playthrough"))
        game_save_profile_name.setMaxLength(64)
        game_save_profile_name.setToolTip(
            _tr("This name distinguishes the Elden Ring save profile in MO2. "
            "The game loads the selected file as ER0000.sl2.")
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

        starting_save_heading = QLabel(_tr("Optional starting save"), card)
        starting_save_heading.setObjectName("eldenRingStartingSaveHeading")
        card.layout().addWidget(starting_save_heading)

        copy_checkbox = QCheckBox(
            _tr("Copy an existing .sl2 save into the new instance"), card
        )
        copy_checkbox.setObjectName(self.CreateInstanceSaveCopyCheckboxName)
        copy_checkbox.setToolTip(
            _tr("Choose a save from Steam or a detected Elden Ring MO2 profile. "
            "The file is copied as ER0000.sl2 after the new instance is opened.")
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
        refresh_button = QPushButton(_tr("Refresh saves"), card)
        refresh_button.setToolTip(
            _tr("Search Steam and detected Elden Ring MO2 profiles again.")
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
                confirmed = self._ask_confirmation(
                    dialog,
                    _tr("Use the global Steam save?"),
                    _tr("This mode shares the save with Steam and every Elden Ring "
                    "instance using the global path. Modded play can change the "
                    "vanilla character. Continue?"),
                    _tr("Use global save"),
                    _tr("Keep current mode"),
                    default_accept=False,
                )
                if not confirmed:
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
        # MO2 Revamped's wizard keeps one size and scrolls a long step; older wizards need
        # room for the card
        if not dialog.property("mo2RevampedFitsPages"):
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

    def _sync_native_create_instance_mode(self, dialog, mode: str) -> None:
        """Keep the fork's profile settings in step with the visible plugin card."""
        self._set_native_save_group_plugin_owned(dialog, True)

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
        advanced = dialog.findChild(QCheckBox, "advancedPathOptions")
        if advanced is not None and not advanced.isChecked():
            # the simple page creates <location>/profiles; the advanced
            # fields keep whatever they held and are not used
            base_edit = dialog.findChild(QLineEdit, "location")
            profiles_edit = base_edit
        if profiles_edit is None:
            qInfo(
                "Elden Ring MO2 Save Isolation: could not read the new instance's "
                "profiles path; configure its mode in Tools > Elden Ring "
                "Save Isolation."
            )
            return

        base_text = base_edit.text().strip() if base_edit is not None else ""
        profiles_text = (
            profiles_edit.text().strip() if profiles_edit is not base_edit else ""
        )
        if not profiles_text:
            profiles_text = "%BASE_DIR%/profiles"
        profiles_text = profiles_text.replace("%BASE_DIR%", base_text)
        profiles_path = Path(profiles_text)
        if not profiles_path.is_absolute() and base_text:
            profiles_path = Path(base_text) / profiles_path
        if not profiles_path.is_absolute():
            qInfo(
                "Elden Ring MO2 Save Isolation: the new instance's profile path "
                "could not be resolved; configure its mode in Tools > Elden Ring "
                "Save Isolation."
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

        if (
            copy_checkbox is not None
            and copy_checkbox.isChecked()
            and copy_checkbox.isEnabled()
        ):
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
    def _normalized_path(path: Path) -> str:
        try:
            return os.path.normcase(os.path.normpath(str(path.resolve())))
        except (OSError, RuntimeError):
            return os.path.normcase(os.path.abspath(str(path)))

    @staticmethod
    def _pending_modes_path() -> Path:
        """Queued choices for instances that have not started yet. They live
        outside every instance: a folder beside the new instance's profiles
        would be listed by MO2 as a profile, and MO2 would then not create
        the Default profile the choice is meant for."""
        local_appdata = os.environ.get("LOCALAPPDATA")
        if local_appdata:
            root = Path(local_appdata) / "MO2 Revamped"
        else:
            try:
                root = Path(__file__).resolve().parents[1]
            except (OSError, RuntimeError, IndexError):
                root = Path(__file__).parent
        return root / "EldenRingMO2SaveIsolation" / EldenRingMo2SaveIsolation.PendingModesFilename

    @staticmethod
    def _legacy_pending_modes_path(profile_directory: Path | None) -> Path | None:
        # earlier builds kept the queue in <profiles>/EldenRingMO2SaveIsolation
        if profile_directory is None:
            return None
        return (
            profile_directory.parent
            / "EldenRingMO2SaveIsolation"
            / EldenRingMo2SaveIsolation.PendingModesFilename
        )

    def _read_pending_file(self, pending_path: Path) -> list[dict[str, str]]:
        try:
            data = json.loads(pending_path.read_text(encoding="utf-8"))
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

    def _read_pending_modes(
        self, profile_directory: Path | None = None
    ) -> list[dict[str, str]]:
        entries = self._read_pending_file(self._pending_modes_path())
        legacy_path = self._legacy_pending_modes_path(profile_directory)
        if legacy_path is not None:
            known = {self._normalized_path(Path(item["profile_path"])) for item in entries}
            for item in self._read_pending_file(legacy_path):
                if self._normalized_path(Path(item["profile_path"])) not in known:
                    entries.append(item)
        return entries

    def _write_pending_modes(
        self,
        entries: list[dict[str, str]],
        profile_directory: Path | None = None,
    ) -> None:
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
        self._remove_legacy_pending_modes(profile_directory)

    def _remove_legacy_pending_modes(self, profile_directory: Path | None) -> None:
        # its entries are now in the shared queue; the folder only made MO2
        # show a profile that is not one
        legacy_path = self._legacy_pending_modes_path(profile_directory)
        if legacy_path is None:
            return
        try:
            legacy_path.unlink(missing_ok=True)
            legacy_path.parent.rmdir()
        except OSError:
            pass

    def _queue_pending_mode(
        self,
        profile_directory: Path,
        mode: str,
        source_save_path: str | None = None,
        relative_save_path: str | None = None,
        game_save_profile_name: str | None = None,
    ) -> None:
        if mode not in {self.ProfileIsolated, self.InstanceShared, self.GlobalShared}:
            raise ValueError(_tr("Unknown Elden Ring save mode."))
        key = self._normalized_path(profile_directory)
        entries = [
            item
            for item in self._read_pending_modes(profile_directory)
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
        self._write_pending_modes(entries, profile_directory)

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
            raise ValueError(_tr("The queued save's relative path is invalid."))
        if (
            source_path.suffix.casefold() != ".sl2"
            or cls._is_reparse_point(source_path)
            or not source_path.is_file()
        ):
            raise FileNotFoundError(
                _tr("The selected .sl2 save is no longer available: {path}").format(path=source_path)
            )
        resolved_source = source_path.resolve(strict=True)
        if len(relative_path.parts) > len(resolved_source.parts) - 1:
            raise ValueError(_tr("The queued save's source folder could not be resolved."))
        source_root = resolved_source.parents[len(relative_path.parts) - 1]
        try:
            actual_relative_path = resolved_source.relative_to(source_root)
        except ValueError as error:
            raise ValueError(_tr("The queued save is outside its source folder.")) from error
        if actual_relative_path.as_posix().casefold() != relative_path.as_posix().casefold():
            raise ValueError(_tr("The queued save's source path no longer matches."))
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
            raise ValueError(_tr("The queued save's relative path is invalid."))
        if (
            source_path.suffix.casefold() != ".sl2"
            or self._is_reparse_point(source_path)
            or not source_path.is_file()
        ):
            raise FileNotFoundError(
                _tr("The selected .sl2 save is no longer available: {path}").format(path=source_path)
            )
        source_path = source_path.resolve(strict=True)

        if mode == self.GlobalShared:
            # the global route is the real Steam folder: the new instance
            # does not write into it
            return "global"
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
                    _tr("The destination exists and is not a regular save file: "
                    "{path}").format(path=destination_path)
                )
            if self._same_file_contents(source_path, destination_path):
                return "identical"
            raise FileExistsError(
                _tr("A different save already exists at the destination; it was "
                "left unchanged: {path}").format(path=destination_path)
            )

        parent_path = destination_path.parent
        while parent_path == destination_root or destination_root in parent_path.parents:
            if os.path.lexists(parent_path) and self._is_reparse_point(parent_path):
                raise OSError(
                    _tr("The destination save folder contains a link or reparse point: "
                    "{path}").format(path=parent_path)
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
                    _tr("A save appeared at the destination during copying and was "
                    "left unchanged: {path}").format(path=destination_path)
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

        profile_directory = Path(profile.absolutePath())
        active_path = self._normalized_path(profile_directory)
        pending = self._read_pending_modes(profile_directory)
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
                            try:
                                source_root = self._queued_save_source_root(item)
                                source_path = Path(item["save_source_path"])
                            except (OSError, ValueError) as error:
                                # the profile is still created, empty, so the
                                # chosen mode is not held back by a lost save
                                qInfo(
                                    "Elden Ring MO2 Save Isolation: the selected "
                                    f"save was not imported: {error}"
                                )
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
                        elif result == "global":
                            qInfo(
                                "Elden Ring MO2 Save Isolation: the new instance "
                                "uses the Steam saves directly; nothing was copied "
                                "into the Steam folder."
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
                self._write_pending_modes(remaining, profile_directory)
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

    def _refresh_create_instance_page(self, dialog) -> None:
        try:
            if not dialog.isVisible():
                return
        except RuntimeError:
            return
        # MO2 populates the game cards when their stacked page is activated.
        # Reattach to those newly created buttons, then synchronize the save
        # controls after the page's activation has finished.
        self._watch_create_game_buttons(dialog)
        self._sync_create_instance_dialog(dialog)

    def _watch_create_instance_pages(self, dialog) -> None:
        pages = dialog.findChild(QStackedWidget, "pages")
        if pages is None or dialog.property("_eldenRingSaveUiPageWatchConnected"):
            return
        dialog.setProperty("_eldenRingSaveUiPageWatchConnected", True)
        pages.currentChanged.connect(
            lambda _index, target=dialog: QTimer.singleShot(
                0,
                lambda target=target: self._refresh_create_instance_page(target),
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
        if preferred_dialog is not None:
            try:
                if not preferred_dialog.isVisible():
                    return
            except RuntimeError:
                # The dialog may have closed before the queued layout refresh.
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
                            native_transfer = dialog.findChild(
                                QPushButton, "transferButton"
                            )
                            if native_transfer is not None:
                                native_transfer.setProperty(
                                    "_eldenRingOriginalTransferVisible",
                                    not native_transfer.isHidden(),
                                )
                                native_transfer.setProperty(
                                    "_eldenRingOriginalTransferEnabled",
                                    native_transfer.isEnabled(),
                                )
                                native_transfer.hide()
                            for checkbox in (
                                local_saves_checkbox,
                                dialog.findChild(QCheckBox, "localIniFilesBox"),
                                dialog.findChild(QCheckBox, "invalidationBox"),
                            ):
                                self._hide_native_checkbox(checkbox)
                            dialog.setProperty("_eldenRingSaveUiInjected", True)
                            dialog.setProperty("_eldenRingSaveUiProfilesToolOnly", True)
                    elif dialog.property("_eldenRingSaveUiInjected"):
                        self._restore_native_dialog(dialog)
                    continue

                self._watch_create_instance_pages(dialog)
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
    def _read_save_config_directory(
        cls, profile_directory: Path
    ) -> configparser.ConfigParser:
        config = configparser.ConfigParser(interpolation=None)
        config_path = profile_directory / cls.ConfigFilename
        try:
            with config_path.open("r", encoding="utf-8") as stream:
                config.read_file(stream)
        except FileNotFoundError:
            # A profile can be created before its first save-route setting.
            # Preserve that case while allowing permission and parse failures
            # to reach the caller instead of treating an existing INI as empty.
            pass
        return config

    @classmethod
    def _read_mode_directory(cls, profile_directory: Path) -> str:
        try:
            config = cls._read_save_config_directory(profile_directory)
            mode = config.get("Saves", "mode", fallback=cls.ProfileIsolated).strip().lower()
        except (OSError, UnicodeError, configparser.Error, ValueError) as error:
            qInfo(
                "Elden Ring MO2 Save Isolation: could not read the saved route "
                f"for '{profile_directory}'; using profile mode: {error}"
            )
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
            raise ValueError(_tr("Unknown Elden Ring save mode."))
        if not profile_directory.is_dir():
            raise FileNotFoundError(
                _tr("The selected MO2 profile folder was not found:\n{path}").format(path=profile_directory)
            )

        config = cls._read_save_config_directory(profile_directory)
        config_path = profile_directory / cls.ConfigFilename
        if not config.has_section("Saves"):
            config.add_section("Saves")
        config.set("Saves", "mode", mode)
        if save_profile_id is not None:
            key = cls._save_profile_key(mode)
            if key is not None:
                normalized_id = cls._valid_save_profile_id(save_profile_id)
                if save_profile_id and not normalized_id:
                    raise ValueError(_tr("The selected named save profile is invalid."))
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
        try:
            config = cls._read_save_config_directory(profile_directory)
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
            raise ValueError(_tr("Enter a name for this game save profile."))
        cleaned = name.strip()
        if not cleaned or len(cleaned) > 64:
            raise ValueError(_tr("Use a name between 1 and 64 characters."))
        if any(ord(character) < 32 or ord(character) == 127 for character in cleaned):
            raise ValueError(_tr("The name cannot contain control characters."))
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
        if mode == cls.ProfileIsolated:
            default_label = _tr("Standard saves for this MO2 profile")
        elif mode == cls.InstanceShared:
            default_label = _tr("Default saves shared in this MO2 instance")
        else:
            default_label = _tr("Not used with global Steam saves")
        combo.addItem(default_label, "")
        profiles = cls._read_named_save_profiles(profile_directory, mode)
        for profile_id, name in profiles:
            combo.addItem(name, profile_id)
        normalized_id = cls._valid_save_profile_id(selected_id)
        index = combo.findData(normalized_id)
        missing_profile_id = ""
        if normalized_id and index < 0:
            index = combo.count()
            combo.addItem(_tr("Missing save profile ({id})").format(id=normalized_id[:8]), normalized_id)
            combo.setItemData(
                index,
                _tr("This saved selection has no matching profile folder. Choose an available profile."),
                Qt.ItemDataRole.ToolTipRole,
            )
            missing_profile_id = normalized_id
        combo.setCurrentIndex(max(0, index))
        combo.setProperty("_eldenRingMissingSaveProfileId", missing_profile_id)
        combo.setProperty("_eldenRingMissingSaveProfile", bool(missing_profile_id))
        combo.blockSignals(False)
        combo.setEnabled(mode in {cls.ProfileIsolated, cls.InstanceShared})

    @staticmethod
    def _is_missing_save_profile_selection(combo: QComboBox, selected_id: str) -> bool:
        missing_profile_id = str(
            combo.property("_eldenRingMissingSaveProfileId") or ""
        )
        return bool(missing_profile_id and selected_id == missing_profile_id)

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
            raise ValueError(_tr("Named save profiles are unavailable in global Steam mode."))
        existing_names = {
            existing_name.casefold()
            for _profile_id, existing_name in self._read_named_save_profiles(
                profile_directory, mode
            )
        }
        if clean_name.casefold() in existing_names:
            raise ValueError(_tr("A save profile with this name already exists here."))

        root.mkdir(parents=True, exist_ok=True)
        if self._is_reparse_point(root):
            raise OSError(_tr("The save profile folder is a link and cannot be used: {path}").format(path=root))
        profile_id = uuid.uuid4().hex
        destination = root / profile_id
        if os.path.lexists(destination) or os.path.lexists(root / f"{profile_id}.json"):
            raise FileExistsError(_tr("A save profile with this identifier already exists."))

        temporary_directory = Path(tempfile.mkdtemp(prefix=f".{profile_id}.", dir=root))
        created_directory = False
        metadata_temporary = None
        try:
            if source_path is not None:
                if source_root is None:
                    raise ValueError(_tr("The selected save source has no known folder."))
                if (
                    source_path.suffix.casefold() not in (".sl2", ".co2")
                    or self._is_reparse_point(source_path)
                    or not source_path.is_file()
                ):
                    raise FileNotFoundError(
                        _tr("The selected save is no longer available: {path}").format(path=source_path)
                    )
                source_root_resolved = source_root.resolve(strict=True)
                source_path_resolved = source_path.resolve(strict=True)
                relative_path = source_path_resolved.relative_to(source_root_resolved)
                if (
                    relative_path.is_absolute()
                    or ".." in relative_path.parts
                    or relative_path.suffix.casefold() not in (".sl2", ".co2")
                ):
                    raise ValueError(_tr("The selected save path is outside its source folder."))
                source_parent = source_path.parent
                while source_parent == source_root or source_root in source_parent.parents:
                    if self._is_reparse_point(source_parent):
                        raise OSError(
                            _tr("The selected save folder contains a link: {path}").format(path=source_parent)
                        )
                    if source_parent == source_root:
                        break
                    source_parent = source_parent.parent

                extension = relative_path.suffix.lower()
                canonical_relative = relative_path.parent / f"ER0000{extension}"
                canonical_path = temporary_directory / canonical_relative
                canonical_path.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source_path_resolved, canonical_path)

                backup_source = source_path_resolved.with_name(
                    source_path_resolved.name + ".bak"
                )
                if backup_source.is_file() and not self._is_reparse_point(backup_source):
                    backup_relative = relative_path.parent / f"ER0000{extension}.bak"
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
        dialog.setWindowTitle(_tr("Create Elden Ring save profile"))
        dialog.setMinimumSize(700, 460)
        dialog.resize(780, 500)
        dialog.setSizeGripEnabled(True)
        layout = QVBoxLayout(dialog)
        layout.setContentsMargins(16, 16, 16, 14)
        layout.setSpacing(10)

        header = QFrame(dialog)
        header.setObjectName("saveProfileHeader")
        header_layout = QVBoxLayout(header)
        header_layout.setContentsMargins(14, 10, 14, 10)
        header_layout.setSpacing(3)
        _add_circular_heading(
            header_layout,
            header,
            _tr("Create a save profile"),
            "saveProfileTitle",
            ":/MO/gui/mainwindow/files/save.svg",
            "saveProfileHeaderIcon",
            30,
            "S",
        )
        intro = QLabel(
            _tr("Start with a fresh save or copy an existing save into a separate profile."),
            header,
        )
        intro.setObjectName("saveProfileSubtitle")
        intro.setWordWrap(True)
        header_layout.addWidget(intro)
        layout.addWidget(header)

        details_group = QFrame(dialog)
        details_group.setObjectName("saveProfileDetailsCard")
        details_layout = QVBoxLayout(details_group)
        details_layout.setContentsMargins(14, 11, 14, 11)
        details_layout.setSpacing(6)
        details_heading = QLabel(_tr("Profile name"), details_group)
        details_heading.setObjectName("saveProfileCardTitle")
        details_layout.addWidget(details_heading)
        name_row = QHBoxLayout()
        name_edit = QLineEdit(details_group)
        name_edit.setObjectName("saveProfileNameEdit")
        name_edit.setAccessibleName(_tr("Save profile name"))
        name_edit.setPlaceholderText(_tr("For example: Modded journey"))
        name_edit.setMaxLength(64)
        name_row.addWidget(name_edit, 1)
        details_layout.addLayout(name_row)
        layout.addWidget(details_group)

        source_group = QFrame(dialog)
        source_group.setObjectName("saveProfileStartingSaveCard")
        source_layout = QVBoxLayout(source_group)
        source_layout.setContentsMargins(14, 11, 14, 11)
        source_layout.setSpacing(7)
        source_heading = QLabel(_tr("Starting save"), source_group)
        source_heading.setObjectName("saveProfileCardTitle")
        source_layout.addWidget(source_heading)
        source_note = QLabel(
            _tr("Choose a new empty save or import an existing one: a vanilla .sl2 or "
            "a Seamless Co-op .co2. Imports are copied as ER0000 with the same "
            "extension, with their .bak when there is one. The source is left "
            "unchanged. Close Elden Ring before importing."),
            source_group,
        )
        source_note.setObjectName("saveProfileSourceNote")
        source_note.setWordWrap(True)
        source_layout.addWidget(source_note)
        source_row = QHBoxLayout()
        source_label = QLabel(_tr("Start from:"), source_group)
        source_combo = QComboBox(source_group)
        source_combo.setObjectName("saveProfileSourceCombo")
        source_combo.addItem(_tr("Start a new game with an empty save profile"), None)
        source_combo.setMaxVisibleItems(10)
        source_combo.setSizeAdjustPolicy(
            QComboBox.SizeAdjustPolicy.AdjustToMinimumContentsLengthWithIcon
        )
        source_combo.setMinimumContentsLength(34)
        source_combo.view().setMinimumWidth(480)
        try:
            sources = self._create_instance_save_sources((".sl2", ".co2"))
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
        location_caption = QLabel(_tr("Save profile folder"), location_card)
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
            create_button.setText(_tr("Create save profile"))
            create_button.setObjectName("saveProfileCreateButton")
            create_button.setDefault(True)
        cancel_button = buttons.button(QDialogButtonBox.StandardButton.Cancel)
        if cancel_button is not None:
            cancel_button.setObjectName("saveProfileCancelButton")
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
                    _tr("A save profile with this name already exists here.")
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
                self._show_notice(
                    dialog, _tr("Could not create save profile"), str(error), "warning"
                )
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
            self._show_notice(
                self.__parent_widget,
                _tr("No active Elden Ring profile"),
                _tr("Select an Elden Ring profile in MO2 before changing save options."),
                "warning",
            )
            return

        dialog = QDialog(self.__parent_widget)
        dialog.setObjectName("EldenRingSaveIsolationDialog")
        dialog.setWindowTitle(_tr("Elden Ring Save Isolation"))
        dialog.setMinimumSize(800, 560)
        dialog.resize(960, 710)
        dialog.setSizeGripEnabled(True)
        layout = QVBoxLayout(dialog)
        layout.setContentsMargins(16, 16, 16, 14)
        layout.setSpacing(9)

        header = QFrame(dialog)
        header.setObjectName("saveIsolationHeaderPanel")
        header_layout = QVBoxLayout(header)
        header_layout.setContentsMargins(14, 10, 14, 10)
        header_layout.setSpacing(3)
        _add_circular_heading(
            header_layout,
            header,
            _tr("Save routing and profiles"),
            "saveIsolationPageTitle",
            ":/MO/gui/mainwindow/files/save.svg",
            "saveIsolationHeaderIcon",
            30,
            "S",
        )
        subtitle = QLabel(
            _tr("Choose the save route for the active MO2 profile."),
            header,
        )
        subtitle.setObjectName("saveIsolationPageSubtitle")
        subtitle.setWordWrap(True)
        header_layout.addWidget(subtitle)
        layout.addWidget(header)

        scroll_area = QScrollArea(dialog)
        scroll_area.setObjectName("saveIsolationBody")
        scroll_area.setFrameShape(QFrame.Shape.NoFrame)
        scroll_area.setWidgetResizable(True)
        scroll_area.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        content = QWidget(scroll_area)
        content.setObjectName("saveIsolationContent")
        content_layout = QGridLayout(content)
        content_layout.setContentsMargins(2, 2, 8, 2)
        content_layout.setSpacing(8)
        content_layout.setColumnStretch(0, 1)
        content_layout.setColumnStretch(1, 1)
        scroll_area.setWidget(content)
        layout.addWidget(scroll_area, 1)

        profile_group = QFrame(content)
        profile_group.setObjectName("saveIsolationProfileCard")
        profile_layout = QVBoxLayout(profile_group)
        profile_layout.setContentsMargins(13, 11, 13, 11)
        profile_layout.setSpacing(5)
        _add_circular_heading(
            profile_layout,
            profile_group,
            _tr("Active MO2 profile"),
            "saveIsolationCardTitle",
            ":/MO/gui/mainwindow/profile.svg",
            "saveIsolationProfileIcon",
            26,
            "P",
        )
        profile_label = QLabel(profile.name(), profile_group)
        profile_label.setObjectName("saveIsolationProfileName")
        profile_layout.addWidget(profile_label)
        profile_path_label = QLabel(
            str(self._profile_directory(profile)), profile_group
        )
        profile_path_label.setObjectName("saveIsolationProfilePath")
        profile_path_label.setWordWrap(True)
        profile_path_label.setTextInteractionFlags(
            Qt.TextInteractionFlag.TextSelectableByMouse
        )
        profile_layout.addWidget(profile_path_label)
        profile_note = QLabel(
            _tr("Changes between the profile and instance-shared routes take effect "
            "on the next game launch without restarting MO2. Restart MO2 only "
            "when switching to or from the global Steam save.")
        )
        profile_note.setObjectName("saveIsolationSectionNote")
        profile_note.setWordWrap(True)
        profile_layout.addWidget(profile_note)
        content_layout.addWidget(profile_group, 0, 0)

        mode_group = QFrame(content)
        mode_group.setObjectName("saveIsolationModeCard")
        mode_layout = QVBoxLayout(mode_group)
        mode_layout.setContentsMargins(13, 11, 13, 11)
        mode_layout.setSpacing(6)
        _add_circular_heading(
            mode_layout,
            mode_group,
            _tr("Save mode"),
            "saveIsolationCardTitle",
            ":/MO/gui/mainwindow/settings.svg",
            "saveIsolationModeIcon",
            26,
            "M",
        )
        mode_caption = QLabel(_tr("Route this profile's Elden Ring saves to:"), mode_group)
        mode_caption.setObjectName("saveIsolationFieldCaption")
        mode_layout.addWidget(mode_caption)
        mode_box = QComboBox(mode_group)
        mode_box.setObjectName("saveIsolationModeCombo")
        mode_box.addItem(_tr("Isolate saves for this profile (recommended)"), self.ProfileIsolated)
        mode_box.addItem(_tr("Share modded saves within this MO2 instance"), self.InstanceShared)
        mode_box.addItem(_tr("Use global Steam / Elden Ring saves"), self.GlobalShared)
        current_mode = self._read_mode(profile)
        mode_box.setCurrentIndex(max(0, mode_box.findData(current_mode)))
        mode_layout.addWidget(mode_box)
        mode_description = QLabel()
        mode_description.setObjectName("saveIsolationModeDescription")
        mode_description.setWordWrap(True)
        mode_layout.addWidget(mode_description)
        content_layout.addWidget(mode_group, 0, 1)
        save_profile_group = QFrame(content)
        save_profile_group.setObjectName("saveIsolationGameProfileCard")
        save_profile_layout = QVBoxLayout(save_profile_group)
        save_profile_layout.setContentsMargins(13, 11, 13, 11)
        save_profile_layout.setSpacing(6)
        _add_circular_heading(
            save_profile_layout,
            save_profile_group,
            _tr("Game save profile"),
            "saveIsolationCardTitle",
            ":/MO/gui/mainwindow/files/save.svg",
            "saveIsolationGameProfileIcon",
            26,
            "S",
        )
        save_profile_row = QHBoxLayout()
        save_profile_combo = QComboBox(save_profile_group)
        save_profile_combo.setObjectName("saveIsolationGameProfileCombo")
        save_profile_combo.setMinimumContentsLength(24)
        save_profile_combo.setSizeAdjustPolicy(
            QComboBox.SizeAdjustPolicy.AdjustToMinimumContentsLengthWithIcon
        )
        save_profile_combo.setMaxVisibleItems(10)
        create_save_profile_button = QPushButton(_tr("Create or import…"), save_profile_group)
        create_save_profile_button.setObjectName("saveIsolationCreateProfileButton")
        save_profile_row.addWidget(save_profile_combo, 1)
        save_profile_row.addWidget(create_save_profile_button)
        save_profile_layout.addLayout(save_profile_row)
        save_profile_note = QLabel(
            _tr("Every choice has its own save folder. MO2 presents the selected save "
            "to Elden Ring as ER0000.sl2; importing a save leaves its source intact."),
            save_profile_group,
        )
        save_profile_note.setObjectName("saveIsolationSectionNote")
        save_profile_note.setWordWrap(True)
        save_profile_layout.addWidget(save_profile_note)
        content_layout.addWidget(save_profile_group, 1, 0, 1, 2)

        status_group = QFrame(content)
        status_group.setObjectName("saveIsolationRoutingCard")
        status_layout = QVBoxLayout(status_group)
        status_layout.setContentsMargins(13, 11, 13, 11)
        status_layout.setSpacing(5)
        _add_circular_heading(
            status_layout,
            status_group,
            _tr("Routing status"),
            "saveIsolationCardTitle",
            ":/MO/gui/mainwindow/files/game-data.svg",
            "saveIsolationStatusIcon",
            26,
            "R",
        )
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
            _tr("MO2 profile-specific saves: enabled")
            if mo2_saves_state
            else _tr("MO2 profile-specific saves: disabled")
        )
        mo2_state_label.setObjectName("saveIsolationMo2SaveState")
        status_layout.addWidget(mo2_state_label)
        content_layout.addWidget(status_group, 2, 0)

        safety_card = QFrame(content)
        safety_card.setObjectName("saveIsolationSafetyCard")
        safety_layout = QHBoxLayout(safety_card)
        safety_layout.setContentsMargins(12, 9, 12, 9)
        safety_layout.setSpacing(9)
        safety_layout.addWidget(
            _make_circular_icon(
                safety_card,
                "saveIsolationSafetyIcon",
                ":/MO/gui/mainwindow/status/warning.svg",
                26,
                "!",
            ),
            0,
            Qt.AlignmentFlag.AlignTop,
        )
        safety_caption = QLabel(_tr("Safety"), safety_card)
        safety_caption.setObjectName("saveIsolationSafetyCaption")
        safety_note = QLabel(
            _tr("This only routes save files. It does not change Easy Anti-Cheat or "
            "network mode, and it cannot make modded saves safe for online play. "
            "The global folder can contain separate subfolders for multiple "
            "Steam IDs; identical ER0000.sl2 names under different IDs are "
            "separate account saves."),
            safety_card,
        )
        safety_note.setObjectName("saveIsolationSafetyNote")
        safety_note.setWordWrap(True)
        safety_layout.addWidget(safety_caption, 0, Qt.AlignmentFlag.AlignTop)
        safety_layout.addWidget(safety_note, 1)
        content_layout.addWidget(safety_card, 3, 0, 1, 2)

        actions_group = QFrame(content)
        actions_group.setObjectName("saveIsolationActionsCard")
        actions_layout = QVBoxLayout(actions_group)
        actions_layout.setContentsMargins(13, 11, 13, 11)
        actions_layout.setSpacing(7)
        _add_circular_heading(
            actions_layout,
            actions_group,
            _tr("Folders and tools"),
            "saveIsolationCardTitle",
            ":/MO/gui/mainwindow/files/folder.svg",
            "saveIsolationActionsIcon",
            26,
            "F",
        )
        folder_buttons = QHBoxLayout()
        folder_buttons.setSpacing(7)
        open_active_button = QPushButton(_tr("Open selected save folder"), actions_group)
        open_active_button.setObjectName("saveIsolationOpenSelectedButton")
        open_global_button = QPushButton(_tr("Open global save folder"), actions_group)
        open_global_button.setObjectName("saveIsolationOpenGlobalButton")
        folder_buttons.addWidget(open_active_button)
        folder_buttons.addWidget(open_global_button)
        actions_layout.addLayout(folder_buttons)

        tool_buttons = QHBoxLayout()
        tool_buttons.setSpacing(7)
        copy_report_button = QPushButton(_tr("Copy routing diagnostics"), actions_group)
        copy_report_button.setObjectName("saveIsolationCopyDiagnosticsButton")
        transfer_button = QPushButton(_tr("Transfer one save…"), actions_group)
        transfer_button.setObjectName("saveIsolationTransferButton")
        transfer_button.setToolTip(
            _tr("Choose one .sl2 file and copy it once between MO2 profile, "
            "instance-shared, or global save routes.")
        )
        restore_button = QPushButton(_tr("Restore a backup…"), actions_group)
        restore_button.setObjectName("saveIsolationRestoreButton")
        restore_button.setToolTip(
            _tr("Bring back one of the automatic copies MO2 makes of a save route "
            "before each launch.")
        )
        tool_buttons.addWidget(copy_report_button)
        tool_buttons.addWidget(transfer_button)
        tool_buttons.addWidget(restore_button)
        actions_layout.addLayout(tool_buttons)
        # Keep the heading level with the Routing status card beside it.
        actions_layout.addStretch(1)
        content_layout.addWidget(actions_group, 2, 1)
        content_layout.setRowStretch(4, 1)

        button_box = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Close
        )
        button_box.setObjectName("saveIsolationDialogButtons")
        close_button = button_box.button(QDialogButtonBox.StandardButton.Close)
        if close_button is not None:
            close_button.setObjectName("saveIsolationCloseButton")
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
            save_profile_combo.setProperty(
                "_eldenRingMissingSaveProfile",
                self._is_missing_save_profile_selection(
                    save_profile_combo, selected_save_id
                ),
            )
            local_saves_on = bool(profile.localSavesEnabled())
            active_path = self._save_directory_for_selection(
                profile_directory, mode, selected_save_id
            )
            mode_description.setText(self.mode_description(mode))
            save_profile_note.setText(
                _tr("Named game save profiles are only available with profile-isolated "
                "or instance-shared saves.")
                if mode == self.GlobalShared
                else (
                    _tr("Each named game save profile has its own folder. MO2 presents "
                    "the selected save to Elden Ring as ER0000.sl2; importing a "
                    "save leaves its source intact.")
                )
            )
            active_path_label.setText(
                _tr("<b>Selected save folder:</b><br>{path}").format(path=active_path)
            )
            create_save_profile_button.setEnabled(
                mode in {self.ProfileIsolated, self.InstanceShared}
            )

            if save_profile_combo.property("_eldenRingMissingSaveProfile"):
                status_label.setText(
                    _tr("<b>Save profile unavailable.</b> Choose an available profile "
                    "or the standard saves before saving.")
                )
            elif mode in {self.ProfileIsolated, self.InstanceShared} and not local_saves_on:
                status_label.setText(
                    _tr("<b>Route saved.</b> Restart MO2 before launching so it can "
                    "reload this profile's save mode.")
                )
            elif mode == self.GlobalShared and local_saves_on:
                status_label.setText(
                    _tr("<b>Route saved.</b> Restart MO2 before launching so it can "
                    "reload this profile's save mode.")
                )
            elif mode == self.GlobalShared:
                status_label.setText(
                    _tr("<b>Global save selected.</b> This is shared with Steam and "
                    "any other instance using the global save path.")
                )
            elif mode == self.InstanceShared:
                status_label.setText(
                    _tr("<b>Instance-shared modded save selected.</b> "
                    "Game save profile: {save}. Profiles in this "
                    "instance can choose the same named profile to share it.").format(save=selected_save_name)
                )
            else:
                status_label.setText(
                    _tr("<b>Profile-isolated save selected.</b> "
                    "Game save profile: {save}.").format(save=selected_save_name)
                )

        def open_folder(path: Path) -> None:
            if not path.is_dir():
                self._show_notice(
                    dialog,
                    _tr("Save folder not found"),
                    _tr("This folder does not exist yet:\n\n{path}").format(path=path),
                    "info",
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
            self._show_notice(
                dialog,
                _tr("Diagnostics copied"),
                _tr("Save-routing information was copied to the clipboard. It includes "
                "local paths and may include your Windows user name; review it "
                "before sharing."),
                "success",
            )

        def create_save_profile() -> None:
            mode = mode_box.currentData()
            if mode == self.GlobalShared:
                self._show_notice(
                    dialog,
                    _tr("Global Steam save selected"),
                    _tr("Named game save profiles are available with profile-isolated "
                    "or instance-shared saves."),
                    "info",
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
            persist_route_selection()

        def restore_saved_selection() -> None:
            saved_mode = self._read_mode(profile)
            mode_box.blockSignals(True)
            mode_index = mode_box.findData(saved_mode)
            if mode_index >= 0:
                mode_box.setCurrentIndex(mode_index)
            mode_box.blockSignals(False)
            dialog.setProperty("_eldenRingSaveUiSaveProfileRouteKey", "")
            refresh_details()

        def persist_route_selection(*_args) -> None:
            mode = mode_box.currentData()
            selected_save_id = str(save_profile_combo.currentData() or "")
            if (
                mode != self.GlobalShared
                and save_profile_combo.property("_eldenRingMissingSaveProfile")
            ):
                self._show_notice(
                    dialog,
                    _tr("Save profile unavailable"),
                    _tr("Choose an available named save profile before saving."),
                    "warning",
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
                    self._show_notice(
                        dialog,
                        _tr("Save profile unavailable"),
                        _tr("Choose an available named save profile before saving."),
                        "warning",
                    )
                    return
            try:
                profile_directory = self._profile_directory(profile)
                previous_mode = self._read_mode(profile)
                previous_save_id = self._read_save_profile_id_directory(
                    profile_directory, mode
                )
            except Exception:
                qInfo(
                    "Elden Ring MO2 Save Isolation: route state read failed:\n"
                    + traceback.format_exc()
                )
                self._show_notice(
                    dialog,
                    _tr("Could not read route"),
                    _tr("The current save route could not be read. See the MO2 log."),
                    "warning",
                )
                restore_saved_selection()
                return
            if mode == previous_mode and selected_save_id == previous_save_id:
                refresh_details()
                return
            if mode == self.GlobalShared:
                confirmed_global = self._ask_confirmation(
                    dialog,
                    _tr("Use the global Steam save?"),
                    _tr("This shares the same save folder with Steam and all MO2 "
                    "instances that use the global path. Modded play can alter "
                    "the vanilla character.\n\n"
                    "Choose Use global Steam save to store this route for the "
                    "current profile. Closing this prompt or keeping the current "
                    "mode leaves the saved route unchanged."),
                    _tr("Use global save"),
                    _tr("Keep current mode"),
                    "warning",
                )
                if not confirmed_global:
                    restore_saved_selection()
                    status_label.setText(
                        _tr("<b>Global Steam route not applied.</b> "
                        "The previously saved mode remains active.")
                    )
                    qInfo(
                        "Elden Ring MO2 Save Isolation: global Steam route "
                        f"selection was cancelled for profile '{profile.name()}'; "
                        f"stored mode remains '{self._read_mode(profile)}'."
                    )
                    return
            try:
                self._save_mode(
                    profile,
                    mode,
                    selected_save_id if mode != self.GlobalShared else None,
                )
                persisted_mode = self._read_mode(profile)
                persisted_save_id = self._read_save_profile_id_directory(
                    profile_directory, mode
                )
                expected_save_id = (
                    selected_save_id if mode != self.GlobalShared else ""
                )
                if (
                    persisted_mode != mode
                    or persisted_save_id != expected_save_id
                ):
                    raise OSError(
                        _tr("The selected save route could not be verified after writing. "
                        "The menu will return to the route currently stored on disk.")
                    )
            except (OSError, UnicodeError, configparser.Error, ValueError) as error:
                qInfo(
                    "Elden Ring MO2 Save Isolation: could not verify the selected "
                    f"mode '{mode}' for profile '{profile.name()}': {error}"
                )
                self._show_notice(
                    dialog, _tr("Could not save route"), str(error), "warning"
                )
                restore_saved_selection()
                return
            qInfo(
                "Elden Ring MO2 Save Isolation: saved and verified mode "
                f"'{mode}' for profile '{profile.name()}'; named save profile "
                f"selected: {bool(selected_save_id)}."
            )
            refresh_details()
            restart_required = (
                (previous_mode == self.GlobalShared)
                != (mode == self.GlobalShared)
            )
            if not restart_required:
                status_label.setText(
                    _tr("<b>Saved for {profile}.</b> The selected local "
                    "save route will be used the next time Elden Ring starts. "
                    "MO2 does not need to restart.").format(profile=profile.name())
                )
                return

            restart_now = self._ask_confirmation(
                dialog,
                _tr("Restart MO2 to apply Steam save routing?"),
                _tr("The save route for {profile} has been saved.\n\n"
                "This change switches to or from the global Steam save. Restart "
                "Mod Organizer before launching Elden Ring so it can reload the "
                "profile-specific save setting, or choose Later and restart manually.").format(
                    profile=profile.name()
                ),
                _tr("Restart now"),
                _tr("Later"),
                "info",
            )
            if restart_now:
                dialog.accept()
                QTimer.singleShot(0, self._request_mo2_restart)
            else:
                status_label.setText(
                    _tr("<b>Route saved for {profile}.</b> Restart MO2 "
                    "before launching Elden Ring to apply the Steam save route.").format(profile=profile.name())
                )

        def route_selection_changed(*_args) -> None:
            refresh_details()
            persist_route_selection()

        mode_box.currentIndexChanged.connect(route_selection_changed)
        save_profile_combo.currentIndexChanged.connect(route_selection_changed)
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
        restore_button.clicked.connect(
            lambda: self._display_restore_dialog(
                self._profile_directory(profile), dialog
            )
        )
        button_box.rejected.connect(dialog.reject)

        refresh_details()
        qInfo(
            "Elden Ring MO2 Save Isolation: opened Tools for profile "
            f"'{profile.name()}' with stored mode '{self._read_mode(profile)}'; "
            "named save profile selected: "
            f"{bool(save_profile_combo.currentData())}."
        )
        dialog.exec()


class EldenRingMo2SaveDiagnose(mobase.IPluginDiagnose):
    """Notifications about the Elden Ring save routes: Steam saves still hidden
    by an earlier launch, and a profile route that has no saves of its own
    while the Steam folder has some."""

    PendingRecovery = 0
    EmptyRoute = 1

    def __init__(self, tool: EldenRingMo2SaveIsolation) -> None:
        super().__init__()
        self.__organizer = None
        self.__tool = tool

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self.__organizer = organizer
        return True

    def name(self) -> str:
        return "Elden Ring MO2 Save Diagnose"

    def localizedName(self) -> str:
        return _tr("Elden Ring save checks")

    def author(self) -> str:
        return "ArialSenki"

    def description(self) -> str:
        return _tr("Warns about Elden Ring save routes that need attention.")

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(1, 0, 0, mobase.ReleaseType.FINAL)

    def requirements(self) -> list:
        return [mobase.PluginRequirementFactory.gameDependency("ELDEN RING")]

    def settings(self) -> list[mobase.PluginSetting]:
        return []

    @staticmethod
    def _account_folders(root: Path) -> list[Path]:
        try:
            return [
                entry
                for entry in root.iterdir()
                if entry.is_dir() and len(entry.name) == 17 and entry.name.isdigit()
            ]
        except OSError:
            return []

    @staticmethod
    def _has_saves(root: Path) -> bool:
        try:
            for account in EldenRingMo2SaveDiagnose._account_folders(root):
                for entry in account.iterdir():
                    if entry.is_file() and entry.name.casefold().startswith("er0000."):
                        return True
        except OSError:
            pass
        return False

    @staticmethod
    def _pending_sessions() -> list[Path]:
        # while the game runs its own session hides the saves on purpose
        names = _running_process_names()
        if names is None or "eldenring.exe" in names:
            return []
        appdata = os.environ.get("APPDATA")
        if not appdata:
            return []
        # this install's recovery folder, named as the game plugin names it
        try:
            install_root = Path(__file__).resolve().parents[1]
            identity = os.path.normcase(str(install_root.resolve()))
        except (IndexError, OSError, RuntimeError):
            return []
        token = hashlib.sha256(identity.encode("utf-8", errors="replace")).hexdigest()[:24]
        recovery = Path(appdata) / ".MO2RevampedSaveRecovery" / token
        sessions = []
        try:
            for instance in [recovery]:
                if not instance.is_dir():
                    continue
                for session in instance.iterdir():
                    if session.is_dir() and any(
                        child.name not in ("unrouted-global", "session.json")
                        for child in session.iterdir()
                    ):
                        sessions.append(session)
        except OSError:
            pass
        return sessions

    def _active_route(self) -> tuple[str, Path] | None:
        try:
            profile = self.__organizer.profile()
            if profile is None:
                return None
            directory = Path(profile.absolutePath())
        except (AttributeError, RuntimeError):
            return None
        tool = EldenRingMo2SaveIsolation
        mode = tool._read_mode_directory(directory)
        return mode, tool._save_directory_for_selection(directory, mode, None)

    def activeProblems(self) -> list[int]:
        problems = []
        if self._pending_sessions():
            problems.append(self.PendingRecovery)
        route = self._active_route()
        if route is not None:
            mode, root = route
            if (
                mode != EldenRingMo2SaveIsolation.GlobalShared
                and not self._has_saves(root)
                and self._has_saves(EldenRingMo2SaveIsolation._global_save_directory())
            ):
                problems.append(self.EmptyRoute)
        return problems

    def shortDescription(self, key: int) -> str:
        if key == self.PendingRecovery:
            return _tr("Steam saves from an earlier launch are still hidden")
        return _tr("This profile has no Elden Ring saves of its own yet")

    def fullDescription(self, key: int) -> str:
        if key == self.PendingRecovery:
            sessions = "<br>".join(str(session) for session in self._pending_sessions())
            return _tr(
                "<p>MO2 hides the Steam save folders while the game runs with "
                "isolated saves and brings them back when it closes. A launch that "
                "did not finish (MO2 or Windows closed while playing, a crash) left "
                "them hidden.</p>"
                "<p>Close Elden Ring and start MO2 again: it restores them on its "
                "own before the next launch. Until then they are safe in:</p>"
                "<p>{sessions}</p>"
                "<p>Don't start the game from Steam meanwhile: it would not see "
                "those characters.</p>"
            ).format(sessions=sessions)
        route = self._active_route()
        folder = str(route[1]) if route is not None else ""
        return _tr(
            "<p>The save route this profile uses has no Elden Ring saves yet, "
            "while the Steam save folder has some. The game will start without "
            "your existing characters, and a new character is saved only to this "
            "route.</p>"
            "<p>To play an existing character here, open Tools &gt; Save Isolation "
            "and use Transfer one save, or create or import a named save "
            "profile. To use the Steam saves directly, choose the global route "
            "there.</p>"
            "<p>Route: {folder}</p>"
        ).format(folder=folder)

    def hasGuidedFix(self, key: int) -> bool:
        return key == self.EmptyRoute

    def startGuidedFix(self, key: int) -> None:
        if key == self.EmptyRoute:
            self.__tool.display()


def createPlugins() -> list:
    """The save-isolation tool and its save checks, as MO2 expects them."""
    tool = EldenRingMo2SaveIsolation()
    diagnose = EldenRingMo2SaveDiagnose(tool)
    tool._save_diagnose = diagnose
    return [tool, diagnose]
