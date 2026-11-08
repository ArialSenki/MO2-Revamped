"""Elden Ring game support for Mod Organizer 2 without an external loader."""

from __future__ import annotations

import ctypes
from ctypes import wintypes
import configparser
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import tempfile
import time
import uuid

try:
    import msvcrt
except ImportError:  # not Windows
    msvcrt = None

import mobase
from PyQt6.QtCore import (
    QEvent,
    QUrl,
    QFileInfo,
    QDir,
    QObject,
    QProcess,
    QTimer,
    Qt,
    qCritical,
    qInfo,
    qWarning,
)
from PyQt6.QtGui import QDesktopServices
from PyQt6.QtWidgets import (
    QAbstractButton,
    QApplication,
    QBoxLayout,
    QComboBox,
    QCheckBox,
    QMessageBox,
    QSizePolicy,
    QPushButton,
    QToolButton,
)

from ..basic_features import BasicModDataChecker, GlobPatterns
from ..basic_features.basic_save_game_info import BasicGameSaveGame
from ..basic_features.utils import is_directory
from ..basic_game import BasicGame
from PyQt6.QtCore import QCoreApplication


# PyQt passes Qt's log functions ASCII only: a message with any other
# character (a path with an accent, a Windows error in Spanish) raised
# UnicodeEncodeError and stopped the code that logged it. Such messages go
# through Python's logging, which MO2's Python plugin writes to the same log.
import logging  # noqa: E402
import sys  # noqa: E402


_QT_LOG_CALLS: dict = {}


# the game this MO2 manages, for the log; set once its interface is up
_log_game_name = "Elden Ring"


def _safe_qt_log(qt_function, level):
    def log(message) -> None:
        text = "" if message is None else str(message)
        text = text.replace("Elden Ring MO2", f"{_log_game_name} MO2")
        caller = sys._getframe(1)
        filename, line = caller.f_code.co_filename, caller.f_lineno
        if not text.isascii():
            logging.getLogger("mo2revamped").log(
                level, f"[{os.path.basename(filename)}:{line}] {text}"
            )
            return
        # Qt logs the file and line of the code that calls it; a call compiled
        # at the caller's place keeps the log pointing there instead of here
        code = _QT_LOG_CALLS.get((filename, line))
        if code is None:
            code = compile("\n" * (line - 1) + "_qt(_text)", filename, "exec")
            _QT_LOG_CALLS[(filename, line)] = code
        exec(code, {"_qt": qt_function, "_text": text})

    return log


qCritical = _safe_qt_log(qCritical, logging.CRITICAL)
qInfo = _safe_qt_log(qInfo, logging.INFO)
qWarning = _safe_qt_log(qWarning, logging.WARNING)
# for code that shadows the names to collect its lines first
_module_qinfo = qInfo
_module_qwarning = qWarning


def _with_true_case(path) -> str:
    """The path as the disk spells it. Steam's registry value can be all lower
    case, and MO2 shows and stores the game folder as it gets it."""
    original = Path(path)
    if not original.is_absolute():
        return str(path)
    drive = original.drive
    result = Path(original.anchor.upper() if len(drive) == 2 else original.anchor)
    for part in original.parts[1:]:
        try:
            names = os.listdir(result)
            match = (
                part
                if part in names
                else next((name for name in names if name.casefold() == part.casefold()), None)
            )
        except OSError:
            return str(path)
        if match is None:
            return str(path)
        result = result / match
    return str(result)


# MO2 installs translations/revamped_<language>.qm; the source text is English.
_TR_CONTEXT = "EldenRingGame"


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
    processes = _running_processes()
    return None if processes is None else {name for _pid, name in processes}


def _running_processes() -> list[tuple[int, str]] | None:
    """(process id, lowercased executable name) of every running process, or
    None when the list can't be read."""
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
            processes: list[tuple[int, str]] = []
            ok = first(snapshot, ctypes.byref(entry))
            while ok:
                processes.append((entry.th32ProcessID, entry.szExeFile.casefold()))
                ok = following(snapshot, ctypes.byref(entry))
            # the walk ends with ERROR_NO_MORE_FILES; anything else cut it short
            if ctypes.get_last_error() != 18:
                return None
            return processes or None
        finally:
            close(snapshot)
    except (OSError, AttributeError, ValueError):
        return None


def _process_image_path(pid: int) -> str | None:
    """The full path of a running process's executable, or None when Windows
    doesn't give it."""
    try:
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        open_process = kernel32.OpenProcess
        open_process.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
        open_process.restype = wintypes.HANDLE
        query = kernel32.QueryFullProcessImageNameW
        query.argtypes = (
            wintypes.HANDLE,
            wintypes.DWORD,
            wintypes.LPWSTR,
            ctypes.POINTER(wintypes.DWORD),
        )
        query.restype = wintypes.BOOL
        close = kernel32.CloseHandle
        close.argtypes = (wintypes.HANDLE,)
        handle = open_process(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
        if not handle:
            return None
        try:
            size = wintypes.DWORD(32768)
            buffer = ctypes.create_unicode_buffer(size.value)
            if not query(handle, 0, buffer, ctypes.byref(size)):
                return None
            return buffer.value or None
        finally:
            close(handle)
    except (OSError, AttributeError, ValueError):
        return None


class _RunButtonEnabledMirror(QObject):
    """Mirror MO2's launch-button enabled state onto the visible split button."""

    def __init__(self, source: QAbstractButton, replacement: QToolButton) -> None:
        super().__init__(source)
        self._source = source
        self._replacement = replacement

    def eventFilter(self, watched, event) -> bool:
        if watched == self._source and event.type() == QEvent.Type.EnabledChange:
            self._replacement.setEnabled(self._source.isEnabled())
        return False


class _ProgramListLock(QObject):
    """Keeps a combo box on its program while it still looks and reads as usual;
    disabled, it would lose its colors and icon."""

    Blocked = (
        QEvent.Type.MouseButtonPress,
        QEvent.Type.MouseButtonDblClick,
        QEvent.Type.Wheel,
        QEvent.Type.KeyPress,
    )

    def __init__(self, parent) -> None:
        super().__init__(parent)
        self.locked = False

    def eventFilter(self, _watched, event) -> bool:
        if not self.locked or event.type() not in self.Blocked:
            return False
        # the keyboard can still leave the list
        return not (
            event.type() == QEvent.Type.KeyPress
            and event.key() in (Qt.Key.Key_Tab, Qt.Key.Key_Backtab)
        )


class _MainWindowCloseWatcher(QObject):
    """Calls back when MO2's main window closes (MO2 quits or switches
    instance), before the plugin's timers stop."""

    def __init__(self, window, on_close) -> None:
        super().__init__(window)
        self._on_close = on_close

    def eventFilter(self, watched, event) -> bool:
        # the window hides once MO2 has accepted the close: a Close event
        # comes first, also when the user then cancels MO2's "still running"
        # question. Minimizing hides it too, but spontaneously.
        if event.type() == QEvent.Type.Hide and not event.spontaneous():
            self._on_close()
        return False


class EldenRingModDataChecker(BasicModDataChecker):
    """Recognize Elden Ring asset and native-mod archive layouts.

    MO2 stores game assets below ``Game`` and native plugins under
    ``Game/DLLs``. Flat mixed archives therefore keep game resource folders in
    place while DLLs, configuration sidecars, and a mod's companion fonts
    directory are routed together during installation.
    """

    _dll_directory = "DLLs"
    _archive_wrapper_depth_limit = 16
    _legacy_dll_directories = {"external_dlls", "mo2_dlls", "mods"}
    # folders whose DLLs are already where the standard layout puts them
    _managed_dll_directories = frozenset(
        # _reforgedonline: the Reforged release kept for its own launcher
        {_dll_directory.casefold(), *_legacy_dll_directories, "plugins", "_reforgedonline", "seamlesscoop"}
    )

    _asset_directories = {
        "action",
        "animation",
        "animations",
        "asset",
        "assets",
        "chr",
        "common",
        "cutscene",
        "event",
        "expression",
        "facegen",
        "font",
        "map",
        "material",
        "menu",
        "msg",
        "movie",
        "movie_dlc",
        # game files with no folder of their own (ELDEN RING Reforged's
        # autoinvadepoint, decaltex and mapinfotex)
        "other",
        "param",
        "parts",
        "script",
        "sd",
        "sfx",
        "shader",
        "sound",
        "system",
        "texture",
        "textures",
        "time",
        "wp",
    }


    _content_directories = _asset_directories | {
        "config",
        "configs",
        "dlls",
        "external_dlls",
        "fonts",
        "mod",
        "mods",
        "mo2_dlls",
        "native",
        "natives",
        "plugins",
    }
    _package_directories = {
        "<game>",
        "elden ring",
        "elden-ring",
        "eldenring",
        "elden_ring",
        "nightreign",
        "elden ring nightreign",
        "elden-ring-nightreign",
        "eldenring nightreign",
        "elden_ring_nightreign",
        "data",
        "game",
        "dlls",
        "external_dlls",
        "mod",
        "mods",
        "mo2_dlls",
        "native",
        "natives",
    }
    _root_loader_dlls = {
        "d3d11.dll",
        "d3d12.dll",
        "dbghelp.dll",
        "dinput8.dll",
        "dxgi.dll",
        "version.dll",
        "winmm.dll",
        "xinput1_3.dll",
        "xinput9_1_0.dll",
    }
    _sidecar_extensions = {
        ".cfg",
        ".ini",
        ".json",
        ".log",
        ".toml",
        ".xml",
        ".yaml",
        ".yml",
    }
    # Freecam loads its settings and fonts from <DLL folder>/Freecam.
    # Preserve those exact paths when MO2 moves its native DLL into DLLs.
    _dll_companion_directories = {"freecammod": "Freecam"}
    _supported_extensions = {
        ".acb",
        ".anibnd",
        ".bak",
        ".bin",
        ".bnk",
        ".bnd",
        ".chrbnd",
        ".cfg",
        ".dcx",
        ".dsaproj",
        ".dsasbak",
        ".dll",
        ".dds",
        ".emevd",
        ".fdbnd",
        ".flac",
        ".flver",
        ".fmg",
        ".ffxbnd",
        ".gfx",
        ".hkx",
        ".hks",
        ".ini",
        ".json",
        ".matbin",
        ".mp3",
        ".mtd",
        ".msgbnd",
        ".ogg",
        ".lua",
        ".otf",
        ".png",
        ".prev",
        ".partsbnd",
        ".tga",
        ".toml",
        ".tpf",
        ".tpfbdt",
        ".tpfbhd",
        ".wav",
        ".wem",
        ".xml",
        ".yaml",
        ".yml",
    }
    _metadata_patterns = [
        "<game>",
        "Game",
        "ELDEN RING",
        "ELDENRING",
    ]

    def __init__(
        self,
        organizer: mobase.IOrganizer,
        plugin_name: str,
        preserve_extensions: tuple[str, ...] = (),
        watch_mod_list: bool = False,
    ):
        self._organizer = organizer
        self._plugin_name = plugin_name
        # the install window's summary runs the layout on a copy: nothing to log
        self.previewing = False
        self._preserve_extensions = frozenset(
            extension.casefold() for extension in preserve_extensions
        )
        self._saved_archive_layout_signatures: set[tuple[str, ...]] | None = None
        self._saved_archive_layout_roots: set[frozenset[str]] = set()
        self._saved_archive_layout_cache_epoch = None
        # only the game plugin's own checker follows the mod list: MO2 never
        # drops these callbacks, so the short-lived checker the archive
        # installer makes for each install must not add more of them
        if watch_mod_list:
            try:
                mod_list = organizer.modList()
                mod_list.onModInstalled(self._invalidate_saved_archive_layout_cache)
                mod_list.onModRemoved(self._invalidate_saved_archive_layout_cache)
            except Exception:
                # Validation still works without the optional cache notifications.
                pass
        super().__init__(
            GlobPatterns(
                unfold=self._metadata_patterns,
                valid=[
                    *sorted(self._content_directories),
                    *sorted(
                        f"*{extension}" for extension in self._supported_extensions
                    ),
                    "load.txt",
                    "seamlesscoop",
                    self.ReforgedOnlineDirectory,
                ],
                delete=[
                    "*.md",
                    "*.nfo",
                    "*.pdf",
                    "*.url",
                    "CHANGELOG*",
                    "CREDITS*",
                    "LICENSE*",
                    "README*",
                ],
            )
        )

    def dataLooksValid(
        self, filetree: mobase.IFileTree
    ) -> mobase.ModDataChecker.CheckReturn:
        # The archive installer sets this short-lived flag only after the user
        # chooses to keep a package exactly as it appears in the archive. MO2
        # may call the checker with a nested tree during installation, so do
        # not require this tree to have no parent. Validate that it actually
        # contains Elden Ring payload before accepting it.
        if self._preserve_archive_layout_requested():
            return (
                self.VALID
                if self._contains_supported_payload(filetree)
                else self.INVALID
            )

        # MO2 evaluates installed mod trees again after the installer has
        # cleared its temporary choice. Match either saved layout to the
        # installed tree so both supported choices count as valid game data.
        if self._is_saved_archive_layout_tree(filetree):
            return self.VALID

        # Common archive wrappers are normalized during install. The target
        # layout uses the short ``DLLs`` folder and does not expose wrappers.
        if self._has_wrapped_payload(filetree):
            return self.FIXABLE

        # Native DLLs inside archive subfolders are organized below DLLs.
        # Files already at the archive root stay there and are loaded through
        # the game's forced-load list, regardless of their DLL basename.
        if self._has_misplaced_dll(filetree):
            return self.FIXABLE

        status = super().dataLooksValid(filetree)
        if status is self.VALID:
            return status
        # game folders with loose files beside them (notes kept by the install
        # window's choice, a README, Seamless Co-op's ersc_launcher.exe): the
        # game never reads those files, and MO2 flags an installed mod that
        # isn't VALID as having no game data
        if self._valid_apart_from_loose_files(filetree):
            return self.VALID
        if status is not self.INVALID:
            return status

        # Handle a wrapper folder containing the mod payload and its files.
        if any(
            is_directory(entry) and self._is_package_wrapper(entry)
            for entry in filetree
        ):
            return self.FIXABLE

        # A recognized game payload nested in one or more archive wrappers can
        # be repaired; archives without a known payload remain invalid.
        if self._contains_supported_payload(filetree):
            return self.FIXABLE
        return status

    def _valid_apart_from_loose_files(self, filetree: mobase.IFileTree) -> bool:
        """Whether every folder at the top is a game folder MO2 takes as it is,
        with at least one of them or a game file there, and the rest only loose
        files."""
        patterns = self._regex_patterns
        found = False
        for entry in filetree:
            name = entry.name().casefold()
            if patterns.valid.match(name):
                found = True
            elif is_directory(entry) or patterns.unfold.match(name):
                # a wrapper or an option folder: left to the checks below
                return False
        return found

    def fix(self, filetree: mobase.IFileTree) -> mobase.IFileTree:
        if self._preserve_archive_layout_requested():
            return filetree
        return self.normalize_standard_layout(filetree)

    def normalize_standard_layout(
        self,
        filetree: mobase.IFileTree,
        recognize_mod_engine_paths: bool = True,
        keep_reforged_online: bool = False,
    ) -> mobase.IFileTree:
        """Apply the standard DLL/assets layout directly to an archive tree."""
        # ELDEN RING Reforged first: the release it keeps for its own launcher
        # must stay as it ships, notes and licences included
        self._reshape_reforged_layout(filetree, keep_reforged_online)
        self._remove_loader_launchers(filetree)
        self._remove_shipped_logs(filetree)
        self.optional_files_left_out = self._remove_optional_folders(filetree)
        # Apply MO2's standard root patterns before peeling the outer archive
        # folders. The wrapper scan itself stays at this game-root boundary.
        self._fix_known_layouts(filetree)
        self._unwrap_package_folders(filetree)
        # Mod Engine 2 packages place the actual game payload under ``mod/``.
        # Lift that recognized payload before moving DLLs so its assets retain
        # their game-root paths (for example, ``mod/menu`` becomes ``menu``).
        if recognize_mod_engine_paths:
            self._unwrap_mod_engine_paths(filetree)

        # Older archives use these as package containers rather than game
        # asset paths. Peel their inner package folders, route native files,
        # then flatten the remaining assets only when paths do not collide.
        legacy_folders = (
            "native",
            "natives",
            *(("mod",) if recognize_mod_engine_paths else ()),
            *sorted(self._legacy_dll_directories),
        )
        for folder_name in legacy_folders:
            folder = self._direct_directory(filetree, folder_name)
            if folder is not None:
                self._unwrap_package_folders(folder)

        self._move_standard_dlls_and_ini(filetree)
        for folder_name in legacy_folders:
            folder = self._direct_directory(filetree, folder_name)
            if folder is None:
                continue
            if len(folder) == 0:
                folder.detach()
            elif self._package_folders_merge_without_conflicts(
                filetree, [folder]
            ):
                self._merge_package_folder(filetree, folder)
        self._unwrap_package_folders(filetree, allow_safe_multiple=True)
        self._remove_preview_images(filetree)
        return filetree

    def normalize_preserved_layout(
        self,
        filetree: mobase.IFileTree,
    ) -> mobase.IFileTree:
        """Map install-source wrappers and preserve every payload-relative path.

        FRZN Merge calls its game-root payload ``Drag Files Into Here``. That
        folder is an install container, not an in-game path component: lift its
        contents to MO2's virtual game root while leaving their internal paths
        intact. Native roots such as ``natives/`` remain in the archive layout.

        Only folders that cannot be part of the layout are lifted: the
        archive's own top folder, ``Game``/``ELDEN RING`` folders and the
        FRZN staging folder. A level holding game files, a launcher or its
        ``.toml`` is the payload root (Seamless Co-op keeps ``SeamlessCoop/``
        beside ``ersc_launcher.exe``, Mod Engine 2 keeps ``modengine2/bin``).
        """
        game_folders = {
            "<game>", "game", "elden ring", "elden-ring", "eldenring", "elden_ring",
            "nightreign", "elden ring nightreign", "elden-ring-nightreign",
            "eldenring nightreign", "elden_ring_nightreign",
        }
        for _ in range(self._archive_wrapper_depth_limit):
            entries = [
                entry
                for entry in filetree
                if not self._regex_patterns.delete.match(entry.name().casefold())
            ]
            # screenshots or notes beside the top folder do not count
            if any(
                not is_directory(entry)
                and Path(entry.name()).suffix.casefold()
                in {".bdt", ".bhd", ".bin", ".dcx", ".dll", ".exe", ".ini", ".toml"}
                for entry in entries
            ):
                return filetree
            folders = [
                entry
                for entry in entries
                if is_directory(entry) and self._contains_supported_payload(entry)
            ]
            # FRZN's staging folder goes to the root even beside its natives/
            staging = [
                folder for folder in folders if folder.name().casefold() == "drag files into here"
            ]
            if staging:
                folders = staging
            if len(folders) != 1:
                return filetree
            folder = folders[0]
            name = folder.name().casefold()
            if name == "drag files into here" or name in game_folders:
                pass
            elif name in self._content_directories or name in {"seamlesscoop", "modengine2"}:
                return filetree
            if not self._package_folders_merge_without_conflicts(filetree, [folder]):
                return filetree
            if not self._merge_package_folder(filetree, folder):
                return filetree
        return filetree

    # ELDEN RING Reforged ships its own me3 and launcher: its game files are
    # under mod/ and its natives under internals/modengine/dll. MO2 serves the
    # files and loads the natives itself, and hands reforged.dll the attach
    # config me3 would give it (see EldenRingMo2Game._reforged_attach_config).
    _reforged_native_path = "internals/modengine/dll"
    _reforged_dll = "reforged.dll"
    # what the launcher, me3 and the UI setup tool (Gideon) need, not the game
    _reforged_tool_paths = ("mod/menu/deploy",)
    # the release kept for Reforged's own launcher (its online mode); its DLLs
    # (me3's among them) are never MO2's natives
    ReforgedOnlineDirectory = "_ReforgedOnline"
    ReforgedLauncherPath = "internals/launcher/ReforgedLauncher.exe"

    @classmethod
    def known_package(cls, filetree: mobase.IFileTree, depth: int = 0) -> str | None:
        """Which mod with its own install needs this archive is, if any."""
        if depth == 0 and cls.reforged_root(filetree) is not None:
            return "reforged"
        me3 = cls._direct_directory(filetree, "me3")
        if me3 is not None and any(
            entry.name().casefold() == "convergence.me3" for entry in me3
        ):
            return "convergence"
        seamless = cls._direct_directory(filetree, "seamlesscoop")
        if seamless is not None and any(
            entry.name().casefold() in {"ersc.dll", "nrsc.dll"} for entry in seamless
        ):
            return "seamless"
        if cls._direct_directory(filetree, "drag files into here") is not None:
            return "frzn"
        if depth >= 2:
            return None
        for entry in filetree:
            if is_directory(entry):
                found = cls.known_package(entry, depth + 1)
                if found is not None:
                    return found
        return None

    @classmethod
    def _is_me3_folder(cls, folder: mobase.IFileTree, depth: int = 0) -> bool:
        for entry in folder:
            if is_directory(entry):
                if depth < 3 and cls._is_me3_folder(entry, depth + 1):
                    return True
            elif entry.name().casefold() == "me3.exe" or entry.name().casefold().endswith(".me3"):
                return True
        return False

    def _remove_loader_launchers(self, filetree: mobase.IFileTree, depth: int = 0) -> None:
        """A package made for me3 or Mod Engine 2 brings that loader and the
        script that starts it; MO2 loads the package's files and natives itself."""
        loader_files = [
            entry
            for entry in filetree
            if not is_directory(entry)
            and (
                entry.name().casefold().endswith(".me3")
                or entry.name().casefold() in {"me3.exe", "modengine2_launcher.exe"}
                or re.fullmatch(r"launchmod_.*\.(bat|cmd)", entry.name().casefold())
            )
        ]
        me3 = self._direct_directory(filetree, "me3")
        me3 = me3 if me3 is not None and self._is_me3_folder(me3) else None
        modengine = self._direct_directory(filetree, "modengine2")
        if loader_files or me3 is not None or modengine is not None:
            for folder in (me3, modengine):
                if folder is not None:
                    folder.detach()
            for entry in list(filetree):
                name = entry.name().casefold()
                if not is_directory(entry) and (
                    Path(name).suffix in {".bat", ".cmd", ".sh", ".lnk", ".me3"}
                    or name in {"me3.exe", "modengine2_launcher.exe"}
                    or re.fullmatch(r"config_[a-z0-9_]+\.toml", name)
                ):
                    entry.detach()
            return
        if depth < 2:
            for entry in list(filetree):
                if is_directory(entry) and entry.name() != self.ReforgedOnlineDirectory:
                    self._remove_loader_launchers(entry, depth + 1)

    _optional_folder_names = re.compile(
        r"(^|[\s_\-.])optionals?($|[\s_\-.])|^(extras?|add-?ons?)$", re.IGNORECASE
    )

    def _remove_optional_folders(self, filetree: mobase.IFileTree, depth: int = 0) -> int:
        """An archive's optional extras aren't the mod: left out, and counted so
        the window can say so. A folder that is all the payload there is stays."""
        removed = 0
        for entry in list(filetree):
            if (
                is_directory(entry)
                and entry.name() != self.ReforgedOnlineDirectory
                and self._optional_folder_names.search(entry.name())
                and any(
                    other is not entry
                    and other.name() != entry.name()
                    and (
                        self._contains_supported_payload(other)
                        if is_directory(other)
                        else Path(other.name()).suffix.casefold() in self._supported_extensions
                    )
                    for other in filetree
                )
            ):
                removed += self._count_files(entry)
                entry.detach()
        if depth < 2:
            for entry in list(filetree):
                if is_directory(entry) and entry.name() != self.ReforgedOnlineDirectory:
                    removed += self._remove_optional_folders(entry, depth + 1)
        return removed

    @classmethod
    def _count_files(cls, tree: mobase.IFileTree) -> int:
        return sum(cls._count_files(entry) if is_directory(entry) else 1 for entry in tree)

    _preview_image_suffixes = {".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp"}

    def _remove_preview_images(self, filetree: mobase.IFileTree) -> None:
        """Screenshots beside the mod would end up in the game's folder. Only at
        the root: a DLL's own icons stay with it in DLLs."""
        for entry in list(filetree):
            if is_directory(entry):
                if entry.name().casefold() in {
                    "images", "image", "screenshots", "screenshot", "previews", "preview", "pictures",
                } and all(
                    not is_directory(child)
                    and Path(child.name()).suffix.casefold() in self._preview_image_suffixes
                    for child in entry
                ):
                    entry.detach()
            elif Path(entry.name()).suffix.casefold() in self._preview_image_suffixes:
                entry.detach()

    def _dll_companion_folders(self, tree: mobase.IFileTree) -> set[str]:
        """Folders beside a DLL holding only its data (settings, icons, text):
        they stay with it, whole, instead of being taken for package wrappers."""
        if not any(
            not is_directory(entry) and Path(entry.name()).suffix.casefold() == ".dll"
            and entry.name().casefold() not in self._root_loader_dlls
            for entry in tree
        ):
            return set()
        names = set()
        for entry in tree:
            name = entry.name().casefold()
            if (
                is_directory(entry)
                and len(entry) > 0
                and name not in self._content_directories
                and name not in self._package_directories
                and name not in {"seamlesscoop", "logs", self.ReforgedOnlineDirectory.casefold()}
                and not self._has_game_payload(entry)
            ):
                names.add(name)
        return names

    @classmethod
    def _has_game_payload(cls, tree: mobase.IFileTree, depth: int = 0) -> bool:
        if depth > cls._archive_wrapper_depth_limit:
            return True
        for entry in tree:
            if is_directory(entry):
                if entry.name().casefold() in cls._asset_directories or cls._has_game_payload(
                    entry, depth + 1
                ):
                    return True
            elif Path(entry.name()).suffix.casefold() in {".dll", ".exe", ".dcx", ".bin", ".gfx", ".bdt", ".bhd"}:
                return True
        return False

    def _remove_shipped_logs(self, filetree: mobase.IFileTree, depth: int = 0) -> None:
        """A logs folder that came with the archive is its author's, not the mod."""
        if depth > self._archive_wrapper_depth_limit:
            return
        for entry in list(filetree):
            if not is_directory(entry) or entry.name() == self.ReforgedOnlineDirectory:
                continue
            if entry.name().casefold() == "logs" and len(entry) > 0 and all(
                not is_directory(child) and child.name().casefold().endswith(".log")
                for child in entry
            ):
                entry.detach()
            else:
                self._remove_shipped_logs(entry, depth + 1)

    @classmethod
    def reforged_root(
        cls, filetree: mobase.IFileTree, depth: int = 0
    ) -> mobase.IFileTree | None:
        """The folder holding an ELDEN RING Reforged release, if any."""
        natives = cls._directory_at_path(filetree, cls._reforged_native_path)
        if natives is not None and any(
            not is_directory(entry) and entry.name().casefold() == cls._reforged_dll
            for entry in natives
        ):
            return filetree
        if depth >= 2:
            return None
        folders = [entry for entry in filetree if is_directory(entry)]
        for folder in folders:
            found = cls.reforged_root(folder, depth + 1)
            if found is not None:
                return found
        return None

    def _reshape_reforged_layout(
        self, filetree: mobase.IFileTree, keep_online: bool = False
    ) -> bool:
        """Lay out ELDEN RING Reforged as MO2 runs it: mod/ at the game root,
        its natives in DLLs, and nothing of its launcher or me3.

        With ``keep_online`` the rest of the release (launcher, me3, its tools
        and notes) moves whole into ``_ReforgedOnline``; the archive installer
        then hard-links the game files and natives back to their release paths
        there, so Reforged's own launcher finds the release it checks for its
        online mode without a second copy on disk. ``self.reforged_layout``
        records what goes where.
        """
        self.reforged_layout = None
        root = self.reforged_root(filetree)
        if root is None:
            return False
        online = self.ReforgedOnlineDirectory
        natives = self._directory_at_path(root, self._reforged_native_path)
        mod_folder = self._direct_directory(root, "mod")
        for path in self._reforged_tool_paths:
            tool = self._directory_at_path(root, path)
            if tool is None:
                continue
            if keep_online:
                filetree.move(tool, f"{online}/{Path(path).parent.as_posix()}/")
            else:
                tool.detach()
        native_names = []
        for entry in list(natives):
            if not is_directory(entry) and Path(entry.name()).suffix.casefold() in {
                ".dll",
                ".ini",
            }:
                native_names.append(entry.name())
                filetree.move(entry, f"{self._dll_directory}/")
        mod_entries = []
        if mod_folder is not None:
            mod_entries = [entry.name() for entry in mod_folder]
            if mod_folder.detach():
                filetree.merge(mod_folder)
        if keep_online:
            # everything else of the release, as it ships
            release = [entry for entry in root] if root is not filetree else [
                entry
                for entry in filetree
                if entry.name() not in mod_entries
                and entry.name().casefold() != self._dll_directory.casefold()
                and entry.name() != online
            ]
            for entry in release:
                filetree.move(entry, f"{online}/")
            if root is not filetree:
                root.detach()
            self.reforged_layout = {
                "online": online,
                "mod_entries": mod_entries,
                "natives": native_names,
            }
            if not self.previewing:
                qInfo(
                    "Elden Ring MO2: laid out ELDEN RING Reforged for MO2 and kept its "
                    f"release for its online mode in {online}."
                )
            return True
        # a release at the archive root keeps its notes (README, CHANGELOG)
        leftovers = (
            [root]
            if root is not filetree
            else [
                entry
                for entry in filetree
                if (
                    is_directory(entry)
                    and entry.name().casefold()
                    in {"internals", "dll", "addons", "extras", "troubleshooting"}
                )
                or (
                    not is_directory(entry)
                    and Path(entry.name()).suffix.casefold() in {".bat", ".sh", ".lnk"}
                )
            ]
        )
        for entry in leftovers:
            entry.detach()
        if not self.previewing:
            qInfo(
                "Elden Ring MO2: laid out ELDEN RING Reforged for MO2: its game files "
                "at the game root and its natives in DLLs; its launcher and me3 are "
                "not installed."
            )
        return True

    def _unwrap_mod_engine_paths(self, filetree: mobase.IFileTree) -> None:
        """Map a recognized Mod Engine ``mod/`` payload to the game root."""
        mod_folder = self._direct_directory(filetree, "mod")
        if mod_folder is None:
            return

        # Archives may add a named package directory inside ``mod`` too.
        self._unwrap_package_folders(mod_folder, allow_safe_multiple=True)
        recognized_directories = (
            self._asset_directories
            | self._legacy_dll_directories
            | {"config", "configs", "dlls", "fonts", "mod", "native", "natives", "plugins"}
        )
        has_known_route = any(
            is_directory(entry)
            and entry.name().casefold() in recognized_directories
            for entry in mod_folder
        )
        has_root_payload = any(
            not is_directory(entry)
            and Path(entry.name()).suffix.casefold() in self._supported_extensions
            for entry in mod_folder
        )
        if not (has_known_route or has_root_payload):
            return

        # Do not flatten when the archive already contains a conflicting path.
        # This preserves both copies instead of replacing one silently.
        if not self._package_folders_merge_without_conflicts(
            filetree, [mod_folder]
        ):
            return

        if self._merge_package_folder(filetree, mod_folder):
            # an archive's top folder can itself be called mod: mod/mod/parts
            self._unwrap_mod_engine_paths(filetree)

    @staticmethod
    def _merge_package_folder(
        destination: mobase.IFileTree, folder: mobase.IFileTree
    ) -> bool:
        """Flatten a preflighted wrapper even if detach is unavailable."""
        if folder.detach():
            destination.merge(folder)
            return True

        # Some archive-backed trees reject detach. Merge only when this cannot
        # recurse into the still-attached wrapper with a same-named child.
        # The caller's path-conflict preflight prevents replacement of payload.
        if any(
            entry.name().casefold() == folder.name().casefold()
            for entry in folder
        ):
            return False
        destination.merge(folder)
        if len(folder) == 0:
            folder.detach()
        return True

    def _unwrap_package_folders(
        self,
        filetree: mobase.IFileTree,
        allow_safe_multiple: bool = False,
    ) -> None:
        """Peel redundant wrappers at this tree boundary, not inside game assets."""
        for _ in range(self._archive_wrapper_depth_limit):
            companions = self._dll_companion_folders(filetree)
            candidates = [
                entry
                for entry in filetree
                if is_directory(entry)
                and entry.name().casefold() not in companions
                and self._is_package_wrapper(entry)
            ]
            # Several top-level folders can each contain valid game files.
            # Preserve them unless standard-layout processing has already
            # routed DLLs and a path scan proves their remaining assets do not
            # collide when merged.
            if len(candidates) > 1 and not allow_safe_multiple:
                return
            if not self._package_folders_merge_without_conflicts(filetree, candidates):
                return
            if not candidates:
                return

            for entry in candidates:
                if not self._merge_package_folder(filetree, entry):
                    return

    @staticmethod
    def _package_folders_merge_without_conflicts(
        destination: mobase.IFileTree,
        folders: list[mobase.IFileTree],
    ) -> bool:
        """Allow shared directories but reject conflicts with existing paths."""
        paths: dict[str, bool] = {}

        def add_entry(entry: mobase.IFileTree, prefix: str = "") -> bool:
            path = f"{prefix}/{entry.name()}" if prefix else entry.name()
            is_dir = is_directory(entry)
            key = path.casefold()
            previous_is_dir = paths.get(key)
            if previous_is_dir is not None and (
                not previous_is_dir or not is_dir
            ):
                return False
            paths[key] = is_dir
            if is_dir:
                return all(add_entry(child, path) for child in entry)
            return True

        folder_names = {folder.name().casefold() for folder in folders}
        if not all(
            add_entry(entry)
            for entry in destination
            if entry.name().casefold() not in folder_names
        ):
            return False
        return all(
            add_entry(entry)
            for folder in folders
            for entry in folder
        )

    def _move_standard_dlls_and_ini(self, filetree: mobase.IFileTree) -> None:
        """Place native DLLs and their companion files under Game/DLLs safely."""
        records: list[tuple[mobase.IFileTree, str]] = []
        font_directories: list[tuple[mobase.IFileTree, str]] = []
        asset_directories: list[tuple[mobase.IFileTree, str]] = []
        companion_directories: list[tuple[mobase.IFileTree, str]] = []

        def collect(tree: mobase.IFileTree, parent_path: str = "") -> None:
            companions = self._dll_companion_folders(tree)
            for entry in tree:
                relative_path = (
                    f"{parent_path}/{entry.name()}" if parent_path else entry.name()
                )
                if is_directory(entry):
                    # Seamless Co-op reads its settings, locale and crash
                    # reporter beside its DLL: its folder stays whole, as
                    # does the Reforged release kept for its own launcher
                    if entry.name().casefold() in {
                        "seamlesscoop",
                        self.ReforgedOnlineDirectory.casefold(),
                    }:
                        continue
                    if entry.name().casefold() in companions:
                        companion_directories.append((entry, parent_path))
                        continue
                    if entry.name().casefold() == "fonts":
                        font_directories.append((entry, parent_path))
                    if entry.name().casefold() == "assets":
                        asset_directories.append((entry, parent_path))
                    collect(entry, relative_path)
                else:
                    records.append((entry, parent_path))

        collect(filetree)
        dlls = [
            (entry, parent_path)
            for entry, parent_path in records
            if Path(entry.name()).suffix.casefold() == ".dll"
            and entry.name().casefold() not in self._root_loader_dlls
        ]
        root_loader_stems = {
            Path(entry.name()).stem.casefold()
            for entry, parent_path in records
            if not parent_path
            and Path(entry.name()).suffix.casefold() == ".dll"
            and entry.name().casefold() in self._root_loader_dlls
        }
        dll_groups: dict[str, list[tuple[mobase.IFileTree, str]]] = {}
        for entry, parent_path in dlls:
            dll_groups.setdefault(entry.name().casefold(), []).append(
                (entry, parent_path)
            )

        # two DLL folders with a config.ini each, or an assets folder each, can't
        # share DLLs/: those DLLs keep a folder of their own there
        dll_parents = {parent_path.casefold() for _entry, parent_path in dlls}
        sidecar_parents: dict[str, set[str]] = {}
        for entry, parent_path in records:
            if (
                parent_path.casefold() in dll_parents
                and Path(entry.name()).suffix.casefold() == ".ini"
            ):
                sidecar_parents.setdefault(entry.name().casefold(), set()).add(parent_path.casefold())
        for folder, parent_path in asset_directories:
            if parent_path.casefold() in dll_parents:
                sidecar_parents.setdefault(f"{folder.name().casefold()}/", set()).add(parent_path.casefold())
        shared_sidecar_parents = {
            parent
            for parents in sidecar_parents.values()
            if len(parents) > 1
            for parent in parents
            if parent and parent != self._dll_directory.casefold()
        }

        target_directories: dict[int, str] = {}
        for group in dll_groups.values():
            if len(group) == 1:
                entry, parent_path = group[0]
                if parent_path.casefold() in shared_sidecar_parents:
                    target_directories[id(entry)] = f"{self._dll_directory}/{parent_path}"
                    continue
                target_directories[id(entry)] = (
                    parent_path
                    if parent_path.casefold() == self._dll_directory.casefold()
                    else self._dll_directory
                )
                continue

            # Keep one already-direct DLL in place, then retain source folders
            # for same-named copies so none is silently overwritten.
            direct = next(
                (
                    (entry, parent_path)
                    for entry, parent_path in group
                    if parent_path.casefold() == self._dll_directory.casefold()
                ),
                None,
            )
            used_targets: set[str] = set()
            for index, (entry, parent_path) in enumerate(group, start=1):
                if direct is not None and entry is direct[0]:
                    destination = self._dll_directory
                elif parent_path.casefold().startswith(
                    self._dll_directory.casefold() + "/"
                ):
                    destination = parent_path
                elif parent_path:
                    destination = f"{self._dll_directory}/{parent_path}"
                else:
                    destination = f"{self._dll_directory}/_archive_conflicts/{index}"

                target_key = f"{destination}/{entry.name()}".casefold()
                while target_key in used_targets:
                    destination = f"{destination}/_duplicate_{index}"
                    target_key = f"{destination}/{entry.name()}".casefold()
                used_targets.add(target_key)
                target_directories[id(entry)] = destination

        for entry, parent_path in dlls:
            destination = target_directories[id(entry)]
            if parent_path.casefold() == destination.casefold():
                continue
            if not filetree.move(entry, f"{destination}/"):
                raise RuntimeError(
                    _tr("Could not move {0} into Game/{1}.").format(entry.name(), destination)
                )

        # a DLL's own data folders go with it, whole
        for folder, parent_path in companion_directories:
            destinations = {
                target_directories[id(dll)].casefold(): target_directories[id(dll)]
                for dll, dll_parent in dlls
                if dll_parent.casefold() == parent_path.casefold()
            }
            if len(destinations) != 1:
                continue
            destination = next(iter(destinations.values()))
            target = self._directory_at_path(filetree, destination)
            if parent_path.casefold() != destination.casefold() and (
                target is None or self._direct_directory(target, folder.name()) is None
            ):
                filetree.move(folder, f"{destination}/")

        # Keep sidecar assets beside native DLLs that came from a Revamped
        # ``native(s)/<mod>/`` tree. In particular, Nightreign Movement looks
        # for ``assets`` beside its DLL. MO2 layout moves that DLL under DLLs;
        # leaving assets in the original container would silently break it.
        for asset_directory, parent_path in asset_directories:
            container = parent_path.split("/", 1)[0].casefold()
            if container not in {"native", "natives"}:
                continue
            sibling_dlls = [
                dll
                for dll, dll_parent in dlls
                if dll_parent.casefold() == parent_path.casefold()
            ]
            destinations = {
                target_directories[id(dll)].casefold(): target_directories[id(dll)]
                for dll in sibling_dlls
            }
            if len(destinations) != 1:
                continue
            destination = next(iter(destinations.values()))
            target_parent = self._directory_at_path(filetree, destination)
            if target_parent is None:
                raise RuntimeError(
                    _tr("Could not preserve {0} beside its native DLL.").format(asset_directory.name())
                )
            existing_assets = next(
                (
                    entry
                    for entry in target_parent
                    if is_directory(entry)
                    and entry.name().casefold() == asset_directory.name().casefold()
                ),
                None,
            )
            if existing_assets is not None:
                if len(existing_assets) == 0:
                    existing_assets.detach()
                else:
                    raise RuntimeError(
                        _tr("MO2 layout found a conflicting assets directory beside "
                        "a native DLL. Choose Install as it comes in the archive to "
                        "preserve both folders without merging them.")
                    )
            if not filetree.move(asset_directory, f"{destination}/"):
                raise RuntimeError(
                    _tr("Could not move {0} beside its native DLL.").format(asset_directory.name())
                )

        # INI files are native-mod configuration files in the standard layout.
        # Keep a root loader's matching INI beside that proxy DLL; MO2 forces
        # those Windows loader proxies from the game root before managed DLLs.
        sidecar_records = [
            (entry, parent_path)
            for entry, parent_path in records
            if Path(entry.name()).suffix.casefold() in self._sidecar_extensions
        ]
        for entry, parent_path in sidecar_records:
            suffix = Path(entry.name()).suffix.casefold()
            stem = Path(entry.name()).stem.casefold()
            if not parent_path and (
                stem in root_loader_stems
                or self._is_dll_sidecar(
                    stem,
                    root_loader_stems,
                    {"config", "settings", "configuration", "options"},
                )
                or (stem in {"config", "configuration", "options", "settings"}
                    and len(root_loader_stems) == 1)
                or (stem == "mod_loader_config" and root_loader_stems)
            ):
                continue

            if suffix == ".ini":
                candidate_dlls = [
                    (dll, dll_parent)
                    for dll, dll_parent in dlls
                    if dll_parent.casefold() == parent_path.casefold()
                ]
                matched = [
                    (dll, dll_parent)
                    for dll, dll_parent in candidate_dlls
                    if Path(dll.name()).stem.casefold() == stem
                ]
                if not matched and stem in {
                    "config",
                    "configuration",
                    "options",
                    "settings",
                } and len(candidate_dlls) == 1:
                    matched = candidate_dlls
                if not matched and not parent_path and len(dlls) == 1:
                    matched = dlls

                if len(matched) == 1:
                    destination = self._dll_resource_directory(
                        matched[0][0], target_directories[id(matched[0][0])]
                    )
                elif not candidate_dlls:
                    # no DLL beside it moves (a ReShade preset, a loader's
                    # settings): it stays where the archive puts it
                    continue
                else:
                    destination = self._sidecar_destination(
                        entry, parent_path, records
                    )
            else:
                matching_dlls = [
                    (dll, dll_parent)
                    for dll, dll_parent in dlls
                    if dll_parent.casefold() == parent_path.casefold()
                    and self._is_dll_sidecar(
                        stem,
                        {Path(dll.name()).stem.casefold()},
                        {"config", "settings", "configuration", "options"},
                    )
                ]
                if len(matching_dlls) != 1:
                    continue
                destination = self._dll_resource_directory(
                    matching_dlls[0][0],
                    target_directories[id(matching_dlls[0][0])],
                )

            if parent_path.casefold() != destination.casefold():
                if not filetree.move(entry, f"{destination}/"):
                    raise RuntimeError(
                        _tr("Could not move {0} into Game/{1}.").format(entry.name(), destination)
                    )

        # Some native mods load fonts relative to the DLL's own directory.
        # Keep the conventional fonts folder beside the DLL after moving it
        # into MO2's DLLs folder. Do this only for a single, unambiguous font
        # folder so mixed archives cannot silently combine unrelated assets.
        if len(font_directories) == 1 and dlls:
            font_directory, font_parent = font_directories[0]
            sibling_dlls = [
                (dll, dll_parent)
                for dll, dll_parent in dlls
                if dll_parent.casefold() == font_parent.casefold()
            ]
            matching_dlls = sibling_dlls
            if not matching_dlls and font_parent.casefold() in {"", "dlls"}:
                matching_dlls = dlls
            if not matching_dlls:
                matching_dlls = [
                    (dll, dll_parent)
                    for dll, dll_parent in dlls
                    if self._dll_companion_directory_name(dll)
                    and font_parent.casefold()
                    == self._dll_companion_directory_name(dll).casefold()
                ]

            target_options = {
                self._dll_resource_directory(
                    dll, target_directories[id(dll)]
                ).casefold(): self._dll_resource_directory(
                    dll, target_directories[id(dll)]
                )
                for dll, _ in matching_dlls
            }
            if len(target_options) == 1:
                target_directory = next(iter(target_options.values()))
                source_font_path = (
                    f"{font_parent}/fonts" if font_parent else "fonts"
                )
                target_font_path = f"{target_directory}/fonts"
                target_parent = self._directory_at_path(filetree, target_directory)
                target_font_entry = (
                    next(
                        (
                            entry
                            for entry in target_parent
                            if entry.name().casefold() == "fonts"
                        ),
                        None,
                    )
                    if target_parent is not None
                    else None
                )
                if (
                    source_font_path.casefold() != target_font_path.casefold()
                    and target_font_entry is None
                    and not filetree.move(font_directory, f"{target_directory}/")
                ):
                    raise RuntimeError(
                        _tr("Could not move the mod fonts directory beside its DLL.")
                    )

        self._remove_empty_directories(filetree)

    @classmethod
    def _remove_empty_directories(cls, filetree: mobase.IFileTree) -> None:
        for entry in list(filetree):
            if is_directory(entry):
                cls._remove_empty_directories(entry)
                if len(entry) == 0:
                    entry.detach()

    def _sidecar_destination(
        self,
        entry: mobase.IFileTree,
        parent_path: str,
        records: list[tuple[mobase.IFileTree, str]],
    ) -> str:
        """Keep duplicate INI names in their original subfolders under DLLs."""
        count = sum(
            1
            for candidate, _ in records
            if Path(candidate.name()).suffix.casefold() == ".ini"
            and candidate.name().casefold() == entry.name().casefold()
        )
        if count > 1 and parent_path:
            if parent_path.casefold() == self._dll_directory.casefold():
                return self._dll_directory
            if parent_path.casefold().startswith(self._dll_directory.casefold() + "/"):
                return parent_path
            return f"{self._dll_directory}/{parent_path}"
        if count > 1:
            return f"{self._dll_directory}/_archive_conflicts/root"
        return self._dll_directory

    def _dll_companion_directory_name(
        self, dll: mobase.IFileTree
    ) -> str | None:
        return self._dll_companion_directories.get(
            Path(dll.name()).stem.casefold()
        )

    def _dll_resource_directory(
        self, dll: mobase.IFileTree, target_directory: str
    ) -> str:
        companion_directory = self._dll_companion_directory_name(dll)
        if companion_directory is None:
            return target_directory
        return f"{target_directory}/{companion_directory}"

    def _preserve_archive_layout_requested(self) -> bool:
        return bool(
            self._organizer.pluginSetting(
                self._plugin_name,
                EldenRingMo2Game.ArchiveLayoutOverrideSetting,
            )
        )

    def _is_saved_archive_layout_tree(self, filetree: mobase.IFileTree) -> bool:
        # MO2 asks for every mod at startup and on refreshes, on its GUI
        # thread. Most setups keep no archive layout, so answer those without
        # walking the tree; otherwise compare the root names before the full
        # path signature. Both shortcuts only skip trees that cannot match.
        signatures, roots = self._saved_archive_layout_signature_set()
        if not signatures:
            return False
        if not self._root_names_may_match(filetree, roots):
            return False
        target_signature = self._file_tree_signature(filetree)
        return bool(target_signature) and target_signature in signatures

    @classmethod
    def _signature_root_names(
        cls, filetree: mobase.IFileTree
    ) -> tuple[set[str], set[str]]:
        """First path parts of the root files a signature counts, and of the
        root folders (which count only when they hold such a file)."""
        files: set[str] = set()
        folders: set[str] = set()
        for entry in filetree:
            name = entry.name().replace("\\", "/")
            key = name.casefold().split("/", 1)[0]
            if is_directory(entry):
                folders.add(key)
            elif cls._signature_counts_file(name):
                files.add(key)
        return files, folders

    @classmethod
    def _root_names_may_match(
        cls, filetree: mobase.IFileTree, roots: set[frozenset[str]]
    ) -> bool:
        files, folders = cls._signature_root_names(filetree)
        present = files | folders
        return any(files <= root and root <= present for root in roots)

    def _saved_archive_layout_signature_set(
        self,
    ) -> tuple[set[tuple[str, ...]], set[frozenset[str]]]:
        """Path signatures of the mods installed with their archive layout
        kept, and the first path parts of each (cached until a mod is
        installed or removed, or the installer bumps the cache epoch)."""
        cache_epoch = self._archive_layout_cache_epoch()
        if (
            self._saved_archive_layout_signatures is not None
            and cache_epoch == self._saved_archive_layout_cache_epoch
        ):
            return (
                self._saved_archive_layout_signatures,
                self._saved_archive_layout_roots,
            )

        self._saved_archive_layout_signatures = None

        try:
            mod_list = self._organizer.modList()
            mod_names = mod_list.allMods()
        except Exception:
            return set(), set()

        signatures: set[tuple[str, ...]] = set()
        complete = True
        for mod_name in mod_names:
            try:
                mod = mod_list.getMod(mod_name)
                if mod is None or mod.isOverwrite():
                    continue
                settings = mod.pluginSettings(
                    EldenRingMo2Game.ArchiveLayoutInstallerName
                )
                if EldenRingMo2Game.PreserveArchiveLayoutSetting not in settings:
                    continue
                signature = self._file_tree_signature(mod.fileTree())
                if signature:
                    signatures.add(signature)
            except Exception:
                # A mod may be changing while MO2 rebuilds its data view. Keep
                # checking other entries and avoid retaining a partial cache.
                complete = False
                continue
        roots = {
            frozenset(path.split("/", 1)[0] for path in signature)
            for signature in signatures
        }
        current_epoch = self._archive_layout_cache_epoch()
        if complete and current_epoch == cache_epoch:
            self._saved_archive_layout_signatures = signatures
            self._saved_archive_layout_roots = roots
            self._saved_archive_layout_cache_epoch = current_epoch
        return signatures, roots

    def _archive_layout_cache_epoch(self):
        try:
            return self._organizer.pluginSetting(
                self._plugin_name,
                EldenRingMo2Game.ArchiveLayoutCacheEpochSetting,
            )
        except Exception:
            return None

    def _invalidate_saved_archive_layout_cache(self, *_args) -> None:
        self._saved_archive_layout_signatures = None

    @staticmethod
    def _name_suffix(name: str) -> str:
        """``Path(name).suffix`` for one entry name, without building a Path
        (this runs for every file of every mod)."""
        if "/" in name or "\\" in name or ":" in name:
            return Path(name).suffix
        index = name.rfind(".")
        return name[index:] if 0 < index < len(name) - 1 else ""

    @classmethod
    def _signature_counts_file(cls, name: str) -> bool:
        return (
            name.casefold() != "meta.ini"
            and cls._name_suffix(name).casefold() != ".mohidden"
        )

    @classmethod
    def _file_tree_signature(cls, filetree: mobase.IFileTree) -> tuple[str, ...]:
        paths: list[str] = []

        def collect(tree: mobase.IFileTree, prefix: str = "") -> None:
            for entry in tree:
                name = entry.name().replace("\\", "/")
                relative_path = f"{prefix}/{name}" if prefix else name
                if is_directory(entry):
                    collect(entry, relative_path)
                elif cls._signature_counts_file(name):
                    paths.append(relative_path.casefold())

        collect(filetree)
        return tuple(sorted(paths))

    def _fix_known_layouts(self, filetree: mobase.IFileTree, depth: int = 0) -> None:
        if depth > self._archive_wrapper_depth_limit:
            return
        self._fix_basic_patterns(filetree, depth)
        for entry in list(filetree):
            if is_directory(entry) and not (
                depth == 0 and entry.name() == self.ReforgedOnlineDirectory
            ):
                self._fix_known_layouts(entry, depth + 1)

    def _fix_basic_patterns(
        self, filetree: mobase.IFileTree, depth: int = 0
    ) -> mobase.IFileTree:
        """Apply MO2's basic patterns while retaining user-selected text files."""
        patterns = self._regex_patterns
        for entry in list(filetree):
            name = entry.name()
            if (
                not is_directory(entry)
                and Path(name).suffix.casefold() in self._preserve_extensions
            ):
                continue

            if patterns.unfold.match(name):
                # only a folder at the top, and never over a file already there:
                # chr/game is a game path, and a file can have that name too
                if (
                    is_directory(entry)
                    and depth == 0
                    and self._package_folders_merge_without_conflicts(filetree, [entry])
                    and entry.detach()
                ):
                    filetree.merge(entry)
            elif patterns.valid.match(name):
                continue
            elif patterns.delete.match(name):
                # the patterns mean notes: a folder called "Credits Skip" that
                # holds a mod isn't one
                if not is_directory(entry) or not self._contains_supported_payload(entry):
                    entry.detach()
            elif (move_key := patterns.move_match(name)) is not None:
                filetree.move(entry, self._file_patterns.move[move_key])

        return filetree

    def _has_wrapped_payload(self, filetree: mobase.IFileTree) -> bool:
        for entry in filetree:
            if (
                is_directory(entry)
                and entry.name().casefold() in {"mod", "native", "natives"}
                and self._contains_supported_payload(entry)
            ):
                return True
            if (
                is_directory(entry)
                and entry.name().casefold() in self._legacy_dll_directories
                and any(
                    not is_directory(child)
                    and Path(child.name()).suffix.casefold() == ".dll"
                    for child in entry
                )
            ):
                return True
        return False

    def _has_misplaced_dll(
        self, filetree: mobase.IFileTree, parent_name: str = "", depth: int = 0
    ) -> bool:
        if depth > self._archive_wrapper_depth_limit:
            return False
        managed = self._managed_dll_directories
        # a DLL in this folder is misplaced unless the folder is a managed one
        dll_misplaced_here = bool(parent_name) and parent_name not in managed
        name_suffix = self._name_suffix
        for entry in filetree:
            name = entry.name().casefold()
            if is_directory(entry):
                # Managed DLL directories are already in a valid destination.
                if name in managed:
                    continue
                if name in {"mod", "native", "natives"} and any(
                    not is_directory(child)
                    and name_suffix(child.name()).casefold() == ".dll"
                    for child in entry
                ):
                    return True
                if self._has_misplaced_dll(entry, name, depth + 1):
                    return True
            elif dll_misplaced_here and name_suffix(name).casefold() == ".dll":
                return True
        return False

    def _is_package_wrapper(self, folder: mobase.IFileTree) -> bool:
        name = folder.name().casefold()
        # FRZN Merge and similar archives use this name for files intended to
        # land at the game root. Preserve every child path, but do not expose
        # the staging folder itself as a virtual game directory.
        if name == "drag files into here":
            return self._contains_supported_payload(folder)

        # Seamless Co-op reads its settings and locale beside its DLL
        if (
            name in self._content_directories
            or name == "seamlesscoop"
            or name == self.ReforgedOnlineDirectory.casefold()
        ):
            return False

        entries = list(folder)
        entries = [
            entry
            for entry in entries
            if not self._regex_patterns.delete.match(entry.name().casefold())
        ]
        if not entries or not self._contains_supported_payload(folder):
            return False

        # A named archive folder containing only known game-root containers is
        # a redundant package wrapper. Also handle a named DLL package with
        # files directly inside it.
        if all(
            is_directory(entry)
            and entry.name().casefold() in self._package_directories
            for entry in entries
        ):
            return True

        # A named folder may contain game-root directories mixed with root
        # files. Treat those known children as the payload boundary so MO2
        # unwraps the name rather than preserving it above game paths.
        if any(
            (
                is_directory(entry)
                and entry.name().casefold() in self._content_directories
            )
            or (
                not is_directory(entry)
                and Path(entry.name()).suffix.casefold() in self._supported_extensions
            )
            for entry in entries
        ):
            return True

        # Archive files sometimes add one or more named folders around the
        # actual payload, alongside notes or other non-game files. Unwrap a
        # folder when exactly one child directory contains supported game
        # content; sibling entries remain in the tree when it is merged.
        payload_directories = [
            entry
            for entry in entries
            if is_directory(entry) and self._contains_supported_payload(entry)
        ]
        if len(payload_directories) == 1:
            return True

        # A single named folder can simply wrap a complete game-root payload.
        # Unwrap it, but keep recognized game directories such as ``parts``
        # and ``menu`` intact via the content-directory guard above.
        if len(entries) == 1 and is_directory(entries[0]):
            return self._contains_supported_payload(entries[0])

        return all(not is_directory(entry) for entry in entries) and any(
            Path(entry.name()).suffix.casefold() == ".dll" for entry in entries
        )

    def _contains_supported_payload(
        self, filetree: mobase.IFileTree, depth: int = 0
    ) -> bool:
        if depth > self._archive_wrapper_depth_limit:
            return False
        for entry in filetree:
            if is_directory(entry):
                if self._contains_supported_payload(entry, depth + 1):
                    return True
            elif Path(entry.name()).suffix.casefold() in self._supported_extensions:
                return True
        return False

    @staticmethod
    def _direct_directory(
        filetree: mobase.IFileTree, name: str
    ) -> mobase.IFileTree | None:
        normalized_name = name.casefold()
        for entry in filetree:
            if is_directory(entry) and entry.name().casefold() == normalized_name:
                return entry
        return None

    @staticmethod
    def _directory_at_path(
        filetree: mobase.IFileTree, path: str
    ) -> mobase.IFileTree | None:
        current = filetree
        for part in (part for part in path.replace("\\", "/").split("/") if part):
            current = EldenRingModDataChecker._direct_directory(current, part)
            if current is None:
                return None
        return current

    @staticmethod
    def _is_dll_sidecar(
        sidecar_stem: str, dll_stems: set[str], config_suffixes: set[str]
    ) -> bool:
        if sidecar_stem in dll_stems:
            return True

        # Some mods use names like ``AdaptiveBarScaling_config.ini`` rather
        # than an exact DLL stem. Only recognized config labels are associated
        # so unrelated game files are not pulled into DLLs.
        for dll_stem in dll_stems:
            for separator in ("_", "-", ".", " "):
                prefix = f"{dll_stem}{separator}"
                if (
                    sidecar_stem.startswith(prefix)
                    and sidecar_stem[len(prefix):] in config_suffixes
                ):
                    return True
        return False


class EldenRingLocalSavegames(mobase.LocalSavegames):
    """Route Elden Ring saves according to the active profile's save mode."""

    # the instance's folder for saves shared by its profiles
    SharedSavesFolderName = "Elden Ring Shared Saves"

    ConfigFilename = "eldenring_mo2_saves.ini"
    ProfileIsolated = "profile"
    InstanceShared = "instance_shared"
    GlobalShared = "global"
    SaveProfilesDirectoryName = "save_profiles"
    ProfileSaveProfileKey = "profile_save_profile"
    InstanceSaveProfileKey = "instance_save_profile"
    Modes = {ProfileIsolated, InstanceShared, GlobalShared}

    # the organizer of this MO2 instance, for its base folder
    _path_organizer: mobase.IOrganizer | None = None

    def __init__(self, game, organizer: mobase.IOrganizer):
        super().__init__()
        self._game = game
        self._organizer = organizer
        EldenRingLocalSavegames._path_organizer = organizer
        self._prepared_profile_routes: dict[str, tuple[str, bool, str]] = {}

    @staticmethod
    def _profile_key(profile: mobase.IProfile) -> str:
        path = Path(profile.absolutePath())
        try:
            return os.path.normcase(str(path.resolve()))
        except (OSError, RuntimeError):
            return os.path.normcase(os.path.abspath(path))

    @classmethod
    def mode_for_profile_directory(cls, profile_directory: Path) -> str:
        config_path = profile_directory / cls.ConfigFilename
        config = configparser.ConfigParser(interpolation=None)
        try:
            config.read(config_path, encoding="utf-8-sig")
            mode = config.get("Saves", "mode", fallback=cls.ProfileIsolated).strip().lower()
        except (OSError, UnicodeError, configparser.Error, ValueError):
            mode = cls.ProfileIsolated
        return mode if mode in cls.Modes else cls.ProfileIsolated

    @classmethod
    def mode_for_profile(cls, profile) -> str:
        directory = Path(profile.absolutePath())
        mode = cls.mode_for_profile_directory(directory)
        if mode == cls.ProfileIsolated and not cls._mode_is_stored(directory):
            # no valid mode in the ini: MO2 then follows the profile's own
            # LocalSaves setting, so this does too (Global when it is off)
            try:
                if not profile.localSavesEnabled():
                    return cls.GlobalShared
            except (AttributeError, RuntimeError):
                pass
        return mode

    @classmethod
    def _mode_is_stored(cls, profile_directory: Path) -> bool:
        config = configparser.ConfigParser(interpolation=None)
        try:
            config.read(profile_directory / cls.ConfigFilename, encoding="utf-8-sig")
            mode = config.get("Saves", "mode", fallback="").strip().lower()
        except (OSError, UnicodeError, configparser.Error, ValueError):
            return False
        return mode in cls.Modes

    @classmethod
    def save_profile_for_profile_directory(
        cls, profile_directory: Path, mode: str
    ) -> str:
        if mode == cls.ProfileIsolated:
            key = cls.ProfileSaveProfileKey
        elif mode == cls.InstanceShared:
            key = cls.InstanceSaveProfileKey
        else:
            return ""

        config = configparser.ConfigParser(interpolation=None)
        try:
            config.read(profile_directory / cls.ConfigFilename, encoding="utf-8-sig")
            profile_id = config.get("Saves", key, fallback="").strip().lower()
        except (OSError, UnicodeError, configparser.Error, ValueError):
            return ""
        if len(profile_id) != 32 or any(
            character not in "0123456789abcdef" for character in profile_id
        ):
            return ""
        return profile_id

    @classmethod
    def save_profile_is_available(
        cls, profile_directory: Path, mode: str
    ) -> bool:
        profile_id = cls.save_profile_for_profile_directory(profile_directory, mode)
        if not profile_id:
            return True
        if mode == cls.ProfileIsolated:
            root = profile_directory / cls.SaveProfilesDirectoryName
        elif mode == cls.InstanceShared:
            root = cls.instance_shared_directory(profile_directory) / cls.SaveProfilesDirectoryName
        else:
            return False
        save_directory = root / profile_id
        metadata_path = root / f"{profile_id}.json"
        try:
            save_stat = os.lstat(save_directory)
            metadata_stat = os.lstat(metadata_path)
            reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
            if (
                not stat.S_ISDIR(save_stat.st_mode)
                or not stat.S_ISREG(metadata_stat.st_mode)
                or stat.S_ISLNK(save_stat.st_mode)
                or stat.S_ISLNK(metadata_stat.st_mode)
                or getattr(save_stat, "st_file_attributes", 0) & reparse_flag
                or getattr(metadata_stat, "st_file_attributes", 0) & reparse_flag
            ):
                return False
            metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError, ValueError):
            return False
        return (
            isinstance(metadata, dict)
            and metadata.get("id") == profile_id
            and isinstance(metadata.get("name"), str)
            and bool(metadata.get("name", "").strip())
        )
    @classmethod
    def active_save_directory(
        cls, profile_directory: Path, mode: str
    ) -> Path | None:
        if mode == cls.GlobalShared:
            return None
        profile_id = cls.save_profile_for_profile_directory(profile_directory, mode)
        if mode == cls.InstanceShared:
            shared_root = cls.instance_shared_directory(profile_directory)
            if profile_id:
                return shared_root / cls.SaveProfilesDirectoryName / profile_id
            return shared_root
        if profile_id:
            return profile_directory / cls.SaveProfilesDirectoryName / profile_id
        return profile_directory / "saves"

    @classmethod
    def instance_shared_directory(cls, profile_path: Path) -> Path:
        # The folder belongs to the MO2 instance. Earlier builds put it beside
        # the profiles folder, which is the same place unless the profiles
        # path was customised (two instances could then share it); such a
        # folder that already holds saves is kept so nothing goes missing.
        legacy = profile_path.parent.parent / cls.SharedSavesFolderName
        try:
            base = Path(cls._path_organizer.basePath()) if cls._path_organizer else None
        except (AttributeError, RuntimeError, TypeError):
            base = None
        if base is None or not str(base):
            return legacy
        anchored = base / cls.SharedSavesFolderName
        if os.path.normcase(os.path.abspath(anchored)) == os.path.normcase(os.path.abspath(legacy)):
            return anchored
        try:
            anchored_used = anchored.is_dir() and any(anchored.iterdir())
            if not anchored_used and legacy.is_dir() and any(legacy.iterdir()):
                return legacy
        except OSError:
            pass
        return anchored

    def prepareProfile(self, profile: mobase.IProfile) -> bool:
        mode = self.mode_for_profile(profile)
        local_saves_enabled = bool(profile.localSavesEnabled())
        save_profile_id = self.save_profile_for_profile_directory(
            Path(profile.absolutePath()), mode
        )
        self._prepared_profile_routes[self._profile_key(profile)] = (
            mode,
            local_saves_enabled,
            save_profile_id,
        )
        return mode != self.GlobalShared and local_saves_enabled

    def profile_route_is_current(self, profile: mobase.IProfile) -> bool:
        mode = self.mode_for_profile(profile)
        current_route = (
            mode,
            bool(profile.localSavesEnabled()),
            self.save_profile_for_profile_directory(
                Path(profile.absolutePath()), mode
            ),
        )
        profile_key = self._profile_key(profile)
        prepared_route = self._prepared_profile_routes.get(profile_key)
        if prepared_route == current_route:
            return True

        # Profile-isolated and instance-shared saves both use MO2's local-save
        # mapping. Those routes, including named save profiles, can be refreshed
        # for the next launch without restarting MO2. Crossing the Steam
        # boundary still requires reloading MO2's profile-specific-save setting.
        local_modes = {self.ProfileIsolated, self.InstanceShared}
        if (
            prepared_route is not None
            and prepared_route[0] in local_modes
            and mode in local_modes
            and prepared_route[1]
            and current_route[1]
        ):
            self._prepared_profile_routes[profile_key] = current_route
            return True

        return False

    def mappings(self, profile_save_dir: QDir):
        # MO2 passes the save directory for the profile whose VFS mapping it is
        # currently building. During a profile or instance switch,
        # organizer.profile() can still refer to the previous profile or be
        # temporarily unavailable. Use the supplied path as the source of
        # truth so the mapping cannot silently fall back to the Steam save.
        profile_save_path = Path(profile_save_dir.absolutePath())
        profile_directory = profile_save_path.parent
        mode = self.mode_for_profile_directory(profile_directory)
        if mode == self.GlobalShared:
            qInfo(
                "Elden Ring MO2: save mapping uses the global Steam folder; "
                f"profile='{profile_directory}', game saves='{self._game.savesDirectory().absolutePath()}'."
            )
            return []

        source = self.active_save_directory(profile_directory, mode)
        if source is None:
            return []

        destination = self._game.savesDirectory().absolutePath()
        qInfo(
            "Elden Ring MO2: save mapping prepared; "
            f"mode='{mode}', profile='{profile_directory}', "
            f"source='{source}', game saves='{destination}'."
        )

        return [
            mobase.Mapping(
                source=str(source),
                destination=destination,
                is_directory=True,
                create_target=True,
            )
        ]


class EldenRingMo2Game(BasicGame):
    Name = "Elden Ring MO2 Support"
    Author = "ArialSenki"
    Version = "0.5.0-alpha.94"
    NativeBridgeVersion = "0.5.0-alpha.62"

    GameName = "ELDEN RING"
    GameShortName = "eldenring"
    GameNexusName = "eldenring"
    GameSteamId = 1245620
    GameBinary = "Game/eldenring.exe"
    GameDataPath = "Game"
    GameSaveExtension = "sl2"
    # the names that differ between Elden Ring and Nightreign
    GameProcess = "eldenring.exe"
    # the code around a call to the game's file loader, Elden Ring then Nightreign:
    # install_loose_asset_hook in the bridge looks for the same, change both together
    ResolverCallSites = (
        (b"", bytes.fromhex("48837B2008488D4B087203488B094C8B4B1841B8050000004D3BC8")),
        (bytes.fromhex("33D24889442420498BCD"), bytes.fromhex("48837F18087203488B3F48837B1805")),
    )
    SaveFolderName = "EldenRing"  # in %APPDATA%
    SavePrefix = "er0000."  # the save files: ER0000.sl2, .co2, .bak...
    GlobalBackupsFolderName = "Elden Ring Global Save Backups"
    LocalSavegamesClass = EldenRingLocalSavegames
    # Elden Ring keeps the recovery folder name it always had; other games
    # get their own, so a session is never restored into another game
    RecoveryScope = ""
    NativeDllSetting = "load_native_dlls"
    SaveBackupsSetting = "save_backups_kept"
    SaveBackupsDefault = 5
    ArchiveLayoutOverrideSetting = "_archive_layout_override"
    ArchiveLayoutInstallerName = "Elden Ring Archive Layout Installer"
    PreserveArchiveLayoutSetting = "preserve_archive_layout"
    ArchiveLayoutCacheEpochSetting = "_archive_layout_cache_epoch"
    InstalledNativeRouteIndexSetting = "installed_native_route_index"
    ProfileLaunchConfig = "eldenring_mo2_startup.ini"
    ProfileLaunchConfigEnvironment = "ELDENRING_MO2_PROFILE_CONFIG"
    NativeInitializerEnvironment = "ELDENRING_MO2_NATIVE_INITIALIZERS"
    SaveRecoveryDirectoryName = ".MO2RevampedSaveRecovery"
    SaveRecoveryManifestName = "session.json"
    # held open by the MO2 process that hid the saves, so a second MO2 of the
    # same install leaves its session alone
    SaveRecoveryLockName = ".owner.lock"
    UnroutedSaveDirectoryName = "unrouted-global"
    # the game's name in messages that name Elden Ring
    DisplayName = "Elden Ring"
    # Seamless Co-op's DLL, run from its MO2 mod with the game
    SeamlessDll = "ersc.dll"
    # Programs that start eldenring.exe themselves (Reforged's launcher) get
    # the same save guard as the game, and the guard waits for the game they
    # start instead of for the launcher.
    GameLaunchers = frozenset(
        {
            "eldenring.exe",
            "start_protected_game.exe",
            # ELDEN RING Reforged's own launcher (its online mode)
            "reforgedlauncher.exe",
        }
    )
    # Launchers that fail under MO2's virtual file system: MO2 starts them
    # itself, outside the VFS, and swaps the route's saves into the Steam
    # folder for the run. Each executable can be switched either way in Save
    # Isolation.
    AutoSwapLaunchers = frozenset(
        {
            # its online mode checks a clean release: no MO2 files or DLLs
            "reforgedlauncher.exe",
        }
    )
    # Reforged's online launcher, always run outside the VFS (inside it, MO2's
    # mods and natives would join the release its online mode checks)
    ReforgedOnlineLauncher = "reforgedlauncher.exe"
    # Online play keeps its own save, in this folder inside the profile's
    # route, apart from the offline save played with other MO2 mods (the
    # first online run starts from a copy of it). Save Isolation's switch,
    # kept in the launch methods file, can share one save between both.
    ReforgedOnlineSavesFolder = "reforged_online"
    ReforgedOnlineSavesKey = "reforged_online_saves"
    LaunchMethodsFilename = "eldenring_mo2_launch_methods.json"
    LaunchMethodVfs = "vfs"
    LaunchMethodSwap = "swap"
    # in a swap session's recovery folder: the Steam folder's own loose
    # files (GraphicsConfig.xml) while the route's copies stand in for them
    SwapRootFilesDirectoryName = "steam-root-files"
    # Easy Anti-Cheat's launcher has the same name in every game, so only the
    # one in this game's folder counts
    AntiCheatLauncher = "start_protected_game.exe"
    LauncherStartGrace = 90  # seconds a launcher may take to start the game
    # seconds without a readable process list before a watch gives up; a launcher
    # that is open is waited for without limit: it can start the game at any time
    LauncherWaitLimit = 600
    # seconds a launcher stays open after its game closed before MO2 says so
    LauncherNoticeDelay = 15
    GameWatchInterval = 5000  # milliseconds between checks for the game process
    EarlyExitSeconds = 30  # a game that closes sooner did not really start
    EarlyExitRecheck = 3000  # milliseconds to see that it did not start again
    # while a launch waits for the game to appear: a game that crashes at
    # start lives about two seconds, and once seen its close restores the
    # saves without the launcher grace
    LaunchWatchInterval = 1000
    # Steam Cloud looks at its save a second or two after the game closes: saves
    # of a launch outside the VFS have to be back by then
    SwapWatchInterval = 500
    # what Steam Cloud syncs of this game, and the game's own copy of it
    SteamCloudSaves = ("ER0000.sl2", "ER0000.sl2.bak")

    def init(self, organizer: mobase.IOrganizer) -> bool:
        if not super().init(organizer):
            return False

        self._refined_ui_filters = []

        self._profile_config_env_active = False
        self._profile_config_env_previous = None
        self._native_initializer_env_active = False
        self._native_initializer_env_previous = None
        self._overwrite_cleanup_armed = False
        self._overwrite_cleanup_path: Path | None = None
        self._active_save_quarantine: Path | None = None
        self._launch_via_launcher = False
        # each game launch gets a number; watchers of an older launch leave the
        # newer one alone
        self._game_run = 0
        self._game_seen_run = -1
        self._overwrite_cleanup_config: Path | None = None
        # a launcher may take a while to start the game; until then a new
        # launch must not treat the game as closed
        self._launch_pending_until = 0.0
        # the paths of accepted game launches, for the finished-run hook
        self._guarded_launches: set[str] = set()
        # recovery sessions this process holds (see SaveRecoveryLockName)
        self._session_locks: dict[str, object] = {}
        # set when recovery waits for the game, a launcher or another MO2
        # window rather than failing
        self._recovery_waiting = False
        self._recovered_sessions = 0
        self._recovery_retry_pending = False
        # folders with newer saves that a restore had to move aside
        self._displaced_newer_saves: list[Path] = []
        # saves a swap's return kept in the recovery folder instead of replacing them
        self._kept_aside_saves: list[Path] = []
        # (route, files changed) of swaps returned by recovery, with no MO2 there
        # when their game closed
        self._returned_saves: list[tuple[Path, int]] = []
        self._recovering_earlier_session = False
        self._main_window = None
        self._launcher_notice = None
        self._launcher_notice_session: Path | None = None
        self._launcher_open_since: float | None = None
        self._loose_files_warning_shown = False
        self._early_exit_warning_shown = False
        self._file_loader_scan: tuple[str, bool | None] = ("", None)
        self._save_extension_scan: tuple[str, bool | None] = ("", None)
        self._online_programs_selector = None
        self._online_program_locked = False
        self._online_programs_sync_queued = False
        self._last_launch_time = 0.0
        self._mod_turned_on_for_launch: str | None = None

        self._local_savegames_feature = self.LocalSavegamesClass(self, organizer)
        self._register_feature(self._local_savegames_feature)
        self._register_feature(
            EldenRingModDataChecker(organizer, self.name(), watch_mod_list=True)
        )
        if not self._recover_stale_global_save_quarantines():
            qCritical(
                "Elden Ring MO2: a previous save-isolation session needs "
                "recovery before Elden Ring can be launched."
            )
        # The game plugin is discovered for every MO2 install, but its UI work
        # must only run in the instance that actually manages Elden Ring.
        organizer.onUserInterfaceInitialized(self._initialize_elden_ring_ui)
        if not organizer.onAboutToRun(self._prepare_profile_launch):
            qCritical("Elden Ring MO2: could not register the profile launch hook.")
            return False
        if not organizer.onFinishedRun(self._finish_profile_launch):
            qCritical("Elden Ring MO2: could not register the profile cleanup hook.")
            return False
        qInfo(
            "Elden Ring MO2 support "
            f"{self.Version} initialized; native bridge component "
            f"{self.NativeBridgeVersion}."
        )
        return True

    def setGamePath(self, path) -> None:
        super().setGamePath(_with_true_case(path))

    def _install_root(self) -> Path:
        try:
            return Path(__file__).resolve().parents[3]
        except (IndexError, OSError, RuntimeError):
            return Path(self._organizer.basePath())

    def _save_quarantine_parent(self) -> Path:
        """The folder that holds every install's recovery folder: beside the
        Steam save folder, or beside its target when that folder is a link to
        another drive (a folder can't be moved across drives in one step)."""
        global_save_root = Path(self.savesDirectory().absolutePath())
        parent = global_save_root.parent
        try:
            target = global_save_root.resolve()
            if (
                os.path.splitdrive(str(target))[0].casefold()
                != os.path.splitdrive(str(parent.resolve()))[0].casefold()
            ):
                parent = target.parent
        except (OSError, RuntimeError):
            pass
        return parent / self.SaveRecoveryDirectoryName

    def _save_quarantine_root(self) -> Path:
        install_root = self._install_root()
        try:
            install_identity = os.path.normcase(str(install_root.resolve()))
        except (OSError, RuntimeError):
            install_identity = os.path.normcase(os.path.abspath(install_root))
        instance_token = hashlib.sha256(
            (install_identity + self.RecoveryScope).encode("utf-8", errors="replace")
        ).hexdigest()[:24]
        return self._save_quarantine_parent() / instance_token

    def _session_is_pending(self, session: Path) -> bool:
        # a session that only keeps unrouted output has nothing left to restore
        children = [child for child in session.iterdir() if child.is_dir()]
        return (
            not children
            or any(child.name != self.UnroutedSaveDirectoryName for child in children)
            or self._swap_not_returned(session)
        )

    def _swap_not_returned(self, session: Path) -> bool:
        """Whether the route's saves of this session may still be in the Steam
        folder. Such a session can hold no folder at all (a Steam folder that was
        empty)."""
        try:
            manifest = json.loads(
                (session / self.SaveRecoveryManifestName).read_text(encoding="utf-8")
            )
            swap = manifest.get("swap")
        except (OSError, UnicodeError, ValueError, AttributeError):
            return False
        return isinstance(swap, dict) and not swap.get("returned")

    def _other_install_sessions(self) -> list[tuple[Path, bool]]:
        """Sessions of other MO2 Revamped installs that still hide Steam
        account folders of this game, each with whether the install that made
        it still exists."""
        own = self._save_quarantine_root()
        own_saves = self.savesDirectory().absolutePath()
        sessions: list[tuple[Path, bool]] = []
        try:
            for install in own.parent.iterdir():
                if not install.is_dir() or os.path.normcase(str(install)) == os.path.normcase(str(own)):
                    continue
                for session in install.iterdir():
                    if not session.is_dir() or not (
                        any(
                            child.is_dir() and child.name != self.UnroutedSaveDirectoryName
                            for child in session.iterdir()
                        )
                        or self._swap_not_returned(session)
                    ):
                        continue
                    try:
                        manifest = json.loads(
                            (session / self.SaveRecoveryManifestName).read_text(encoding="utf-8")
                        )
                        owner = manifest.get("install_root", "")
                        hidden_from = str(manifest.get("global_save_root", ""))
                    except (OSError, UnicodeError, ValueError, AttributeError):
                        continue
                    if hidden_from and os.path.normcase(
                        os.path.abspath(hidden_from)
                    ) != os.path.normcase(os.path.abspath(own_saves)):
                        continue
                    # a record without its install (older releases), or of an
                    # install that was moved or removed: nothing else will
                    # ever put these saves back
                    owner_exists = (
                        isinstance(owner, str)
                        and bool(owner.strip())
                        and Path(owner).is_dir()
                    )
                    sessions.append((session, owner_exists))
        except OSError:
            pass
        return sessions

    def _foreign_pending_sessions(self) -> list[Path]:
        """Sessions of other MO2 Revamped installs that still hide Steam
        account folders and that this install may not restore yet (see
        _stale_foreign_sessions); a launch from here would play against a
        Steam folder that is not complete."""
        return [session for session, owner_exists in self._other_install_sessions() if owner_exists]

    def _stale_foreign_sessions(self) -> list[Path]:
        """Sessions of other installs old enough that their launch is over:
        recovery restores them here too, once their lock is free (no MO2 of
        that install waits for its game) and the game and its launchers are
        closed. Their record says where everything goes back, so the other
        install no longer has to be started just to put the Steam saves back.
        A newer one may still belong to a launch that is starting."""
        stale = []
        now = time.time()
        for session in self._foreign_pending_sessions():
            try:
                written = (session / self.SaveRecoveryManifestName).stat().st_mtime
            except OSError:
                continue
            if now - written >= self.LauncherStartGrace:
                stale.append(session)
        return stale

    def _orphaned_sessions(self) -> list[Path]:
        """Sessions whose install no longer exists: this install restores
        them, so they don't block every launch forever."""
        return [
            session for session, owner_exists in self._other_install_sessions() if not owner_exists
        ]

    def _claim_save_quarantine(self, session_directory: Path) -> bool:
        """Takes the session for this MO2 process. False while another MO2
        process of this install holds it: it hid the saves for its own launch
        and may still be waiting for its game. Windows drops the hold when that
        process ends, even in a crash."""
        key = os.path.normcase(str(session_directory))
        if msvcrt is None or key in self._session_locks:
            return True
        try:
            stream = (session_directory / self.SaveRecoveryLockName).open("a+b")
        except OSError:
            # no lock file (a read-only folder): the session works as before
            return True
        try:
            stream.seek(0)
            msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError:
            stream.close()
            return False
        self._session_locks[key] = stream
        return True

    def _release_save_quarantine(self, session_directory: Path) -> None:
        stream = self._session_locks.pop(os.path.normcase(str(session_directory)), None)
        if stream is not None:
            try:
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            except OSError:
                pass
            stream.close()
        try:
            (session_directory / self.SaveRecoveryLockName).unlink(missing_ok=True)
        except OSError:
            # still open in another MO2 process
            pass

    def _newest_save_time(self, directory: Path) -> int:
        newest = 0
        try:
            for entry in directory.iterdir():
                if entry.is_file() and entry.name.casefold().startswith(self.SavePrefix):
                    newest = max(newest, entry.stat().st_mtime_ns)
        except OSError:
            pass
        return newest

    @staticmethod
    def _write_save_quarantine_manifest(session_directory: Path, manifest: dict) -> None:
        manifest_path = session_directory / EldenRingMo2Game.SaveRecoveryManifestName
        temporary_path = session_directory / f".{manifest_path.name}.tmp"
        with temporary_path.open("w", encoding="utf-8", newline="\n") as stream:
            json.dump(manifest, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_path, manifest_path)

    @classmethod
    def _save_directory_contains_elden_ring_saves(cls, directory: Path) -> bool:
        # A SteamID64 folder is an account's save folder even when it only holds
        # files of mods with their own extension (Seamless Co-op .co2,
        # alt-saves/Convergence .mod, a custom save_file_extension), so the name
        # alone is enough; other folders count when they hold Elden Ring saves.
        name = directory.name
        if len(name) == 17 and name.isdigit() and name.startswith("7656119"):
            return True
        for entry in directory.iterdir():
            if not entry.is_file():
                continue
            entry_name = entry.name.casefold()
            if entry_name == "steam_autocloud.vdf" or entry_name.startswith(cls.SavePrefix):
                return True
            if entry_name.endswith((".sl2", ".sl2.bak", ".co2", ".co2.bak", ".err", ".err.bak")):
                return True
        return False

    @classmethod
    def _is_game_launch(cls, app_path: str, args: str = "") -> bool:
        # exact names only: a save tool or another program with a similar
        # name must not hide the Steam saves
        return Path(app_path).name.casefold() in cls.GameLaunchers

    @classmethod
    def launch_methods_path(cls, instance_base: Path) -> Path:
        return instance_base / cls.LaunchMethodsFilename

    @classmethod
    def read_launch_methods(cls, instance_base: Path) -> dict[str, str]:
        """The executables switched by hand in Save Isolation, keyed by their
        normalised binary path."""
        try:
            data = json.loads(cls.launch_methods_path(instance_base).read_text(encoding="utf-8"))
        except (OSError, UnicodeError, ValueError):
            return {}
        methods = data.get("methods") if isinstance(data, dict) else None
        if not isinstance(methods, dict):
            return {}
        return {
            str(key): value
            for key, value in methods.items()
            if value in {cls.LaunchMethodVfs, cls.LaunchMethodSwap}
        }

    @classmethod
    def reforged_online_saves_separate(cls, instance_base: Path) -> bool:
        """Whether Reforged's online play keeps its own save (the default)."""
        try:
            data = json.loads(cls.launch_methods_path(instance_base).read_text(encoding="utf-8"))
        except (OSError, UnicodeError, ValueError):
            return True
        if not isinstance(data, dict):
            return True
        return data.get(cls.ReforgedOnlineSavesKey, "separate") != "shared"

    def _reforged_online_route(self, route: Path) -> Path:
        """The online save folder inside the route. Its first use starts from
        a copy of the route's saves (account folders and loose files), so the
        character carries over once; after that both go their own way."""
        online = route / self.ReforgedOnlineSavesFolder
        if self._route_save_files(online):
            return online
        online.mkdir(parents=True, exist_ok=True)
        copied = 0
        if route.is_dir():
            for child in route.iterdir():
                if child.name in {self.ReforgedOnlineSavesFolder, "save_profiles"}:
                    continue
                if child.is_dir() and self._save_directory_contains_elden_ring_saves(child):
                    shutil.copytree(child, online / child.name, dirs_exist_ok=True)
                    copied += 1
                elif child.is_file():
                    shutil.copy2(child, online / child.name)
        qInfo(
            "Elden Ring MO2: Reforged's online saves start from a copy of the "
            f"route's ({copied} account folder(s)) in '{online}'."
        )
        return online

    @classmethod
    def launch_method_for(cls, binary: str, methods: dict[str, str]) -> tuple[str, bool]:
        """(method, chosen by hand) for one executable."""
        if Path(binary).name.casefold() == cls.ReforgedOnlineLauncher:
            return cls.LaunchMethodSwap, False
        key = os.path.normcase(os.path.abspath(binary)) if binary else ""
        if key in methods:
            return methods[key], True
        automatic = Path(binary).name.casefold() in cls.AutoSwapLaunchers
        return (cls.LaunchMethodSwap if automatic else cls.LaunchMethodVfs), False

    def _launch_method(self, app_path: str) -> str:
        try:
            methods = self.read_launch_methods(Path(self._organizer.basePath()))
        except (AttributeError, RuntimeError, TypeError):
            methods = {}
        return self.launch_method_for(app_path, methods)[0]

    def _swap_route_saves_in(
        self,
        session_directory: Path,
        manifest: dict,
        global_save_root: Path,
        route: Path,
        keep_steam_saves: bool = False,
    ) -> None:
        """Puts the route's saves where the game looks without the VFS: its
        account folders into the (now empty) Steam folder, and its loose files
        (GraphicsConfig.xml) in place of the Steam ones, which wait in the
        session. The manifest records each step first, so recovery after a
        crash can take everything back.

        With keep_steam_saves (a game that plays another file, like Reforged's
        .err) Steam's own .sl2 stays visible instead of the route's: Steam Cloud
        syncs that name when the game closes, whatever started it."""
        swap = {
            "route": str(route),
            "directories": [],
            "steam_root_files": [],
            "route_root_files": [],
            # until "complete" the game has not run: _swap_route_saves_out then
            # takes nothing back to the route, and until "steam_files_held" the
            # loose files in the Steam folder are still Steam's
            "steam_files_held": False,
            "complete": False,
        }
        manifest["swap"] = swap
        self._write_save_quarantine_manifest(session_directory, manifest)
        held = session_directory / self.SwapRootFilesDirectoryName
        for child in list(global_save_root.iterdir()):
            if child.is_file():
                held.mkdir(exist_ok=True)
                os.replace(child, held / child.name)
                swap["steam_root_files"].append(child.name)
                self._write_save_quarantine_manifest(session_directory, manifest)
        swap["steam_files_held"] = True
        self._write_save_quarantine_manifest(session_directory, manifest)
        if not route.is_dir():
            if keep_steam_saves:
                self._put_steam_saves_in_place(session_directory, manifest, global_save_root)
            swap["complete"] = True
            self._write_save_quarantine_manifest(session_directory, manifest)
            return
        for child in route.iterdir():
            if child.is_file():
                shutil.copy2(child, global_save_root / child.name)
                swap["route_root_files"].append(child.name)
            elif (
                child.is_dir()
                and child.name not in {"save_profiles"}
                and self._save_directory_contains_elden_ring_saves(child)
            ):
                shutil.copytree(
                    child,
                    global_save_root / child.name,
                    ignore=(
                        (lambda folder, names, top=str(child): [
                            name for name in names if name in self.SteamCloudSaves
                        ] if folder == top else [])
                        if keep_steam_saves
                        else None
                    ),
                )
                swap["directories"].append(child.name)
            self._write_save_quarantine_manifest(session_directory, manifest)
        if keep_steam_saves:
            self._put_steam_saves_in_place(session_directory, manifest, global_save_root)
        swap["complete"] = True
        self._write_save_quarantine_manifest(session_directory, manifest)
        qInfo(
            "Elden Ring MO2: swapped the route's saves into the Steam folder for "
            f"a launch outside the VFS: {len(swap['directories'])} account folder(s), "
            f"{len(swap['route_root_files'])} loose file(s)."
        )

    def _put_steam_saves_in_place(
        self, session_directory: Path, manifest: dict, global_save_root: Path
    ) -> None:
        swap = manifest["swap"]
        placed = swap.setdefault("steam_saves_in_place", [])
        for directory in manifest.get("directories", []):
            name = str(directory.get("name", ""))
            for save in self.SteamCloudSaves:
                source = session_directory / name / save
                if not source.is_file():
                    continue
                # recorded first: a cut copy is still recognised as Steam's
                placed.append(f"{name}/{save}")
                self._write_save_quarantine_manifest(session_directory, manifest)
                target = global_save_root / name / save
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)

    def _clear_steam_saves_in_place(
        self, session_directory: Path, swap: dict, global_save_root: Path
    ) -> None:
        """Takes out the copies of Steam's saves that stayed visible, when they
        are still Steam's. One the game changed is the game's output, and goes on
        to the route like the rest."""
        placed = swap.get("steam_saves_in_place")
        if not isinstance(placed, list):
            return
        for relative in placed:
            copy = global_save_root / str(relative)
            if copy.is_file() and self._file_is_held_by(copy, session_directory / str(relative)):
                copy.unlink()
            try:
                # nothing else of this account: it mustn't block its folder's return
                if copy.parent.is_dir() and not any(copy.parent.iterdir()):
                    copy.parent.rmdir()
            except OSError:
                pass

    def _swap_route_saves_out(
        self, session_directory: Path, manifest: dict, swap: dict, global_save_root: Path
    ) -> None:
        """Takes what the game wrote in the Steam folder back to the route (the
        route was backed up before the launch), then puts the Steam folder's
        own loose files back."""
        route = Path(str(swap.get("route", "")))
        if not route.is_absolute():
            raise OSError(_tr("The save-recovery record has no valid route for the swapped saves."))
        route.mkdir(parents=True, exist_ok=True)
        # Steam's own loose files already put back by a try that failed partway are
        # not the game's to take to the route; records older than this have no list
        restored = swap.get("restored_root_files")
        if not isinstance(restored, list):
            restored = []
        swap["restored_root_files"] = restored
        self._clear_steam_saves_in_place(session_directory, swap, global_save_root)
        # a swap that stopped partway (records older than this finished theirs):
        # the game never ran, and what is here is the route's own copy, perhaps
        # cut short by the failure, so the route keeps what it has
        complete = swap.get("complete", True)
        steam_files_held = swap.get("steam_files_held", True)
        changed = 0
        if global_save_root.is_dir():
            for child in list(global_save_root.iterdir()):
                if child.is_dir() and self._save_directory_contains_elden_ring_saves(child):
                    if complete:
                        target = route / child.name
                        target.mkdir(parents=True, exist_ok=True)
                        for entry in child.rglob("*"):
                            destination = target / entry.relative_to(child)
                            if entry.is_dir():
                                destination.mkdir(parents=True, exist_ok=True)
                            else:
                                destination.parent.mkdir(parents=True, exist_ok=True)
                                changed += self._return_swapped_file(
                                    entry, destination, session_directory, route
                                )
                        shutil.rmtree(child)
                    elif self._tree_is_held_by(child, route / child.name):
                        shutil.rmtree(child)
                    else:
                        # not the route's copy: played from Steam after a crash
                        self._keep_aside(session_directory, child)
                elif child.is_file() and child.name not in restored:
                    if complete:
                        changed += self._return_swapped_file(
                            child, route / child.name, session_directory, route
                        )
                        child.unlink()
                    elif steam_files_held:
                        if self._file_is_held_by(child, route / child.name):
                            child.unlink()
                        else:
                            self._keep_aside(session_directory, child)
        held = session_directory / self.SwapRootFilesDirectoryName
        if held.is_dir():
            global_save_root.mkdir(parents=True, exist_ok=True)
            for entry in list(held.iterdir()):
                # recorded first: by now the game's copy of this name has gone to the
                # route, so whatever a retry finds under it is Steam's
                if entry.name not in restored:
                    restored.append(entry.name)
                    self._write_save_quarantine_manifest(session_directory, manifest)
                in_place = global_save_root / entry.name
                if in_place.is_file() and not self._file_is_held_by(in_place, entry):
                    self._keep_aside(session_directory, in_place)
                os.replace(entry, in_place)
            held.rmdir()
        qInfo(
            "Elden Ring MO2: the saves of the launch outside the VFS are back in "
            f"the route '{route}'; {changed} file(s) had changed."
        )
        if changed and self._recovering_earlier_session:
            self._returned_saves.append((route, changed))

    @staticmethod
    def _file_is_held_by(file: Path, other: Path) -> bool:
        """Whether every byte of the file is in the other one: the same file, or
        a copy of it cut short."""
        try:
            if not other.is_file() or file.stat().st_size > other.stat().st_size:
                return False
            with file.open("rb") as mine, other.open("rb") as theirs:
                while chunk := mine.read(1024 * 1024):
                    if theirs.read(len(chunk)) != chunk:
                        return False
        except OSError:
            return False
        return True

    @classmethod
    def _tree_is_held_by(cls, folder: Path, other: Path) -> bool:
        return all(
            cls._file_is_held_by(entry, other / entry.relative_to(folder))
            for entry in folder.rglob("*")
            if not entry.is_dir()
        )

    def _keep_aside(self, session_directory: Path, path: Path) -> None:
        """Moves something of the Steam folder into the recovery session, which
        stays for review."""
        root = session_directory / self.UnroutedSaveDirectoryName
        root.mkdir(parents=True, exist_ok=True)
        destination = root / path.name
        if os.path.lexists(destination):
            destination = root / f"{path.name}.{uuid.uuid4().hex}"
        os.replace(path, destination)
        self._kept_aside_saves.append(destination)
        qCritical(f"Elden Ring MO2: kept '{path}' in '{destination}' instead of deleting it.")

    @staticmethod
    def _copy_whole(source: Path, destination: Path) -> None:
        """A failure leaves the destination as it was, never a cut copy."""
        temporary = destination.with_name(f".{destination.name}.swap-tmp")
        shutil.copy2(source, temporary)
        os.replace(temporary, destination)

    def _return_swapped_file(
        self, entry: Path, destination: Path, session_directory: Path, route: Path
    ) -> int:
        """Copies a file of the Steam folder to the route and returns 1 if that
        changes the route. A route file newer than it (the route was written while
        the session was held) is kept aside first."""
        changed = 1
        if destination.is_file():
            mine = entry.stat()
            theirs = destination.stat()
            if mine.st_size == theirs.st_size and mine.st_mtime_ns == theirs.st_mtime_ns:
                changed = 0
            if theirs.st_mtime_ns > mine.st_mtime_ns and not self._file_is_held_by(
                destination, entry
            ):
                root = session_directory / self.UnroutedSaveDirectoryName / "route-newer"
                aside = root / destination.relative_to(route)
                aside.parent.mkdir(parents=True, exist_ok=True)
                if aside.exists() and not (
                    self._file_is_held_by(destination, aside)
                    and self._file_is_held_by(aside, destination)
                ):
                    aside = aside.with_name(f"{aside.name}.{uuid.uuid4().hex}")
                if not aside.exists():
                    self._copy_whole(destination, aside)
                if aside not in self._kept_aside_saves:
                    self._kept_aside_saves.append(aside)
                qCritical(
                    f"Elden Ring MO2: '{destination}' was newer than the copy coming "
                    f"back from the Steam folder; it was kept in '{aside}'."
                )
        self._copy_whole(entry, destination)
        return changed

    @staticmethod
    def _split_arguments(args: str) -> list[str]:
        """Splits an executable's argument line as Windows does: spaces part
        the arguments, double quotes keep one together (and are dropped), and
        \\" is a literal quote. PyQt6 has no QProcess.splitCommand."""
        arguments: list[str] = []
        current: list[str] = []
        quoted = False
        started = False
        index = 0
        while index < len(args):
            character = args[index]
            if character == "\\" and index + 1 < len(args) and args[index + 1] == '"':
                current.append('"')
                started = True
                index += 2
                continue
            if character == '"':
                quoted = not quoted
                started = True
            elif character.isspace() and not quoted:
                if started:
                    arguments.append("".join(current))
                    current, started = [], False
            else:
                current.append(character)
                started = True
            index += 1
        if started:
            arguments.append("".join(current))
        return arguments

    @classmethod
    def seamless_dll_in_mods(
        cls, mods: list[tuple[str, Path, bool]]
    ) -> tuple[str, Path] | None:
        """The Seamless Co-op DLL of the highest-priority active mod (name,
        folder, active) that holds one."""
        for name, folder, active in reversed(mods):
            dll = folder / "SeamlessCoop" / cls.SeamlessDll
            if active and dll.is_file():
                return name, dll
        return None

    def _mod_folders(self) -> list[tuple[str, Path, bool]]:
        mods = []
        try:
            mod_list = self._organizer.modList()
            for name in mod_list.allModsByProfilePriority():
                mod = mod_list.getMod(name)
                if mod is None:
                    continue
                active = bool(mod_list.state(name) & mobase.ModState.ACTIVE)
                mods.append((name, Path(mod.absolutePath()), active))
        except (AttributeError, RuntimeError):
            pass
        return mods

    def _start_outside_vfs(self, app_path: str, working_directory, args: str) -> bool:
        """Starts a launcher without MO2's hooks (QProcess runs it as a plain
        child of MO2, which itself is not hooked)."""
        try:
            directory = (
                working_directory.absolutePath()
                if hasattr(working_directory, "absolutePath")
                else str(working_directory or "")
            )
        except RuntimeError:
            directory = ""
        if not directory:
            directory = str(Path(app_path).parent)
        if self._is_console_program(app_path):
            # a console launcher (ELDEN RING Reforged's asks for its settings
            # there): QProcess gives it a console nobody sees, so it waited
            # for a key forever. It gets a window of its own.
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 1  # SW_SHOWNORMAL
            try:
                process = subprocess.Popen(
                    [app_path, *self._split_arguments(args)],
                    cwd=directory,
                    creationflags=subprocess.CREATE_NEW_CONSOLE,
                    startupinfo=startup,
                )
                started, pid = True, process.pid
            except OSError as error:
                qCritical(f"Elden Ring MO2: could not start '{app_path}': {error}")
                started, pid = False, 0
        else:
            started, pid = QProcess.startDetached(app_path, self._split_arguments(args), directory)
        if started:
            qInfo(
                f"Elden Ring MO2: started '{Path(app_path).name}' outside the VFS "
                f"(process {pid}); MO2's own run of it was cancelled."
            )
        else:
            qCritical(f"Elden Ring MO2: could not start '{app_path}' outside the VFS.")
        return bool(started)

    @staticmethod
    def _is_console_program(app_path: str) -> bool:
        """Whether the executable's PE header asks for a console."""
        try:
            with open(app_path, "rb") as image:
                header = image.read(4096)
            pe = int.from_bytes(header[0x3C:0x40], "little")
            if header[:2] != b"MZ" or header[pe : pe + 4] != b"PE\0\0":
                return False
            # the optional header's Subsystem field; 3 is IMAGE_SUBSYSTEM_WINDOWS_CUI
            return int.from_bytes(header[pe + 0x5C : pe + 0x5E], "little") == 3
        except (OSError, ValueError):
            return False

    def _restore_global_save_quarantine(
        self, session_directory: Path, global_save_root: Path
    ) -> bool:
        if not session_directory.is_dir():
            return True

        manifest_path = session_directory / self.SaveRecoveryManifestName
        if manifest_path.is_file():
            try:
                manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            except (OSError, UnicodeError, json.JSONDecodeError) as error:
                raise OSError(self._game_text(_tr("The Elden Ring save-recovery record could not be read."))) from error
            if not isinstance(manifest, dict):
                raise OSError(self._game_text(_tr("The Elden Ring save-recovery record has an invalid format.")))
            recorded_root = manifest.get("global_save_root")
            if not isinstance(recorded_root, str) or not recorded_root.strip():
                raise OSError(
                    self._game_text(_tr("The Elden Ring save-recovery record has no valid save-folder path."))
                )
            try:
                same_root = os.path.normcase(os.path.abspath(recorded_root)) == os.path.normcase(
                    os.path.abspath(global_save_root)
                )
            except (OSError, TypeError, ValueError):
                same_root = False
            if not same_root:
                raise OSError(
                    self._game_text(_tr("The save-recovery record points to a different Elden Ring save folder."))
                )
            names = manifest.get("directories", [])
        else:
            # Recover a crash between moving a folder and writing its manifest.
            names = [
                {"name": child.name}
                for child in session_directory.iterdir()
                if child.is_dir()
                and child.name != self.UnroutedSaveDirectoryName
            ]
            manifest = {"global_save_root": str(global_save_root), "directories": names}

        if not isinstance(names, list):
            raise OSError(self._game_text(_tr("The Elden Ring save-recovery record has an invalid directory list.")))

        validated_directories = []
        seen_names = set()
        for item in names:
            name = item.get("name") if isinstance(item, dict) else None
            if (
                not isinstance(name, str)
                or not name
                or name in {".", ".."}
                or any(separator in name for separator in ("/", "\\", ":"))
                or Path(name).is_absolute()
                or Path(name).name != name
            ):
                raise OSError(_tr("The save-recovery record contains an invalid folder name."))
            normalized_name = os.path.normcase(name)
            if normalized_name in seen_names:
                raise OSError(_tr("The save-recovery record contains duplicate folder names."))
            seen_names.add(normalized_name)
            validated_directories.append((item, name))

        swap = manifest.get("swap")
        if isinstance(swap, dict) and not swap.get("returned"):
            # a launch outside the VFS played from the Steam folder: its
            # saves go back to the route before the Steam ones return
            self._swap_route_saves_out(session_directory, manifest, swap, global_save_root)
            swap["returned"] = True
            self._write_save_quarantine_manifest(session_directory, manifest)

        global_save_root.mkdir(parents=True, exist_ok=True)
        for item, name in validated_directories:
            backup_directory = session_directory / name
            if not backup_directory.is_dir():
                continue

            original_directory = global_save_root / name
            if original_directory.exists():
                unrouted_root = session_directory / self.UnroutedSaveDirectoryName
                unrouted_root.mkdir(parents=True, exist_ok=True)
                preserved_directory = unrouted_root / name
                if preserved_directory.exists():
                    preserved_directory = unrouted_root / f"{name}.{uuid.uuid4().hex}"
                # the folder in the way may hold the newer progress (the game
                # started from Steam after a crash, or a save written outside
                # the route): it is kept, and the user is told where
                newer = self._newest_save_time(original_directory) > self._newest_save_time(
                    backup_directory
                )
                os.replace(original_directory, preserved_directory)
                item["set_aside"] = str(preserved_directory)
                if newer:
                    self._displaced_newer_saves.append(preserved_directory)
                    qCritical(
                        "Elden Ring MO2: a global save folder with newer saves "
                        "than the hidden ones appeared during the isolated "
                        f"session; it was kept in '{preserved_directory}' "
                        "before restoring the Steam save."
                    )
                else:
                    qCritical(
                        "Elden Ring MO2: an unexpected global save folder appeared "
                        "during the isolated session; it was preserved in recovery "
                        f"('{preserved_directory}') before restoring the Steam save."
                    )

            os.replace(backup_directory, original_directory)
            item["restored"] = True
            self._write_save_quarantine_manifest(session_directory, manifest)

        temporary_manifest = session_directory / f".{self.SaveRecoveryManifestName}.tmp"
        temporary_manifest.unlink(missing_ok=True)
        self._release_save_quarantine(session_directory)
        if (session_directory / self.UnroutedSaveDirectoryName).is_dir():
            # Preserve unexpected game output for manual inspection.
            return False

        manifest_path.unlink(missing_ok=True)
        try:
            session_directory.rmdir()
        except OSError as error:
            # the saves are all back: an empty folder another MO2 window still
            # holds open (its .owner.lock) is no reason to report a failure
            qWarning(
                "Elden Ring MO2: the saves were restored, but their emptied "
                f"recovery folder could not be removed yet: {error}"
            )
            return True
        try:
            # the install's folder, then the shared one, once they are empty
            session_directory.parent.rmdir()
            session_directory.parent.parent.rmdir()
        except OSError:
            pass
        return True

    def _recover_stale_global_save_quarantines(self) -> bool:
        self._recovery_waiting = False
        # whether this call put any session back (a launch may have done it
        # before a queued retry runs)
        self._recovered_sessions = 0
        recovery_root = self._save_quarantine_root()
        sessions: list[Path] = []
        try:
            if recovery_root.is_dir():
                sessions = [
                    session
                    for session in sorted(recovery_root.iterdir())
                    # the session of this process's own launch is restored
                    # when its game closes
                    if session.is_dir()
                    and session != self._active_save_quarantine
                    and self._session_is_pending(session)
                ]
        except OSError as error:
            qCritical(f"Elden Ring MO2: the save-recovery folder could not be read: {error}")
            return False
        sessions += self._orphaned_sessions() + self._stale_foreign_sessions()
        if not sessions:
            return True

        # a launcher still open (from an instance MO2 just switched away
        # from) may be about to start the game against the hidden saves
        if self._elden_ring_process_state() is not False or self._launcher_running():
            self._recovery_waiting = True
            qCritical(
                "Elden Ring MO2: prior global saves need recovery, but Elden Ring "
                "or one of its launchers is running, or its process state cannot "
                "be confirmed."
            )
            return False

        global_save_root = Path(self.savesDirectory().absolutePath())
        for session_directory in sessions:
            if not self._claim_save_quarantine(session_directory):
                self._recovery_waiting = True
                qCritical(
                    "Elden Ring MO2: another MO2 window (this installation or "
                    "another one) still holds the hidden Steam saves in "
                    f"'{session_directory}'; it restores them when its game closes."
                )
                return False
            self._recovering_earlier_session = True
            try:
                restored_cleanly = self._restore_global_save_quarantine(
                    session_directory, global_save_root
                )
                self._recovered_sessions += 1
                if not restored_cleanly:
                    qInfo(
                        "Elden Ring MO2: previous Steam saves were restored; "
                        "additional unrouted output remains in the recovery folder."
                    )
            except (OSError, RuntimeError, ValueError) as error:
                qCritical(
                    "Elden Ring MO2: save recovery could not restore a prior "
                    f"session: {error}"
                )
                return False
            finally:
                self._recovering_earlier_session = False
        return True

    def _launcher_running(self, processes: list[tuple[int, str]] | None = None) -> bool:
        return self._open_launcher(processes) is not None

    def _open_launcher(self, processes: list[tuple[int, str]] | None = None) -> str | None:
        """The executable name of a launcher of this game that is open. A launcher
        still open may yet start the game (an update prompt, a Steam sign-in, a
        slow first start). None too when the process list can't be read: the
        watchers then rely on the game's own state and their time limit.
        A watcher's tick passes the list it has read; without one it is read here."""
        if processes is None:
            processes = _running_processes()
        if processes is None:
            return None
        game_directory = None
        for pid, name in processes:
            if name not in self.GameLaunchers or name == self.GameProcess:
                continue
            if name == self.AntiCheatLauncher:
                image = _process_image_path(pid)
                if image is not None:
                    if game_directory is None:
                        try:
                            game_directory = os.path.normcase(
                                os.path.abspath(self.gameDirectory().absolutePath())
                            )
                        except Exception:  # no game folder yet: count it
                            return name
                    if not os.path.normcase(image).startswith(game_directory + os.sep):
                        continue
            return name
        return None

    @classmethod
    def _elden_ring_process_state(
        cls, processes: list[tuple[int, str]] | None = None
    ) -> bool | None:
        # this game's process: eldenring.exe or nightreign.exe. A watcher's tick
        # passes the list it has read; without one it is read here.
        names = (
            {name for _pid, name in processes}
            if processes is not None
            else _running_process_names()
        )
        if names is not None:
            return cls.GameProcess in names
        try:
            result = subprocess.run(
                ["tasklist", "/FI", f"IMAGENAME eq {cls.GameProcess}", "/FO", "CSV", "/NH"],
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=5,
                check=False,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
        except (OSError, subprocess.SubprocessError):
            return None
        if result.returncode != 0:
            return None
        return any(
            line.lstrip().casefold().startswith(f'"{cls.GameProcess}"')
            for line in result.stdout.splitlines()
        )

    def _restore_quarantine_if_launch_failed(
        self, session_directory: Path, global_save_root: Path
    ) -> None:
        if self._active_save_quarantine != session_directory:
            return
        process_state = self._elden_ring_process_state()
        if process_state is True:
            return
        if process_state is None:
            qCritical(
                "Elden Ring MO2: could not confirm that Elden Ring started; "
                "the hidden Steam saves were left in recovery for startup recovery."
            )
            return
        try:
            self._restore_global_save_quarantine(session_directory, global_save_root)
            self._active_save_quarantine = None
            qInfo(
                "Elden Ring MO2: launch did not start; the temporarily hidden "
                "global saves were restored."
            )
            self._report_displaced_saves()
        except (OSError, RuntimeError, ValueError) as error:
            qCritical(
                "Elden Ring MO2: launch did not start and save restoration "
                f"needs attention: {error}"
            )

    @staticmethod
    def _seed_route_root_files(global_save_root: Path, route: Path) -> None:
        """Copies loose files at the top of the Steam save folder (the game's
        GraphicsConfig.xml) into an isolated route that lacks them. The VFS
        sends only new files to the route: a file that exists just in the
        Steam folder would be opened and rewritten there."""
        try:
            files = [child for child in global_save_root.iterdir() if child.is_file()]
        except OSError:
            return
        for source in files:
            target = route / source.name
            if target.exists():
                continue
            try:
                route.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
                qInfo(
                    f"Elden Ring MO2: copied {source.name} into the save route so "
                    "the game writes the route's copy, not the Steam one."
                )
            except OSError as error:
                qWarning(
                    f"Elden Ring MO2: could not copy {source.name} into the save "
                    f"route; the game may change the Steam copy: {error}"
                )

    def _hide_existing_global_save_directories(
        self, swap_route: Path | None = None, keep_steam_saves: bool = False
    ) -> Path | None:
        """Moves the Steam account folders into a recovery session for the
        launch. With swap_route (a launch outside the VFS) the route's saves
        then take their place, and the session exists even when Steam had no
        saves, so the route gets its saves back after the run."""
        global_save_root = Path(self.savesDirectory().absolutePath())
        if swap_route is not None:
            global_save_root.mkdir(parents=True, exist_ok=True)
        if not global_save_root.is_dir():
            return None
        if not self._recover_stale_global_save_quarantines():
            raise OSError(self._game_text(_tr("A previous Elden Ring save session still needs recovery.")))

        save_directories = [
            child
            for child in global_save_root.iterdir()
            if child.is_dir() and self._save_directory_contains_elden_ring_saves(child)
        ]
        if not save_directories and swap_route is None:
            return None

        recovery_root = self._save_quarantine_root()
        session_directory = recovery_root / uuid.uuid4().hex
        session_directory.mkdir(parents=True, exist_ok=False)
        self._claim_save_quarantine(session_directory)
        manifest = {
            "version": 1,
            "global_save_root": str(global_save_root),
            # other installs block their launches only while this one exists
            "install_root": str(self._install_root()),
            "directories": [{"name": directory.name} for directory in save_directories],
        }
        self._write_save_quarantine_manifest(session_directory, manifest)
        self._active_save_quarantine = session_directory
        try:
            for directory in save_directories:
                os.replace(directory, session_directory / directory.name)
                self._write_save_quarantine_manifest(session_directory, manifest)
            if swap_route is not None:
                self._swap_route_saves_in(
                    session_directory, manifest, global_save_root, swap_route, keep_steam_saves
                )
        except OSError:
            try:
                self._restore_global_save_quarantine(session_directory, global_save_root)
                self._active_save_quarantine = None
            except (OSError, RuntimeError, ValueError) as restore_error:
                qCritical(
                    "Elden Ring MO2: partial save isolation could not be fully "
                    f"rolled back; recovery is required: {restore_error}"
                )
            raise

        qInfo(
            "Elden Ring MO2: temporarily isolated "
            f"{len(save_directories)} existing global save folder(s) for this launch."
        )
        grace = self.LauncherStartGrace if self._launch_via_launcher else 5
        self._launch_pending_until = time.monotonic() + grace
        step = self.LaunchWatchInterval / 1000
        QTimer.singleShot(
            self.LaunchWatchInterval,
            lambda session=session_directory, save_root=global_save_root, left=grace - step: self._watch_launch_start(
                session, save_root, left
            ),
        )
        return session_directory

    def _watch_launch_start(
        self, session_directory: Path, global_save_root: Path, remaining: float
    ) -> None:
        # A launcher can take a while to start eldenring.exe. Until the game
        # shows up, or the time runs out, the Steam saves stay hidden.
        if self._active_save_quarantine != session_directory:
            self._launch_pending_until = 0.0
            return
        processes = _running_processes()
        state = self._elden_ring_process_state(processes)
        if state is True:
            # the game is up: the finish watcher knows it was seen
            self._game_seen_run = self._game_run
            self._launch_pending_until = 0.0
            return
        if state is False and (remaining > 0 or self._launcher_running(processes)):
            if remaining <= 0:
                # keep new launches waiting while the launcher is still open
                self._launch_pending_until = time.monotonic() + self.LaunchWatchInterval / 1000 + 1
            QTimer.singleShot(
                self.LaunchWatchInterval,
                lambda session=session_directory, save_root=global_save_root, left=remaining - self.LaunchWatchInterval / 1000: self._watch_launch_start(
                    session, save_root, left
                ),
            )
            return
        self._launch_pending_until = 0.0
        self._restore_quarantine_if_launch_failed(session_directory, global_save_root)

    def _initialize_elden_ring_ui(self, _main_window) -> None:
        if not self.isActive():
            # MO2 switched to the other game's instance with this game's launcher
            # still open: its saves still come back once it closes
            if self._recovery_waiting:
                self._retry_recovery_later()
            return

        self._main_window = _main_window
        if _main_window is not None:
            # MO2 deletes its window itself: no message may use it after that
            _main_window.destroyed.connect(self._forget_main_window)

        global _log_game_name
        _log_game_name = self.DisplayName

        if not self._recover_stale_global_save_quarantines():
            if self._recovery_waiting:
                # MO2 started while the game (or another MO2 window's game)
                # still runs: the saves come back once it closes
                QMessageBox.information(
                    _main_window,
                    _tr("Steam saves still hidden"),
                    _tr("An earlier launch hid the Steam save folders, and the "
                    "game or one of its launchers is still running, or another "
                    "MO2 window is waiting for its game. MO2 puts the saves "
                    "back as soon as it closes. Don't start the game from "
                    "Steam until then."),
                )
                self._retry_recovery_later()
            else:
                self._show_recovery_failed(_main_window)
        self._report_displaced_saves(_main_window)

        # the game-close watcher checks every few seconds: MO2 closed right
        # after the game would leave the saves set aside until its next start
        # (and block other installs' launches meanwhile)
        if _main_window is not None:
            self._close_watcher = _MainWindowCloseWatcher(
                _main_window, self._restore_saves_on_close
            )
            _main_window.installEventFilter(self._close_watcher)

        self._organizer.setPluginSetting(
            self.name(), self.ArchiveLayoutOverrideSetting, False
        )
        self._start_refined_ui_alignment()

    def _restore_saves_on_close(self) -> None:
        """MO2 is closing: Steam saves this launch set aside come back now,
        when neither the game nor a launcher of it runs any more; otherwise
        they wait for the next MO2 start, as after a crash."""
        self._close_launcher_notice()
        session = self._active_save_quarantine
        if session is None:
            return
        state = self._elden_ring_process_state()
        launcher = self._open_launcher()
        if state is not False or launcher:
            qInfo(
                "Elden Ring MO2: MO2 closes while the game or its launcher runs; "
                "the Steam saves come back at the next MO2 start."
            )
            still_open = self.GameProcess if state is True else launcher
            if still_open:
                QMessageBox.information(
                    # closed from the taskbar while minimized, MO2's window
                    # would hide this box and MO2 would seem not to close
                    self._message_parent(),
                    _tr("Steam saves still hidden"),
                    _tr("{0} is still open as MO2 closes. The Steam saves come back the "
                        "next time MO2 runs, once it has closed. Don't start the game "
                        "from Steam until then.").format(still_open),
                )
            return
        try:
            self._restore_global_save_quarantine(
                session, Path(self.savesDirectory().absolutePath())
            )
            self._active_save_quarantine = None
            qInfo("Elden Ring MO2: MO2 closes; the Steam saves are back in their folder.")
            self._report_displaced_saves()
        except (OSError, RuntimeError, ValueError) as error:
            qCritical(
                "Elden Ring MO2: the Steam saves could not be restored as MO2 "
                f"closes; they come back at the next MO2 start: {error}"
            )

    def _game_text(self, text: str) -> str:
        # a translated text that names Elden Ring, for the game this plugin manages
        name = self.DisplayName
        if name == "Elden Ring":
            return text
        # German joins the name with hyphens (Elden-Ring-Spielstand); French
        # elides before it (d'Elden Ring, qu'Elden Ring)
        for old, new in (
            ("d'Elden Ring", f"de {name}"),
            ("d’Elden Ring", f"de {name}"),
            ("qu'Elden Ring", f"que {name}"),
            ("qu’Elden Ring", f"que {name}"),
            ("Elden-Ring", name),
            ("Elden Ring", name),
        ):
            text = text.replace(old, new)
        return text

    def _show_recovery_failed(self, parent=None) -> None:
        QMessageBox.critical(
            parent or self._message_parent(),
            self._game_text(_tr("Elden Ring save recovery needed")),
            self._game_text(_tr("MO2 found a previous save-isolation session that it could not "
            "restore automatically. The original saves remain in the "
            "MO2 Revamped recovery folder. Do not launch Elden Ring or Steam "
            "until the saved folders have been restored.")),
        )

    def _show_launch_recovery_blocked(self) -> None:
        if self._recovery_waiting:
            launcher = self._open_launcher()
            if launcher and self._elden_ring_process_state() is False:
                QMessageBox.warning(
                    self._message_parent(),
                    _tr("Steam saves still hidden"),
                    _tr("The Steam save folders an earlier launch hid can't be put "
                        "back yet: {0} is still open. Close it and try again. The game "
                        "was not started.").format(launcher),
                )
                return
            QMessageBox.warning(
                self._message_parent(),
                _tr("Steam saves still hidden"),
                _tr("The Steam save folders an earlier launch hid can't be put "
                "back yet: the game or one of its launchers is still running, "
                "or another MO2 window is waiting for its game. Close it and "
                "try again. The game was not started."),
            )
            return
        QMessageBox.critical(
            self._message_parent(),
            self._game_text(_tr("Elden Ring save recovery needed")),
            _tr("MO2 could not restore an earlier global save folder. "
            "The game was not started. Restore the files from the "
            "MO2 Revamped recovery folder before continuing."),
        )

    def _report_displaced_saves(self, parent=None) -> None:
        """Tells the user about save folders with newer saves that a restore
        had to move into the recovery folder, and what a swap's return did."""
        parent = parent or self._message_parent()
        if self._displaced_newer_saves:
            folders = "\n".join(str(folder) for folder in self._displaced_newer_saves)
            self._displaced_newer_saves = []
            QMessageBox.warning(
                parent,
                _tr("Newer saves set aside"),
                _tr("MO2 put back the Steam saves an earlier launch had hidden, but "
                "a save folder had appeared in their place with newer saves (the "
                "game started from Steam meanwhile, or a save written outside the "
                "MO2 route). MO2 did not delete it; it is kept here:\n\n{0}\n\n"
                "If it holds progress you want, close the game and copy its files "
                "back.").format(folders),
            )
        if self._kept_aside_saves:
            kept = "\n".join(str(path) for path in self._kept_aside_saves)
            self._kept_aside_saves = []
            QMessageBox.warning(
                parent,
                _tr("Saves kept aside"),
                _tr("MO2 found saves that differ from, or are newer than, the copy "
                    "that takes their place. Nothing was deleted; they are kept "
                    "here:\n\n{0}\n\nIf one holds progress you want, close the game "
                    "and copy it back.").format(kept),
            )
        if self._returned_saves:
            returned = "\n".join(
                _tr("{0} changed file(s) to {1}").format(count, route)
                for route, count in self._returned_saves
            )
            self._returned_saves = []
            QMessageBox.information(
                parent,
                _tr("Saves moved back to their route"),
                _tr("A launch outside MO2's virtual file system ended while MO2 wasn't "
                    "watching. What was played from the Steam folder went to the route, "
                    "and the Steam saves are back:\n\n{0}").format(returned),
            )

    def _retry_recovery_later(self) -> None:
        # one retry chain at a time
        if self._recovery_retry_pending:
            return
        self._recovery_retry_pending = True
        QTimer.singleShot(self.GameWatchInterval, self._retry_recovery)

    def _retry_recovery(self) -> None:
        self._recovery_retry_pending = False
        if self._recover_stale_global_save_quarantines():
            if self._recovered_sessions:
                qInfo(
                    "Elden Ring MO2: the game closed; the Steam saves an earlier "
                    "launch hid are back in their folder."
                )
            self._report_displaced_saves()
        elif self._recovery_waiting:
            self._retry_recovery_later()
        else:
            self._show_recovery_failed()

    def _start_refined_ui_alignment(self) -> None:
        """Align the executable selector and match the native title bar to MO2's theme."""
        if os.name != "nt":
            return
        application = QApplication.instance()
        if application is None:
            return
        self._refined_ui_attempts = 0
        self._refined_ui_timer = QTimer(application)
        self._refined_ui_timer.setInterval(250)
        self._refined_ui_timer.timeout.connect(self._align_refined_main_window)
        QTimer.singleShot(0, self._align_refined_main_window)
        self._refined_ui_timer.start()

    def _align_refined_main_window(self) -> None:
        self._refined_ui_attempts += 1
        application = QApplication.instance()
        if application is not None:
            for window in application.topLevelWidgets():
                selector = window.findChild(QComboBox, "executablesListBox")
                run_button = window.findChild(QAbstractButton, "startButton")
                if selector is None or run_button is None:
                    continue
                # The Fluent v4 window (recognized by its single "more" button)
                # sizes and centers the picker and Run itself.
                fluent_layout = window.findChild(QToolButton, "moreActionsBtn") is not None
                selector_parent = selector.parentWidget()
                layout = selector_parent.layout() if selector_parent is not None else None
                if layout is not None and not fluent_layout:
                    layout.setAlignment(selector, Qt.AlignmentFlag.AlignTop)
                selector.setMinimumContentsLength(32)
                selector.setSizeAdjustPolicy(
                    QComboBox.SizeAdjustPolicy.AdjustToMinimumContentsLengthWithIcon
                )
                selector.setMaxVisibleItems(8)
                selector.view().setMinimumWidth(max(320, selector.width()))
                self._follow_reforged_online_programs(selector)
                if not self._integrate_shortcut_menu(window, run_button):
                    continue
                self._set_native_title_bar(window, self._uses_dark_theme(application, window))
                self._refined_ui_timer.stop()
                return
        if self._refined_ui_attempts >= 40:
            self._refined_ui_timer.stop()

    def _integrate_shortcut_menu(self, window, run_button: QAbstractButton) -> bool:
        # Source builds with the split-button change already have a single
        # QToolButton. Keep that native control and its auto-connected Run slot.
        if isinstance(run_button, QToolButton) and run_button.menu() is not None:
            run_button.setPopupMode(QToolButton.ToolButtonPopupMode.MenuButtonPopup)
            if window.findChild(QToolButton, "moreActionsBtn") is None:
                run_button.setMinimumHeight(44)
            return True

        if window.findChild(QToolButton, "refinedRunButton") is not None:
            return True

        shortcut = window.findChild(QPushButton, "linkButton")
        if shortcut is None:
            return False

        # MO2's compiled 2.5.2 UI names this QVBoxLayout verticalLayout_12.
        # The fork source renames it runButtonsLayout; support both layouts.
        layout = window.findChild(QBoxLayout, "runButtonsLayout")
        if layout is None:
            layout = window.findChild(QBoxLayout, "verticalLayout_12")

        if (
            not isinstance(layout, QBoxLayout)
            or layout.indexOf(run_button) < 0
            or layout.indexOf(shortcut) < 0
        ):
            # Find the containing layout by its actual child buttons if the
            # UI uses a different layout object name.
            layout = None
            parent = shortcut.parentWidget()
            while parent is not None:
                candidate = parent.layout()
                if (
                    isinstance(candidate, QBoxLayout)
                    and candidate.indexOf(run_button) >= 0
                    and candidate.indexOf(shortcut) >= 0
                ):
                    layout = candidate
                    break
                parent = parent.parentWidget()

        if not isinstance(layout, QBoxLayout):
            return False

        shortcut_menu = shortcut.menu()
        if shortcut_menu is None:
            return False

        run_index = layout.indexOf(run_button)
        shortcut_index = layout.indexOf(shortcut)
        layout.setDirection(QBoxLayout.Direction.LeftToRight)
        layout.setSpacing(0)
        layout.setContentsMargins(0, 0, 0, 0)
        split_button = QToolButton(layout.parentWidget() or run_button.parentWidget())
        split_button.setObjectName("refinedRunButton")
        split_button.setText(run_button.text())
        split_button.setIcon(run_button.icon())
        split_button.setIconSize(run_button.iconSize())
        split_button.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextBesideIcon)
        split_button.setPopupMode(QToolButton.ToolButtonPopupMode.MenuButtonPopup)
        split_button.setMenu(shortcut_menu)
        split_button.setToolTip(_tr("Run the selected program; use the arrow for shortcut options."))
        split_button.setAccessibleName(_tr("Run"))
        split_button.setAccessibleDescription(
            _tr("Run the selected program. Use the arrow section for shortcut options.")
        )
        split_button.setSizePolicy(
            QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed
        )
        split_button.setMinimumWidth(max(132, run_button.minimumWidth()))
        split_button.setFixedHeight(44)
        split_button.setEnabled(run_button.isEnabled())
        split_button.setProperty("mo2RefinedSplitRun", True)

        # Route the main button area through MO2's existing auto-connected
        # slot. The menu area still refreshes shortcut state before opening.
        split_button.clicked.connect(
            lambda _checked=False, original=run_button: original.click()
        )
        split_button.pressed.connect(shortcut.pressed.emit)
        enable_mirror = _RunButtonEnabledMirror(run_button, split_button)
        run_button.installEventFilter(enable_mirror)
        self._refined_ui_filters.append(enable_mirror)

        layout.removeWidget(run_button)
        layout.removeWidget(shortcut)
        layout.insertWidget(min(run_index, shortcut_index), split_button, 1)
        layout.setStretch(layout.indexOf(split_button), 1)
        run_button.hide()
        shortcut.hide()
        split_button.show()
        return True

    @staticmethod
    def _uses_dark_theme(application, window) -> bool:
        """Detect the selected MO2 stylesheet so Windows matches its title bar."""
        return application is not None and bool(application.property("revampedDark"))

    @staticmethod
    def _set_native_title_bar(window, dark_mode: bool) -> None:
        """Ask Windows to draw this window's native title bar in the MO2 theme."""
        try:
            dwm = ctypes.WinDLL("dwmapi", use_last_error=True)
            set_attribute = dwm.DwmSetWindowAttribute
            set_attribute.argtypes = [
                ctypes.c_void_p,
                ctypes.c_uint,
                ctypes.c_void_p,
                ctypes.c_uint,
            ]
            set_attribute.restype = ctypes.c_long
            hwnd = ctypes.c_void_p(int(window.winId()))
            use_dark_mode = ctypes.c_int(1 if dark_mode else 0)
            for attribute in (20, 19):
                result = set_attribute(
                    hwnd,
                    attribute,
                    ctypes.byref(use_dark_mode),
                    ctypes.sizeof(use_dark_mode),
                )
                if result == 0:
                    break
        except (AttributeError, OSError, TypeError, ValueError):
            # Older Windows versions may not expose either title-bar attribute.
            pass

    def settings(self) -> list[mobase.PluginSetting]:
        settings = super().settings()
        settings.append(
            mobase.PluginSetting(
                self.NativeDllSetting,
                (
                    _tr("Load enabled mod DLLs through MO2. Some mods require an "
                    "extension API that this plugin does not provide.")
                ),
                True,
            )
        )
        settings.append(
            mobase.PluginSetting(
                self.SaveBackupsSetting,
                self._game_text(_tr("How many automatic backups of the active Elden Ring save "
                "route to keep. A copy is made before each launch when the saves "
                "changed since the last one; 0 turns the backups off.")),
                self.SaveBackupsDefault,
            )
        )
        return settings

    @classmethod
    def save_backup_root(cls, route_root: Path, instance_base: Path, is_global: bool) -> Path:
        """Where the automatic backups of a save route live: beside the route
        ("<folder> Backups/auto"), or inside the MO2 instance for the global
        Steam route, so nothing is ever written next to the Steam saves."""
        if is_global:
            return instance_base / cls.GlobalBackupsFolderName / "auto"
        return route_root.parent / f"{route_root.name} Backups" / "auto"

    @classmethod
    def _route_save_files(cls, route_root: Path) -> list[Path]:
        """The ER0000.* files of a route: in its account folders and loose in
        its root, without following links."""
        files: list[Path] = []
        if not route_root.is_dir():
            return files
        for entry in route_root.iterdir():
            try:
                if entry.is_symlink() or EldenRingMo2Game._is_link_or_junction(entry):
                    continue
                if entry.is_dir():
                    for child in entry.iterdir():
                        if child.is_file() and not child.is_symlink() and child.name.casefold().startswith(cls.SavePrefix):
                            files.append(child)
                elif entry.is_file() and entry.name.casefold().startswith(cls.SavePrefix):
                    files.append(entry)
            except OSError:
                continue
        return files

    @staticmethod
    def _is_link_or_junction(path: Path) -> bool:
        try:
            attributes = os.lstat(path).st_file_attributes
        except (OSError, AttributeError):
            return False
        return bool(attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT)

    BackupNamePattern = re.compile(r"^\d{8}-\d{6}(-\d+)?$")

    @staticmethod
    def _backup_age_key(entry: Path) -> tuple[int, str, int]:
        # oldest first: by when the copy was made (the names use local time,
        # which repeats an hour when summer time ends), then by name with the
        # "-N" suffix as a number, so "-10" comes after "-2"
        try:
            made = entry.stat().st_mtime_ns
        except OSError:
            made = 0
        _stamp, _separator, suffix = entry.name[15:].partition("-")
        return made, entry.name[:15], int(suffix) if suffix.isdigit() else 1

    @staticmethod
    def _file_signature(path: Path) -> tuple[int, int]:
        info = path.stat()
        return info.st_size, info.st_mtime_ns

    def _backup_route_saves(self, route_root: Path, backup_root: Path, keep: int) -> None:
        """Copies the route's saves into backup_root/<timestamp>, unless they
        match the newest backup (size and time; a copy keeps the file's time,
        so nothing is read twice), then keeps only the newest `keep` copies.
        Failures are logged; they never stop the launch."""
        if keep <= 0:
            return
        staging = None
        try:
            files = self._route_save_files(route_root)
            if not files:
                return
            current = {
                file.relative_to(route_root).as_posix(): self._file_signature(file)
                for file in files
            }
            backup_root.mkdir(parents=True, exist_ok=True)
            # copies a failed run left behind
            for leftover in backup_root.glob(".*.copying"):
                shutil.rmtree(leftover, ignore_errors=True)
            backups = sorted(
                (
                    entry
                    for entry in backup_root.iterdir()
                    if entry.is_dir() and self.BackupNamePattern.match(entry.name)
                ),
                key=self._backup_age_key,
            )
            if backups:
                newest = backups[-1]
                previous = {
                    file.relative_to(newest).as_posix(): self._file_signature(file)
                    for file in newest.rglob("*")
                    if file.is_file()
                }
                if previous == current:
                    qInfo("Elden Ring MO2: saves unchanged since the last automatic backup.")
                    return

            stamp = time.strftime("%Y%m%d-%H%M%S")
            target = backup_root / stamp
            suffix = 2
            while target.exists():
                target = backup_root / f"{stamp}-{suffix}"
                suffix += 1
            staging = backup_root / f".{target.name}.copying"
            for file in files:
                destination = staging / file.relative_to(route_root)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(file, destination)
            os.replace(staging, target)
            staging = None
            backups.append(target)
            qInfo(
                f"Elden Ring MO2: backed up {len(files)} save file(s) to '{target}'."
            )

            for old in backups[:-keep]:
                shutil.rmtree(old, ignore_errors=True)
        except Exception as error:  # a backup must never stop the launch
            qCritical(f"Elden Ring MO2: the automatic save backup failed: {error}")
        finally:
            if staging is not None:
                shutil.rmtree(staging, ignore_errors=True)

    def _save_backups_kept(self) -> int:
        try:
            value = int(
                self._organizer.pluginSetting(self.name(), self.SaveBackupsSetting)
            )
        except (TypeError, ValueError):
            value = self.SaveBackupsDefault
        return max(0, min(value, 50))

    def _prepare_profile_launch(
        self, app_path: str, working_directory, args: str
    ) -> bool:
        """A launch that doesn't go ahead leaves off the mod it turned on."""
        self._mod_turned_on_for_launch = None
        allowed = False
        quarantine_before = self._active_save_quarantine
        try:
            allowed = self._check_profile_launch(app_path, working_directory, args)
            return allowed
        except Exception as error:
            # an error must not reach MO2 with the launch half prepared: the
            # environment is cleared and saves hidden by this launch go back
            qCritical(f"Elden Ring MO2: the launch could not be prepared: {error!r}")
            self._undo_failed_launch(quarantine_before)
            QMessageBox.warning(
                self._message_parent(),
                self._game_text(_tr("Could not start Elden Ring")),
                _tr("MO2 could not prepare the launch, so the game was not started. "
                "The details are in MO2's log."),
            )
            return False
        finally:
            if not allowed and self._mod_turned_on_for_launch:
                try:
                    self._organizer.modList().setActive(self._mod_turned_on_for_launch, False)
                except (AttributeError, RuntimeError) as error:
                    qWarning(f"Elden Ring MO2: could not turn the mod back off: {error}")
                self._mod_turned_on_for_launch = None

    def _undo_failed_launch(self, quarantine_before) -> None:
        """Clears what a launch that failed half-way had set up."""
        try:
            self._restore_profile_config_environment()
        except Exception as error:
            qWarning(f"Elden Ring MO2: could not clear the launch environment: {error!r}")
        session = self._active_save_quarantine
        if session is None or session == quarantine_before:
            return
        try:
            self._restore_global_save_quarantine(
                session, Path(self.savesDirectory().absolutePath())
            )
            self._active_save_quarantine = None
        except Exception as error:
            qCritical(f"Elden Ring MO2: the saves of the failed launch need recovery: {error!r}")
            self._report_displaced_saves()

    def _forget_main_window(self, *_args) -> None:
        self._main_window = None

    def _message_parent(self):
        """MO2's main window, so the plugin's messages open centred on it."""
        window = getattr(self, "_main_window", None)
        if window is not None:
            try:
                # minimized, it would hide a box it owns (a message after the
                # game closed): such a box gets a taskbar button of its own
                if window.isMinimized():
                    return None
                return window
            except RuntimeError:
                # a window Qt has already deleted can't be a parent
                self._main_window = None
        return QApplication.activeWindow()

    def _check_profile_launch(
        self, app_path: str, _working_directory, _args: str
    ) -> bool:
        self._restore_profile_config_environment()
        qInfo(
            "Elden Ring MO2: about-to-run save guard received "
            f"'{Path(app_path).name}'."
        )
        # every game plugin hears every run; only the managed game's acts
        if not self.isActive():
            return True
        if not self._is_game_launch(app_path, _args or ""):
            # tools (save editors and the like) run without touching the
            # hidden Steam saves of a game that may still be running, nor the
            # Overwrite cleanup and save restore that wait for that game
            return True
        # each launch's log says once what its native profile and forced
        # loads are, even when nothing changed since the last refresh
        self.__dict__.get("_logged_notes", {}).clear()
        self._last_forced_load_notes = None
        if not self._reforged_online_mod_ready(app_path):
            return False
        if not self._seamless_password_ready(app_path):
            return False
        if not self._regulation_conflict_ready(app_path):
            return False
        if not self._file_loader_ready(app_path):
            return False
        if not self._save_extension_ready(app_path):
            return False
        if not self._steam_cloud_swap_ready(app_path):
            return False
        if self._active_save_quarantine is not None:
            starting = time.monotonic() < self._launch_pending_until
            state = self._elden_ring_process_state()
            launcher = self._open_launcher()
            if starting or state is not False or launcher:
                qCritical(
                    "Elden Ring MO2: a previous save quarantine is still active; "
                    "launch was blocked to protect the global save."
                )
                if state is False and not launcher:
                    # nothing to close: only the wait for the game to appear
                    QMessageBox.information(
                        self._message_parent(),
                        _tr("Steam saves still hidden"),
                        _tr("MO2 is still waiting for the last launch to start its game. "
                            "If it doesn't start, MO2 puts the saves back within a minute "
                            "and a half; try again then. The game was not started."),
                    )
                    return False
                # a launcher can stay open for as long as the user leaves it:
                # this says what to close instead of doing nothing
                self._recovery_waiting = True
                self._show_launch_recovery_blocked()
                return False
            try:
                self._restore_global_save_quarantine(
                    self._active_save_quarantine,
                    Path(self.savesDirectory().absolutePath()),
                )
                self._active_save_quarantine = None
            except (OSError, RuntimeError, ValueError) as error:
                qCritical(f"Elden Ring MO2: stale save recovery blocked launch: {error}")
                return False

        profile = self._organizer.profile()
        if profile is None:
            self._accept_game_launch(app_path)
            return True
        if not self._recover_stale_global_save_quarantines():
            self._show_launch_recovery_blocked()
            return False
        self._report_displaced_saves()
        foreign_sessions = self._foreign_pending_sessions()
        if foreign_sessions:
            QMessageBox.critical(
                self._message_parent(),
                self._game_text(_tr("Elden Ring save recovery needed")),
                self._game_text(_tr("Another MO2 Revamped installation has just hidden the Steam "
                "saves for a launch of its own. Wait until that game closes, then "
                "try again: MO2 puts the saves back itself. The game was not "
                "started.\n\n{0}")).format(
                    foreign_sessions[0]
                ),
            )
            qCritical(
                "Elden Ring MO2: blocked launch; another install still holds "
                f"hidden Steam saves in '{foreign_sessions[0]}'."
            )
            return False
        direct_game = Path(app_path).name.casefold() == self.GameProcess
        # read by the save hiding below (how long a launcher may take)
        self._launch_via_launcher = not direct_game
        # a launcher run outside the VFS (the game itself always runs inside,
        # Seamless Co-op included: started bare it would restart through Steam
        # with Easy Anti-Cheat)
        swap_launch = (
            not direct_game and self._launch_method(app_path) == self.LaunchMethodSwap
        )

        # the native profile applies to every game launch: its switched-off
        # entries stay out of the forced loads and its initializers go to
        # the bridge. A profile problem blocks eldenring.exe started
        # directly; a launcher run goes ahead as before without it.
        launch_allowed, revamped_native_profile = self._native_profile_for_launch(
            profile, direct_game
        )
        if not launch_allowed:
            return False

        profile_config = Path(profile.absolutePath()) / self.ProfileLaunchConfig
        profile_save_dir = Path(profile.absolutePath()) / "saves"
        game_save_dir = Path(self.savesDirectory().absolutePath())
        profile_saves_enabled = bool(profile.localSavesEnabled())
        save_mode = self.LocalSavegamesClass.mode_for_profile(profile)
        if not self._local_savegames_feature.profile_route_is_current(profile):
            QMessageBox.warning(
                self._message_parent(),
                self._game_text(_tr("Reload Elden Ring save routing")),
                self._game_text(_tr("The save mode, named save profile, or MO2 profile-specific-save setting changed "
                "after MO2 loaded this profile. The game was not started because "
                "the active save mapping may still point to the previous folder.\n\n"
                "Restart MO2, or switch to another profile and back, before "
                "launching Elden Ring.")),
            )
            qInfo(
                "Elden Ring MO2: blocked launch because the active save mapping "
                f"is stale for profile '{profile.name()}'."
            )
            return False
        if not self.LocalSavegamesClass.save_profile_is_available(
            Path(profile.absolutePath()), save_mode
        ):
            QMessageBox.warning(
                self._message_parent(),
                self._game_text(_tr("Named Elden Ring save profile is missing")),
                self._game_text(_tr("The selected named save profile could not be found or its metadata "
                "is invalid. The game was not started. Open Tools > "
                "Save Isolation and choose an available profile, or restore the "
                "missing profile folder.")),
            )
            qInfo(
                "Elden Ring MO2: blocked launch because the selected named "
                "save profile is unavailable."
            )
            return False
        if save_mode in {
            self.LocalSavegamesClass.ProfileIsolated,
            self.LocalSavegamesClass.InstanceShared,
        } and not profile_saves_enabled:
            QMessageBox.warning(
                self._message_parent(),
                self._game_text(_tr("Elden Ring save isolation is not active")),
                self._game_text(_tr("This profile is set to use isolated Elden Ring saves, but "
                "MO2's 'Use profile-specific Save Games' option is still off "
                "in this session. The saved route needs that setting. Restart "
                "MO2 to reload the route before launching. The game was not "
                "started, and the global Steam save was left untouched.")),
            )
            qInfo(
                "Elden Ring MO2: blocked launch because profile-specific "
                f"saves are disabled for '{profile.name()}'."
            )
            return False
        if (
            save_mode == self.LocalSavegamesClass.GlobalShared
            and profile_saves_enabled
        ):
            QMessageBox.warning(
                self._message_parent(),
                self._game_text(_tr("Elden Ring save mode does not match MO2")),
                _tr("This profile is set to use the global Steam save, but MO2's "
                "'Use profile-specific Save Games' option is still on in this "
                "session. The global Steam route is saved. Restart MO2 to "
                "reload it before launching. The game was not started."),
            )
            qInfo(
                "Elden Ring MO2: blocked launch because global-save mode "
                f"conflicts with profile-specific saves for '{profile.name()}'."
            )
            return False

        active_save_dir = self.LocalSavegamesClass.active_save_directory(
            Path(profile.absolutePath()), save_mode
        )
        if save_mode == self.LocalSavegamesClass.GlobalShared:
            active_save_dir = game_save_dir
            save_route = "global save shared with Steam and all MO2 instances"
        elif save_mode == self.LocalSavegamesClass.InstanceShared:
            save_route = "instance-shared modded saves"
        else:
            save_route = "profile-isolated saves"
        selected_save_profile = (
            self.LocalSavegamesClass.save_profile_for_profile_directory(
                Path(profile.absolutePath()), save_mode
            )
        )
        if selected_save_profile:
            save_route += f" (named save profile {selected_save_profile})"

        def normalize(path: Path) -> str:
            try:
                return os.path.normcase(str(path.resolve()))
            except (OSError, RuntimeError):
                return os.path.normcase(os.path.abspath(path))

        if save_mode != self.LocalSavegamesClass.GlobalShared and normalize(
            active_save_dir
        ) == normalize(game_save_dir):
            QMessageBox.critical(
                self._message_parent(),
                self._game_text(_tr("Elden Ring save path conflict")),
                self._game_text(_tr("The selected isolated save folder resolves to the same path "
                "as the global Elden Ring save folder. The game was not started "
                "to protect the original save. Check the MO2 profile paths.")),
            )
            qCritical(
                "Elden Ring MO2: blocked launch because isolated and global "
                f"save paths resolve to the same folder '{game_save_dir}'."
            )
            return False

        qInfo(
            "Elden Ring MO2: save routing for profile "
            f"'{profile.name()}': {save_route}; "
            f"active save folder='{active_save_dir}'; "
            f"profile saves='{profile_save_dir}'; "
            f"game save root='{game_save_dir}'."
        )
        if active_save_dir is not None and self._elden_ring_process_state() is False:
            self._backup_route_saves(
                active_save_dir,
                self.save_backup_root(
                    active_save_dir,
                    Path(self._organizer.basePath()),
                    save_mode == self.LocalSavegamesClass.GlobalShared,
                ),
                self._save_backups_kept(),
            )

        # Reforged online with its own save: the swap below takes the online
        # folder instead of the route (a global route has none to keep apart:
        # both modes use the Steam saves)
        swap_route = active_save_dir
        if (
            swap_launch
            and active_save_dir is not None
            and save_mode != self.LocalSavegamesClass.GlobalShared
            and Path(app_path).name.casefold() == self.ReforgedOnlineLauncher
            and self.reforged_online_saves_separate(Path(self._organizer.basePath()))
        ):
            try:
                swap_route = self._reforged_online_route(active_save_dir)
            except OSError as error:
                QMessageBox.critical(
                    self._message_parent(),
                    self._game_text(_tr("Could not prepare the online saves")),
                    self._game_text(_tr("MO2 could not set up Reforged's online save folder, so the "
                    "game was not started. Your saves were not changed.\n\n{0}")).format(error),
                )
                qCritical(f"Elden Ring MO2: the Reforged online save folder failed: {error}")
                return False
            if self._elden_ring_process_state() is False:
                self._backup_route_saves(
                    swap_route,
                    active_save_dir.parent / f"{active_save_dir.name} Backups" / "online",
                    self._save_backups_kept(),
                )
            qInfo(f"Elden Ring MO2: Reforged online plays with its own saves in '{swap_route}'.")
        (
            start_minimized,
            black_startup_background,
            exclude_cpu0,
            clear_overwrite_after_game,
            clear_overwrite_logs_before_game,
            process_priority,
            bridge_log_history,
        ) = self._read_profile_launch_settings(profile_config)

        if save_mode != self.LocalSavegamesClass.GlobalShared:
            # earlier sessions were recovered at the start of this launch, and
            # the save hiding below checks them once more
            if self._elden_ring_process_state() is not False:
                QMessageBox.warning(
                    self._message_parent(),
                    self._game_text(_tr("Could not safely isolate Elden Ring saves")),
                    self._game_text(_tr("MO2 could not confirm that Elden Ring is closed. Close any "
                    "running Elden Ring process and try again. Existing global "
                    "saves were left in place.")),
                )
                return False
            try:
                self._active_save_quarantine = self._hide_existing_global_save_directories(
                    swap_route if swap_launch else None,
                    # Reforged plays ER0000.err and has no use for the .sl2
                    swap_launch
                    and Path(app_path).name.casefold() == self.ReforgedOnlineLauncher,
                )
            except OSError as error:
                qCritical(f"Elden Ring MO2: save isolation could not begin: {error}")
                QMessageBox.critical(
                    self._message_parent(),
                    self._game_text(_tr("Elden Ring saves could not be isolated")),
                    _tr("MO2 could not safely separate the existing Steam saves. "
                    "The game was not started. The original files remain in the "
                    "recovery folder if a partial move had already begun."),
                )
                self._report_displaced_saves()
                return False
            if active_save_dir is not None and not swap_launch:
                self._seed_route_root_files(game_save_dir, active_save_dir)

        settings_file_state = (
            "found" if profile_config.is_file() else "missing; using defaults"
        )
        qInfo(
            "Elden Ring MO2: experimental options for profile "
            f"'{profile.name()}' ({settings_file_state}): "
            f"start minimized={'on' if start_minimized else 'off'}, "
            "black background="
            f"{'on' if black_startup_background else 'off'}, "
            "skip intro logos="
            f"{'on' if self._read_skip_intro_logos(profile_config) else 'off'}, "
            f"exclude CPU 0={'on' if exclude_cpu0 else 'off'}, "
            "process priority="
            f"{'above normal' if process_priority == 1 else 'system default'}, "
            "clear old Overwrite logs="
            f"{'on' if clear_overwrite_logs_before_game else 'off'}, "
            "clear Overwrite after exit="
            f"{'on' if clear_overwrite_after_game else 'off'}, "
            f"previous bridge sessions={bridge_log_history}."
        )

        initializer_entries = (
            [
                entry
                for entry in revamped_native_profile.get("entries", [])
                if entry.get("initializer")
            ]
            if revamped_native_profile is not None
            else []
        )
        if swap_launch:
            return self._run_swap_launch(app_path, _working_directory, _args or "")
        if direct_game:
            # Seamless Co-op with the game run from MO2
            initializer_entries = initializer_entries + self._seamless_initializer()
        self._activate_profile_config_environment(profile_config, initializer_entries)
        # a new run only once nothing above blocked the launch: a blocked
        # attempt must not disown the watch of a game that is still running
        self._game_run += 1
        self._accept_game_launch(app_path)
        if clear_overwrite_logs_before_game and not clear_overwrite_after_game:
            self._clear_overwrite_logs_before_game()
        if clear_overwrite_after_game:
            self._arm_overwrite_cleanup()
            self._overwrite_cleanup_config = profile_config
        self._preserve_previous_bridge_log(bridge_log_history)
        return True

    def _run_swap_launch(self, app_path: str, working_directory, args: str) -> bool:
        """Starts a launcher outside the VFS and cancels MO2's own run of it.
        The route's saves are already in the Steam folder; they go back when
        the game closes, as for any launcher run."""
        session = self._active_save_quarantine
        self._game_run += 1
        # this run has no bridge: the bridge log left by an earlier run is not
        # this one's, and must not bring back that run's warnings
        self._last_launch_time = time.time()
        try:
            started = self._start_outside_vfs(app_path, working_directory, args)
        except Exception as error:
            # the saves are already swapped: any failure puts them back now
            qCritical(f"Elden Ring MO2: the launch outside the VFS failed: {error}")
            started = False
        if not started:
            if session is not None:
                try:
                    self._restore_global_save_quarantine(
                        session, Path(self.savesDirectory().absolutePath())
                    )
                    self._active_save_quarantine = None
                except (OSError, RuntimeError, ValueError) as error:
                    qCritical(
                        "Elden Ring MO2: the launch outside the VFS did not start "
                        f"and its saves need recovery: {error}"
                    )
            QMessageBox.warning(
                self._message_parent(),
                _tr("Could not start the launcher"),
                _tr("MO2 could not start {0} outside its virtual file system. "
                "The game was not started.").format(Path(app_path).name),
            )
            self._report_displaced_saves()
            return False
        if session is not None:
            self._finish_when_game_closes(True, session, False, None, 0.0, False, self._game_run)
        # it did start: the False below only stops MO2's own run, and the mod
        # it turned on stays on
        self._mod_turned_on_for_launch = None
        # MO2 must not start it a second time under its hooks
        return False

    def _native_profile_for_launch(self, profile, direct_game: bool) -> tuple[bool, dict | None]:
        """Resolves the Revamped native profile for a game launch.

        Returns (launch allowed, profile data). A problem blocks a direct
        launch; a launcher launch goes ahead without the profile's
        initializers (forced loads then fall back to every active DLL)."""
        revamped_native_profile = self._resolve_revamped_native_profile(profile)
        if revamped_native_profile is not None and revamped_native_profile.get("error"):
            message = str(revamped_native_profile["error"])
            if not direct_game:
                qWarning(
                    "Elden Ring MO2: the Revamped native profile could not be "
                    f"prepared for this launcher run: {message}. The launcher "
                    "was started anyway; switched-off entries and initializers "
                    "are not applied."
                )
                return True, None
            QMessageBox.warning(
                self._message_parent(),
                _tr("Revamped native profile could not be prepared"),
                _tr("{0}\n\nThe game was not started.").format(message),
            )
            qCritical(
                "Elden Ring MO2: blocked launch for invalid Revamped native "
                f"profile: {message}"
            )
            return False, None
        if revamped_native_profile is not None and revamped_native_profile.get("entries") and not bool(
            self._organizer.pluginSetting(self.name(), self.NativeDllSetting)
        ):
            if not direct_game:
                qWarning(
                    "Elden Ring MO2: this profile declares native DLLs in its "
                    "Revamped native profile, but native DLL loading is "
                    "disabled in plugin settings. The launcher was started "
                    "anyway; no native DLLs or initializers are passed."
                )
                return True, None
            QMessageBox.warning(
                self._message_parent(),
                _tr("Native DLL loading is disabled"),
                self._game_text(_tr("This MO2 profile declares native DLLs in its Revamped native "
                "profile, but "
                "Elden Ring native DLL loading is disabled in plugin settings. "
                "The game was not started.")),
            )
            return False, None
        return True, revamped_native_profile

    @staticmethod
    def _preserve_previous_bridge_log(previous_sessions: int = 1) -> None:
        log_directory = Path(tempfile.gettempdir())
        current_log = log_directory / "EldenRingMO2Bridge.log"
        if previous_sessions not in (1, 3, 5):
            previous_sessions = 1

        def previous_log_path(index: int) -> Path:
            suffix = "previous.log" if index == 1 else f"previous-{index}.log"
            return log_directory / f"EldenRingMO2Bridge.{suffix}"

        temporary_paths: list[Path] = []
        try:
            if not current_log.is_file():
                qInfo(
                    "Elden Ring MO2: no existing bridge log to preserve before launch."
                )
                return

            for index in range(previous_sessions, 1, -1):
                source = previous_log_path(index - 1)
                if not source.is_file():
                    continue
                destination = previous_log_path(index)
                temporary_log = log_directory / f".{destination.name}.tmp"
                temporary_paths.append(temporary_log)
                shutil.copyfile(source, temporary_log)
                os.replace(temporary_log, destination)

            previous_log = previous_log_path(1)
            temporary_log = log_directory / f".{previous_log.name}.tmp"
            temporary_paths.append(temporary_log)
            shutil.copyfile(current_log, temporary_log)
            os.replace(temporary_log, previous_log)
            qInfo(
                f"Elden Ring MO2: rotated bridge logs, keeping up to "
                f"{previous_sessions} previous session(s)."
            )
        except OSError as error:
            qCritical(
                "Elden Ring MO2: could not preserve the previous bridge log; "
                f"the game will still start: {error}"
            )
        finally:
            for temporary_log in temporary_paths:
                try:
                    temporary_log.unlink(missing_ok=True)
                except OSError:
                    pass

    def _accept_game_launch(self, app_path: str) -> None:
        # the finished-run hook only gets the path: it recognises this run by
        # it, even when other tools ran in between
        self._guarded_launches.add(os.path.normcase(str(app_path)))
        self._last_launch_time = time.time()
        # this launch arms its own Overwrite cleanup below, if any
        self._overwrite_cleanup_armed = False
        self._overwrite_cleanup_path = None

    def _finish_profile_launch(self, app_path: str, _exit_code: int) -> None:
        qInfo(
            "Elden Ring MO2: finished-run save cleanup received "
            f"'{Path(app_path).name}'."
        )
        # every game plugin hears every run; only the managed game's acts
        if not self.isActive():
            return
        launch_key = os.path.normcase(str(app_path))
        guarded = self._is_game_launch(app_path) or launch_key in self._guarded_launches
        if not guarded:
            # a tool that ran meanwhile: the game's restore and cleanup still
            # wait for the game's own run
            return
        self._guarded_launches.discard(launch_key)
        self._restore_profile_config_environment()
        # MO2 reports the run as finished when its lock is off or unlocked, and
        # a launcher finishes while the game it started keeps running; the
        # Steam saves come back and Overwrite is cleared only once the game is
        # closed
        self._finish_when_game_closes(
            Path(app_path).name.casefold() != self.GameProcess,
            self._active_save_quarantine,
            self._overwrite_cleanup_armed,
            self._overwrite_cleanup_path,
            0.0,
            self._game_seen_run == self._game_run,
            self._game_run,
        )
        self._overwrite_cleanup_path = None
        self._overwrite_cleanup_armed = False

    def _warn_if_loose_files_were_skipped(
        self, via_launcher: bool = True, seen: bool = False
    ) -> None:
        """A game update can change the code the bridge looks for; the mods'
        game files then stay out with nothing but a line in its log."""
        if self._loose_files_warning_shown:
            return
        log = Path(tempfile.gettempdir()) / "EldenRingMO2Bridge.log"
        try:
            # the bridge rewrites it when it loads: an older one is another run's
            if log.stat().st_mtime < self._last_launch_time:
                self._warn_if_bridge_did_not_load(via_launcher, seen)
                return
            if "Loose asset hook skipped" not in log.read_text("utf-8", "replace"):
                return
        except OSError:
            self._warn_if_bridge_did_not_load(via_launcher, seen)
            return
        qWarning("Elden Ring MO2: the bridge could not hook the game's file loader.")
        if not self._active_mods_have_game_files():
            return
        self._loose_files_warning_shown = True
        # not from inside MO2's finished-run hook
        QTimer.singleShot(
            0,
            lambda: QMessageBox.warning(
                self._message_parent(),
                _tr("Mods may not have loaded"),
                self._game_text(_tr("Elden Ring started, but MO2 could not connect to its "
                "file loader, so the game files of your mods were not used (their DLLs "
                "were not affected). This usually follows a game update: look for a new "
                "version of MO2 Revamped.")),
            ),
        )

    def _warn_if_bridge_did_not_load(self, via_launcher: bool, seen: bool) -> None:
        """The game ran from MO2 but the bridge never wrote its log: Force
        load libraries is off for the executable (or something stopped the
        injection), and the mods' game files and DLLs were left out."""
        if (
            via_launcher
            or not seen
            or time.time() - self._last_launch_time < self.EarlyExitSeconds
            or not self._active_mods_have_game_files()
        ):
            return
        self._loose_files_warning_shown = True
        qWarning("Elden Ring MO2: the bridge did not load in this run.")
        QTimer.singleShot(
            0,
            lambda: QMessageBox.warning(
                self._message_parent(),
                _tr("Mods did not load"),
                self._game_text(_tr("Elden Ring ran without MO2's bridge, so your mods' "
                "game files and DLLs were not used. Check that Force load libraries "
                "is on for this executable in Modify Executables.")),
            ),
        )

    def _warn_if_game_closed_at_once(self, via_launcher: bool, run: int) -> None:
        """Without the license on the Steam account, or with a mod that stops it,
        the game closes in seconds with no message of its own."""
        if (
            via_launcher
            or run != self._game_run
            or self._early_exit_warning_shown
            or time.time() - self._last_launch_time >= self.EarlyExitSeconds
        ):
            return
        launch_time = self._last_launch_time
        # Steam can restart the game by itself: the notice waits to see that
        QTimer.singleShot(
            self.EarlyExitRecheck, lambda: self._show_early_exit_warning(launch_time)
        )

    def _show_early_exit_warning(self, launch_time: float) -> None:
        if (
            self._early_exit_warning_shown
            or launch_time != self._last_launch_time
            or self._elden_ring_process_state(_running_processes()) is not False
        ):
            return
        self._early_exit_warning_shown = True
        qWarning("Elden Ring MO2: the game closed right after starting.")
        QMessageBox.warning(
            self._message_parent(),
            self._game_text(_tr("Elden Ring closed right after starting")),
            _tr("Check that Steam is open on the account that owns the game, and that "
            "the game starts from Steam. If it does, turn off your mods and add them "
            "back one by one to find the one stopping it."),
        )

    def _active_mods_have_game_files(self) -> bool:
        folders = EldenRingModDataChecker._asset_directories
        for _name, root, active in self._mod_folders():
            if not active:
                continue
            try:
                if any(
                    entry.is_dir() and entry.name.casefold() in folders
                    for entry in root.iterdir()
                ):
                    return True
            except OSError:
                continue
        return False

    def _finish_when_game_closes(
        self,
        via_launcher: bool,
        session_directory: Path | None,
        cleanup_armed: bool,
        overwrite_path: Path | None,
        waited: float,
        seen: bool,
        run: int = 0,
        unknown: float = 0.0,
    ) -> None:
        # one reading per tick for the game and its launchers
        processes = _running_processes()
        state = self._elden_ring_process_state(processes)
        seen = seen or state is True
        interval = (
            self.SwapWatchInterval
            if via_launcher and session_directory is not None
            else self.GameWatchInterval
        )
        # seconds in a row the process list could not be read
        unknown = unknown + interval / 1000 if state is None else 0.0
        if unknown >= self.LauncherWaitLimit:
            # the saves stay hidden: the next launch, or the next MO2 start,
            # restores them once the game is known to be closed
            qCritical(
                "Elden Ring MO2: could not tell for ten minutes whether the game "
                "is still running; stopped waiting. The hidden Steam saves stay "
                "in recovery until the next launch or MO2 start."
            )
            return
        # a launcher still open can start the game again, against these saves
        game_closed = state is False and (
            not via_launcher
            or (
                (seen or waited >= self.LauncherStartGrace)
                and not self._launcher_running(processes)
            )
        )
        if via_launcher:
            self._update_launcher_notice(session_directory, state, seen, processes)
        if not game_closed:
            if waited == 0:
                qInfo(
                    "Elden Ring MO2: waiting for Elden Ring to close before "
                    "restoring the Steam saves."
                )
            QTimer.singleShot(
                interval,
                lambda: self._finish_when_game_closes(
                    via_launcher,
                    session_directory,
                    cleanup_armed,
                    overwrite_path,
                    waited + interval / 1000,
                    seen,
                    run,
                    unknown,
                ),
            )
            return

        # the notices must not stop the saves coming back below
        try:
            self._warn_if_loose_files_were_skipped(via_launcher, seen)
            self._warn_if_game_closed_at_once(via_launcher, run)
        except Exception as error:
            qWarning(f"Elden Ring MO2: a notice after the game closed failed: {error!r}")
        # a later launch may have restored this session and started its own
        if session_directory is not None and self._active_save_quarantine == session_directory:
            try:
                restored_cleanly = self._restore_global_save_quarantine(
                    session_directory, Path(self.savesDirectory().absolutePath())
                )
                self._active_save_quarantine = None
                qInfo(
                    "Elden Ring MO2: the game closed; the Steam saves are back "
                    "in their folder."
                )
                if self._displaced_newer_saves or self._kept_aside_saves:
                    self._report_displaced_saves()
                elif not restored_cleanly:
                    QMessageBox.warning(
                        self._message_parent(),
                        self._game_text(_tr("Elden Ring save recovery folder retained")),
                        _tr("The original Steam saves were restored. Additional "
                        "save output created outside the selected MO2 route was "
                        "preserved in the recovery folder for review."),
                    )
            except (OSError, RuntimeError, ValueError) as error:
                qCritical(f"Elden Ring MO2: global save restoration failed: {error}")
                QMessageBox.critical(
                    self._message_parent(),
                    self._game_text(_tr("Elden Ring save recovery needed")),
                    self._game_text(_tr("MO2 could not restore the original Steam saves automatically. "
                    "The files remain in the MO2 Revamped recovery folder. Do not "
                    "launch Elden Ring or Steam until recovery is complete.")),
                )
        # a newer launch owns Overwrite now; its own cleanup runs after it
        if cleanup_armed and overwrite_path is not None and run == self._game_run:
            # the option may have been turned off while the game ran
            config_path = getattr(self, "_overwrite_cleanup_config", None)
            if config_path is not None and not self._read_clear_overwrite_after_game(config_path):
                qInfo(
                    "Elden Ring MO2: Overwrite cleanup was turned off while the "
                    "game ran; Overwrite was left as it is."
                )
            else:
                self._clear_overwrite_contents(overwrite_path)

    def _update_launcher_notice(
        self,
        session_directory: Path | None,
        state: bool | None,
        seen: bool,
        processes: list[tuple[int, str]] | None = None,
    ) -> None:
        """Says so once when the game has closed and its launcher stays open: the
        saves wait for the launcher, and the user may not know it is there."""
        # a watch of an older launch, or one with no saves hidden
        if session_directory is None or session_directory != self._active_save_quarantine:
            return
        launcher = self._open_launcher(processes) if state is False and seen else None
        if launcher is None:
            self._launcher_open_since = None
            self._close_launcher_notice()
            return
        now = time.monotonic()
        if self._launcher_open_since is None:
            self._launcher_open_since = now
        if (
            now - self._launcher_open_since < self.LauncherNoticeDelay
            or self._launcher_notice_session == session_directory
        ):
            return
        self._launcher_notice_session = session_directory
        # not modal, and it doesn't take the focus: the watch goes on behind it
        box = QMessageBox(self._message_parent())
        box.setWindowModality(Qt.WindowModality.NonModal)
        box.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        box.setIcon(QMessageBox.Icon.Information)
        box.setWindowTitle(_tr("Steam saves still hidden"))
        box.setText(
            _tr("The game has closed, but {0} is still open. MO2 puts the Steam saves "
                "back when it closes. Don't start the game from Steam until then.").format(launcher)
        )
        self._launcher_notice = box
        box.show()

    def _close_launcher_notice(self) -> None:
        box = self._launcher_notice
        self._launcher_notice = None
        if box is not None:
            try:
                box.close()
            except RuntimeError:
                pass

    @staticmethod
    def _read_clear_overwrite_after_game(path: Path) -> bool:
        config = configparser.ConfigParser()
        try:
            with path.open("r", encoding="utf-8-sig") as stream:
                config.read_file(stream)
            return config.getboolean("Cleanup", "clear_overwrite_after_game", fallback=False)
        except (OSError, UnicodeError, configparser.Error, ValueError):
            # unreadable: nothing is deleted
            return False

    def _read_boot_boost(self, path: Path) -> bool:
        # on unless the profile turned it off
        default = True
        config = configparser.ConfigParser()
        try:
            config.read(path, encoding="utf-8-sig")
            return config.getboolean("Startup", "boot_boost", fallback=default)
        except (OSError, UnicodeError, configparser.Error, ValueError):
            return default

    def _read_skip_intro_logos(self, path: Path) -> bool:
        # on unless the profile turned it off: the logos only cost time
        # (Elden Ring only; the bridge has no logo patch for Nightreign)
        default = self.GameShortName != "nightreign"
        config = configparser.ConfigParser()
        try:
            config.read(path, encoding="utf-8-sig")
            return config.getboolean("Startup", "skip_intro_logos", fallback=default)
        except (OSError, UnicodeError, configparser.Error, ValueError):
            return default

    def _read_profile_launch_settings(
        self, path: Path
    ) -> tuple[bool, bool, bool, bool, bool, int, int]:
        config = configparser.ConfigParser()
        try:
            config.read(path, encoding="utf-8-sig")
            start_minimized = config.getboolean(
                "Startup", "start_minimized", fallback=None
            )
            if start_minimized is None:
                start_minimized = config.getboolean(
                    "Startup", "prevent_focus_steal", fallback=None
                )
            if start_minimized is None:
                # Keep profiles created by earlier releases working.
                start_minimized = config.getboolean(
                    "Startup", "start_in_background", fallback=False
                )
            exclude_cpu0 = config.getboolean(
                "Performance", "exclude_cpu0_after_start", fallback=False
            )
            black_startup_background = config.getboolean(
                "Startup", "black_startup_background", fallback=False
            )
            clear_overwrite_after_game = config.getboolean(
                "Cleanup", "clear_overwrite_after_game", fallback=False
            )
            clear_overwrite_logs_before_game = config.getboolean(
                "Cleanup", "clear_overwrite_logs_before_game", fallback=False
            )
            process_priority = config.getint(
                "Performance", "process_priority", fallback=0
            )
            if process_priority not in (0, 1):
                process_priority = 0
            bridge_log_history = config.getint(
                "Diagnostics", "previous_bridge_sessions", fallback=1
            )
            if bridge_log_history not in (1, 3, 5):
                bridge_log_history = 1
            return (
                start_minimized,
                black_startup_background,
                exclude_cpu0,
                clear_overwrite_after_game,
                clear_overwrite_logs_before_game,
                process_priority,
                bridge_log_history,
            )
        except (OSError, UnicodeError, configparser.Error, ValueError) as error:
            qCritical(
                f"Elden Ring MO2: could not read profile launch settings "
                f"{path.name}: {error}. Using safe defaults."
            )
            return False, False, False, False, False, 0, 1

    def _installed_native_routes_for_mod(
        self, mapped_mod_info, mod_root: Path, mapped_mod: str
    ) -> tuple[dict[str, str], list[str]]:
        """Read and validate one mod's installed native-route index."""
        route_index_by_source: dict[str, str] = {}
        installed_native_routes: list[str] = []
        try:
            stored_route_index = mapped_mod_info.pluginSettings(
                self.ArchiveLayoutInstallerName
            ).get(self.InstalledNativeRouteIndexSetting, "")
            if isinstance(stored_route_index, str) and stored_route_index.strip():
                stored_route_index = json.loads(stored_route_index)
            if isinstance(stored_route_index, dict):
                stored_routes = stored_route_index.get("dlls", [])
                if isinstance(stored_routes, list):
                    for installed in stored_routes:
                        if not isinstance(installed, str):
                            continue
                        installed_path = Path(installed.replace("\\", "/"))
                        if (
                            installed_path.is_absolute()
                            or not installed_path.parts
                            or any(part in {"", ".", ".."} for part in installed_path.parts)
                            or ":" in installed_path.parts[0]
                            or installed_path.suffix.casefold() != ".dll"
                        ):
                            continue
                        try:
                            resolved_installed_path = mod_root.joinpath(
                                *installed_path.parts
                            ).resolve(strict=True)
                            resolved_installed_path.relative_to(mod_root)
                        except (OSError, ValueError):
                            continue
                        if resolved_installed_path.is_file():
                            installed_native_routes.append(installed_path.as_posix())
                stored_mapping = stored_route_index.get("source_to_installed", {})
                if isinstance(stored_mapping, dict):
                    for source, installed in stored_mapping.items():
                        if not isinstance(source, str) or not isinstance(installed, str):
                            continue
                        source_key = source.replace("\\", "/").strip("/").casefold()
                        installed_path = Path(installed.replace("\\", "/"))
                        if (
                            not source_key
                            or installed_path.is_absolute()
                            or not installed_path.parts
                            or any(part in {"", ".", ".."} for part in installed_path.parts)
                            or ":" in installed_path.parts[0]
                            or installed_path.suffix.casefold() != ".dll"
                        ):
                            continue
                        route_index_by_source[source_key] = installed_path.as_posix()
        except (OSError, TypeError, ValueError, json.JSONDecodeError) as error:
            qInfo(
                "Elden Ring MO2: ignored an unavailable installed native-route "
                f"index for '{mapped_mod}': {error}."
            )

        return route_index_by_source, installed_native_routes

    def _resolve_revamped_native_profile(self, profile) -> dict | None:
        """Resolve Revamped's ordered native list against installed mod routes.
        MO2 asks for the forced loads on every refresh and a launch resolves the
        profile several times: its log lines are written when they change."""
        notes: list = []
        try:
            return self._read_revamped_native_profile(profile, notes)
        finally:
            self._log_when_changed("native_profile", notes)

    def _log_when_changed(self, key: str, notes: list) -> None:
        """Writes (log function, text) lines unless they are the ones last
        written under this key."""
        logged = self.__dict__.setdefault("_logged_notes", {})
        if logged.get(key) == notes:
            return
        logged[key] = list(notes)
        for log, text in notes:
            log(text)

    def _read_revamped_native_profile(self, profile, notes: list) -> dict | None:
        def qInfo(text):  # noqa: N802 - collected for _log_when_changed
            notes.append((_module_qinfo, text))

        def qWarning(text):  # noqa: N802
            notes.append((_module_qwarning, text))

        mo2_mod_name = ""
        try:
            profile_root = Path(profile.absolutePath()).resolve()
            profile_config = profile_root / self.ProfileLaunchConfig
            config = configparser.ConfigParser(interpolation=None)
            # a file that exists but cannot be read is reported, not taken
            # as "no native profile"
            try:
                with profile_config.open("r", encoding="utf-8-sig") as stream:
                    config.read_file(stream)
            except FileNotFoundError:
                return None
            section = "RevampedNativeProfile"
            if not config.has_section(section):
                return None
            mo2_mod_name = config.get(section, "mod", fallback="").strip()

            natives_by_index: dict[int, dict] = {}
            for key, value in config.items(section):
                if key in {"mod", "version"}:
                    continue
                prefix, separator, raw_index = key.partition("_")
                if (
                    not separator
                    or not raw_index.isdecimal()
                    or prefix not in {"native", "optional", "enabled", "initializer", "mod"}
                ):
                    raise ValueError(
                        _tr("Unsupported Revamped native profile setting '{0}'.").format(key)
                    )
                index = int(raw_index)
                if index <= 0:
                    raise ValueError(_tr("Native entry numbers must be greater than zero."))
                entry = natives_by_index.setdefault(index, {})
                if prefix == "native":
                    if "path" in entry:
                        raise ValueError(_tr("Native entry {0} is declared more than once.").format(index))
                    entry["path"] = value.strip()
                elif prefix in {"optional", "enabled"}:
                    if value.strip().casefold() not in {"true", "false"}:
                        raise ValueError(
                            _tr("'{0}' must be set to true or false.").format(key)
                        )
                    # optional_1 and optional_01 name the same entry
                    if prefix in entry:
                        raise ValueError(
                            _tr("Native entry {0} is declared more than once.").format(index)
                        )
                    entry[prefix] = value.strip().casefold() == "true"
                elif prefix == "mod":
                    if "mod_name" in entry:
                        raise ValueError(_tr("Native entry {0} has more than one mod mapping.").format(index))
                    entry["mod_name"] = value.strip()
                else:
                    if "initializer" in entry:
                        raise ValueError(
                            _tr("Native entry {0} has more than one initializer.").format(index)
                        )
                    # an empty value (a hand edit) means no initializer
                    if value.strip():
                        entry["initializer"] = {"function": value.strip()}

            natives = [natives_by_index[index] for index in sorted(natives_by_index)]
            if not natives:
                raise ValueError(
                    _tr("The [RevampedNativeProfile] section does not declare any native DLLs.")
                )

            mod_list = self._organizer.modList()
            active_mods = [
                name
                for name in mod_list.allModsByProfilePriority()
                if mod_list.state(name) & mobase.ModState.ACTIVE
            ]
            active_mod_by_name: dict[str, str] = {}
            active_mod_priority: dict[str, int] = {}
            for priority, name in enumerate(active_mods):
                folded_name = name.casefold()
                active_mod_by_name.setdefault(folded_name, name)
                active_mod_priority.setdefault(name, priority)
            entries = []
            declared_names: set[str] = set()
            declared_libraries: set[str] = set()
            # (mod, DLL name) of entries switched off in the tool: the
            # general DLL scan must not load them either
            disabled_libraries: set[tuple[str, str]] = set()
            seen_routes: set[tuple[str, str]] = set()
            seen_virtual_paths: dict[str, dict] = {}
            scope_priorities: set[int] = set()
            native_route_index_cache: dict[
                str, tuple[dict[str, str], list[str]]
            ] = {}
            legacy_scope_root = None
            legacy_scope_mod = None
            legacy_profile = not any("mod_name" in native for native in natives)
            if legacy_profile:
                if not mo2_mod_name:
                    raise ValueError(
                        _tr("The [RevampedNativeProfile] section needs a mod mapping.")
                    )
                legacy_scope_mod = active_mod_by_name.get(mo2_mod_name.casefold())
                if legacy_scope_mod is None:
                    raise ValueError(
                        _tr("The mapped MO2 mod '{0}' is not enabled in this profile.").format(mo2_mod_name)
                    )
                legacy_mod_info = mod_list.getMod(legacy_scope_mod)
                if legacy_mod_info is None:
                    raise ValueError(
                        _tr("The mapped MO2 mod '{0}' is unavailable.").format(legacy_scope_mod)
                    )
                legacy_scope_root = Path(legacy_mod_info.absolutePath()).resolve()
                if not legacy_scope_root.is_dir():
                    raise ValueError(
                        _tr("The mapped MO2 mod folder '{0}' is unavailable.").format(legacy_scope_root)
                    )
            # known before any entry resolves: an entry of an overridden copy can
            # come before the winner's switched-off one
            for native in natives:
                if isinstance(native, dict) and native.get("enabled", True) is False:
                    disabled_mod = str(native.get("mod_name") or mo2_mod_name).strip()
                    disabled_route = str(native.get("path") or "").replace("\\", "/")
                    if disabled_mod and disabled_route:
                        disabled_libraries.add(
                            (disabled_mod.casefold(), disabled_route.rsplit("/", 1)[-1].casefold())
                        )
            for native in natives:
                if not isinstance(native, dict):
                    raise ValueError(_tr("A Revamped native profile entry is invalid."))
                enabled = native.get("enabled", True)
                if not isinstance(enabled, bool):
                    raise ValueError(_tr("A native profile entry's enabled value must be true or false."))
                if not enabled:
                    continue

                native_mod_name = str(native.get("mod_name") or mo2_mod_name).strip()
                if not native_mod_name:
                    raise ValueError(
                        _tr("Every native entry needs a mod mapping, either in 'mod' "
                        "or in its indexed 'mod_N' value.")
                    )
                mapped_mod = active_mod_by_name.get(native_mod_name.casefold())
                if mapped_mod is None:
                    if "mod_name" in native:
                        qInfo(
                            "Elden Ring MO2: skipped native profile entry for "
                            f"inactive mod '{native_mod_name}'."
                        )
                        continue
                    raise ValueError(
                        _tr("The mapped MO2 mod '{0}' is not enabled in this profile.").format(native_mod_name)
                    )
                mapped_mod_info = mod_list.getMod(mapped_mod)
                if mapped_mod_info is None:
                    raise ValueError(_tr("The mapped MO2 mod '{0}' is unavailable.").format(mapped_mod))
                mod_root = Path(mapped_mod_info.absolutePath()).resolve()
                if not mod_root.is_dir():
                    raise ValueError(_tr("The mapped MO2 mod folder '{0}' is unavailable.").format(mod_root))

                route_index_cache_key = os.path.normcase(str(mod_root))
                if route_index_cache_key not in native_route_index_cache:
                    native_route_index_cache[route_index_cache_key] = (
                        self._installed_native_routes_for_mod(
                            mapped_mod_info, mod_root, mapped_mod
                        )
                    )
                route_index_by_source, installed_native_routes = (
                    native_route_index_cache[route_index_cache_key]
                )

                raw_path = native.get("path")
                if not isinstance(raw_path, str) or not raw_path.strip():
                    raise ValueError(_tr("An enabled Revamped native entry is missing its path."))
                normalized_path = raw_path.strip().replace("\\", "/")
                relative_path = Path(normalized_path)
                if (
                    relative_path.is_absolute()
                    or not relative_path.parts
                    or any(part in {"", ".", ".."} for part in relative_path.parts)
                    or ":" in relative_path.parts[0]
                    or relative_path.suffix.casefold() != ".dll"
                ):
                    raise ValueError(
                        _tr("Unsupported Revamped native path '{0}'. Use a relative DLL path.").format(raw_path)
                    )

                route = relative_path.as_posix()
                # a profile saved before these stayed out of the list
                if (
                    self._is_game_runtime_dll(relative_path.name)
                    or relative_path.name.casefold() in EldenRingModDataChecker._root_loader_dlls
                ):
                    qInfo(f"Elden Ring MO2: skipped '{route}' from the native profile: never forced.")
                    continue
                route_key = (mapped_mod.casefold(), route.casefold())
                if route_key in seen_routes:
                    raise ValueError(
                        _tr("The Revamped native profile lists '{0}' more than once.").format(route)
                    )
                seen_routes.add(route_key)

                optional = native.get("optional", False)
                if not isinstance(optional, bool):
                    raise ValueError(_tr("A native profile entry's optional value must be true or false."))

                # The installer records the final mod-relative route for every
                # archive DLL it can identify. Use that first, then support
                # older or manually maintained mods through known MO2 paths.
                route_candidates = []
                # the index is keyed by the archive's own path, without the mod
                indexed_route = route_index_by_source.get(route.casefold())
                if indexed_route:
                    route_candidates.append(indexed_route)
                route_candidates.append(route)
                route_parts = relative_path.parts
                if route_parts[0].casefold() in {"native", "natives"} and len(route_parts) > 1:
                    native_tail = Path(*route_parts[1:]).as_posix()
                    route_candidates.extend((
                        f"DLLs/{route}",
                        f"DLLs/{native_tail}",
                        f"DLLs/{relative_path.name}",
                    ))
                else:
                    route_candidates.extend((f"DLLs/{route}", f"DLLs/{relative_path.name}"))

                resolved_candidates: list[tuple[str, Path]] = []
                checked_routes: set[str] = set()
                for candidate_route in route_candidates:
                    candidate_key = candidate_route.casefold()
                    if candidate_key in checked_routes:
                        continue
                    checked_routes.add(candidate_key)
                    candidate_path = mod_root.joinpath(*Path(candidate_route).parts)
                    try:
                        candidate_path = candidate_path.resolve(strict=True)
                        candidate_path.relative_to(mod_root)
                    except (OSError, ValueError):
                        continue
                    if candidate_path.is_file():
                        resolved_candidates.append(
                            (candidate_route.replace("\\", "/"), candidate_path)
                        )
                        break

                if not resolved_candidates and installed_native_routes:
                    route_aliases = [route.casefold()]
                    if (
                        route_parts[0].casefold() in {"native", "natives"}
                        and len(route_parts) > 1
                    ):
                        route_aliases.append(native_tail.casefold())
                    indexed_matches = {
                        installed
                        for installed in installed_native_routes
                        if any(
                            installed.casefold() == alias
                            or installed.casefold().endswith("/" + alias)
                            for alias in route_aliases
                        )
                    }
                    if not indexed_matches:
                        indexed_matches = {
                            installed
                            for installed in installed_native_routes
                            if Path(installed).name.casefold()
                            == relative_path.name.casefold()
                        }
                    if len(indexed_matches) > 1:
                        raise ValueError(
                            _tr("The installed route index contains multiple matches for '{0}'. Keep the DLL at a distinct relative path or remove the duplicate.").format(route)
                        )
                    if indexed_matches:
                        indexed_route = next(iter(indexed_matches))
                        indexed_path = mod_root.joinpath(*Path(indexed_route).parts)
                        try:
                            indexed_path = indexed_path.resolve(strict=True)
                            indexed_path.relative_to(mod_root)
                        except (OSError, ValueError):
                            indexed_path = None
                        if indexed_path is not None and indexed_path.is_file():
                            resolved_candidates.append(
                                (indexed_route, indexed_path)
                            )

                if not resolved_candidates:
                    dll_root = mod_root / "DLLs"
                    if dll_root.is_dir():
                        try:
                            matches = []
                            for candidate_path in dll_root.rglob(relative_path.name):
                                try:
                                    resolved = candidate_path.resolve(strict=True)
                                    resolved.relative_to(mod_root)
                                except (OSError, ValueError):
                                    continue
                                if resolved.is_file():
                                    matches.append((resolved.relative_to(mod_root).as_posix(), resolved))
                        except OSError as error:
                            raise ValueError(
                                _tr("Could not search MO2 layout DLLs for '{0}': {1}").format(route, error)
                            ) from error
                        unique_matches = {
                            str(path).casefold(): (virtual, path)
                            for virtual, path in matches
                        }
                        if len(unique_matches) > 1:
                            raise ValueError(
                                _tr("MO2 layout contains multiple DLLs named '{0}'. Keep the DLL at its declared folder or remove the duplicate.").format(relative_path.name)
                            )
                        if unique_matches:
                            resolved_candidates.append(next(iter(unique_matches.values())))

                if not resolved_candidates:
                    if optional:
                        qInfo(f"Elden Ring MO2: skipped optional Revamped native '{route}'.")
                        continue
                    # a mod's update can rename or remove it: the launch goes on
                    # with the DLLs the mods have now
                    qWarning(f"Elden Ring MO2: Revamped native '{route}' is no longer in its mod; skipped.")
                    continue

                virtual_path, _source_path = resolved_candidates[0]
                virtual_key = virtual_path.casefold()
                try:
                    origins = self._organizer.getFileOrigins(virtual_path)
                except Exception as error:
                    raise ValueError(
                        _tr("MO2 could not resolve the winning origin for '{0}': {1}").format(virtual_path, error)
                    ) from error
                # the highest priority wins the file in MO2, as in the
                # general DLL scan
                winner = max(
                    (
                        name
                        for name in origins
                        if name in active_mod_priority
                    ),
                    key=lambda name: active_mod_priority[name],
                    default=None,
                )
                if winner is None:
                    if not optional:
                        qWarning(f"Elden Ring MO2: Revamped native '{virtual_path}' has no active file origin; skipped.")
                    continue
                if (winner.casefold(), Path(virtual_path).name.casefold()) in disabled_libraries:
                    qInfo(
                        f"Elden Ring MO2: not loading '{virtual_path}' from '{winner}'; "
                        "it is switched off in the native profile."
                    )
                    continue
                winning_mod = mod_list.getMod(winner)
                if winning_mod is None:
                    raise ValueError(
                        _tr("MO2's winning mod '{0}' for '{1}' is unavailable.").format(winner, virtual_path)
                    )
                winning_root = Path(winning_mod.absolutePath()).resolve()
                try:
                    library = winning_root.joinpath(*Path(virtual_path).parts).resolve(strict=True)
                except OSError:
                    # MO2's file list is older than the disk (the file went
                    # since its last refresh): the launch goes on without it
                    qWarning(
                        f"Elden Ring MO2: Revamped native '{virtual_path}' is missing from "
                        f"'{winner}'; skipped."
                    )
                    continue
                try:
                    library.relative_to(winning_root)
                except ValueError as error:
                    raise ValueError(
                        _tr("The winning path for '{0}' leaves its MO2 mod.").format(virtual_path)
                    ) from error
                if not library.is_file():
                    raise ValueError(_tr("The winning file for '{0}' is not a DLL.").format(virtual_path))
                # loaded by its path inside MO2's mods folder, not the resolved
                # one: through a linked mods folder the resolved path loses the
                # folder names the bridge looks for (SeamlessCoop's DLL is known
                # by "mods"), and Seamless Co-op's own load names it this way
                load_path = Path(winning_mod.absolutePath()).joinpath(*Path(virtual_path).parts)

                initializer = native.get("initializer")
                if initializer is not None and not isinstance(initializer, dict):
                    raise ValueError(
                        _tr("Revamped native '{0}' has an unsupported initializer condition.").format(route)
                    )
                initializer_function = initializer.get("function") if initializer else None
                if initializer and initializer_function is None:
                    raise ValueError(
                        _tr("Revamped native '{0}' uses an initializer condition this plugin does not support yet.").format(route)
                    )
                if initializer_function is not None and (
                    not isinstance(initializer_function, str)
                    or not initializer_function.isascii()
                    or not initializer_function.isidentifier()
                ):
                    raise ValueError(_tr("Revamped native '{0}' has an unsupported initializer name.").format(route))
                if initializer_function == "NrmInitialize":
                    assets_directory = next(
                        (
                            child
                            for child in library.parent.iterdir()
                            if child.is_dir() and child.name.casefold() == "assets"
                        ),
                        None,
                    )
                    if assets_directory is None:
                        raise ValueError(
                            _tr("NightreignMovement.dll needs its adjacent 'assets' folder. "
                            "Keep original structure retains it; MO2 layout must keep it beside the DLL under DLLs/.")
                        )

                if virtual_path.casefold() != route.casefold():
                    qInfo(
                        f"Elden Ring MO2: resolved Revamped native '{route}' via "
                        f"MO2 layout path '{virtual_path}'."
                    )
                # one game path is one file, the winner's, however many entries
                # name it (a profile saved when another mod won it): it loads
                # once, at the first entry's place
                earlier = seen_virtual_paths.get(virtual_key)
                if earlier is not None:
                    qInfo(
                        f"Elden Ring MO2: '{route}' of '{mapped_mod}' is the same game "
                        f"file as an earlier native profile entry; it loads once, from '{winner}'."
                    )
                    # an initializer names an export of its own copy of the DLL
                    if initializer_function and not earlier["initializer"]:
                        if os.path.normcase(str(_source_path)) == os.path.normcase(str(earlier["real_library"])):
                            earlier["initializer"] = initializer_function
                        else:
                            qInfo(
                                f"Elden Ring MO2: dropped initializer '{initializer_function}' of "
                                f"'{mapped_mod}': the copy that loads is from '{winner}'."
                            )
                    continue
                entry = {
                    "route": route,
                    "virtual_path": virtual_path,
                    "mod_name": winner,
                    "library": load_path,
                    "real_library": library,
                    "initializer": initializer_function,
                }
                seen_virtual_paths[virtual_key] = entry
                entries.append(entry)
                declared_libraries.add(os.path.normcase(str(library.resolve())))
                scope_priorities.add(active_mod_priority[winner])
                if legacy_profile:
                    legacy_scope_root = mod_root
                    legacy_scope_mod = mapped_mod
                declared_names.add(relative_path.name.casefold())

            return {
                "error": None,
                "configuration": profile_config,
                "scope_mod": legacy_scope_mod or mo2_mod_name,
                "scope_root": legacy_scope_root,
                "scope_priority": (
                    active_mods.index(legacy_scope_mod)
                    if legacy_profile and legacy_scope_mod in active_mods
                    else min(scope_priorities, default=len(active_mods))
                ),
                "entries": entries,
                "declared_names": declared_names,
                "declared_libraries": declared_libraries,
                "disabled_libraries": disabled_libraries,
            }
        except (OSError, UnicodeError, configparser.Error, ValueError) as error:
            return {
                "error": str(error),
                "scope_mod": mo2_mod_name,
                "entries": [],
                "declared_names": set(),
            }

    def _apply_revamped_native_load_order(
        self, libraries: list[Path], profile_data: dict, notes: list | None = None
    ) -> list[Path]:
        """Apply the Revamped profile's declared native DLL order. With notes,
        its log lines join the forced loads' summary instead."""
        note = notes.append if notes is not None else _module_qinfo
        scope_root = profile_data.get("scope_root")
        declared_libraries = profile_data.get("declared_libraries", set())
        disabled_libraries = profile_data.get("disabled_libraries", set())
        if scope_root is None and not declared_libraries and not disabled_libraries:
            return libraries
        scope_root = Path(scope_root) if scope_root is not None else None
        declared_names = profile_data.get("declared_names", set())
        mod_list = self._organizer.modList()
        active_mods = [
            name
            for name in mod_list.allModsByProfilePriority()
            if mod_list.state(name) & mobase.ModState.ACTIVE
        ]

        def library_priority(library: Path) -> int:
            for index, mod_name in enumerate(active_mods):
                mod = mod_list.getMod(mod_name)
                if mod is None:
                    continue
                try:
                    library.relative_to(Path(mod.absolutePath()).resolve())
                    return index
                except (OSError, ValueError):
                    continue
            return len(active_mods)

        retained: list[Path] = []
        for library in libraries:
            library_path = Path(library)
            if scope_root is not None:
                try:
                    library_path.relative_to(scope_root)
                    continue
                except ValueError:
                    pass
                if library_path.name.casefold() in declared_names:
                    continue
            try:
                library_key = os.path.normcase(str(library_path.resolve()))
            except (OSError, RuntimeError):
                library_key = os.path.normcase(os.path.abspath(str(library_path)))
            if library_key in declared_libraries:
                continue
            if disabled_libraries:
                owner = library_priority(library_path)
                if owner < len(active_mods) and (
                    active_mods[owner].casefold(),
                    library_path.name.casefold(),
                ) in disabled_libraries:
                    note(
                        f"Elden Ring MO2: not loading '{library_path.name}' from "
                        f"'{active_mods[owner]}'; it is switched off in the native profile."
                    )
                    continue
            retained.append(library_path)

        scope_priority = int(profile_data.get("scope_priority", len(active_mods)))

        insertion = next(
            (
                index
                for index, library in enumerate(retained)
                if library_priority(library) > scope_priority
            ),
            len(retained),
        )
        ordered = [Path(entry["library"]) for entry in profile_data.get("entries", [])]
        for entry in profile_data.get("entries", []):
            note(
                f"Elden Ring MO2: Revamped native load order queued '{entry['route']}' "
                f"as '{entry['virtual_path']}' from '{entry['mod_name']}'."
            )
        return [*retained[:insertion], *ordered, *retained[insertion:]]

    def _clear_overwrite_logs_before_game(self) -> None:
        # a running game (or a tool) may still be writing its logs there
        if self._elden_ring_process_state() is not False:
            qInfo(
                "Elden Ring MO2: Elden Ring is already running; old logs in "
                "Overwrite were left in place."
            )
            return
        try:
            overwrite_path = Path(self._organizer.overwritePath())
            if overwrite_path.name.casefold() != "overwrite":
                raise ValueError(_tr("MO2 returned a path that is not named Overwrite"))

            root_stat = os.lstat(overwrite_path)
            reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
            if getattr(root_stat, "st_file_attributes", 0) & reparse_flag:
                raise ValueError(_tr("the Overwrite directory is a reparse point"))
            if not stat.S_ISDIR(root_stat.st_mode):
                raise ValueError(_tr("the Overwrite path is not a directory"))

            removed = self._remove_overwrite_logs(overwrite_path)
            qInfo(
                "Elden Ring MO2: removed "
                f"{removed} old .log file(s) from Overwrite before launch."
            )
        except FileNotFoundError:
            qInfo(
                "Elden Ring MO2: Overwrite was missing or changed before launch; "
                "no old logs were cleared."
            )
        except (OSError, TypeError, ValueError) as error:
            qCritical(
                "Elden Ring MO2: could not clear old .log files from Overwrite "
                f"before launch: {error}. The game will still start."
            )

    @staticmethod
    def _remove_overwrite_logs(overwrite_path: Path) -> int:
        removed = 0
        with os.scandir(overwrite_path) as entries:
            for entry in entries:
                path = Path(entry.path)
                entry_stat = os.lstat(path)
                reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
                if getattr(entry_stat, "st_file_attributes", 0) & reparse_flag:
                    continue
                if stat.S_ISDIR(entry_stat.st_mode):
                    removed += EldenRingMo2Game._remove_overwrite_logs(path)
                elif path.suffix.casefold() == ".log":
                    try:
                        os.unlink(path)
                    except OSError as error:
                        # a log still open elsewhere: the others are removed
                        qInfo(f"Elden Ring MO2: kept '{path.name}' in Overwrite: {error}")
                        continue
                    removed += 1
        return removed

    def _arm_overwrite_cleanup(self) -> None:
        try:
            overwrite_path = Path(self._organizer.overwritePath())
        except (OSError, TypeError, ValueError) as error:
            qCritical(
                f"Elden Ring MO2: automatic Overwrite cleanup could not get "
                f"MO2's Overwrite path: {error}. The game will still start."
            )
            return

        self._overwrite_cleanup_path = overwrite_path
        self._overwrite_cleanup_armed = True
        qInfo("Elden Ring MO2: Overwrite cleanup will run after Elden Ring closes.")

    @staticmethod
    def _remove_overwrite_entry(path: Path, kept: list[str] | None = None) -> int:
        """Removes one Overwrite entry and returns how many were removed. With
        `kept`, an entry that can't be removed (a file still open elsewhere)
        is listed there and the others still go."""
        entry_stat = os.lstat(path)
        reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
        is_reparse_point = bool(
            getattr(entry_stat, "st_file_attributes", 0) & reparse_flag
        )

        # Never follow symlinks, junctions, or other Windows reparse points
        # outside MO2's Overwrite directory.
        if is_reparse_point:
            if stat.S_ISDIR(entry_stat.st_mode):
                os.rmdir(path)
            else:
                os.unlink(path)
            return 1

        if stat.S_ISDIR(entry_stat.st_mode):
            removed = 0
            kept_before = len(kept) if kept is not None else 0
            with os.scandir(path) as entries:
                for entry in entries:
                    try:
                        removed += EldenRingMo2Game._remove_overwrite_entry(
                            Path(entry.path), kept
                        )
                    except OSError as error:
                        if kept is None:
                            raise
                        kept.append(f"{entry.path}: {error}")
            if kept is not None and len(kept) > kept_before:
                # the folder still holds what could not be removed
                return removed
            os.rmdir(path)
            return removed + 1

        os.unlink(path)
        return 1

    def _clear_overwrite_contents(self, overwrite_path: Path) -> None:
        try:
            if overwrite_path.name.casefold() != "overwrite":
                raise ValueError(_tr("MO2 returned a path that is not named Overwrite"))

            root_stat = os.lstat(overwrite_path)
            reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
            if getattr(root_stat, "st_file_attributes", 0) & reparse_flag:
                raise ValueError(_tr("the Overwrite directory is a reparse point"))
            if not stat.S_ISDIR(root_stat.st_mode):
                raise ValueError(_tr("the Overwrite path is not a directory"))

            removed = 0
            kept: list[str] = []
            with os.scandir(overwrite_path) as entries:
                for entry in entries:
                    try:
                        removed += self._remove_overwrite_entry(Path(entry.path), kept)
                    except OSError as error:
                        kept.append(f"{entry.path}: {error}")
            qInfo(
                "Elden Ring MO2: cleared "
                f"{removed} file(s) and folder(s) from Overwrite after the game closed."
            )
            if kept:
                # one locked file must not keep everything else in Overwrite
                qCritical(
                    f"Elden Ring MO2: kept {len(kept)} Overwrite entr(y/ies) that "
                    "could not be removed: " + "; ".join(kept[:10])
                )
        except FileNotFoundError:
            qInfo("Elden Ring MO2: Overwrite was already empty after the game closed.")
        except (OSError, ValueError) as error:
            qCritical(
                f"Elden Ring MO2: could not fully clear Overwrite after the game "
                f"closed: {error}"
            )

    def _activate_profile_config_environment(self, path: Path, initializers: list[dict] | None = None) -> None:
        config_key = self.ProfileLaunchConfigEnvironment
        self._profile_config_env_previous = os.environ.get(config_key)
        self._profile_config_env_active = True
        if path.is_file():
            os.environ[config_key] = str(path)
            if len(str(path)) >= 260:
                # the bridge reads this path into a MAX_PATH buffer
                qWarning(
                    f"Elden Ring MO2: the profile's settings path is {len(str(path))} "
                    "characters long; the bridge reads at most 259, so it ignores this "
                    "profile's startup options (minimized start, black background, "
                    "CPU 0, priority) and skips the intro logos whatever the profile "
                    "says. A shorter instance or profile path fixes it."
                )
        else:
            os.environ.pop(config_key, None)

        initializer_key = self.NativeInitializerEnvironment
        self._native_initializer_env_previous = os.environ.get(initializer_key)
        self._native_initializer_env_active = True
        initializer_values = [
            # the path as its forced load names it: the bridge compares the
            # two exactly, so a linked mods folder must not be resolved here
            f"{os.path.abspath(entry['library'])}|{entry['initializer']}"
            for entry in (initializers or [])
            if entry.get("initializer")
        ]
        if initializer_values and self._set_bridge_environment(
            initializer_key, "\n".join(initializer_values)
        ):
            qInfo(
                "Elden Ring MO2: Revamped native initializer(s) passed to bridge: "
                + ", ".join(
                    entry["initializer"]
                    for entry in (initializers or [])
                    if entry.get("initializer")
                )
                + "."
            )
        else:
            os.environ.pop(initializer_key, None)

        self._reforged_env_previous = {
            key: os.environ.get(key)
            for key in (
                self.ReforgedAttachEnvironment,
                self.SaveExtensionEnvironment,
                self.SoundFoldersEnvironment,
                self.BootBoostEnvironment,
            )
        }
        # the compatibility DLL also runs with Reforged: it skips BootBoost
        # when the profile turned it off
        os.environ[self.BootBoostEnvironment] = "1" if self._read_boot_boost(path) else "0"
        self._reforged_env_active = True
        reforged_natives = self._reforged_natives()
        # Seamless Co-op names the saves itself (its save_file_extension)
        save_extension = (
            self.ReforgedSaveExtension
            if reforged_natives is not None and self._active_seamless_dll() is None
            else None
        )
        if reforged_natives is not None and self._set_bridge_environment(
            self.ReforgedAttachEnvironment,
            self._reforged_attach_config(reforged_natives, save_extension),
        ):
            qInfo(
                "Elden Ring MO2: ELDEN RING Reforged is active; its natives get "
                "the me3 attach config of this MO2 run."
            )
        else:
            os.environ.pop(self.ReforgedAttachEnvironment, None)
        if save_extension is not None:
            os.environ[self.SaveExtensionEnvironment] = save_extension
            qInfo(
                f"Elden Ring MO2: the bridge names the saves ER0000.{save_extension}, "
                "as ELDEN RING Reforged does."
            )
        else:
            os.environ.pop(self.SaveExtensionEnvironment, None)
        sound_folders = self._active_sound_folders()
        if sound_folders:
            os.environ[self.SoundFoldersEnvironment] = ";".join(sound_folders)
        else:
            os.environ.pop(self.SoundFoldersEnvironment, None)
        qInfo("Elden Ring MO2: profile launch options passed to bridge.")

    @staticmethod
    def _set_bridge_environment(key: str, value: str) -> bool:
        """Sets a variable for the game, unless Windows' limit of 32767
        characters refuses it; then it is left out with a log line."""
        if len(key) + len(value) >= 32000:
            os.environ.pop(key, None)
            qWarning(
                f"Elden Ring MO2: {key} is too long for Windows "
                f"({len(value)} characters); the game starts without it."
            )
            return False
        os.environ[key] = value
        return True

    # "0" when the profile turned BootBoost off
    BootBoostEnvironment = "ELDENRING_MO2_BOOT_BOOST"

    # the bridge opens mods' sound files from these folders, highest priority first
    SoundFoldersEnvironment = "ELDENRING_MO2_SOUND_FOLDERS"

    def _active_sound_folders(self) -> list[str]:
        mod_list = self._organizer.modList()
        folders = []
        for name in reversed(list(mod_list.allModsByProfilePriority())):
            if not mod_list.state(name) & mobase.ModState.ACTIVE:
                continue
            mod = mod_list.getMod(name)
            if mod is None:
                continue
            folder = Path(mod.absolutePath())
            if not (folder / "sd").is_dir():
                continue
            # the bridge reads the folders as one ";"-separated variable
            if ";" in str(folder):
                qWarning(
                    f"Elden Ring MO2: the sound files of '{name}' are not used: "
                    "its folder path has a ';'."
                )
                continue
            folders.append(str(folder))
        # Overwrite wins over every mod in MO2, so its sound files come first
        # (the bridge takes the first folder that has a file)
        try:
            overwrite = Path(self._organizer.overwritePath())
        except (AttributeError, RuntimeError):
            overwrite = None
        if overwrite is not None and (overwrite / "sd").is_dir() and ";" not in str(overwrite):
            folders.insert(0, str(overwrite))
        if len(";".join(folders)) >= 32000:
            qWarning(
                "Elden Ring MO2: too many mods with sound files for the bridge to "
                "read; mods' sound files are not used this run."
            )
            return []
        return folders

    # reforged.dll reads me3's attach config (JSON, the AttachConfig of me3's
    # launcher-attach-protocol crate) from this variable and stops the game
    # when it is missing
    ReforgedAttachEnvironment = "ME3_ATTACH_CONFIG"
    # the bridge replaces the game's .sl2 with these three letters
    SaveExtensionEnvironment = "ELDENRING_MO2_SAVE_EXTENSION"
    # ELDEN RING Reforged's me3 profile saves to ER0000.err
    ReforgedSaveExtension = "err"

    # me3's BootBoost for every Elden Ring start (the archive headers the game
    # decrypts on each start are cached decrypted), and what me3 does for
    # ELDEN RING Reforged beside loading it: Arxan neutralized (disable_arxan),
    # the raw regulation kept out of the save (its oversized_regulation_fix:
    # Reforged's regulation.bin no longer fits in a save, so every save after
    # the first failed with "run Reforged through its dedicated launcher") and
    # the game's heaps replaced by mimalloc (mem_patch: without it Reforged's
    # sound bank ran out of room and music and voices stopped on every dodge
    # or hit). The Reforged part only runs when ME3_ATTACH_CONFIG is set for
    # Reforged's natives. Source in me3_compat/.
    Me3Compat = "eldenring_mo2_me3_compat.dll"

    def _me3_compat_load(
        self, forced_loads: list[mobase.ExecutableForcedLoadSetting]
    ) -> mobase.ExecutableForcedLoadSetting | None:
        """The me3 compatibility DLL's forced load: on every Elden Ring start
        for BootBoost, and what Reforged needs when it is among the loads."""
        # Nightreign too, for BootBoost (Reforged is an Elden Ring mod)
        reforged = any(
            Path(load.library()).name.casefold() == EldenRingModDataChecker._reforged_dll
            for load in forced_loads
        )
        profile = self._organizer.profile()
        boot_boost = (
            self._read_boot_boost(Path(profile.absolutePath()) / self.ProfileLaunchConfig)
            if profile is not None
            else True
        )
        if not reforged and not boot_boost:
            # nothing else for it to do
            return None
        library = Path(__file__).with_name(self.Me3Compat)
        if not library.is_file():
            qWarning(
                f"Elden Ring MO2: {self.Me3Compat} is missing; "
                + (
                    "ELDEN RING Reforged runs with Arxan active and cannot save."
                    if reforged
                    else "the game starts without BootBoost."
                )
            )
            return None
        return mobase.ExecutableForcedLoadSetting(
            self.GameProcess, str(library)
        ).withEnabled(True).withForced(True)

    def _reforged_natives(self) -> list[Path] | None:
        """This run's native DLLs when ELDEN RING Reforged is among them."""
        own = {
            os.path.normcase(str(Path(__file__).with_name(name)))
            for name in ("eldenring_mo2_bridge.dll", self.Me3Compat)
        }
        # executableForcedLoads lists them in load order
        libraries = [
            Path(load.library())
            for load in self.executableForcedLoads()
            if load.enabled() and os.path.normcase(str(Path(load.library()))) not in own
        ]
        if not any(
            library.name.casefold() == EldenRingModDataChecker._reforged_dll
            for library in libraries
        ):
            return None
        return libraries

    def _reforged_attach_config(
        self, libraries: list[Path], save_extension: str | None
    ) -> str:
        """me3's attach config for ELDEN RING Reforged run from MO2.

        MO2 has already done me3's part: the game files come from the VFS, the
        natives are this run's forced loads and the bridge names the saves, so
        the config lists those, offline (Reforged's online server checks a me3
        launch).
        """
        return json.dumps(
            {
                "game": "eldenring",
                "natives": [{"path": str(library)} for library in libraries],
                "packages": [
                    {"id": "mo2", "path": str(Path(self.dataDirectory().absolutePath()))}
                ],
                "savefile": f"ER0000.{save_extension}" if save_extension else None,
                "cache_path": None,
                "suspend": False,
                "boot_boost": False,
                "skip_logos": False,
                "start_online": False,
                "disable_arxan": Path(__file__).with_name(self.Me3Compat).is_file(),
                "skip_steam_init": False,
            }
        )

    def _restore_profile_config_environment(self) -> None:
        if getattr(self, "_profile_config_env_active", False):
            key = self.ProfileLaunchConfigEnvironment
            previous = self._profile_config_env_previous
            if previous is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = previous
            self._profile_config_env_previous = None
            self._profile_config_env_active = False

        if getattr(self, "_native_initializer_env_active", False):
            key = self.NativeInitializerEnvironment
            previous = self._native_initializer_env_previous
            if previous is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = previous
            self._native_initializer_env_previous = None
            self._native_initializer_env_active = False

        if getattr(self, "_reforged_env_active", False):
            for key, previous in self._reforged_env_previous.items():
                if previous is None:
                    os.environ.pop(key, None)
                else:
                    os.environ[key] = previous
            self._reforged_env_previous = {}
            self._reforged_env_active = False

    def documentsDirectory(self) -> QDir:
        appdata = os.environ.get("APPDATA")
        if not appdata:
            appdata = str(Path.home() / "AppData" / "Roaming")
        return QDir(str(Path(appdata) / self.SaveFolderName))

    def savesDirectory(self) -> QDir:
        return self.documentsDirectory()

    def listSaves(self, folder: QDir) -> list[mobase.ISaveGame]:
        # only the saves the game reads: <account>/ER0000.sl2, a Seamless
        # Co-op .co2, a Convergence .cnv or an ELDEN RING Reforged .err. A recursive search would also
        # list the named save profiles and backups kept inside the shared
        # route.
        return [
            BasicGameSaveGame(file)
            for file in self._route_save_files(Path(folder.absolutePath()))
            if file.suffix.casefold() in {".sl2", ".co2", ".cnv", ".err"}
        ]

    def executables(self) -> list[mobase.ExecutableInfo]:
        game_exe = QFileInfo(
            self.gameDirectory(),
            self.GameBinary,
        )
        executable = mobase.ExecutableInfo(f"{self.GameName} (MO2)", game_exe)
        executable = executable.withWorkingDirectory(self.dataDirectory())
        executable = executable.withSteamAppId(str(self.GameSteamId))
        return [executable, *self._reforged_online_executables()]

    def _reforged_online_executables(self) -> list[mobase.ExecutableInfo]:
        """Reforged's own launcher, for its online mode, from each installed
        ELDEN RING Reforged that kept its release (archive installer choice).
        It starts outside the VFS (AutoSwapLaunchers), as online needs."""
        if not self.AutoSwapLaunchers:
            return []
        try:
            mods_path = Path(self._organizer.modsPath())
        except (AttributeError, RuntimeError, TypeError):
            return []
        online = EldenRingModDataChecker.ReforgedOnlineDirectory
        executables = []
        try:
            launchers = sorted(
                mods_path.glob(f"*/{online}/{EldenRingModDataChecker.ReforgedLauncherPath}")
            )
        except OSError:
            return []
        for index, launcher in enumerate(launchers):
            title = "ELDEN RING Reforged (online)"
            if index:
                title += f" ({launcher.parents[3].name})"
            executables.append(
                mobase.ExecutableInfo(title, QFileInfo(str(launcher)))
                .withWorkingDirectory(QDir(str(launcher.parents[2])))
                .withSteamAppId(str(self.GameSteamId))
            )
        return executables

    def _inactive_reforged_online_programs(
        self, executables: list[mobase.ExecutableInfo]
    ) -> set[str]:
        """The online programs whose Reforged mod is off in this profile."""
        inactive = set()
        try:
            mod_list = self._organizer.modList()
            for executable in executables:
                mod_name = Path(executable.binary().absoluteFilePath()).parents[3].name
                if not mod_list.state(mod_name) & mobase.ModState.ACTIVE:
                    inactive.add(executable.title())
        except (AttributeError, IndexError, RuntimeError):
            return set()
        return inactive

    def _follow_reforged_online_programs(self, selector: QComboBox) -> None:
        """The program list offers Reforged's online mode only with its mod on."""
        if self._online_programs_selector is selector:
            return
        self._online_programs_selector = selector
        self._online_programs_tooltip = selector.toolTip()
        self._online_program_lock = _ProgramListLock(selector)
        selector.installEventFilter(self._online_program_lock)

        def sync(*_args) -> None:
            # several changes in a row need one look at the list
            if not self._online_programs_sync_queued:
                self._online_programs_sync_queued = True
                QTimer.singleShot(0, self._sync_reforged_online_programs)

        selector.model().rowsInserted.connect(sync)
        # the wheel and the arrows can reach a row hidden from the popup
        selector.currentIndexChanged.connect(lambda _row: sync())
        self._organizer.modList().onModStateChanged(lambda _changes: sync())
        self._organizer.onProfileChanged(sync)
        sync()

    def _sync_reforged_online_programs(self) -> None:
        """A Reforged mod installed as the online version decides the program:
        while it is on, the list stays on its launcher."""
        self._online_programs_sync_queued = False
        selector = self._online_programs_selector
        if selector is None or not self.isActive():
            return
        # searches the mods folder: once per look, and anew each time, so a
        # mod just installed is seen
        executables = self._reforged_online_executables()
        hidden = self._inactive_reforged_online_programs(executables)
        # the program of a Reforged mod that was removed stays saved: not offered
        offered = {executable.title() for executable in executables}
        for row in range(selector.count()):
            text = selector.itemText(row)
            if text.startswith("ELDEN RING Reforged (online)") and text not in offered:
                hidden.add(text)
        view = selector.view()
        for row in range(selector.count()):
            view.setRowHidden(row, selector.itemText(row) in hidden)
        online = [
            executable.title()
            for executable in executables
            if executable.title() not in hidden
        ]
        row = selector.findText(online[0]) if online else -1
        if row >= 0:
            if selector.currentIndex() != row:
                selector.setCurrentIndex(row)
            self._online_program_lock.locked = True
            selector.setToolTip(
                _tr("Reforged's online version is on. Turn that mod off to choose another program.")
            )
            self._online_program_locked = True
            return
        if self._online_program_locked:
            self._online_program_locked = False
            self._online_program_lock.locked = False
            selector.setToolTip(self._online_programs_tooltip)
        # MO2's first program is the game
        if selector.currentText() in hidden and selector.count() > 1:
            selector.setCurrentIndex(1)

    def _reforged_online_mod_ready(self, app_path: str) -> bool:
        """A shortcut or the toolbar can still start the online program with its
        mod off: it would run a Reforged the profile has turned off."""
        launcher = Path(app_path)
        if (
            launcher.name.casefold() not in self.AutoSwapLaunchers
            or len(launcher.parents) < 4
            or launcher.parents[2].name != EldenRingModDataChecker.ReforgedOnlineDirectory
        ):
            return True
        mod_name = launcher.parents[3].name
        mod_list = self._organizer.modList()
        try:
            if mod_list.state(mod_name) & mobase.ModState.ACTIVE:
                return True
        except (AttributeError, RuntimeError):
            return True
        box = QMessageBox(self._message_parent())
        box.setIcon(QMessageBox.Icon.Question)
        box.setWindowTitle("ELDEN RING Reforged")
        box.setText(_tr("Reforged is off in this profile"))
        box.setInformativeText(
            _tr("Its online mode starts from the mod {0}. Turn the mod on and start it?").format(mod_name)
        )
        start = box.addButton(_tr("Turn on and start"), QMessageBox.ButtonRole.AcceptRole)
        box.addButton(QMessageBox.StandardButton.Cancel)
        box.setDefaultButton(start)
        box.exec()
        # a child of MO2's window: freed once MO2's event loop runs again,
        # after the choice below is read
        box.deleteLater()
        if box.clickedButton() is not start:
            return False
        mod_list.setActive(mod_name, True)
        self._mod_turned_on_for_launch = mod_name
        return True

    def executableForcedLoads(self) -> list[mobase.ExecutableForcedLoadSetting]:
        if not self.isActive():
            return []
        # MO2 asks again for every executable and refresh; the summary is
        # logged only when it changes
        notes: list[str] = []

        mod_list = self._organizer.modList()
        profile_order = list(mod_list.allModsByProfilePriority())
        active_mods = [
            name
            for name in profile_order
            if mod_list.state(name) & mobase.ModState.ACTIVE
        ]
        profile = self._organizer.profile()
        revamped_native_profile = (
            self._resolve_revamped_native_profile(profile)
            if profile is not None
            else None
        )
        bridge_startup_options = False
        if profile is not None:
            profile_config = Path(profile.absolutePath()) / self.ProfileLaunchConfig
            (
                start_minimized,
                black_startup_background,
                exclude_cpu0,
                _clear_overwrite_after_game,
                _clear_overwrite_logs_before_game,
                _process_priority,
                bridge_log_history,
            ) = self._read_profile_launch_settings(profile_config)
            bridge_startup_options = (
                start_minimized
                or black_startup_background
                or exclude_cpu0
                or _process_priority == 1
                or bridge_log_history > 1
                or self._read_skip_intro_logos(profile_config)
            )

        if not active_mods and not bridge_startup_options:
            notes.append(
                "Elden Ring MO2: no active mods or bridge-dependent profile "
                "options; no native or asset bridge needed."
            )
            # BootBoost still speeds up a start without mods (Nightreign has
            # no intro-logo option to keep the bridge in)
            compat = self._me3_compat_load([])
            if compat is not None:
                notes.append(f"Elden Ring MO2: queued the me3 compatibility DLL {self.Me3Compat}.")
            self._log_forced_load_notes(notes)
            return [compat] if compat is not None else []
        if not active_mods:
            notes.append(
                "Elden Ring MO2: no active mods; loading the native bridge "
                "because bridge-dependent profile options are enabled."
            )

        native_loading = bool(
            self._organizer.pluginSetting(self.name(), self.NativeDllSetting)
        )
        libraries = self._active_native_dlls() if native_loading else []
        if native_loading and revamped_native_profile is not None and revamped_native_profile.get("error"):
            # a direct eldenring.exe launch is then blocked in onAboutToRun;
            # a launcher run keeps every active DLL, as before the native profile
            qWarning(
                "Elden Ring MO2: the Revamped native profile could not be "
                f"read ({revamped_native_profile['error']}); its load order and "
                "switched-off entries are not applied to these forced loads."
            )
        if (
            native_loading
            and revamped_native_profile is not None
            and not revamped_native_profile.get("error")
        ):
            libraries = self._apply_revamped_native_load_order(
                libraries, revamped_native_profile, notes
            )
        root_layout_libraries = (
            self._active_root_layout_dlls(active_mods) if native_loading else []
        )
        if (
            root_layout_libraries
            and revamped_native_profile is not None
            and not revamped_native_profile.get("error")
        ):
            # a root DLL listed in the native profile loads once, in its order
            declared = revamped_native_profile.get("declared_libraries", set())
            disabled = revamped_native_profile.get("disabled_libraries", set())

            def queued_elsewhere(mod_name: str, library: Path) -> bool:
                return (
                    os.path.normcase(str(library)) in declared
                    or (mod_name.casefold(), library.name.casefold()) in disabled
                )

            root_layout_libraries = [
                item
                for item in root_layout_libraries
                if not queued_elsewhere(item[0], Path(item[2]))
            ]
        if not native_loading:
            notes.append("Elden Ring MO2: native DLL loading is disabled in plugin settings.")
        if not libraries and not root_layout_libraries and active_mods:
            notes.append(
                "Elden Ring MO2: no active winning DLLs found under "
                "Game/DLLs/, legacy managed folders, or preserved root layouts."
            )

        if libraries:
            notes.append(
                "Elden Ring MO2: requesting MO2 forced loads for "
                f"{len(libraries)} active mod DLL(s)."
            )
            for library in libraries:
                notes.append(f"Elden Ring MO2: queued native DLL {library.name}.")
        mod_forced_loads = [
            mobase.ExecutableForcedLoadSetting(
                self.GameProcess, str(library)
            ).withEnabled(True).withForced(True)
            for library in libraries
        ]
        if root_layout_libraries:
            notes.append(
                "Elden Ring MO2: requesting MO2 forced loads for "
                f"{len(root_layout_libraries)} active root-layout DLL(s)."
            )
            for mod_name, virtual_name, library, preserved in root_layout_libraries:
                if preserved:
                    kind = "preserved root DLL"
                elif Path(virtual_name).name.casefold() in (
                    EldenRingModDataChecker._root_loader_dlls
                ):
                    kind = "root loader DLL"
                else:
                    kind = "standard root DLL"
                notes.append(
                    f"Elden Ring MO2: queued {kind} {virtual_name} "
                    f"from active mod {mod_name}."
                )
        root_forced_loads = [
            mobase.ExecutableForcedLoadSetting(
                self.GameProcess, str(library)
            ).withEnabled(True).withForced(True)
            for _, _, library, _ in root_layout_libraries
        ]
        bridge = Path(__file__).with_name("eldenring_mo2_bridge.dll")
        bridge_load = None
        if bridge.is_file():
            if active_mods:
                notes.append(
                    "Elden Ring MO2: queued native and loose-asset bridge "
                    f"{bridge.name} for {len(active_mods)} active mod(s)."
                )
            else:
                notes.append(
                    "Elden Ring MO2: queued native bridge "
                    f"{bridge.name} for profile startup options."
                )
            bridge_load = mobase.ExecutableForcedLoadSetting(
                Path(self.GameBinary).name, str(bridge)
            ).withEnabled(True).withForced(True)
            notes.append(
                "Elden Ring MO2: placing the bridge before mod DLLs so its "
                "startup window hooks are installed as early as possible."
            )
        else:
            qCritical(
                "Elden Ring MO2: native and loose-asset bridge is missing; "
                "loose-file overrides and bridge diagnostics will not be available. "
                "MO2 can still load enabled native mod DLLs."
            )
        forced_loads = []
        if bridge_load is not None:
            forced_loads.append(bridge_load)
        # Load root-level proxy DLLs from their winning MO2 mod directory.
        # This avoids relying on the Windows loader finding the virtual root
        # file before MO2's VFS hooks are ready. Managed DLLs follow the proxy
        # so proxy loaders can discover their root-level companion files.
        forced_loads.extend(root_forced_loads)
        forced_loads.extend(mod_forced_loads)
        # Seamless Co-op from its active mod: loaded with the others, and
        # started by the bridge through its Mod Engine 2 entry point once
        # the game runs (see _seamless_initializer); launchers such as Mod
        # Engine 2 get no initializer, so there it stays inactive
        seamless = self._active_seamless_dll() if native_loading else None
        if seamless is not None and all(
            os.path.normcase(str(Path(load.library()))) != os.path.normcase(str(seamless))
            for load in forced_loads
        ):
            notes.append(f"Elden Ring MO2: queued Seamless Co-op ({seamless.name}) from its MO2 mod.")
            forced_loads.append(
                mobase.ExecutableForcedLoadSetting(self.GameProcess, str(seamless)).withEnabled(True).withForced(True)
            )
        arxan = self._me3_compat_load(forced_loads)
        if arxan is not None:
            # first: it hooks the entry point before anything else runs
            notes.append(f"Elden Ring MO2: queued the me3 compatibility DLL {self.Me3Compat}.")
            forced_loads.insert(0, arxan)
        self._log_forced_load_notes(notes)
        # in load order: MO2 hands them to usvfs so that they load as listed
        # (UsvfsConnector::updateForcedLibraries), the me3 compatibility DLL
        # first and the natives in the Native DLL Profile's order. Marked as
        # the game plugin's own, so Configure libraries can switch them off
        # but not edit or remove them.
        return forced_loads

    def _log_forced_load_notes(self, notes: list[str]) -> None:
        if notes == getattr(self, "_last_forced_load_notes", None):
            return
        self._last_forced_load_notes = list(notes)
        for note in notes:
            qInfo(note)

    def _runs_outside_vfs(self, app_path: str) -> bool:
        return (
            Path(app_path).name.casefold() != self.GameProcess
            and self._launch_method(app_path) == self.LaunchMethodSwap
        )

    @classmethod
    def file_loader_found(cls, executable: Path) -> bool | None:
        """Whether the bridge will find the game's file loader in this executable,
        by the rules of its own search; None when the file can't be read."""
        try:
            data = executable.read_bytes()
            header = struct.unpack_from("<I", data, 0x3C)[0]
            count = struct.unpack_from("<H", data, header + 6)[0]
            optional_size = struct.unpack_from("<H", data, header + 20)[0]
            sections = []
            for index in range(count):
                entry = header + 24 + optional_size + index * 40
                virtual_size, address, raw_size, raw = struct.unpack_from("<IIII", data, entry + 8)
                if struct.unpack_from("<I", data, entry + 36)[0] & 0x20000000:
                    sections.append((address, min(virtual_size or raw_size, raw_size), raw))
        except (OSError, struct.error):
            return None

        def file_offset(address: int) -> int | None:
            for start, size, raw in sections:
                if start <= address < start + size:
                    return raw + address - start
            return None

        def follow_jumps(address: int) -> int | None:
            for _jump in range(9):
                offset = file_offset(address)
                if offset is None:
                    return None
                if data[offset] != 0xE9:
                    return address
                address += 5 + int.from_bytes(data[offset + 1:offset + 5], "little", signed=True)
            return None

        found = None
        for start, size, raw in sections:
            for before, after in cls.ResolverCallSites:
                first = 5 + len(before)
                match = data.find(after, raw + first, raw + size)
                while match >= 0:
                    if data[match - 5] == 0xE8 and data[match - first:match - 5] == before:
                        target = follow_jumps(
                            start + match - raw
                            + int.from_bytes(data[match - 4:match], "little", signed=True)
                        )
                        if target is not None:
                            if found is not None and found != target:
                                return False
                            found = target
                    match = data.find(after, match + 1, raw + size)
        return found is not None

    @staticmethod
    def game_version_text(executable: Path) -> str:
        """The version as the game shows it (Elden Ring keeps it in the text
        "Gr v1.17.2#1131") with the file's own, or whichever of them is there."""
        try:
            data = executable.read_bytes()
        except OSError:
            return ""
        file_version = ""
        fixed_info = data.find(struct.pack("<I", 0xFEEF04BD))
        if fixed_info >= 0 and fixed_info + 16 <= len(data):
            high, low = struct.unpack_from("<II", data, fixed_info + 8)
            file_version = f"{high >> 16}.{high & 0xFFFF}.{low >> 16}.{low & 0xFFFF}"
        shown = re.search(rb"Gr v(\d+\.\d+\.\d+)#\d+\0dantelion", data)
        if shown is None:
            return file_version
        version = shown.group(1).decode().removesuffix(".0")
        return f"{version} ({file_version})" if file_version else version

    def _file_loader_ready(self, app_path: str) -> bool:
        """A game update can change the code the bridge looks for: said before
        the launch, and again only if the game's own run shows what this missed."""
        if self._runs_outside_vfs(app_path) or not self._active_mods_have_game_files():
            return True
        executable = Path(self.gameDirectory().absolutePath()) / self.GameBinary
        try:
            info = executable.stat()
        except OSError:
            return True
        stamp = f"{info.st_size}:{info.st_mtime_ns}"
        if self._file_loader_scan[0] != stamp:
            self._file_loader_scan = (stamp, self.file_loader_found(executable))
        if self._file_loader_scan[1] is not False:
            return True
        version = self.game_version_text(executable)
        qWarning(f"Elden Ring MO2: the game's file loader wasn't found in '{executable}' ({version}).")
        self._loose_files_warning_shown = True
        key = "file_loader_accepted"
        try:
            if self._organizer.persistent(self.name(), key, "") == stamp:
                return True
        except (AttributeError, RuntimeError):
            pass
        box = QMessageBox(self._message_parent())
        box.setIcon(QMessageBox.Icon.Warning)
        box.setWindowTitle(_tr("Mods may not load"))
        box.setText(self._game_text(_tr("MO2 doesn't recognize this version of Elden Ring")))
        details = _tr("The game files of your mods won't be used, though their DLLs aren't affected. "
                      "This usually follows a game update: look for a new version of MO2 Revamped.")
        if version:
            details += "\n\n" + _tr("Game version: {0}").format(version)
        box.setInformativeText(details)
        remember = QCheckBox(_tr("Don't warn again for this version of the game"))
        box.setCheckBox(remember)
        start = box.addButton(_tr("Start anyway"), QMessageBox.ButtonRole.AcceptRole)
        box.addButton(_tr("Cancel"), QMessageBox.ButtonRole.RejectRole)
        box.setDefaultButton(start)
        box.exec()
        # a child of MO2's window: freed once MO2's event loop runs again,
        # after the choice below is read
        box.deleteLater()
        if box.clickedButton() is not start:
            return False
        if remember.isChecked():
            try:
                self._organizer.setPersistent(self.name(), key, stamp)
            except (AttributeError, RuntimeError) as error:
                qWarning(f"Elden Ring MO2: could not save the file loader choice: {error}")
        return True

    def _regulation_conflict_ready(self, app_path: str) -> bool:
        """Of several active regulation.bin the game gets only the one with the
        highest priority, and nothing else tells the user."""
        if self._runs_outside_vfs(app_path):
            # no mod's regulation.bin reaches it
            return True
        mods = [
            name
            for name, root, active in self._mod_folders()
            if active and (root / "regulation.bin").is_file()
        ]
        if len(mods) < 2:
            return True
        combination = "|".join(sorted(name.casefold() for name in mods))
        profile = self._organizer.profile()
        key = f"regulation_accepted_{profile.name() if profile is not None else ''}"
        try:
            if self._organizer.persistent(self.name(), key, "") == combination:
                return True
        except (AttributeError, RuntimeError):
            pass
        qInfo(f"Elden Ring MO2: several active mods have regulation.bin: {', '.join(mods)}.")
        box = QMessageBox(self._message_parent())
        box.setIcon(QMessageBox.Icon.Warning)
        box.setWindowTitle("regulation.bin")
        box.setText(_tr("Several active mods bring their own regulation.bin"))
        box.setInformativeText(
            _tr("The game uses only one: {0}, the one with the highest priority. The rule "
                "changes of the others ({1}) won't apply, and they may not work properly. "
                "To use another one, change its priority in the mod list or turn off the "
                "mods you don't need.").format(mods[-1], ", ".join(mods[:-1]))
        )
        remember = QCheckBox(_tr("Don't warn again for these mods"))
        box.setCheckBox(remember)
        start = box.addButton(_tr("Start anyway"), QMessageBox.ButtonRole.AcceptRole)
        box.addButton(_tr("Cancel"), QMessageBox.ButtonRole.RejectRole)
        box.setDefaultButton(start)
        box.exec()
        # a child of MO2's window: freed once MO2's event loop runs again,
        # after the choice below is read
        box.deleteLater()
        if box.clickedButton() is not start:
            return False
        if remember.isChecked():
            try:
                self._organizer.setPersistent(self.name(), key, combination)
            except (AttributeError, RuntimeError) as error:
                qWarning(f"Elden Ring MO2: could not save the regulation.bin choice: {error}")
        return True

    @staticmethod
    def save_extension_literal_found(executable: Path) -> bool | None:
        """Whether apply_save_extension_setting in the bridge will find the one
        .sl2 literal it replaces, by the rules of its own search: constant data
        only, at even offsets. None when the file can't be read."""
        pattern = ".sl2".encode("utf-16-le")
        try:
            data = executable.read_bytes()
            header = struct.unpack_from("<I", data, 0x3C)[0]
            count = struct.unpack_from("<H", data, header + 6)[0]
            optional_size = struct.unpack_from("<H", data, header + 20)[0]
            matches = 0
            for index in range(count):
                entry = header + 24 + optional_size + index * 40
                virtual_size, _address, raw_size, raw = struct.unpack_from("<IIII", data, entry + 8)
                flags = struct.unpack_from("<I", data, entry + 36)[0]
                # not executable or writable, initialized data
                if flags & 0xA0000000 or not flags & 0x00000040:
                    continue
                end = raw + min(virtual_size or raw_size, raw_size)
                match = data.find(pattern, raw, end)
                while match >= 0:
                    matches += (match - raw) % 2 == 0
                    match = data.find(pattern, match + 2, end)
        except (OSError, struct.error):
            return None
        return matches == 1

    def _steam_cloud_swap_ready(self, app_path: str) -> bool:
        """A program started outside the VFS plays the route's ER0000.sl2 in the
        Steam folder, and Steam Cloud can upload it as it closes. Reforged online
        plays its own file, and Steam's stays in place."""
        if (
            not self._runs_outside_vfs(app_path)
            or Path(app_path).name.casefold() == self.ReforgedOnlineLauncher
        ):
            return True
        key = "steam_cloud_swap_accepted"
        try:
            if self._organizer.persistent(self.name(), key, False):
                return True
        except (AttributeError, RuntimeError):
            pass
        box = QMessageBox(self._message_parent())
        box.setIcon(QMessageBox.Icon.Warning)
        box.setWindowTitle(_tr("Steam Cloud"))
        box.setText(_tr("This program can send this profile's save to Steam Cloud"))
        box.setInformativeText(
            _tr("It starts outside MO2's virtual files, with this profile's saves in the Steam "
                "folder. If Steam Cloud is on for this game, Steam may upload them when the game "
                "closes, in place of your own Steam save. Your files stay on this PC.")
        )
        remember = QCheckBox(_tr("Don't warn again"))
        box.setCheckBox(remember)
        start = box.addButton(_tr("Start anyway"), QMessageBox.ButtonRole.AcceptRole)
        box.setDefaultButton(box.addButton(_tr("Cancel"), QMessageBox.ButtonRole.RejectRole))
        box.exec()
        # a child of MO2's window: freed once MO2's event loop runs again,
        # after the choice below is read
        box.deleteLater()
        if box.clickedButton() is not start:
            return False
        if remember.isChecked():
            try:
                self._organizer.setPersistent(self.name(), key, True)
            except (AttributeError, RuntimeError) as error:
                qWarning(f"Elden Ring MO2: could not remember the Steam Cloud choice: {error}")
        return True

    def _save_extension_ready(self, app_path: str) -> bool:
        """Without its own save extension ELDEN RING Reforged uses the save of
        the other mods: said before the launch, as for the file loader."""
        if (
            self._runs_outside_vfs(app_path)
            or self._reforged_natives() is None
            # Seamless Co-op names the saves itself
            or self._active_seamless_dll() is not None
        ):
            return True
        executable = Path(self.gameDirectory().absolutePath()) / self.GameBinary
        try:
            info = executable.stat()
        except OSError:
            return True
        stamp = f"{info.st_size}:{info.st_mtime_ns}"
        if self._save_extension_scan[0] != stamp:
            self._save_extension_scan = (stamp, self.save_extension_literal_found(executable))
        if self._save_extension_scan[1] is not False:
            return True
        version = self.game_version_text(executable)
        qWarning(f"Elden Ring MO2: the game's save extension wasn't found in '{executable}' ({version}).")
        key = "save_extension_accepted"
        try:
            if self._organizer.persistent(self.name(), key, "") == stamp:
                return True
        except (AttributeError, RuntimeError):
            pass
        box = QMessageBox(self._message_parent())
        box.setIcon(QMessageBox.Icon.Warning)
        box.setWindowTitle(_tr("Saves may mix"))
        box.setText(_tr("MO2 can't give ELDEN RING Reforged its own save file in this version of the game"))
        details = _tr("Reforged would use ER0000.sl2, the save of your other mods, instead of "
                      "ER0000.err, and could overwrite it. This usually follows a game update: "
                      "look for a new version of MO2 Revamped.")
        if version:
            details += "\n\n" + _tr("Game version: {0}").format(version)
        box.setInformativeText(details)
        remember = QCheckBox(_tr("Don't warn again for this version of the game"))
        box.setCheckBox(remember)
        start = box.addButton(_tr("Start anyway"), QMessageBox.ButtonRole.AcceptRole)
        box.setDefaultButton(box.addButton(_tr("Cancel"), QMessageBox.ButtonRole.RejectRole))
        box.exec()
        # a child of MO2's window: freed once MO2's event loop runs again,
        # after the choice below is read
        box.deleteLater()
        if box.clickedButton() is not start:
            return False
        if remember.isChecked():
            try:
                self._organizer.setPersistent(self.name(), key, stamp)
            except (AttributeError, RuntimeError) as error:
                qWarning(f"Elden Ring MO2: could not save the save extension choice: {error}")
        return True

    def _seamless_password_ready(self, app_path: str) -> bool:
        """Seamless Co-op quits the game at start without a session password
        in its settings file: MO2 says so before the launch instead."""
        if self._runs_outside_vfs(app_path):
            # started outside the VFS, no mod's DLL joins it
            return True
        seamless = self._active_seamless_dll()
        if seamless is None:
            return True
        settings = seamless.with_name(f"{seamless.stem}_settings.ini")
        if not settings.is_file():
            return True
        try:
            config = configparser.ConfigParser(interpolation=None, strict=False)
            config.read(settings, encoding="utf-8-sig")
            password = config.get("PASSWORD", "cooppassword", fallback="").strip()
        except (OSError, UnicodeError, configparser.Error):
            return True
        if password:
            return True
        qWarning(f"Elden Ring MO2: Seamless Co-op has no session password in '{settings}'; launch stopped.")
        box = QMessageBox(self._message_parent())
        box.setIcon(QMessageBox.Icon.Warning)
        box.setWindowTitle(_tr("Seamless Co-op"))
        box.setText(_tr("Seamless Co-op needs a session password"))
        box.setInformativeText(
            _tr("Set cooppassword in its settings file; everyone who plays together uses the "
                "same one. Without it Seamless Co-op closes the game as it starts, so the "
                "game was not started.\n\n{0}").format(settings)
        )
        open_file = box.addButton(_tr("Open the settings file"), QMessageBox.ButtonRole.AcceptRole)
        box.addButton(_tr("Cancel"), QMessageBox.ButtonRole.RejectRole)
        box.setDefaultButton(open_file)
        box.exec()
        # a child of MO2's window: freed once MO2's event loop runs again,
        # after the choice below is read
        box.deleteLater()
        if box.clickedButton() is open_file:
            QDesktopServices.openUrl(QUrl.fromLocalFile(str(settings)))
        return False

    def _active_seamless_dll(self) -> Path | None:
        """Seamless Co-op's DLL from the highest-priority active mod, unless
        the native profile switches it off (as it can any other DLL)."""
        found = self.seamless_dll_in_mods([mod for mod in self._mod_folders() if mod[2]])
        if found is None:
            return None
        mod_name, dll = found
        profile = self._organizer.profile()
        native_profile = self._resolve_revamped_native_profile(profile) if profile is not None else None
        if (
            native_profile is not None
            and not native_profile.get("error")
            and (mod_name.casefold(), dll.name.casefold()) in native_profile.get("disabled_libraries", set())
        ):
            # asked several times per launch and on every refresh
            self._log_when_changed("seamless", [(
                _module_qinfo,
                f"Elden Ring MO2: not loading '{dll.name}' from '{mod_name}'; "
                "it is switched off in the native profile.",
            )])
            return None
        self._log_when_changed("seamless", [])
        return dll

    def _seamless_initializer(self) -> list[dict]:
        """The bridge entry that starts Seamless Co-op's DLL: it exports Mod
        Engine 2's modengine_ext_init, which the bridge calls once the game
        runs its own code, as Mod Engine 2 and me3 do."""
        if not bool(self._organizer.pluginSetting(self.name(), self.NativeDllSetting)):
            return []
        seamless = self._active_seamless_dll()
        if seamless is None:
            return []
        return [{"library": seamless, "initializer": "modengine_ext_init"}]

    @staticmethod
    def _is_game_runtime_dll(name: str) -> bool:
        # libraries the game itself imports: a copy in a mod is never
        # force-loaded ahead of the game's own
        name = name.casefold()
        return (
            name == "steam_api64.dll"
            or (name.startswith("oo2core_") and name.endswith("_win64.dll"))
            # a mod loader's own host and the tools' libraries, shipped by
            # releases that bundle me3 (ELDEN RING Reforged): never a mod
            or name in {"me3_mod_host.dll", "libzstd.dll"}
        )

    def _active_root_layout_dlls(
        self, active_mods: list[str]
    ) -> list[tuple[str, str, Path, bool]]:
        """Return winning root DLLs from every active mod layout.

        Root-level DLLs keep their archive paths in either install mode,
        including proxy loaders with unfamiliar basenames. Only files whose
        winning origin is an enabled mod are queued, so lower-priority copies
        cannot override MO2's file conflict order.
        """
        if not active_mods:
            return []

        organizer = self._organizer
        mod_list = organizer.modList()
        active_priority = {name: index for index, name in enumerate(active_mods)}
        root_loader_names = EldenRingModDataChecker._root_loader_dlls
        selected: list[tuple[int, int, str, str, str, Path, bool]] = []
        seen_virtual: set[str] = set()
        seen_physical: set[str] = set()

        for mod_name in active_mods:
            mod = mod_list.getMod(mod_name)
            if mod is None:
                continue

            try:
                preserve_layout = bool(
                    str(
                        mod.pluginSettings(self.ArchiveLayoutInstallerName).get(
                            self.PreserveArchiveLayoutSetting, False
                        )
                    ).strip().casefold()
                    not in {"", "false", "0"}
                )
                file_tree = mod.fileTree()
            except Exception as error:
                qCritical(
                    "Elden Ring MO2: could not inspect root DLL layout for "
                    f"active mod {mod_name}: {error}"
                )
                continue

            for entry in file_tree:
                if is_directory(entry):
                    continue
                file_name = entry.name()
                relative_path = Path(file_name)
                if (
                    len(relative_path.parts) != 1
                    or relative_path.suffix.casefold() != ".dll"
                    or self._is_game_runtime_dll(file_name)
                ):
                    continue

                is_root_loader = file_name.casefold() in root_loader_names
                virtual_name = relative_path.as_posix()
                virtual_key = virtual_name.casefold()
                if virtual_key in seen_virtual:
                    continue

                try:
                    origins = organizer.getFileOrigins(virtual_name)
                except Exception as error:
                    qCritical(
                        "Elden Ring MO2: could not resolve the winning mod for "
                        f"root DLL {virtual_name}: {error}"
                    )
                    continue

                winner = next(
                    (origin for origin in origins if origin in active_priority), None
                )
                if winner != mod_name:
                    continue

                library = Path(mod.absolutePath()).joinpath(file_name)
                try:
                    library = library.resolve(strict=True)
                except OSError as error:
                    qCritical(
                        f"Elden Ring MO2: cannot access root DLL {virtual_name} "
                        f"from active mod {mod_name}: {error}"
                    )
                    continue

                if not library.is_file():
                    continue

                physical_key = os.path.normcase(str(library))
                if physical_key in seen_physical:
                    continue
                seen_virtual.add(virtual_key)
                seen_physical.add(physical_key)
                selected.append(
                    (
                        0 if is_root_loader else 1,
                        active_priority[mod_name],
                        virtual_key,
                        mod_name,
                        virtual_name,
                        library,
                        preserve_layout,
                    )
                )

        selected.sort(key=lambda item: item[:3])
        return [
            (mod_name, virtual_name, library, preserve_layout)
            for _, _, _, mod_name, virtual_name, library, preserve_layout in selected
        ]

    def _active_native_dlls(self) -> list[Path]:
        """Return winning DLLs in managed directories from active profile mods.

        MO2's virtual file list decides which mod wins a file conflict. The
        physical DLL must come from that winning mod because forced-load
        libraries are resolved before the game process can read its VFS. The
        ``MO2_DLLs`` and ``mods`` paths remain supported for older installs.
        """
        organizer = self._organizer
        mod_list = organizer.modList()
        profile_order = list(mod_list.allModsByProfilePriority())
        active_order = [
            name
            for name in profile_order
            if mod_list.state(name) & mobase.ModState.ACTIVE
        ]
        active_priority = {name: index for index, name in enumerate(active_order)}
        if not active_priority:
            return []

        data_root = Path(self.dataDirectory().absolutePath()).absolute()
        selected: list[tuple[int, str, Path]] = []
        seen_virtual: set[str] = set()
        seen_physical: set[str] = set()

        dll_directories = (
            EldenRingModDataChecker._dll_directory,
            "native",
            "natives",
            "external_dlls",
            "MO2_DLLs",
            "mods",
        )
        dll_roots = {directory.casefold() for directory in dll_directories}
        virtual_files: list[tuple[str, str]] = []
        for directory in dll_directories:
            try:
                virtual_files.extend(
                    (directory, path)
                    for path in organizer.findFiles(directory, "*")
                )
            except Exception as error:
                qCritical(
                    "Elden Ring MO2: could not enumerate DLLs under "
                    f"Game/{directory}/: {error}"
                )

        for search_directory, virtual_file in virtual_files:
            virtual_path = Path(virtual_file)
            if (
                virtual_path.suffix.casefold() != ".dll"
                or virtual_path.name.casefold()
                in EldenRingModDataChecker._root_loader_dlls
                or self._is_game_runtime_dll(virtual_path.name)
            ):
                continue

            try:
                relative_path = virtual_path.absolute().relative_to(data_root)
            except ValueError:
                # MO2 may return a resolved physical path. Preserve any
                # subdirectory below the searched managed root so support
                # DLLs can be recognized and excluded from forced loading.
                if virtual_path.is_absolute():
                    # the last match: the physical path starts with MO2's
                    # own "mods" folder (or a mod named like a DLL folder)
                    path_parts = virtual_path.parts
                    root_index = next(
                        (
                            index
                            for index in range(len(path_parts) - 2, -1, -1)
                            if path_parts[index].casefold() == search_directory.casefold()
                        ),
                        None,
                    )
                    if root_index is None:
                        relative_path = Path(search_directory) / virtual_path.name
                    else:
                        relative_path = Path(*path_parts[root_index:])
                elif (
                    virtual_path.parts
                    and virtual_path.parts[0].casefold() in dll_roots
                ):
                    relative_path = virtual_path
                else:
                    relative_path = Path(search_directory) / virtual_path

            if (
                len(relative_path.parts) < 2
                or relative_path.parts[0].casefold()
                not in dll_roots
                or relative_path.suffix.casefold() != ".dll"
            ):
                continue

            virtual_name = relative_path.as_posix()
            virtual_key = virtual_name.casefold()
            if virtual_key in seen_virtual:
                continue
            seen_virtual.add(virtual_key)

            try:
                origins = organizer.getFileOrigins(virtual_name)
            except Exception as error:
                qCritical(
                    "Elden Ring MO2: could not resolve the winning mod for "
                    f"{virtual_name}: {error}"
                )
                continue

            if not origins:
                continue

            winner = next(
                (origin for origin in origins if origin in active_priority), None
            )
            if winner is None:
                # Ignore unmanaged game files and files with no active origin.
                continue

            mod = mod_list.getMod(winner)
            if mod is None:
                continue

            library = Path(mod.absolutePath()).joinpath(*relative_path.parts)
            try:
                library = library.resolve(strict=True)
            except OSError as error:
                qCritical(
                    f"Elden Ring MO2: cannot access {virtual_name} "
                    f"from active mod {winner}: {error}"
                )
                continue

            if not library.is_file():
                continue

            physical_key = os.path.normcase(str(library))
            if physical_key in seen_physical:
                continue
            seen_physical.add(physical_key)
            selected.append(
                (active_priority[winner], virtual_key, library)
            )

        selected.sort(key=lambda item: (item[0], item[1]))
        return [library for _, _, library in selected]


class NightreignLocalSavegames(EldenRingLocalSavegames):
    """Nightreign routes its saves the same way, in its own shared folder."""

    SharedSavesFolderName = "Nightreign Shared Saves"


class EldenRingNightreignMo2Game(EldenRingMo2Game):
    """ELDEN RING NIGHTREIGN: the Elden Ring support with the game's own names.

    The saves are %APPDATA%/Nightreign/<SteamID64>/NR0000.sl2 (NR0000.co2
    with Seamless Co-op), the game is Game/nightreign.exe."""

    Name = "Elden Ring Nightreign MO2 Support"
    GameName = "ELDEN RING NIGHTREIGN"
    GameShortName = "nightreign"
    GameNexusName = "eldenringnightreign"
    # Nexus download links name it this way
    GameValidShortNames = ["eldenringnightreign"]
    GameSteamId = 2622380
    GameBinary = "Game/nightreign.exe"

    GameProcess = "nightreign.exe"
    SaveFolderName = "Nightreign"
    SavePrefix = "nr0000."
    SteamCloudSaves = ("NR0000.sl2", "NR0000.sl2.bak")
    GlobalBackupsFolderName = "Nightreign Global Save Backups"
    LocalSavegamesClass = NightreignLocalSavegames
    RecoveryScope = "|nightreign"
    DisplayName = "Nightreign"
    SeamlessDll = "nrsc.dll"
    GameLaunchers = frozenset(
        {
            "nightreign.exe",
            # Easy Anti-Cheat's launcher, the program Steam starts
            "start_protected_game.exe",
        }
    )
    # Reforged is an Elden Ring mod: Nightreign has no launcher to start
    # outside the VFS
    AutoSwapLaunchers = frozenset()
