"""Elden Ring game support for Mod Organizer 2 without an external loader."""

from __future__ import annotations

import ctypes
import configparser
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import uuid

import mobase
from PyQt6.QtCore import (
    QEvent,
    QFileInfo,
    QDir,
    QObject,
    QTimer,
    Qt,
    qCritical,
    qInfo,
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
from ..basic_features.utils import is_directory
from ..basic_game import BasicGame


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
    ):
        self._organizer = organizer
        self._plugin_name = plugin_name
        self._preserve_extensions = frozenset(
            extension.casefold() for extension in preserve_extensions
        )
        self._saved_archive_layout_signatures: set[tuple[str, ...]] | None = None
        self._saved_archive_layout_cache_epoch = None
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
        """
        self._unwrap_package_folders(filetree)
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
                    f"Could not move {entry.name()} into Game/{destination}."
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
                    f"Could not preserve {asset_directory.name()} beside its native DLL."
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
                        "MO2 layout found a conflicting assets directory beside "
                        "a native DLL. Choose Keep original structure to preserve "
                        "both folders without merging them."
                    )
            if not filetree.move(asset_directory, f"{destination}/"):
                raise RuntimeError(
                    f"Could not move {asset_directory.name()} beside its native DLL."
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
                        f"Could not move {entry.name()} into Game/{destination}."
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
                        "Could not move the mod fonts directory beside its DLL."
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
        target_signature = self._file_tree_signature(filetree)
        if not target_signature:
            return False

        cache_epoch = self._archive_layout_cache_epoch()
        if (
            self._saved_archive_layout_signatures is not None
            and cache_epoch == self._saved_archive_layout_cache_epoch
        ):
            return target_signature in self._saved_archive_layout_signatures

        self._saved_archive_layout_signatures = None

        try:
            mod_list = self._organizer.modList()
            mod_names = mod_list.allMods()
        except Exception:
            return False

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
        current_epoch = self._archive_layout_cache_epoch()
        if complete and current_epoch == cache_epoch:
            self._saved_archive_layout_signatures = signatures
            self._saved_archive_layout_cache_epoch = current_epoch
        return target_signature in signatures

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
    def _file_tree_signature(filetree: mobase.IFileTree) -> tuple[str, ...]:
        paths: list[str] = []

        def collect(tree: mobase.IFileTree, prefix: str = "") -> None:
            for entry in tree:
                name = entry.name().replace("\\", "/")
                relative_path = f"{prefix}/{name}" if prefix else name
                if is_directory(entry):
                    collect(entry, relative_path)
                elif (
                    name.casefold() != "meta.ini"
                    and Path(name).suffix.casefold() != ".mohidden"
                ):
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
        for entry in filetree:
            name = entry.name().casefold()
            if is_directory(entry):
                # Managed DLL directories are already in a valid destination.
                if name in {
                    self._dll_directory.casefold(),
                    *self._legacy_dll_directories,
                    "plugins",
                }:
                    continue
                if name in {"mod", "native", "natives"} and any(
                    not is_directory(child)
                    and Path(child.name()).suffix.casefold() == ".dll"
                    for child in entry
                ):
                    return True
                if self._has_misplaced_dll(entry, name, depth + 1):
                    return True
            elif (
                Path(name).suffix.casefold() == ".dll"
                and bool(parent_name)
                and parent_name
                not in {
                    self._dll_directory.casefold(),
                    *self._legacy_dll_directories,
                    "plugins",
                }
            ):
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

    def _move_native_files(
        self, filetree: mobase.IFileTree, source: mobase.IFileTree
    ) -> None:
        dlls = [
            entry
            for entry in source
            if not is_directory(entry)
            and Path(entry.name()).suffix.casefold() == ".dll"
            and entry.name().casefold() not in self._root_loader_dlls
        ]
        if not dlls:
            return

        stems = {Path(entry.name()).stem.casefold() for entry in dlls}
        config_suffixes = {"config", "settings", "configuration", "options"}
        sidecars = [
            entry
            for entry in source
            if not is_directory(entry)
            and Path(entry.name()).suffix.casefold() in self._sidecar_extensions
            and self._is_dll_sidecar(
                Path(entry.name()).stem.casefold(), stems, config_suffixes
            )
        ]
        for entry in [*dlls, *sidecars]:
            filetree.move(entry, f"{self._dll_directory}/")

    def _move_root_sidecars_for_nested_dlls(
        self, filetree: mobase.IFileTree
    ) -> None:
        """Move a root-level INI beside a matching DLL found in a payload folder."""
        dll_stems: set[str] = set()

        def collect_dlls(tree: mobase.IFileTree, depth: int = 0) -> None:
            for entry in tree:
                if is_directory(entry):
                    collect_dlls(entry, depth + 1)
                elif (
                    depth > 0
                    and Path(entry.name()).suffix.casefold() == ".dll"
                    and entry.name().casefold() not in self._root_loader_dlls
                ):
                    dll_stems.add(Path(entry.name()).stem.casefold())

        collect_dlls(filetree)
        if not dll_stems:
            return

        config_suffixes = {"config", "settings", "configuration", "options"}
        root_sidecars = [
            entry
            for entry in filetree
            if not is_directory(entry)
            and Path(entry.name()).suffix.casefold() in self._sidecar_extensions
            and self._is_dll_sidecar(
                Path(entry.name()).stem.casefold(), dll_stems, config_suffixes
            )
        ]
        for entry in root_sidecars:
            filetree.move(entry, f"{self._dll_directory}/")

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

    ConfigFilename = "eldenring_mo2_saves.ini"
    ProfileIsolated = "profile"
    InstanceShared = "instance_shared"
    GlobalShared = "global"
    SaveProfilesDirectoryName = "save_profiles"
    ProfileSaveProfileKey = "profile_save_profile"
    InstanceSaveProfileKey = "instance_save_profile"
    Modes = {ProfileIsolated, InstanceShared, GlobalShared}

    def __init__(self, game, organizer: mobase.IOrganizer):
        super().__init__()
        self._game = game
        self._organizer = organizer
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
            config.read(config_path, encoding="utf-8")
            mode = config.get("Saves", "mode", fallback=cls.ProfileIsolated)
        except (OSError, UnicodeError, configparser.Error, ValueError):
            mode = cls.ProfileIsolated
        return mode if mode in cls.Modes else cls.ProfileIsolated

    @classmethod
    def mode_for_profile(cls, profile) -> str:
        return cls.mode_for_profile_directory(Path(profile.absolutePath()))

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
            config.read(profile_directory / cls.ConfigFilename, encoding="utf-8")
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

    @staticmethod
    def instance_shared_directory(profile_path: Path) -> Path:
        # MO2 keeps profiles below an instance-specific profiles directory.
        # Placing shared modded saves beside that directory keeps them separate
        # from profile saves and from the global Roaming save folder.
        return profile_path.parent.parent / "Elden Ring Shared Saves"

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
        return self._prepared_profile_routes.get(self._profile_key(profile)) == current_route

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
    NativeBridgeVersion = "0.5.0-alpha.33"

    GameName = "ELDEN RING"
    GameShortName = "eldenring"
    GameNexusName = "eldenring"
    GameSteamId = 1245620
    GameBinary = "Game/eldenring.exe"
    GameDataPath = "Game"
    GameSaveExtension = "sl2"
    NativeDllSetting = "load_native_dlls"
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
    UnroutedSaveDirectoryName = "unrouted-global"

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

        self._local_savegames_feature = EldenRingLocalSavegames(self, organizer)
        self._register_feature(self._local_savegames_feature)
        self._register_feature(
            EldenRingModDataChecker(organizer, self.name())
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

    def _save_quarantine_root(self) -> Path:
        global_save_root = Path(self.savesDirectory().absolutePath())
        try:
            install_root = Path(__file__).resolve().parents[3]
        except (IndexError, OSError, RuntimeError):
            install_root = Path(self._organizer.basePath())
        try:
            install_identity = os.path.normcase(str(install_root.resolve()))
        except (OSError, RuntimeError):
            install_identity = os.path.normcase(os.path.abspath(install_root))
        instance_token = hashlib.sha256(
            install_identity.encode("utf-8", errors="replace")
        ).hexdigest()[:24]
        return (
            global_save_root.parent
            / self.SaveRecoveryDirectoryName
            / instance_token
        )

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

    @staticmethod
    def _save_directory_contains_elden_ring_saves(directory: Path) -> bool:
        for entry in directory.iterdir():
            if not entry.is_file():
                continue
            if entry.name.casefold().endswith(
                (".sl2", ".sl2.bak", ".co2", ".co2.bak")
            ):
                return True
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
                raise OSError("The Elden Ring save-recovery record could not be read.") from error
            recorded_root = manifest.get("global_save_root")
            if recorded_root:
                try:
                    same_root = os.path.normcase(os.path.abspath(recorded_root)) == os.path.normcase(
                        os.path.abspath(global_save_root)
                    )
                except (OSError, TypeError, ValueError):
                    same_root = False
                if not same_root:
                    raise OSError(
                        "The save-recovery record points to a different Elden Ring save folder."
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

        global_save_root.mkdir(parents=True, exist_ok=True)
        for item in names:
            name = item.get("name") if isinstance(item, dict) else None
            if not isinstance(name, str) or not name or Path(name).name != name:
                raise OSError("The save-recovery record contains an invalid folder name.")
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
                os.replace(original_directory, preserved_directory)
                qCritical(
                    "Elden Ring MO2: an unexpected global save folder appeared "
                    "during the isolated session; it was preserved in recovery "
                    "before restoring the Steam save."
                )

            os.replace(backup_directory, original_directory)
            item["restored"] = True
            self._write_save_quarantine_manifest(session_directory, manifest)

        temporary_manifest = session_directory / f".{self.SaveRecoveryManifestName}.tmp"
        temporary_manifest.unlink(missing_ok=True)
        if (session_directory / self.UnroutedSaveDirectoryName).is_dir():
            # Preserve unexpected game output for manual inspection.
            return False

        manifest_path.unlink(missing_ok=True)
        session_directory.rmdir()
        try:
            self._save_quarantine_root().rmdir()
            self._save_quarantine_root().parent.rmdir()
        except OSError:
            pass
        return True

    def _recover_stale_global_save_quarantines(self) -> bool:
        recovery_root = self._save_quarantine_root()
        if not recovery_root.is_dir():
            return True

        if self._elden_ring_process_state() is not False:
            qCritical(
                "Elden Ring MO2: prior global saves need recovery, but Elden Ring "
                "is running or its process state cannot be confirmed."
            )
            return False

        global_save_root = Path(self.savesDirectory().absolutePath())
        for session_directory in sorted(recovery_root.iterdir()):
            if not session_directory.is_dir():
                continue
            try:
                restored_cleanly = self._restore_global_save_quarantine(
                    session_directory, global_save_root
                )
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

    @staticmethod
    def _elden_ring_process_state() -> bool | None:
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
        except (OSError, subprocess.SubprocessError):
            return None
        if result.returncode != 0:
            return None
        return any(
            line.lstrip().casefold().startswith('"eldenring.exe"')
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
        except (OSError, RuntimeError, ValueError) as error:
            qCritical(
                "Elden Ring MO2: launch did not start and save restoration "
                f"needs attention: {error}"
            )

    def _hide_existing_global_save_directories(self) -> Path | None:
        global_save_root = Path(self.savesDirectory().absolutePath())
        if not global_save_root.is_dir():
            return None
        if not self._recover_stale_global_save_quarantines():
            raise OSError("A previous Elden Ring save session still needs recovery.")

        save_directories = [
            child
            for child in global_save_root.iterdir()
            if child.is_dir() and self._save_directory_contains_elden_ring_saves(child)
        ]
        if not save_directories:
            return None

        recovery_root = self._save_quarantine_root()
        session_directory = recovery_root / uuid.uuid4().hex
        session_directory.mkdir(parents=True, exist_ok=False)
        manifest = {
            "version": 1,
            "global_save_root": str(global_save_root),
            "directories": [{"name": directory.name} for directory in save_directories],
        }
        self._write_save_quarantine_manifest(session_directory, manifest)
        self._active_save_quarantine = session_directory
        try:
            for directory in save_directories:
                os.replace(directory, session_directory / directory.name)
                self._write_save_quarantine_manifest(session_directory, manifest)
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
        QTimer.singleShot(
            5000,
            lambda session=session_directory, save_root=global_save_root: self._restore_quarantine_if_launch_failed(
                session, save_root
            ),
        )
        return session_directory

    def _initialize_elden_ring_ui(self, _main_window) -> None:
        if not self.isActive():
            return

        if not self._recover_stale_global_save_quarantines():
            QMessageBox.critical(
                _main_window,
                "Elden Ring save recovery needed",
                "MO2 found a previous save-isolation session that it could not "
                "restore automatically. The original saves remain in the "
                "MO2 Revamped recovery folder. Do not launch Elden Ring or Steam "
                "until the saved folders have been restored.",
            )

        self._organizer.setPluginSetting(
            self.name(), self.ArchiveLayoutOverrideSetting, False
        )
        self._start_refined_ui_alignment()

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
                selector_parent = selector.parentWidget()
                layout = selector_parent.layout() if selector_parent is not None else None
                if layout is not None:
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
        split_button.setToolTip("Run the selected program; use the arrow for shortcut options.")
        split_button.setAccessibleName("Run")
        split_button.setAccessibleDescription(
            "Run the selected program. Use the arrow section for shortcut options."
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
                    "Load enabled mod DLLs through MO2. Some mods require an "
                    "extension API that this plugin does not provide."
                ),
                True,
            )
        )
        return settings

    def _prepare_profile_launch(
        self, app_path: str, _working_directory, _args: str
    ) -> bool:
        self._restore_profile_config_environment()
        self._overwrite_cleanup_armed = False
        self._overwrite_cleanup_path = None
        qInfo(
            "Elden Ring MO2: about-to-run save guard received "
            f"'{Path(app_path).name}'."
        )
        if self._active_save_quarantine is not None:
            if self._elden_ring_process_state() is not False:
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
        if not self.isActive():
            return True

        profile = self._organizer.profile()
        if profile is None or Path(app_path).name.casefold() != "eldenring.exe":
            return True

        revamped_native_profile = self._resolve_revamped_native_profile(profile)
        if revamped_native_profile is not None and revamped_native_profile.get("error"):
            message = str(revamped_native_profile["error"])
            QMessageBox.warning(
                None,
                "Revamped native profile could not be prepared",
                message + "\n\nThe game was not started.",
            )
            qCritical(
                "Elden Ring MO2: blocked launch for invalid Revamped native "
                f"profile: {message}"
            )
            return False
        if revamped_native_profile is not None and revamped_native_profile.get("entries") and not bool(
            self._organizer.pluginSetting(self.name(), self.NativeDllSetting)
        ):
            QMessageBox.warning(
                None,
                "Native DLL loading is disabled",
                "This MO2 profile declares native DLLs in its Revamped native "
                "profile, but "
                "Elden Ring native DLL loading is disabled in plugin settings. "
                "The game was not started.",
            )
            return False

        profile_config = Path(profile.absolutePath()) / self.ProfileLaunchConfig
        profile_save_dir = Path(profile.absolutePath()) / "saves"
        game_save_dir = Path(self.savesDirectory().absolutePath())
        profile_saves_enabled = bool(profile.localSavesEnabled())
        save_mode = EldenRingLocalSavegames.mode_for_profile(profile)
        if not self._local_savegames_feature.profile_route_is_current(profile):
            QMessageBox.warning(
                None,
                "Reload Elden Ring save routing",
                "The save mode, named save profile, or MO2 profile-specific-save setting changed "
                "after MO2 loaded this profile. The game was not started because "
                "the active save mapping may still point to the previous folder.\n\n"
                "Restart MO2, or switch to another profile and back, before "
                "launching Elden Ring.",
            )
            qInfo(
                "Elden Ring MO2: blocked launch because the active save mapping "
                f"is stale for profile '{profile.name()}'."
            )
            return False
        if not EldenRingLocalSavegames.save_profile_is_available(
            Path(profile.absolutePath()), save_mode
        ):
            QMessageBox.warning(
                None,
                "Named Elden Ring save profile is missing",
                "The selected named save profile could not be found or its metadata "
                "is invalid. The game was not started. Open Elden Ring / Save "
                "Isolation, choose an available profile, or restore the missing "
                "profile folder.",
            )
            qInfo(
                "Elden Ring MO2: blocked launch because the selected named "
                "save profile is unavailable."
            )
            return False
        if save_mode in {
            EldenRingLocalSavegames.ProfileIsolated,
            EldenRingLocalSavegames.InstanceShared,
        } and not profile_saves_enabled:
            QMessageBox.warning(
                None,
                "Elden Ring save isolation is not active",
                "This profile is set to use isolated Elden Ring saves, but "
                "MO2's 'Use profile-specific Save Games' option is off.\n\n"
                "Enable that option for this profile in the Profiles dialog, "
                "then launch Elden Ring again. The game was not started, so "
                "the global Steam save was left untouched.",
            )
            qInfo(
                "Elden Ring MO2: blocked launch because profile-specific "
                f"saves are disabled for '{profile.name()}'."
            )
            return False
        if (
            save_mode == EldenRingLocalSavegames.GlobalShared
            and profile_saves_enabled
        ):
            QMessageBox.warning(
                None,
                "Elden Ring save mode does not match MO2",
                "This profile is set to use the global Steam save, but MO2's "
                "'Use profile-specific Save Games' option is on.\n\n"
                "Disable that option for this profile in the Profiles dialog "
                "to use the global save. The game was not started.",
            )
            qInfo(
                "Elden Ring MO2: blocked launch because global-save mode "
                f"conflicts with profile-specific saves for '{profile.name()}'."
            )
            return False

        active_save_dir = EldenRingLocalSavegames.active_save_directory(
            Path(profile.absolutePath()), save_mode
        )
        if save_mode == EldenRingLocalSavegames.GlobalShared:
            active_save_dir = game_save_dir
            save_route = "global save shared with Steam and all MO2 instances"
        elif save_mode == EldenRingLocalSavegames.InstanceShared:
            save_route = "instance-shared modded saves"
        else:
            save_route = "profile-isolated saves"
        selected_save_profile = (
            EldenRingLocalSavegames.save_profile_for_profile_directory(
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

        if save_mode != EldenRingLocalSavegames.GlobalShared and normalize(
            active_save_dir
        ) == normalize(game_save_dir):
            QMessageBox.critical(
                None,
                "Elden Ring save path conflict",
                "The selected isolated save folder resolves to the same path "
                "as the global Elden Ring save folder. The game was not started "
                "to protect the original save. Check the MO2 profile paths.",
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
        (
            start_minimized,
            black_startup_background,
            exclude_cpu0,
            clear_overwrite_after_game,
            clear_overwrite_logs_before_game,
            process_priority,
            bridge_log_history,
        ) = self._read_profile_launch_settings(profile_config)

        if save_mode != EldenRingLocalSavegames.GlobalShared:
            if not self._recover_stale_global_save_quarantines():
                QMessageBox.critical(
                    None,
                    "Elden Ring save recovery needed",
                    "MO2 could not restore an earlier global save folder. "
                    "The game was not started. Restore the files from the "
                    "MO2 Revamped recovery folder before continuing.",
                )
                return False
            if self._elden_ring_process_state() is not False:
                QMessageBox.warning(
                    None,
                    "Could not safely isolate Elden Ring saves",
                    "MO2 could not confirm that Elden Ring is closed. Close any "
                    "running Elden Ring process and try again. Existing global "
                    "saves were left in place.",
                )
                return False
            try:
                self._active_save_quarantine = self._hide_existing_global_save_directories()
            except OSError as error:
                qCritical(f"Elden Ring MO2: save isolation could not begin: {error}")
                QMessageBox.critical(
                    None,
                    "Elden Ring saves could not be isolated",
                    "MO2 could not safely separate the existing Steam saves. "
                    "The game was not started. The original files remain in the "
                    "recovery folder if a partial move had already begun.",
                )
                return False

        settings_file_state = (
            "found" if profile_config.is_file() else "missing; using defaults"
        )
        qInfo(
            "Elden Ring MO2: experimental options for profile "
            f"'{profile.name()}' ({settings_file_state}): "
            f"start minimized={'on' if start_minimized else 'off'}, "
            "black background="
            f"{'on' if black_startup_background else 'off'}, "
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
        self._activate_profile_config_environment(profile_config, initializer_entries)
        if clear_overwrite_logs_before_game and not clear_overwrite_after_game:
            self._clear_overwrite_logs_before_game()
        if clear_overwrite_after_game:
            self._arm_overwrite_cleanup()
        self._preserve_previous_bridge_log(bridge_log_history)
        return True

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

    def _finish_profile_launch(self, app_path: str, _exit_code: int) -> None:
        qInfo(
            "Elden Ring MO2: finished-run save cleanup received "
            f"'{Path(app_path).name}'."
        )
        if Path(app_path).name.casefold() != "eldenring.exe":
            self._overwrite_cleanup_armed = False
            self._overwrite_cleanup_path = None
            return
        self._restore_profile_config_environment()
        session_directory = self._active_save_quarantine
        if session_directory is not None:
            try:
                restored_cleanly = self._restore_global_save_quarantine(
                    session_directory, Path(self.savesDirectory().absolutePath())
                )
                self._active_save_quarantine = None
                if not restored_cleanly:
                    QMessageBox.warning(
                        None,
                        "Elden Ring save recovery folder retained",
                        "The original Steam saves were restored. Additional "
                        "save output created outside the selected MO2 route was "
                        "preserved in the recovery folder for review.",
                    )
            except (OSError, RuntimeError, ValueError) as error:
                qCritical(f"Elden Ring MO2: global save restoration failed: {error}")
                QMessageBox.critical(
                    None,
                    "Elden Ring save recovery needed",
                    "MO2 could not restore the original Steam saves automatically. "
                    "The files remain in the MO2 Revamped recovery folder. Do not "
                    "launch Elden Ring or Steam until recovery is complete.",
                )
        overwrite_path = self._overwrite_cleanup_path
        cleanup_armed = self._overwrite_cleanup_armed
        self._overwrite_cleanup_path = None
        self._overwrite_cleanup_armed = False
        if cleanup_armed and overwrite_path is not None:
            self._clear_overwrite_contents(overwrite_path)

    def _read_profile_launch_settings(
        self, path: Path
    ) -> tuple[bool, bool, bool, bool, bool, int, int]:
        config = configparser.ConfigParser()
        try:
            config.read(path, encoding="utf-8")
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

    def _resolve_revamped_native_profile(self, profile) -> dict | None:
        """Resolve Revamped's ordered native list against installed mod routes."""
        mo2_mod_name = ""
        try:
            profile_root = Path(profile.absolutePath()).resolve()
            profile_config = profile_root / self.ProfileLaunchConfig
            config = configparser.ConfigParser(interpolation=None)
            config.read(profile_config, encoding="utf-8")
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
                        f"Unsupported Revamped native profile setting '{key}'."
                    )
                index = int(raw_index)
                if index <= 0:
                    raise ValueError("Native entry numbers must be greater than zero.")
                entry = natives_by_index.setdefault(index, {})
                if prefix == "native":
                    if "path" in entry:
                        raise ValueError(f"Native entry {index} is declared more than once.")
                    entry["path"] = value.strip()
                elif prefix in {"optional", "enabled"}:
                    if value.strip().casefold() not in {"true", "false"}:
                        raise ValueError(
                            f"'{key}' must be set to true or false."
                        )
                    entry[prefix] = value.strip().casefold() == "true"
                elif prefix == "mod":
                    if "mod_name" in entry:
                        raise ValueError(f"Native entry {index} has more than one mod mapping.")
                    entry["mod_name"] = value.strip()
                else:
                    if "initializer" in entry:
                        raise ValueError(
                            f"Native entry {index} has more than one initializer."
                        )
                    entry["initializer"] = {"function": value.strip()}

            natives = [natives_by_index[index] for index in sorted(natives_by_index)]
            if not natives:
                raise ValueError(
                    "The [RevampedNativeProfile] section does not declare any native DLLs."
                )

            mod_list = self._organizer.modList()
            active_mods = [
                name
                for name in mod_list.allModsByProfilePriority()
                if mod_list.state(name) & mobase.ModState.ACTIVE
            ]
            entries = []
            declared_names: set[str] = set()
            declared_libraries: set[str] = set()
            seen_routes: set[tuple[str, str]] = set()
            seen_virtual_paths: set[str] = set()
            scope_priorities: set[int] = set()
            legacy_scope_root = None
            legacy_scope_mod = None
            legacy_profile = not any("mod_name" in native for native in natives)
            if legacy_profile:
                if not mo2_mod_name:
                    raise ValueError(
                        "The [RevampedNativeProfile] section needs a mod mapping."
                    )
                legacy_scope_mod = next(
                    (
                        name
                        for name in active_mods
                        if name.casefold() == mo2_mod_name.casefold()
                    ),
                    None,
                )
                if legacy_scope_mod is None:
                    raise ValueError(
                        f"The mapped MO2 mod '{mo2_mod_name}' is not enabled in this profile."
                    )
                legacy_mod_info = mod_list.getMod(legacy_scope_mod)
                if legacy_mod_info is None:
                    raise ValueError(
                        f"The mapped MO2 mod '{legacy_scope_mod}' is unavailable."
                    )
                legacy_scope_root = Path(legacy_mod_info.absolutePath()).resolve()
                if not legacy_scope_root.is_dir():
                    raise ValueError(
                        f"The mapped MO2 mod folder '{legacy_scope_root}' is unavailable."
                    )
            for native in natives:
                if not isinstance(native, dict):
                    raise ValueError("A Revamped native profile entry is invalid.")
                enabled = native.get("enabled", True)
                if not isinstance(enabled, bool):
                    raise ValueError("A native profile entry's enabled value must be true or false.")
                if not enabled:
                    continue

                native_mod_name = str(native.get("mod_name") or mo2_mod_name).strip()
                if not native_mod_name:
                    raise ValueError(
                        "Every native entry needs a mod mapping, either in 'mod' "
                        "or in its indexed 'mod_N' value."
                    )
                mapped_mod = next(
                    (
                        name
                        for name in active_mods
                        if name.casefold() == native_mod_name.casefold()
                    ),
                    None,
                )
                if mapped_mod is None:
                    if "mod_name" in native:
                        qInfo(
                            "Elden Ring MO2: skipped native profile entry for "
                            f"inactive mod '{native_mod_name}'."
                        )
                        continue
                    raise ValueError(
                        f"The mapped MO2 mod '{native_mod_name}' is not enabled in this profile."
                    )
                mapped_mod_info = mod_list.getMod(mapped_mod)
                if mapped_mod_info is None:
                    raise ValueError(f"The mapped MO2 mod '{mapped_mod}' is unavailable.")
                mod_root = Path(mapped_mod_info.absolutePath()).resolve()
                if not mod_root.is_dir():
                    raise ValueError(f"The mapped MO2 mod folder '{mod_root}' is unavailable.")

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
                                    installed_native_routes.append(
                                        installed_path.as_posix()
                                    )
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

                raw_path = native.get("path")
                if not isinstance(raw_path, str) or not raw_path.strip():
                    raise ValueError("An enabled Revamped native entry is missing its path.")
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
                        f"Unsupported Revamped native path '{raw_path}'. Use a relative DLL path."
                    )

                route = relative_path.as_posix()
                route_key = (mapped_mod.casefold(), route.casefold())
                if route_key in seen_routes:
                    raise ValueError(
                        f"The Revamped native profile lists '{route}' more than once."
                    )
                seen_routes.add(route_key)

                optional = native.get("optional", False)
                if not isinstance(optional, bool):
                    raise ValueError("A native profile entry's optional value must be true or false.")

                # The installer records the final mod-relative route for every
                # archive DLL it can identify. Use that first, then support
                # older or manually maintained mods through known MO2 paths.
                route_candidates = []
                indexed_route = route_index_by_source.get(route_key)
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
                            f"The installed route index contains multiple matches for '{route}'. "
                            "Keep the DLL at a distinct relative path or remove the duplicate."
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
                                f"Could not search MO2 layout DLLs for '{route}': {error}"
                            ) from error
                        unique_matches = {
                            str(path).casefold(): (virtual, path)
                            for virtual, path in matches
                        }
                        if len(unique_matches) > 1:
                            raise ValueError(
                                f"MO2 layout contains multiple DLLs named '{relative_path.name}'. "
                                "Keep the DLL at its declared folder or remove the duplicate."
                            )
                        if unique_matches:
                            resolved_candidates.append(next(iter(unique_matches.values())))

                if not resolved_candidates:
                    if optional:
                        qInfo(f"Elden Ring MO2: skipped optional Revamped native '{route}'.")
                        continue
                    raise ValueError(
                        f"Revamped native '{route}' was not found in the mapped MO2 mod '{mapped_mod}'. "
                        "Keep original structure may retain natives/...; MO2 layout must place it under DLLs/."
                    )

                virtual_path, _source_path = resolved_candidates[0]
                virtual_key = virtual_path.casefold()
                if virtual_key in seen_virtual_paths:
                    raise ValueError(
                        f"The Revamped native profile resolves more than one entry "
                        f"to '{virtual_path}'. Keep only one entry for that game path."
                    )
                seen_virtual_paths.add(virtual_key)
                try:
                    origins = self._organizer.getFileOrigins(virtual_path)
                except Exception as error:
                    raise ValueError(
                        f"MO2 could not resolve the winning origin for '{virtual_path}': {error}"
                    ) from error
                winner = next((name for name in active_mods if name in origins), None)
                if winner is None:
                    if optional:
                        continue
                    raise ValueError(
                        f"MO2 has no active file origin for Revamped native '{virtual_path}'."
                    )
                winning_mod = mod_list.getMod(winner)
                if winning_mod is None:
                    raise ValueError(
                        f"MO2's winning mod '{winner}' for '{virtual_path}' is unavailable."
                    )
                winning_root = Path(winning_mod.absolutePath()).resolve()
                library = winning_root.joinpath(*Path(virtual_path).parts).resolve(strict=True)
                try:
                    library.relative_to(winning_root)
                except ValueError as error:
                    raise ValueError(
                        f"The winning path for '{virtual_path}' leaves its MO2 mod."
                    ) from error
                if not library.is_file():
                    raise ValueError(f"The winning file for '{virtual_path}' is not a DLL.")

                initializer = native.get("initializer")
                if initializer is not None and not isinstance(initializer, dict):
                    raise ValueError(
                        f"Revamped native '{route}' has an unsupported initializer condition."
                    )
                initializer_function = initializer.get("function") if initializer else None
                if initializer and initializer_function is None:
                    raise ValueError(
                        f"Revamped native '{route}' uses an initializer condition this plugin does not support yet."
                    )
                if initializer_function is not None and (
                    not isinstance(initializer_function, str)
                    or not initializer_function.isascii()
                    or not initializer_function.isidentifier()
                ):
                    raise ValueError(f"Revamped native '{route}' has an unsupported initializer name.")
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
                            "NightreignMovement.dll needs its adjacent 'assets' folder. "
                            "Keep original structure retains it; MO2 layout must keep it beside the DLL under DLLs/."
                        )

                if virtual_path.casefold() != route_key:
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
                scope_priorities.add(active_mods.index(winner))
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
        if scope_root is None and not declared_libraries:
            return libraries
        scope_root = Path(scope_root) if scope_root is not None else None
        declared_names = profile_data.get("declared_names", set())
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
            retained.append(library_path)

        mod_list = self._organizer.modList()
        active_mods = [
            name
            for name in mod_list.allModsByProfilePriority()
            if mod_list.state(name) & mobase.ModState.ACTIVE
        ]
        scope_priority = int(profile_data.get("scope_priority", len(active_mods)))

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
        try:
            overwrite_path = Path(self._organizer.overwritePath())
            if overwrite_path.name.casefold() != "overwrite":
                raise ValueError("MO2 returned a path that is not named Overwrite")

            root_stat = os.lstat(overwrite_path)
            reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
            if getattr(root_stat, "st_file_attributes", 0) & reparse_flag:
                raise ValueError("the Overwrite directory is a reparse point")
            if not stat.S_ISDIR(root_stat.st_mode):
                raise ValueError("the Overwrite path is not a directory")

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
                    os.unlink(path)
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
    def _remove_overwrite_entry(path: Path) -> int:
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
            with os.scandir(path) as entries:
                for entry in entries:
                    removed += EldenRingMo2Game._remove_overwrite_entry(
                        Path(entry.path)
                    )
            os.rmdir(path)
            return removed + 1

        os.unlink(path)
        return 1

    def _clear_overwrite_contents(self, overwrite_path: Path) -> None:
        try:
            if overwrite_path.name.casefold() != "overwrite":
                raise ValueError("MO2 returned a path that is not named Overwrite")

            root_stat = os.lstat(overwrite_path)
            reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
            if getattr(root_stat, "st_file_attributes", 0) & reparse_flag:
                raise ValueError("the Overwrite directory is a reparse point")
            if not stat.S_ISDIR(root_stat.st_mode):
                raise ValueError("the Overwrite path is not a directory")

            removed = 0
            with os.scandir(overwrite_path) as entries:
                for entry in entries:
                    removed += self._remove_overwrite_entry(Path(entry.path))
            qInfo(
                "Elden Ring MO2: cleared "
                f"{removed} file(s) and folder(s) from Overwrite after the game closed."
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
        return QDir(str(Path(appdata) / "EldenRing"))

    def savesDirectory(self) -> QDir:
        return self.documentsDirectory()

    def executables(self) -> list[mobase.ExecutableInfo]:
        game_exe = QFileInfo(
            self.gameDirectory(),
            self.GameBinary,
        )
        executable = mobase.ExecutableInfo("ELDEN RING (MO2)", game_exe)
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
                "eldenring.exe", str(library)
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
                "eldenring.exe", str(library)
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
            ):
                continue

            try:
                relative_path = virtual_path.absolute().relative_to(data_root)
            except ValueError:
                # MO2 may return a resolved physical path. Preserve any
                # subdirectory below the searched managed root so support
                # DLLs can be recognized and excluded from forced loading.
                if virtual_path.is_absolute():
                    path_parts = virtual_path.parts
                    root_index = next(
                        (
                            index
                            for index, part in enumerate(path_parts)
                            if part.casefold() == search_directory.casefold()
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
