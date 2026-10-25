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
from PyQt6.QtWidgets import (
    QAbstractButton,
    QApplication,
    QBoxLayout,
    QComboBox,
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
        {_dll_directory.casefold(), *_legacy_dll_directories, "plugins"}
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
        "facegen",
        "map",
        "material",
        "menu",
        "msg",
        "movie",
        "movie_dlc",
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

    def fix(self, filetree: mobase.IFileTree) -> mobase.IFileTree:
        if self._preserve_archive_layout_requested():
            return filetree
        return self.normalize_standard_layout(filetree)

    def normalize_standard_layout(
        self,
        filetree: mobase.IFileTree,
        recognize_mod_engine_paths: bool = True,
    ) -> mobase.IFileTree:
        """Apply the standard DLL/assets layout directly to an archive tree."""
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
        game_folders = {"<game>", "game", "elden ring", "elden-ring", "eldenring", "elden_ring"}
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
            | {"config", "configs", "dlls", "fonts", "native", "natives", "plugins"}
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

        self._merge_package_folder(filetree, mod_folder)

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
            candidates = [
                entry
                for entry in filetree
                if is_directory(entry) and self._is_package_wrapper(entry)
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

        def collect(tree: mobase.IFileTree, parent_path: str = "") -> None:
            for entry in tree:
                relative_path = (
                    f"{parent_path}/{entry.name()}" if parent_path else entry.name()
                )
                if is_directory(entry):
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

        target_directories: dict[int, str] = {}
        for group in dll_groups.values():
            if len(group) == 1:
                entry, parent_path = group[0]
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
                        "a native DLL. Choose Keep original structure to preserve "
                        "both folders without merging them.")
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
        self._fix_basic_patterns(filetree)
        for entry in list(filetree):
            if is_directory(entry):
                self._fix_known_layouts(entry, depth + 1)

    def _fix_basic_patterns(self, filetree: mobase.IFileTree) -> mobase.IFileTree:
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
                assert is_directory(entry)
                if entry.detach():
                    filetree.merge(entry)
            elif patterns.valid.match(name):
                continue
            elif patterns.delete.match(name):
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

        if name in self._content_directories:
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
    Version = "0.5.0-alpha.75"
    NativeBridgeVersion = "0.5.0-alpha.35"

    GameName = "ELDEN RING"
    GameShortName = "eldenring"
    GameNexusName = "eldenring"
    GameSteamId = 1245620
    GameBinary = "Game/eldenring.exe"
    GameDataPath = "Game"
    GameSaveExtension = "sl2"
    # the names that differ between Elden Ring and Nightreign
    GameProcess = "eldenring.exe"
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
    # Seamless Co-op for this game: its launchers and the DLL they inject; me3
    # names the game so (a Seamless mod that only lives in MO2 runs through me3)
    SeamlessLaunchers = frozenset({"ersc_launcher.exe", "launch_elden_ring_seamlesscoop.exe"})
    SeamlessDll = "ersc.dll"
    Me3GameId = "eldenring"
    # Programs that start eldenring.exe themselves (Seamless Co-op, ModEngine2,
    # me3) get the same save guard as the game, and the guard waits for the
    # game they start instead of for the launcher.
    GameLaunchers = frozenset(
        {
            "eldenring.exe",
            "start_protected_game.exe",
            "ersc_launcher.exe",
            "modengine2_launcher.exe",
            "me3.exe",
            "me3-launcher.exe",
            # the launcher of older Seamless Co-op releases
            "launch_elden_ring_seamlesscoop.exe",
        }
    )
    # Mod Engine 2 starts the game from launchmod_eldenring.bat (Convergence
    # ships its own launchmod_*.bat); MO2 may run it through a script host
    # me3 starts Nightreign too: while that game runs, a me3 process is its
    # launcher; Easy Anti-Cheat's launcher has the same name in every game, so
    # only the one in this game's folder counts
    SharedLaunchers = frozenset({"me3.exe", "me3-launcher.exe"})
    # Launchers that fail under MO2's virtual file system (Seamless Co-op's
    # mod stays inactive, me3 crashes the game): MO2 starts them itself,
    # outside the VFS, and swaps the route's saves into the Steam folder for
    # the run. Each executable can be switched either way in Save Isolation.
    AutoSwapLaunchers = frozenset(
        {
            "ersc_launcher.exe",
            "launch_elden_ring_seamlesscoop.exe",
            "nrsc_launcher.exe",
            "me3.exe",
            "me3-launcher.exe",
        }
    )
    LaunchMethodsFilename = "eldenring_mo2_launch_methods.json"
    LaunchMethodVfs = "vfs"
    LaunchMethodSwap = "swap"
    # in a swap session's recovery folder: the Steam folder's own loose
    # files (GraphicsConfig.xml) while the route's copies stand in for them
    SwapRootFilesDirectoryName = "steam-root-files"
    AntiCheatLauncher = "start_protected_game.exe"
    FromSoftwareGameProcesses = frozenset({"eldenring.exe", "nightreign.exe"})
    LauncherScriptPrefix = "launchmod"
    ScriptHosts = frozenset({"cmd.exe", "powershell.exe", "pwsh.exe"})
    LauncherStartGrace = 90  # seconds a launcher may take to start the game
    LauncherWaitLimit = 600  # a launcher still open after this is not waited for
    GameWatchInterval = 5000  # milliseconds between checks for the game process
    # while a launch waits for the game to appear: a game that crashes at
    # start lives about two seconds, and once seen its close restores the
    # saves without the launcher grace
    LaunchWatchInterval = 1000

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
        return not children or any(
            child.name != self.UnroutedSaveDirectoryName for child in children
        )

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
                    if not session.is_dir() or not any(
                        child.is_dir() and child.name != self.UnroutedSaveDirectoryName
                        for child in session.iterdir()
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
        account folders. Only that install restores them; a launch from here
        would play against a Steam folder that is not complete."""
        return [session for session, owner_exists in self._other_install_sessions() if owner_exists]

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
            if entry_name.endswith((".sl2", ".sl2.bak", ".co2", ".co2.bak")):
                return True
        return False

    @classmethod
    def _is_launcher_script(cls, name: str) -> bool:
        name = name.casefold()
        return name.startswith(cls.LauncherScriptPrefix) and name.endswith((".bat", ".cmd"))

    @classmethod
    def _is_game_launch(cls, app_path: str, args: str = "") -> bool:
        # exact names only: a save tool or another program with a similar
        # name must not hide the Steam saves
        name = Path(app_path).name.casefold()
        if name in cls.GameLaunchers or cls._is_launcher_script(name):
            return True
        if name in cls.ScriptHosts:
            return any(
                cls._is_launcher_script(Path(part.strip("\"'")).name)
                or Path(part.strip("\"'")).name.casefold() in cls.GameLaunchers
                for part in args.split()
            )
        return False

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
    def launch_method_for(cls, binary: str, methods: dict[str, str]) -> tuple[str, bool]:
        """(method, chosen by hand) for one executable."""
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
        self, session_directory: Path, manifest: dict, global_save_root: Path, route: Path
    ) -> None:
        """Puts the route's saves where the game looks without the VFS: its
        account folders into the (now empty) Steam folder, and its loose files
        (GraphicsConfig.xml) in place of the Steam ones, which wait in the
        session. The manifest records each step first, so recovery after a
        crash can take everything back."""
        swap = {"route": str(route), "directories": [], "steam_root_files": [], "route_root_files": []}
        manifest["swap"] = swap
        self._write_save_quarantine_manifest(session_directory, manifest)
        held = session_directory / self.SwapRootFilesDirectoryName
        for child in list(global_save_root.iterdir()):
            if child.is_file():
                held.mkdir(exist_ok=True)
                os.replace(child, held / child.name)
                swap["steam_root_files"].append(child.name)
                self._write_save_quarantine_manifest(session_directory, manifest)
        if not route.is_dir():
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
                shutil.copytree(child, global_save_root / child.name)
                swap["directories"].append(child.name)
            self._write_save_quarantine_manifest(session_directory, manifest)
        qInfo(
            "Elden Ring MO2: swapped the route's saves into the Steam folder for "
            f"a launch outside the VFS: {len(swap['directories'])} account folder(s), "
            f"{len(swap['route_root_files'])} loose file(s)."
        )

    def _swap_route_saves_out(
        self, session_directory: Path, swap: dict, global_save_root: Path
    ) -> None:
        """Takes what the game wrote in the Steam folder back to the route (the
        route was backed up before the launch), then puts the Steam folder's
        own loose files back."""
        route = Path(str(swap.get("route", "")))
        if not route.is_absolute():
            raise OSError(_tr("The save-recovery record has no valid route for the swapped saves."))
        route.mkdir(parents=True, exist_ok=True)
        if global_save_root.is_dir():
            for child in list(global_save_root.iterdir()):
                if child.is_dir() and self._save_directory_contains_elden_ring_saves(child):
                    target = route / child.name
                    target.mkdir(parents=True, exist_ok=True)
                    for entry in child.rglob("*"):
                        destination = target / entry.relative_to(child)
                        if entry.is_dir():
                            destination.mkdir(parents=True, exist_ok=True)
                        else:
                            destination.parent.mkdir(parents=True, exist_ok=True)
                            shutil.copy2(entry, destination)
                    shutil.rmtree(child)
                elif child.is_file():
                    shutil.copy2(child, route / child.name)
                    child.unlink()
        tidy = swap.get("seamless_game_folder")
        if isinstance(tidy, dict):
            try:
                self.tidy_seamless_game_folder(tidy)
            except OSError as error:
                qWarning(f"Elden Ring MO2: the Seamless folder in the game folder was not tidied: {error}")
        held = session_directory / self.SwapRootFilesDirectoryName
        if held.is_dir():
            global_save_root.mkdir(parents=True, exist_ok=True)
            for entry in list(held.iterdir()):
                os.replace(entry, global_save_root / entry.name)
            held.rmdir()
        qInfo(
            "Elden Ring MO2: the saves of the launch outside the VFS are back in "
            f"the route '{route}'."
        )

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
        """The Seamless Co-op DLL of a mod (name, folder, active), highest
        priority first: an active mod wins, an inactive one will do (the mod
        needs no VFS when me3 loads it)."""
        for want_active in (True, False):
            for name, folder, active in reversed(mods):
                if active != want_active:
                    continue
                dll = folder / "SeamlessCoop" / cls.SeamlessDll
                if dll.is_file():
                    return name, dll
        return None

    @staticmethod
    def find_me3(candidates: list[Path]) -> Path | None:
        for candidate in candidates:
            try:
                if candidate.name.casefold() == "me3.exe" and candidate.is_file():
                    return candidate
            except OSError:
                continue
        return None

    @classmethod
    def write_seamless_me3_profile(cls, folder: Path, dll: Path) -> Path:
        """A me3 profile that loads Seamless Co-op from its MO2 mod folder."""
        folder.mkdir(parents=True, exist_ok=True)
        profile = folder / "seamless-coop.me3"
        profile.write_text(
            "# Written by MO2 Revamped for Seamless Co-op from its MO2 mod.\n"
            'profileVersion = "v1"\n\n'
            "[[supports]]\n"
            f"game = {json.dumps(cls.Me3GameId)}\n\n"
            "[[natives]]\n"
            f"path = {json.dumps(str(dll))}\n",
            encoding="utf-8",
        )
        return profile

    def _me3_candidates(self) -> list[Path]:
        candidates: list[Path] = []
        try:
            ini = Path(self._organizer.basePath()) / "ModOrganizer.ini"
            config = configparser.ConfigParser(interpolation=None, strict=False)
            config.read(ini, encoding="utf-8")
            if config.has_section("customExecutables"):
                for key, value in config.items("customExecutables"):
                    if key.endswith("\\binary") and value.strip():
                        candidates.append(Path(value.strip()))
        except (AttributeError, RuntimeError, OSError, configparser.Error, UnicodeError):
            pass
        found = shutil.which("me3")
        if found:
            candidates.append(Path(found))
        for root, parts in (
            ("LOCALAPPDATA", ("Programs", "me3", "bin")),
            ("ProgramFiles", ("me3", "bin")),
            ("ProgramFiles", ("garyttierney", "me3", "bin")),
        ):
            base = os.environ.get(root)
            if base:
                candidates.append(Path(base, *parts, "me3.exe"))
        return candidates

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

    def _launch_target(self, app_path: str, working_directory, args: str):
        """What a launch outside the VFS starts: the program itself, or, for
        Seamless Co-op that is not installed in the game folder, me3 loading
        it from its MO2 mod. None when it cannot start (the user was told)."""
        name = Path(app_path).name.casefold()
        if name not in self.SeamlessLaunchers:
            return app_path, working_directory, args
        game_folder = Path(self.dataDirectory().absolutePath())
        installed = (game_folder / "SeamlessCoop" / self.SeamlessDll).is_file() and (
            game_folder / name
        ).is_file()
        if installed and os.path.normcase(str(Path(app_path).parent)) == os.path.normcase(str(game_folder)):
            return app_path, working_directory, args
        seamless = self.seamless_dll_in_mods(self._mod_folders())
        me3 = self.find_me3(self._me3_candidates())
        if seamless is None or me3 is None:
            missing = (
                _tr("No MO2 mod holds SeamlessCoop/{0}.").format(self.SeamlessDll)
                if seamless is None
                else _tr("me3 was not found (add it as an executable of this instance).")
            )
            QMessageBox.warning(
                None,
                _tr("Seamless Co-op cannot start"),
                _tr("Seamless Co-op is not installed in the game folder, so MO2 starts it "
                "through me3 from its mod. {0} The game was not started.").format(missing),
            )
            qWarning(f"Elden Ring MO2: Seamless Co-op cannot start through me3: {missing}")
            return None
        profile = self.write_seamless_me3_profile(
            Path(self._organizer.basePath()) / "eldenring_mo2_me3", seamless[1]
        )
        # Seamless still writes its crash dumps beside the game
        # (Game/SeamlessCoop/crashdumps): the session remembers to tidy that up
        self._record_seamless_tidy(
            game_folder / "SeamlessCoop", seamless[1].parent / "crashdumps"
        )
        qInfo(
            f"Elden Ring MO2: Seamless Co-op starts through me3 ('{me3}') from the mod "
            f"'{seamless[0]}'; the game folder is left alone."
        )
        return str(me3), str(me3.parent), f'launch -p "{profile}"'

    def _record_seamless_tidy(self, folder: Path, dumps_to: Path) -> None:
        session = self._active_save_quarantine
        if session is None:
            return
        manifest_path = session / self.SaveRecoveryManifestName
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            swap = manifest.get("swap")
            if not isinstance(swap, dict):
                return
            swap["seamless_game_folder"] = {
                "folder": str(folder),
                "existed": folder.exists(),
                "dumps_to": str(dumps_to),
            }
            self._write_save_quarantine_manifest(session, manifest)
        except (OSError, UnicodeError, ValueError, AttributeError) as error:
            qWarning(f"Elden Ring MO2: could not note the Seamless folder to tidy: {error}")

    @staticmethod
    def tidy_seamless_game_folder(record: dict) -> None:
        """After a Seamless run through me3: its crash dumps move to the mod,
        and the SeamlessCoop folder it made in the game folder goes away (only
        when it did not exist before and holds no other file)."""
        folder = Path(str(record.get("folder", "")))
        dumps_to = Path(str(record.get("dumps_to", "")))
        if not folder.is_absolute() or not folder.is_dir() or folder.name != "SeamlessCoop":
            return
        dumps = folder / "crashdumps"
        if dumps.is_dir() and dumps_to.is_absolute():
            for entry in list(dumps.rglob("*")):
                if entry.is_file():
                    target = dumps_to / entry.relative_to(dumps)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    if target.exists():
                        target = target.with_name(f"{target.stem}.{uuid.uuid4().hex[:8]}{target.suffix}")
                    shutil.move(str(entry), str(target))
        if record.get("existed"):
            return
        if any(entry.is_file() for entry in folder.rglob("*")):
            qWarning(f"Elden Ring MO2: '{folder}' holds other files; it was left in place.")
            return
        shutil.rmtree(folder)
        qInfo(f"Elden Ring MO2: removed '{folder}', which Seamless Co-op made in the game folder.")

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
        started, pid = QProcess.startDetached(app_path, self._split_arguments(args), directory)
        if started:
            qInfo(
                f"Elden Ring MO2: started '{Path(app_path).name}' outside the VFS "
                f"(process {pid}); MO2's own run of it was cancelled."
            )
        else:
            qCritical(f"Elden Ring MO2: could not start '{app_path}' outside the VFS.")
        return bool(started)

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
            self._swap_route_saves_out(session_directory, swap, global_save_root)
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
        session_directory.rmdir()
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
        sessions += self._orphaned_sessions()
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
                    "Elden Ring MO2: another MO2 window of this installation "
                    f"still holds the hidden Steam saves in '{session_directory}'; "
                    "it restores them when its game closes."
                )
                return False
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
        return True

    def _launcher_running(self) -> bool:
        # a launcher still open may yet start the game (an update prompt, a
        # Steam sign-in, a slow first start)
        processes = _running_processes()
        if processes is None:
            return False
        other_game_running = any(
            name in self.FromSoftwareGameProcesses and name != self.GameProcess
            for _pid, name in processes
        )
        game_directory = None
        for pid, name in processes:
            if name not in self.GameLaunchers or name == self.GameProcess:
                continue
            if name in self.SharedLaunchers and other_game_running:
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
                            return True
                    if not os.path.normcase(image).startswith(game_directory + os.sep):
                        continue
            return True
        return False

    @classmethod
    def _elden_ring_process_state(cls) -> bool | None:
        # this game's process: eldenring.exe or nightreign.exe
        names = _running_process_names()
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
        self, swap_route: Path | None = None
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
                    session_directory, manifest, global_save_root, swap_route
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
        state = self._elden_ring_process_state()
        if state is True:
            # the game is up: the finish watcher knows it was seen
            self._game_seen_run = self._game_run
            self._launch_pending_until = 0.0
            return
        if state is False and (
            remaining > 0
            or (
                remaining > self.LauncherStartGrace - self.LauncherWaitLimit
                and self._launcher_running()
            )
        ):
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
            return

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

        self._organizer.setPluginSetting(
            self.name(), self.ArchiveLayoutOverrideSetting, False
        )
        self._start_refined_ui_alignment()

    def _game_text(self, text: str) -> str:
        # a translated text that names Elden Ring, for the game this plugin manages
        return text.replace("Elden Ring", self.DisplayName)

    def _show_recovery_failed(self, parent=None) -> None:
        QMessageBox.critical(
            parent,
            self._game_text(_tr("Elden Ring save recovery needed")),
            self._game_text(_tr("MO2 found a previous save-isolation session that it could not "
            "restore automatically. The original saves remain in the "
            "MO2 Revamped recovery folder. Do not launch Elden Ring or Steam "
            "until the saved folders have been restored.")),
        )

    def _show_launch_recovery_blocked(self) -> None:
        if self._recovery_waiting:
            QMessageBox.warning(
                None,
                _tr("Steam saves still hidden"),
                _tr("The Steam save folders an earlier launch hid can't be put "
                "back yet: the game or one of its launchers is still running, "
                "or another MO2 window is waiting for its game. Close it and "
                "try again. The game was not started."),
            )
            return
        QMessageBox.critical(
            None,
            self._game_text(_tr("Elden Ring save recovery needed")),
            _tr("MO2 could not restore an earlier global save folder. "
            "The game was not started. Restore the files from the "
            "MO2 Revamped recovery folder before continuing."),
        )

    def _report_displaced_saves(self, parent=None) -> None:
        """Tells the user about save folders with newer saves that a restore
        had to move into the recovery folder."""
        if not self._displaced_newer_saves:
            return
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
        style_sheets = (
            application.styleSheet() if application is not None else "",
            window.styleSheet() if window is not None else "",
        )
        return any(
            "MO2 Classic Refined - Dark" in style_sheet
            or "background-color: #242424" in style_sheet
            for style_sheet in style_sheets
        )

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
        if self._active_save_quarantine is not None:
            starting = time.monotonic() < self._launch_pending_until
            if starting or self._elden_ring_process_state() is not False:
                qCritical(
                    "Elden Ring MO2: a previous save quarantine is still active; "
                    "launch was blocked to protect the global save."
                )
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
                None,
                self._game_text(_tr("Elden Ring save recovery needed")),
                self._game_text(_tr("Another MO2 Revamped installation still holds Steam saves it "
                "hid for its own launch. Start that installation once with Elden "
                "Ring closed so it puts them back. The game was not started.\n\n{0}")).format(
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
        # a launcher run outside the VFS (the game itself always runs inside:
        # started bare it would restart through Steam with Easy Anti-Cheat)
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
                None,
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
                None,
                self._game_text(_tr("Named Elden Ring save profile is missing")),
                self._game_text(_tr("The selected named save profile could not be found or its metadata "
                "is invalid. The game was not started. Open Tools > Elden Ring "
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
                None,
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
                None,
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
                None,
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
                    None,
                    self._game_text(_tr("Could not safely isolate Elden Ring saves")),
                    self._game_text(_tr("MO2 could not confirm that Elden Ring is closed. Close any "
                    "running Elden Ring process and try again. Existing global "
                    "saves were left in place.")),
                )
                return False
            try:
                self._active_save_quarantine = self._hide_existing_global_save_directories(
                    active_save_dir if swap_launch else None
                )
            except OSError as error:
                qCritical(f"Elden Ring MO2: save isolation could not begin: {error}")
                QMessageBox.critical(
                    None,
                    self._game_text(_tr("Elden Ring saves could not be isolated")),
                    _tr("MO2 could not safely separate the existing Steam saves. "
                    "The game was not started. The original files remain in the "
                    "recovery folder if a partial move had already begun."),
                )
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
        try:
            target = self._launch_target(app_path, working_directory, args)
            started = target is not None and self._start_outside_vfs(*target)
        except Exception as error:
            # the saves are already swapped: any failure puts them back now
            qCritical(f"Elden Ring MO2: the launch outside the VFS failed: {error}")
            target, started = (app_path, working_directory, args), False
        if not started and target is None:
            # nothing to start (the user was told why): only the saves go back
            if session is not None:
                try:
                    self._restore_global_save_quarantine(
                        session, Path(self.savesDirectory().absolutePath())
                    )
                    self._active_save_quarantine = None
                except (OSError, RuntimeError, ValueError) as error:
                    qCritical(f"Elden Ring MO2: the swapped saves need recovery: {error}")
            return False
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
                None,
                _tr("Could not start the launcher"),
                _tr("MO2 could not start {0} outside its virtual file system. "
                "The game was not started.").format(Path(app_path).name),
            )
            return False
        if session is not None:
            self._finish_when_game_closes(True, session, False, None, 0.0, False, self._game_run)
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
                None,
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
                None,
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
        # the finished-run hook only gets the path (cmd.exe for a launchmod
        # script): it recognises this run by it, even when other tools ran
        # in between
        self._guarded_launches.add(os.path.normcase(str(app_path)))
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
        state = self._elden_ring_process_state()
        seen = seen or state is True
        # seconds in a row the process list could not be read
        unknown = unknown + self.GameWatchInterval / 1000 if state is None else 0.0
        if unknown >= self.LauncherWaitLimit:
            # the saves stay hidden: the next launch, or the next MO2 start,
            # restores them once the game is known to be closed
            qCritical(
                "Elden Ring MO2: could not tell for ten minutes whether the game "
                "is still running; stopped waiting. The hidden Steam saves stay "
                "in recovery until the next launch or MO2 start."
            )
            return
        game_closed = state is False and (
            seen
            or not via_launcher
            or (
                waited >= self.LauncherStartGrace
                and (waited >= self.LauncherWaitLimit or not self._launcher_running())
            )
        )
        if not game_closed:
            if waited == 0:
                qInfo(
                    "Elden Ring MO2: waiting for Elden Ring to close before "
                    "restoring the Steam saves."
                )
            QTimer.singleShot(
                self.GameWatchInterval,
                lambda: self._finish_when_game_closes(
                    via_launcher,
                    session_directory,
                    cleanup_armed,
                    overwrite_path,
                    waited + self.GameWatchInterval / 1000,
                    seen,
                    run,
                    unknown,
                ),
            )
            return

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
                if self._displaced_newer_saves:
                    self._report_displaced_saves()
                elif not restored_cleanly:
                    QMessageBox.warning(
                        None,
                        self._game_text(_tr("Elden Ring save recovery folder retained")),
                        _tr("The original Steam saves were restored. Additional "
                        "save output created outside the selected MO2 route was "
                        "preserved in the recovery folder for review."),
                    )
            except (OSError, RuntimeError, ValueError) as error:
                qCritical(f"Elden Ring MO2: global save restoration failed: {error}")
                QMessageBox.critical(
                    None,
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

    @staticmethod
    def _read_skip_intro_logos(path: Path) -> bool:
        config = configparser.ConfigParser()
        try:
            config.read(path, encoding="utf-8-sig")
            return config.getboolean("Startup", "skip_intro_logos", fallback=False)
        except (OSError, UnicodeError, configparser.Error, ValueError):
            return False

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
        """Resolve Revamped's ordered native list against installed mod routes."""
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
            seen_virtual_paths: set[str] = set()
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
            for native in natives:
                if not isinstance(native, dict):
                    raise ValueError(_tr("A Revamped native profile entry is invalid."))
                enabled = native.get("enabled", True)
                if not isinstance(enabled, bool):
                    raise ValueError(_tr("A native profile entry's enabled value must be true or false."))
                if not enabled:
                    disabled_mod = str(native.get("mod_name") or mo2_mod_name).strip()
                    disabled_route = str(native.get("path") or "").replace("\\", "/")
                    if disabled_mod and disabled_route:
                        disabled_libraries.add(
                            (disabled_mod.casefold(), disabled_route.rsplit("/", 1)[-1].casefold())
                        )
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
                    raise ValueError(
                        _tr("Revamped native '{0}' was not found in the mapped MO2 mod '{1}'. Keep original structure may retain natives/...; MO2 layout must place it under DLLs/.").format(route, mapped_mod)
                    )

                virtual_path, _source_path = resolved_candidates[0]
                virtual_key = virtual_path.casefold()
                if virtual_key in seen_virtual_paths:
                    raise ValueError(
                        _tr("The Revamped native profile resolves more than one entry to '{0}'. Keep only one entry for that game path.").format(virtual_path)
                    )
                seen_virtual_paths.add(virtual_key)
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
                    if optional:
                        continue
                    raise ValueError(
                        _tr("MO2 has no active file origin for Revamped native '{0}'.").format(virtual_path)
                    )
                winning_mod = mod_list.getMod(winner)
                if winning_mod is None:
                    raise ValueError(
                        _tr("MO2's winning mod '{0}' for '{1}' is unavailable.").format(winner, virtual_path)
                    )
                winning_root = Path(winning_mod.absolutePath()).resolve()
                library = winning_root.joinpath(*Path(virtual_path).parts).resolve(strict=True)
                try:
                    library.relative_to(winning_root)
                except ValueError as error:
                    raise ValueError(
                        _tr("The winning path for '{0}' leaves its MO2 mod.").format(virtual_path)
                    ) from error
                if not library.is_file():
                    raise ValueError(_tr("The winning file for '{0}' is not a DLL.").format(virtual_path))

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
                entries.append({
                    "route": route,
                    "virtual_path": virtual_path,
                    "mod_name": winner,
                    "library": library,
                    "initializer": initializer_function,
                })
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

    def _apply_revamped_native_load_order(self, libraries: list[Path], profile_data: dict) -> list[Path]:
        """Apply the Revamped profile's declared native DLL order."""
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
                    qInfo(
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
            qInfo(
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
        else:
            os.environ.pop(config_key, None)

        initializer_key = self.NativeInitializerEnvironment
        self._native_initializer_env_previous = os.environ.get(initializer_key)
        self._native_initializer_env_active = True
        initializer_values = [
            f"{Path(entry['library']).resolve()}|{entry['initializer']}"
            for entry in (initializers or [])
            if entry.get("initializer")
        ]
        if initializer_values:
            os.environ[initializer_key] = "\n".join(initializer_values)
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
        qInfo("Elden Ring MO2: profile launch options passed to bridge.")

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

    def documentsDirectory(self) -> QDir:
        appdata = os.environ.get("APPDATA")
        if not appdata:
            appdata = str(Path.home() / "AppData" / "Roaming")
        return QDir(str(Path(appdata) / self.SaveFolderName))

    def savesDirectory(self) -> QDir:
        return self.documentsDirectory()

    def listSaves(self, folder: QDir) -> list[mobase.ISaveGame]:
        # only the saves the game reads: <account>/ER0000.sl2, a Seamless
        # Co-op .co2 or a Convergence .cnv. A recursive search would also
        # list the named save profiles and backups kept inside the shared
        # route.
        return [
            BasicGameSaveGame(file)
            for file in self._route_save_files(Path(folder.absolutePath()))
            if file.suffix.casefold() in {".sl2", ".co2", ".cnv"}
        ]

    def executables(self) -> list[mobase.ExecutableInfo]:
        game_exe = QFileInfo(
            self.gameDirectory(),
            self.GameBinary,
        )
        executable = mobase.ExecutableInfo(f"{self.GameName} (MO2)", game_exe)
        executable = executable.withWorkingDirectory(self.dataDirectory())
        executable = executable.withSteamAppId(str(self.GameSteamId))
        return [executable]

    def executableForcedLoads(self) -> list[mobase.ExecutableForcedLoadSetting]:
        if not self.isActive():
            return []

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
            qInfo(
                "Elden Ring MO2: no active mods or bridge-dependent profile "
                "options; no native or asset bridge needed."
            )
            return []
        if not active_mods:
            qInfo(
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
                libraries, revamped_native_profile
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
            qInfo("Elden Ring MO2: native DLL loading is disabled in plugin settings.")
        if not libraries and not root_layout_libraries and active_mods:
            qInfo(
                "Elden Ring MO2: no active winning DLLs found under "
                "Game/DLLs/, legacy managed folders, or preserved root layouts."
            )

        if libraries:
            qInfo(
                "Elden Ring MO2: requesting MO2 forced loads for "
                f"{len(libraries)} active mod DLL(s)."
            )
            for library in libraries:
                qInfo(f"Elden Ring MO2: queued native DLL {library.name}.")
        mod_forced_loads = [
            mobase.ExecutableForcedLoadSetting(
                self.GameProcess, str(library)
            ).withEnabled(True)
            for library in libraries
        ]
        if root_layout_libraries:
            qInfo(
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
                qInfo(
                    f"Elden Ring MO2: queued {kind} {virtual_name} "
                    f"from active mod {mod_name}."
                )
        root_forced_loads = [
            mobase.ExecutableForcedLoadSetting(
                self.GameProcess, str(library)
            ).withEnabled(True)
            for _, _, library, _ in root_layout_libraries
        ]
        bridge = Path(__file__).with_name("eldenring_mo2_bridge.dll")
        bridge_load = None
        if bridge.is_file():
            if active_mods:
                qInfo(
                    "Elden Ring MO2: queued native and loose-asset bridge "
                    f"{bridge.name} for {len(active_mods)} active mod(s)."
                )
            else:
                qInfo(
                    "Elden Ring MO2: queued native bridge "
                    f"{bridge.name} for profile startup options."
                )
            bridge_load = mobase.ExecutableForcedLoadSetting(
                Path(self.GameBinary).name, str(bridge)
            ).withEnabled(True)
            qInfo(
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
        return forced_loads

    @staticmethod
    def _is_game_runtime_dll(name: str) -> bool:
        # libraries the game itself imports: a copy in a mod is never
        # force-loaded ahead of the game's own
        name = name.casefold()
        return name == "steam_api64.dll" or (
            name.startswith("oo2core_") and name.endswith("_win64.dll")
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
                    mod.pluginSettings(self.ArchiveLayoutInstallerName).get(
                        self.PreserveArchiveLayoutSetting, False
                    )
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
    with Seamless Co-op), the game is Game/nightreign.exe, and mods are
    usually started through me3 or the Seamless Co-op launcher."""

    Name = "Elden Ring Nightreign MO2 Support"
    GameName = "ELDEN RING NIGHTREIGN"
    GameShortName = "nightreign"
    GameNexusName = "eldenringnightreign"
    GameSteamId = 2622380
    GameBinary = "Game/nightreign.exe"

    GameProcess = "nightreign.exe"
    SaveFolderName = "Nightreign"
    SavePrefix = "nr0000."
    GlobalBackupsFolderName = "Nightreign Global Save Backups"
    LocalSavegamesClass = NightreignLocalSavegames
    RecoveryScope = "|nightreign"
    DisplayName = "Nightreign"
    SeamlessLaunchers = frozenset({"nrsc_launcher.exe"})
    SeamlessDll = "nrsc.dll"
    Me3GameId = "nightreign"
    GameLaunchers = frozenset(
        {
            "nightreign.exe",
            # Easy Anti-Cheat's launcher, the program Steam starts
            "start_protected_game.exe",
            # Seamless Co-op for Nightreign
            "nrsc_launcher.exe",
            "me3.exe",
            "me3-launcher.exe",
        }
    )
