"""Offer a per-install layout choice for supported Elden Ring archives."""

from __future__ import annotations

from pathlib import Path
from typing import Optional, Union
import json
import os
import shutil
import uuid

import mobase
from PyQt6 import QtGui, QtWidgets
from PyQt6.QtCore import QTimer, Qt, qCritical, qInfo, qWarning


# PyQt passes Qt's log functions ASCII only: a message with any other
# character (a path with an accent, a Windows error in Spanish) raised
# UnicodeEncodeError and stopped the code that logged it. Such messages go
# through Python's logging, which MO2's Python plugin writes to the same log.
import logging  # noqa: E402
import sys  # noqa: E402


_QT_LOG_CALLS: dict = {}


def _safe_qt_log(qt_function, level):
    def log(message) -> None:
        text = "" if message is None else str(message)
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


def _flag(value) -> bool:
    """A setting read back from meta.ini arrives as the text "false"."""
    if isinstance(value, str):
        return value.strip().casefold() not in {"", "false", "0"}
    return bool(value)


class EldenRingArchiveInstaller(mobase.IPluginInstallerSimple):
    """Choose how supported Elden Ring archive contents are installed."""

    # Elden Ring and Nightreign share the archive layouts
    _game_short_names = frozenset({"eldenring", "nightreign"})
    _archive_wrapper_depth_limit = 16
    _default_game_plugin_name = "Elden Ring MO2 Support"
    _game_override_key = "_archive_layout_override"
    _game_cache_epoch_key = "_archive_layout_cache_epoch"
    _mod_choice_key = "preserve_archive_layout"
    _omit_txt_files_key = "omit_txt_files"
    _omit_md_files_key = "omit_md_files"
    _recognize_mod_engine_paths_key = "recognize_mod_engine_paths"
    _installed_native_route_index_key = "installed_native_route_index"
    # ELDEN RING Reforged: keep its release for its own launcher (online mode)
    _reforged_online_key = "reforged_online"
    _archive_preview_entry_limit = 250
    _supported_payload_extensions = {
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
        ".ffxbnd",
        ".flac",
        ".flver",
        ".fmg",
        ".hkx",
        ".hks",
        ".gfx",
        ".ini",
        ".json",
        ".matbin",
        ".mp3",
        ".mtd",
        ".msgbnd",
        ".ogg",
        ".lua",
        ".otf",
        ".partsbnd",
        ".png",
        ".tga",
        ".toml",
        ".tpf",
        ".tpfbdt",
        ".tpfbhd",
        ".wav",
        ".prev",
        ".wem",
        ".xml",
        ".yaml",
        ".yml",
    }

    def __init__(self):
        super().__init__()
        self._organizer: Optional[mobase.IOrganizer] = None
        # MO2 initializes standalone installers before a managed game is
        # guaranteed to exist. Use the game plugin's stable name as a fallback
        # and refresh it once Elden Ring is the active managed game.
        self._game_plugin_name = self._default_game_plugin_name
        self._default_preserve = False
        self._default_omit_txt = False
        self._default_omit_md = False
        self._default_recognize_mod_engine_paths = True
        self._default_mod_name: Optional[str] = None
        self._pending_choice: Optional[tuple[bool, bool, bool, bool]] = None
        self._pending_source_dll_routes: list[str] = []

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        qInfo("Elden Ring MO2: archive layout installer alpha.74 initialized.")
        return True

    def name(self) -> str:
        return "Elden Ring Archive Layout Installer"

    def localizedName(self) -> str:
        return self.tr("Elden Ring Archive Layout Installer")

    def author(self) -> str:
        return "ArialSenki"

    def description(self) -> str:
        return self.tr(
            "Choose how supported Elden Ring mod archives are organized during installation."
        )

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(0, 5, 0, 74)

    def requirements(self):
        return [mobase.PluginRequirementFactory.gameDependency({"ELDEN RING", "ELDEN RING NIGHTREIGN"})]

    def settings(self) -> list[mobase.PluginSetting]:
        # MO2's plugin manager already provides the enable/disable switch.
        # Avoid a second hidden setting that can silently disable this installer.
        return []

    def isActive(self) -> bool:
        organizer = self._organizer
        if organizer is None:
            return False
        game = organizer.managedGame()
        if game is None:
            return False
        if game.gameShortName().casefold() not in self._game_short_names:
            return False

        game_plugin_name = game.name()
        if game_plugin_name:
            self._game_plugin_name = game_plugin_name
        return True

    def priority(self) -> int:
        return 200

    def isManualInstaller(self) -> bool:
        return False

    def onInstallationStart(
        self,
        archive: str,
        reinstallation: bool,
        current_mod: Optional[mobase.IModInterface],
    ) -> None:
        self._pending_choice = None
        self._pending_source_dll_routes = []
        self._default_preserve = False
        self._default_omit_txt = False
        self._default_omit_md = False
        self._default_recognize_mod_engine_paths = True
        self._default_reforged_online = False
        self._pending_reforged_layout = None
        self._default_mod_name = None
        if self._organizer is not None:
            self._organizer.setPluginSetting(
                self._game_plugin_name, self._game_override_key, False
            )

        if current_mod is not None:
            self._default_mod_name = current_mod.name()
            settings = current_mod.pluginSettings(self.name())
            self._default_preserve = _flag(settings.get(self._mod_choice_key, False))
            self._default_omit_txt = _flag(settings.get(self._omit_txt_files_key, False))
            self._default_omit_md = _flag(settings.get(self._omit_md_files_key, False))
            self._default_recognize_mod_engine_paths = _flag(
                settings.get(self._recognize_mod_engine_paths_key, True)
            )
            self._default_reforged_online = _flag(
                settings.get(self._reforged_online_key, False)
            )

    def onInstallationEnd(
        self,
        result: mobase.InstallResult,
        new_mod: Optional[mobase.IModInterface],
    ) -> None:
        try:
            if (
                result == mobase.InstallResult.SUCCESS
                and new_mod is not None
                and self._pending_choice is not None
            ):
                preserve, omit_txt, omit_md, recognize_mod_engine_paths = (
                    self._pending_choice
                )
                settings_to_store = (
                    (self._mod_choice_key, preserve),
                    (self._omit_txt_files_key, omit_txt),
                    (self._omit_md_files_key, omit_md),
                    (
                        self._recognize_mod_engine_paths_key,
                        recognize_mod_engine_paths,
                    ),
                )
                if self._pending_reforged_layout is not None:
                    settings_to_store += (
                        (self._reforged_online_key, self._pending_reforged_layout["keep"]),
                    )
                    layout = self._pending_reforged_layout["layout"]
                    if layout is not None:
                        missing = self._link_reforged_release(Path(new_mod.absolutePath()), layout)
                        if missing:
                            settings_to_store = tuple(
                                (key, False if key == self._reforged_online_key else value)
                                for key, value in settings_to_store
                            )
                            QtWidgets.QMessageBox.warning(
                                self._parentWidget(),
                                "ELDEN RING Reforged",
                                self.tr(
                                    "{0} files of Reforged's online version could not be "
                                    "placed, so its launcher may refuse to start. Free some "
                                    "disk space and reinstall the mod."
                                ).format(missing),
                            )
                    elif not self._pending_reforged_layout["keep"]:
                        # merged over the online version with the switch off: its
                        # release would keep the program list on its launcher
                        online = Path(new_mod.absolutePath()) / "_ReforgedOnline"
                        if online.is_dir():
                            shutil.rmtree(online, ignore_errors=True)
                            qInfo(f"Elden Ring MO2: removed the online release of {new_mod.name()}.")
                installed_dll_routes = self._collect_installed_dll_routes(
                    Path(new_mod.absolutePath())
                )
                route_index = {
                    "version": 1,
                    "layout": "keep-original" if preserve else "mo2-standard",
                    "dlls": installed_dll_routes,
                    "source_to_installed": self._map_source_dll_routes(
                        self._pending_source_dll_routes, installed_dll_routes
                    ),
                }
                settings_to_store += (
                    (
                        self._installed_native_route_index_key,
                        json.dumps(route_index, ensure_ascii=False, separators=(",", ":")),
                    ),
                )
                store_results = [
                    new_mod.setPluginSetting(self.name(), key, value)
                    for key, value in settings_to_store
                ]
                stored = all(store_results)
                layout_name = (
                    "keep original layout"
                    if preserve
                    else "MO2 DLL organization"
                )
                if stored:
                    qInfo(
                        "Elden Ring MO2: saved per-mod archive layout for "
                        f"{new_mod.name()}: {layout_name}; omit .txt={omit_txt}, "
                        f"omit .md={omit_md}; Mod Engine path mapping preference="
                        f"{recognize_mod_engine_paths} (standard layout only); "
                        f"indexed {len(installed_dll_routes)} installed DLL route(s), "
                        f"mapped {len(route_index['source_to_installed'])} archive route(s)."
                    )
                else:
                    qCritical(
                        "Elden Ring MO2: could not save the per-mod archive "
                        f"layout for {new_mod.name()}."
                    )
                self._bump_archive_layout_cache_epoch()
            elif (
                result == mobase.InstallResult.SUCCESS
                and new_mod is not None
                and self._pending_choice is None
            ):
                # installed some other way (Manual, FOMOD): a replace brings the
                # old meta.ini back, and this window's choices no longer describe it
                new_mod.clearPluginSettings(self.name())
        finally:
            if self._organizer is not None:
                self._organizer.setPluginSetting(
                    self._game_plugin_name, self._game_override_key, False
                )
            self._pending_choice = None
            self._pending_source_dll_routes = []
            self._pending_reforged_layout = None

    @staticmethod
    def _link_reforged_release(mod_root: Path, layout: dict) -> int:
        """Hard-links the game files and natives back to their paths in the
        kept Reforged release, so its launcher checks the files it shipped
        without a second copy on disk (a copy where links are impossible).
        Returns how many files could not be placed."""
        online = mod_root / layout["online"]
        pairs: list[tuple[Path, Path]] = []
        for name in layout["mod_entries"]:
            source = mod_root / name
            target = online / "mod" / name
            if source.is_dir():
                for current, _directories, filenames in os.walk(source):
                    for filename in filenames:
                        path = Path(current) / filename
                        pairs.append((path, target / path.relative_to(source)))
            elif source.is_file():
                pairs.append((source, target))
        for name in layout["natives"]:
            pairs.append(
                (mod_root / "DLLs" / name, online / "internals" / "modengine" / "dll" / name)
            )
        linked = copied = missing = 0
        for source, target in pairs:
            try:
                # an update merged over the release: a link to the old file is
                # no longer the file MO2 runs
                if target.exists():
                    if os.path.samefile(source, target):
                        continue
                    target.unlink()
                target.parent.mkdir(parents=True, exist_ok=True)
                try:
                    os.link(source, target)
                    linked += 1
                except OSError:
                    shutil.copy2(source, target)
                    copied += 1
            except OSError as error:
                qCritical(
                    "Elden Ring MO2: could not place a Reforged file for its "
                    f"launcher: {target}: {error}"
                )
                missing += 1
        qInfo(
            "Elden Ring MO2: Reforged release for its launcher ready in "
            f"{online}: {linked} hard link(s), {copied} copy(ies), {missing} missing."
        )
        return missing

    @classmethod
    def _collect_tree_dll_routes(
        cls, filetree: mobase.IFileTree, prefix: str = ""
    ) -> list[str]:
        routes: list[str] = []
        for entry in filetree:
            route = f"{prefix}/{entry.name()}" if prefix else entry.name()
            if entry.isDir():
                routes.extend(cls._collect_tree_dll_routes(entry, route))
            elif entry.isFile() and Path(entry.name()).suffix.casefold() == ".dll":
                routes.append(route.replace("\\", "/"))
        return routes

    @staticmethod
    def _collect_installed_dll_routes(mod_root: Path) -> list[str]:
        """Index DLLs at their final mod-relative paths after installation."""
        try:
            mod_root = mod_root.resolve(strict=True)
            if not mod_root.is_dir():
                return []
        except OSError:
            return []

        routes: list[str] = []
        for current, directories, filenames in os.walk(mod_root, followlinks=False):
            current_path = Path(current)
            directories[:] = [
                directory
                for directory in directories
                if not (current_path / directory).is_symlink()
                # the Reforged release kept for its own launcher (me3's DLLs)
                and not (current_path == mod_root and directory == "_ReforgedOnline")
            ]
            for filename in filenames:
                if Path(filename).suffix.casefold() != ".dll":
                    continue
                path = current_path / filename
                try:
                    resolved = path.resolve(strict=True)
                    resolved.relative_to(mod_root)
                except (OSError, ValueError):
                    continue
                if not resolved.is_file() or path.is_symlink():
                    continue
                routes.append(resolved.relative_to(mod_root).as_posix())
        return sorted(set(routes), key=str.casefold)

    @staticmethod
    def _map_source_dll_routes(
        source_routes: list[str], installed_routes: list[str]
    ) -> dict[str, str]:
        """Map declared archive paths to unambiguous installed paths."""
        installed_by_key = {route.casefold(): route for route in installed_routes}
        source_counts: dict[str, int] = {}
        installed_counts: dict[str, list[str]] = {}
        for route in source_routes:
            source_counts[Path(route).name.casefold()] = (
                source_counts.get(Path(route).name.casefold(), 0) + 1
            )
        for route in installed_routes:
            installed_counts.setdefault(Path(route).name.casefold(), []).append(route)

        mapping: dict[str, str] = {}
        for source in source_routes:
            source = source.replace("\\", "/").strip("/")
            parts = Path(source).parts
            aliases = [source]
            for index, part in enumerate(parts):
                if part.casefold() in {"native", "natives"}:
                    aliases.append(Path(*parts[index:]).as_posix())

            exact_candidates: list[str] = []
            for alias in aliases:
                exact_candidates.append(alias)
                exact_candidates.append(f"DLLs/{alias}")
                alias_parts = Path(alias).parts
                if alias_parts and alias_parts[0].casefold() in {"native", "natives"}:
                    tail = Path(*alias_parts[1:]).as_posix()
                    if tail:
                        exact_candidates.extend((tail, f"DLLs/{tail}"))
            exact_matches = {
                installed_by_key[candidate.casefold()]
                for candidate in exact_candidates
                if candidate.casefold() in installed_by_key
            }
            if len(exact_matches) == 1:
                mapping[source] = next(iter(exact_matches))
                continue

            suffix_matches = {
                installed
                for installed in installed_routes
                if any(
                    installed.casefold().endswith("/" + alias.casefold())
                    for alias in aliases
                )
            }
            if len(suffix_matches) == 1:
                mapping[source] = next(iter(suffix_matches))
                continue

            basename = Path(source).name.casefold()
            basename_matches = installed_counts.get(basename, [])
            if source_counts.get(basename) == 1 and len(basename_matches) == 1:
                mapping[source] = basename_matches[0]

        return mapping

    def _bump_archive_layout_cache_epoch(self) -> None:
        organizer = self._organizer
        if organizer is None:
            return
        try:
            organizer.setPluginSetting(
                self._game_plugin_name,
                self._game_cache_epoch_key,
                uuid.uuid4().hex,
            )
            qInfo(
                "Elden Ring MO2: invalidated cached archive-layout data checks "
                "after saving the mod's layout choice."
            )
        except Exception as error:
            qCritical(
                "Elden Ring MO2: could not invalidate cached archive-layout "
                f"data checks after installation ({error})."
            )

    _game_file_names = frozenset(
        {
            "eldenring.exe",
            "nightreign.exe",
            "start_protected_game.exe",
            "steam_api64.dll",
            "regulation.bin",
        }
    )

    def _game_files_in(self, filetree: mobase.IFileTree, depth: int = 0) -> set[str]:
        found: set[str] = set()
        if depth > self._archive_wrapper_depth_limit:
            return found
        for entry in filetree:
            if entry.isDir():
                found |= self._game_files_in(entry, depth + 1)
            elif entry.name().casefold() in self._game_file_names:
                found.add(entry.name().casefold())
        return found

    # folders that are part of a layout, never one option among several
    _layout_folder_names = frozenset(
        {
            "action", "animation", "animations", "asset", "assets", "chr",
            "common", "cutscene", "event", "facegen", "map", "material", "menu",
            "msg", "movie", "movie_dlc", "param", "parts", "script", "sd", "sfx",
            "shader", "sound", "system", "texture", "textures", "time", "wp",
            "config", "configs", "dlls", "external_dlls", "fonts", "mod", "mods",
            "mo2_dlls", "native", "natives", "plugins", "seamlesscoop",
            "modengine2", "bin", "locale", "game", "elden ring", "eldenring", "nightreign",
            "elden ring nightreign", "elden-ring-nightreign", "eldenring nightreign",
            "elden_ring_nightreign", "drag files into here", "fomod",
        }
    )

    def _duplicate_dll_names(self, filetree: mobase.IFileTree) -> list[str]:
        # Windows names are case-insensitive: Foo.dll and foo.dll are one DLL
        folders_by_name: dict[str, set[str]] = {}
        shown_names: dict[str, str] = {}

        def walk(tree: mobase.IFileTree, path: str, depth: int) -> None:
            if depth > self._archive_wrapper_depth_limit:
                return
            for entry in tree:
                if entry.isDir():
                    walk(entry, f"{path}/{entry.name()}".casefold(), depth + 1)
                elif entry.name().casefold().endswith(".dll"):
                    key = entry.name().casefold()
                    shown_names.setdefault(key, entry.name())
                    folders_by_name.setdefault(key, set()).add(path)

        walk(filetree, "", 0)
        return sorted(
            (
                shown_names[key]
                for key, folders in folders_by_name.items()
                if len(folders) > 1
            ),
            key=str.casefold,
        )

    def _option_folders(self, filetree: mobase.IFileTree) -> list[str]:
        level = filetree
        for _ in range(self._archive_wrapper_depth_limit):
            folders = [entry for entry in level if entry.isDir()]
            files = [
                entry
                for entry in level
                if entry.isFile()
                and f".{entry.name().rsplit('.', 1)[-1].casefold()}"
                in self._supported_payload_extensions
            ]
            # lift a lone wrapper folder (the archive's own top folder)
            if len(folders) == 1 and not files:
                if folders[0].name().casefold() in self._layout_folder_names:
                    return []
                level = folders[0]
                continue
            options = [
                folder.name()
                for folder in folders
                if folder.name().casefold() not in self._layout_folder_names
                and self._contains_supported_payload(folder)
            ]
            return options if len(options) > 1 else []
        return []

    def isArchiveSupported(self, tree: mobase.IFileTree) -> bool:
        organizer = self._organizer
        if organizer is None or not self.isActive():
            return False

        # Leave archives with their own scripted installer to that installer.
        if tree.find("fomod/ModuleConfig.xml", mobase.FileTreeEntry.FILE):
            return False
        # MO2's FOMOD installer also finds one inside a top folder that is
        # the archive's only entry
        folders = [entry for entry in tree if entry.isDir()]
        if len(folders) == 1 and len(tree) == 1 and folders[0].find(
            "fomod/ModuleConfig.xml", mobase.FileTreeEntry.FILE
        ):
            return False

        supported = self._contains_supported_payload(tree)
        if supported:
            qInfo(
                "Elden Ring MO2: archive layout installer recognized supported "
                "Elden Ring payload; installation choice will be shown."
            )
        return supported

    def _contains_supported_payload(
        self, filetree: mobase.IFileTree, depth: int = 0
    ) -> bool:
        if depth > self._archive_wrapper_depth_limit:
            return False
        for entry in filetree:
            if entry.isDir():
                if self._contains_supported_payload(entry, depth + 1):
                    return True
            elif entry.isFile():
                suffix = entry.name().rsplit(".", 1)
                if len(suffix) == 2:
                    extension = f".{suffix[1].casefold()}"
                    if extension in self._supported_payload_extensions:
                        return True
        return False

    def install(
        self,
        name: mobase.GuessedString,
        tree: mobase.IFileTree,
        version: str,
        nexus_id: int,
    ) -> Union[mobase.InstallResult, mobase.IFileTree]:
        organizer = self._organizer
        if organizer is None:
            return mobase.InstallResult.NOT_ATTEMPTED

        default_mod_name = self._default_mod_name or str(name)
        try:
            from basic_games.games.game_eldenring_mo2 import (
                EldenRingModDataChecker,
            )

            package = EldenRingModDataChecker.known_package(tree)
        except Exception:
            package = None
        reforged = package == "reforged"
        qInfo(
            f"Elden Ring MO2: opening the install window for {default_mod_name}; "
            f"recognized package: {package or 'none'}."
        )
        choice = self._ask_archive_layout(default_mod_name, tree, package)
        keep_reforged_online = self._chosen_reforged_online
        if choice is None:
            return mobase.InstallResult.CANCELED
        if choice == "manual":
            return mobase.InstallResult.MANUAL_REQUESTED

        (
            preserve,
            omit_txt,
            omit_md,
            recognize_mod_engine_paths,
            selected_mod_name,
        ) = choice
        selected_mod_name = selected_mod_name.strip() or default_mod_name
        if selected_mod_name and selected_mod_name != str(name):
            name.update(selected_mod_name, mobase.GuessQuality.USER)
        self._pending_source_dll_routes = self._collect_tree_dll_routes(tree)
        self._pending_choice = (
            preserve,
            omit_txt,
            omit_md,
            recognize_mod_engine_paths,
        )
        organizer.setPluginSetting(
            self._game_plugin_name, self._game_override_key, preserve
        )

        # Both choices peel only redundant outer wrappers. Keep original leaves
        # payload paths and files alone; standard layout also routes native
        # plugins into Game/DLLs.
        try:
            tree, checker = self._apply_layout(
                tree, preserve, omit_txt, omit_md, reforged and keep_reforged_online
            )
            if reforged and not preserve:
                self._pending_reforged_layout = {
                    "keep": keep_reforged_online,
                    "layout": getattr(checker, "reforged_layout", None),
                }
        except Exception as error:
            qCritical(
                "Elden Ring MO2: could not apply the selected archive layout "
                f"for {str(name)}: {error}"
            )
            return mobase.InstallResult.FAILED

        if not preserve:
            qInfo(
                "Elden Ring MO2: applied standard MO2 layout to the archive "
                "tree before installation for "
                f"{str(name)}."
            )

        return tree

    @classmethod
    def _remove_files_with_extension(
        cls, filetree: mobase.IFileTree, extension: str
    ) -> int:
        removed = 0
        from basic_games.games.game_eldenring_mo2 import EldenRingModDataChecker

        for entry in list(filetree):
            if entry.isDir():
                # text inside a game folder is game data (action/*.txt)
                if entry.name().casefold() in EldenRingModDataChecker._asset_directories:
                    continue
                removed += cls._remove_files_with_extension(entry, extension)
                if len(entry) == 0:
                    entry.detach()
            elif entry.name().casefold().endswith(extension) and entry.name().casefold() != "load.txt":
                entry.detach()
                removed += 1
        return removed

    def _apply_layout(
        self,
        tree: mobase.IFileTree,
        preserve: bool,
        omit_txt: bool,
        omit_md: bool,
        keep_reforged_online: bool,
        previewing: bool = False,
    ):
        """Arranges the tree as the mod will be installed; the window's summary
        runs this same code on a copy. Returns the tree and the checker."""
        from basic_games.games.game_eldenring_mo2 import EldenRingModDataChecker

        for extension, omit in ((".txt", omit_txt), (".md", omit_md)):
            if omit:
                self._remove_files_with_extension(tree, extension)
        checker = EldenRingModDataChecker(
            self._organizer,
            self._game_plugin_name,
            preserve_extensions=tuple(
                extension
                for extension, omit in ((".txt", omit_txt), (".md", omit_md))
                if not omit
            ),
        )
        checker.previewing = previewing
        if preserve:
            tree = checker.normalize_preserved_layout(tree)
        else:
            # a mod's mod/ folder (the usual packaging) always maps to the game
            # folders in the standard layout
            tree = checker.normalize_standard_layout(
                tree,
                recognize_mod_engine_paths=True,
                keep_reforged_online=keep_reforged_online,
            )
        return tree, checker

    @classmethod
    def _file_paths(cls, tree: mobase.IFileTree, prefix: str = "") -> list[str]:
        paths = []
        for entry in tree:
            path = f"{prefix}/{entry.name()}" if prefix else entry.name()
            if entry.isDir():
                paths.extend(cls._file_paths(entry, path))
            else:
                paths.append(path)
        return paths

    def _layout_summary(
        self,
        tree: mobase.IFileTree,
        preserve: bool,
        omit_txt: bool,
        omit_md: bool,
        keep_reforged_online: bool,
    ) -> Optional[dict]:
        """What the install would give, from a copy of the archive's tree."""
        try:
            from basic_games.games.game_eldenring_mo2 import EldenRingModDataChecker

            source_files = len(self._file_paths(tree))
            copy = tree.createOrphanTree()
            for entry in tree:
                copy.copy(entry, "", mobase.IFileTree.InsertPolicy.FAIL_IF_EXISTS)
            result, _checker = self._apply_layout(
                copy, preserve, omit_txt, omit_md, keep_reforged_online, previewing=True
            )
            paths = self._file_paths(result)
        except Exception as error:
            # the summary is a courtesy: the install does not depend on it
            qWarning(f"Elden Ring MO2: could not preview the archive layout: {error}")
            return None

        online_folder = EldenRingModDataChecker.ReforgedOnlineDirectory.casefold()
        game_folders = EldenRingModDataChecker._asset_directories
        summary = {
            "game": 0, "dlls": 0, "dlls_routed": 0, "online": 0, "other": 0,
            "regulation": False, "paths": paths, "misplaced": 0, "misplaced_in": "",
        }
        summary["left_out"] = max(0, source_files - len(paths))
        for path in paths:
            parts = path.casefold().split("/")
            # game folders below another folder: the game looks for them at its
            # root, so it wouldn't read these
            if (
                preserve
                and parts[0] not in game_folders
                and parts[0] not in {online_folder, "dlls", "seamlesscoop", "native", "natives", "mods"}
                and any(part in game_folders for part in parts[1:-1])
            ):
                summary["misplaced"] += 1
                summary["misplaced_in"] = summary["misplaced_in"] or path.split("/")[0]
            if parts[0] == online_folder:
                summary["online"] += 1
            elif parts[-1].endswith(".dll"):
                summary["dlls"] += 1
                summary["dlls_routed"] += parts[0] == "dlls"
            elif parts[0] == "dlls":
                # a DLL's settings, beside it
                continue
            elif parts == ["regulation.bin"]:
                summary["regulation"] = True
            elif len(parts) > 1 and parts[0] in game_folders:
                summary["game"] += 1
            else:
                summary["other"] += 1
        return summary

    def _other_mods_with_regulation(self) -> list[str]:
        names = []
        try:
            mod_list = self._organizer.modList()
            for name in mod_list.allModsByProfilePriority():
                if name == self._default_mod_name:
                    continue
                if not mod_list.state(name) & mobase.ModState.ACTIVE:
                    continue
                mod = mod_list.getMod(name)
                if mod is not None and (Path(mod.absolutePath()) / "regulation.bin").is_file():
                    names.append(name)
        except (AttributeError, OSError, RuntimeError):
            return []
        return names

    def _mod_name_taken(self, name: str) -> bool:
        try:
            wanted = name.strip().casefold()
            return any(mod.casefold() == wanted for mod in self._organizer.modList().allMods())
        except (AttributeError, RuntimeError):
            return False

    def _ask_archive_layout(
        self, mod_name: str, archive_tree: mobase.IFileTree, package: Optional[str] = None
    ) -> Optional[tuple[bool, bool, bool, bool, str]]:
        self._chosen_reforged_online = False
        # what the window says about a mod it knows; these two run in one layout
        # only, and their .txt files are game files, so nothing else is offered
        package_names = {
            "reforged": "ELDEN RING Reforged",
            "convergence": "Convergence",
            "seamless": "Seamless Co-op",
            "frzn": "FRZN Merge",
        }
        package_texts = {
            "reforged": self.tr(
                "MO2 runs it itself, with your other mods: its game files and natives "
                "go to MO2's folders; its launcher, me3 and setup tool stay out."
            ),
            "convergence": self.tr(
                "MO2 runs it itself, with your other mods: its game files and natives "
                "go to MO2's folders; me3, its launcher and the logs it ships stay out."
            ),
            "seamless": self.tr(
                "Its SeamlessCoop folder stays whole, and it runs from MO2's own "
                "program, with your other mods."
            ),
            "frzn": self.tr(
                "The contents of 'Drag Files Into Here' go to the game root, and its "
                "natives keep their paths."
            ),
        }
        single_layout = package in {"reforged", "convergence"}
        # a reinstall keeps what was chosen; a new install starts from what the
        # mod needs
        start_preserve = (
            False
            if single_layout
            else self._default_preserve
            if self._default_mod_name or not package
            else package == "frzn"
        )

        parent = self._parentWidget()
        dialog = QtWidgets.QDialog(parent)
        dialog.setWindowTitle(self.tr("Install a mod"))
        dialog.setObjectName("EldenRingArchiveLayoutDialog")
        dialog.setSizeGripEnabled(True)
        dialog.setModal(True)

        layout = QtWidgets.QVBoxLayout(dialog)
        layout.setObjectName("archiveDialogLayout")
        # no right margin: the content scrolls at the window's edge, as in
        # Windows 11, and keeps its gap from it inside the scroll area
        layout.setContentsMargins(16, 16, 0, 14)
        layout.setSpacing(10)
        layout.setSizeConstraint(QtWidgets.QLayout.SizeConstraint.SetNoConstraint)

        scroll_area = QtWidgets.QScrollArea(dialog)
        scroll_area.setObjectName("archiveContentScrollArea")
        scroll_area.setWidgetResizable(True)
        scroll_area.setFrameShape(QtWidgets.QFrame.Shape.NoFrame)
        scroll_area.setHorizontalScrollBarPolicy(
            Qt.ScrollBarPolicy.ScrollBarAlwaysOff
        )
        scroll_area.setMinimumSize(0, 0)
        content = QtWidgets.QWidget(scroll_area)
        content.setObjectName("archiveDialogContent")
        content_layout = QtWidgets.QVBoxLayout(content)
        content_layout.setContentsMargins(0, 0, 16, 0)
        content_layout.setSpacing(10)
        scroll_area.setWidget(content)
        layout.addWidget(scroll_area, 1)

        # a Windows 11 page: its title and text, then cards of rows, as in
        # MO2's Settings and the Save Isolation and Startup options dialogs
        heading = QtWidgets.QLabel(
            self.tr("Detected: {0}").format(package_names[package])
            if package
            else self.tr("Install this mod"),
            content,
        )
        heading.setObjectName("archivePageTitle")
        heading.setProperty("wizardPageTitle", True)
        heading.setWordWrap(True)
        content_layout.addWidget(heading)
        subtitle = QtWidgets.QLabel(
            package_texts[package]
            if package
            else self.tr(
                "MO2 finds the mod's files inside the archive and puts them in its "
                "standard folders."
            ),
            content,
        )
        subtitle.setObjectName("archivePageSubtitle")
        subtitle.setProperty("wizardPageText", True)
        subtitle.setWordWrap(True)
        content_layout.addWidget(subtitle)
        content_layout.addSpacing(4)

        glyph_font = QtGui.QFont()
        glyph_font.setFamilies(["Segoe Fluent Icons", "Segoe MDL2 Assets"])
        glyph_font.setPixelSize(16)
        # where the next card goes: the page, or its advanced part
        cards_layout = [content_layout]

        def add_card(object_name: str, title: str = "") -> QtWidgets.QFrame:
            card = QtWidgets.QFrame(content)
            card.setObjectName(object_name)
            card.setProperty("fluentCard", True)
            card_layout = QtWidgets.QVBoxLayout(card)
            card_layout.setContentsMargins(0, 10 if title else 0, 0, 0)
            card_layout.setSpacing(0)
            if title:
                card_title = QtWidgets.QLabel(title, card)
                card_title.setObjectName("fluentCardTitle")
                card_title.setContentsMargins(18, 0, 18, 8)
                card_layout.addWidget(card_title)
            cards_layout[0].addWidget(card)
            return card

        def add_row(
            card: QtWidgets.QFrame,
            glyph: int,
            title: str,
            description: str = "",
            controls: tuple = (),
            host: Optional[QtWidgets.QBoxLayout] = None,
        ) -> QtWidgets.QFrame:
            row = QtWidgets.QFrame(card)
            row.setObjectName("fluentSettingRow")
            row_layout = QtWidgets.QHBoxLayout(row)
            row_layout.setContentsMargins(18, 10, 14, 10)
            row_layout.setSpacing(14)
            icon = QtWidgets.QLabel(chr(glyph), row)
            icon.setObjectName("instanceRowGlyph")
            icon.setFont(glyph_font)
            icon.setFixedWidth(24)
            icon.setAlignment(Qt.AlignmentFlag.AlignCenter)
            row_layout.addWidget(icon, 0, Qt.AlignmentFlag.AlignTop)
            texts = QtWidgets.QVBoxLayout()
            texts.setSpacing(2)
            title_label = QtWidgets.QLabel(title, row)
            title_label.setObjectName("fluentSettingTitle")
            title_label.setWordWrap(True)
            texts.addWidget(title_label)
            if description:
                description_label = QtWidgets.QLabel(description, row)
                description_label.setObjectName("fluentSettingDescription")
                description_label.setWordWrap(True)
                texts.addWidget(description_label)
            row_layout.addLayout(texts, 1)
            for control in controls:
                control.setParent(row)
                row_layout.addWidget(control, 0, Qt.AlignmentFlag.AlignVCenter)
            (host if host is not None else card.layout()).addWidget(row)
            return row

        def switch(box: QtWidgets.QCheckBox) -> QtWidgets.QCheckBox:
            # the theme draws a check box with this property as a switch; its
            # row shows the text
            box.setAccessibleName(box.text())
            box.setText("")
            box.setProperty("fluentSwitch", True)
            box.setCursor(Qt.CursorShape.PointingHandCursor)
            return box

        def notice_label(object_name: str) -> QtWidgets.QLabel:
            label = QtWidgets.QLabel(content)
            label.setObjectName(object_name)
            label.setWordWrap(True)
            label.setStyleSheet(
                f"QLabel#{object_name} {{ background: rgba(255, 185, 0, 0.14);"
                " border: 1px solid rgba(255, 185, 0, 0.45); border-radius: 6px;"
                " padding: 8px 10px; }"
            )
            label.setVisible(False)
            return label

        # --- the mod ---------------------------------------------------------
        name_card = add_card("archiveNameCard")
        mod_name_edit = QtWidgets.QLineEdit(mod_name)
        mod_name_edit.setObjectName("archiveModNameEdit")
        mod_name_edit.setClearButtonEnabled(True)
        mod_name_edit.setMinimumWidth(320)
        name_help = self.tr(
            "This is how the mod will appear in MO2. It does not rename "
            "the archive or downloaded file."
        )
        mod_name_edit.setToolTip(name_help)
        add_row(
            name_card, 0xE8AC, self.tr("Mod name:").rstrip(": "), name_help, (mod_name_edit,)
        )
        reforged_online_box = None
        if package == "reforged":
            reforged_online_box = switch(
                QtWidgets.QCheckBox(self.tr("Install as the online version"))
            )
            reforged_online_box.setObjectName("archiveReforgedOnlineCheck")
            reforged_online_box.setChecked(self._default_reforged_online)
            add_row(
                name_card, 0xE774, reforged_online_box.accessibleName(),
                self.tr(
                    "While this mod is on, MO2 starts Reforged's own launcher and the "
                    "program can't be changed; online, Reforged allows no other mods "
                    "or DLLs. For offline play with your other mods, install Reforged "
                    "again with this off, as a separate mod.\n\n"
                    "Reinstall the mod to change this."
                ),
                (reforged_online_box,),
            )

            # two versions are two mods: a name of its own, so neither replaces
            # the other
            def name_online_version(online: bool) -> None:
                name = mod_name_edit.text()
                marked = name.casefold().endswith(" online")
                if online and not marked:
                    mod_name_edit.setText(f"{name} Online")
                elif not online and marked:
                    mod_name_edit.setText(name[: -len(" online")])

            reforged_online_box.toggled.connect(name_online_version)
            if self._default_reforged_online:
                name_online_version(True)

        # --- what will be installed ------------------------------------------
        summary_card = add_card("archiveSummaryCard", self.tr("What will be installed"))
        summary_host = QtWidgets.QVBoxLayout()
        summary_host.setSpacing(0)
        summary_card.layout().addLayout(summary_host)
        preview_button = QtWidgets.QPushButton(self.tr("Show"))
        preview_button.setObjectName("archivePreviewButton")
        preview_button.setCheckable(True)
        preview_button.setAccessibleName(self.tr("Show all files"))
        add_row(summary_card, 0xE8B9, self.tr("All files"), "", (preview_button,))
        preview_note = QtWidgets.QLabel(summary_card)
        preview_note.setObjectName("fluentSettingDescription")
        preview_note.setWordWrap(True)
        preview_note.setContentsMargins(18, 4, 18, 4)
        preview_tree = QtWidgets.QTreeWidget(summary_card)
        preview_tree.setObjectName("archivePreviewTree")
        preview_tree.setHeaderHidden(True)
        preview_tree.setAlternatingRowColors(True)
        preview_tree.setFrameShape(QtWidgets.QFrame.Shape.NoFrame)
        preview_tree.setMinimumHeight(190)
        preview_tree.setMaximumHeight(260)
        preview_tree.setUniformRowHeights(True)
        preview_tree.setVisible(False)
        preview_note.setVisible(False)
        summary_card.layout().addWidget(preview_note)
        summary_card.layout().addWidget(preview_tree)

        # --- what the user should know ---------------------------------------
        layout_notice = notice_label("archiveLayoutNotice")
        content_layout.addWidget(layout_notice)
        regulation_notice = notice_label("archiveRegulationNotice")
        content_layout.addWidget(regulation_notice)
        name_notice = notice_label("archiveNameNotice")
        content_layout.addWidget(name_notice)

        notice_lines = []
        if package in {"reforged", "convergence"} and self._nightreign_active():
            notice_lines.append(
                self.tr("This is an Elden Ring mod: this instance manages Nightreign.")
            )
        # game files a mod rarely means to replace
        executables = sorted(self._game_files_in(archive_tree) - {"regulation.bin"})
        if executables:
            notice_lines.append(
                self.tr(
                    "This archive contains game files ({0}). Installed through "
                    "MO2 they replace the game's own copies; most mods do not "
                    "need that."
                ).format(", ".join(executables))
            )
        # several copies of one DLL, or several option folders: both layouts
        # would install every copy, and the game would use only one or none
        # (a mod the window knows is laid out on purpose: nothing to warn about)
        duplicate_dlls = [] if package else self._duplicate_dll_names(archive_tree)
        if duplicate_dlls:
            notice_lines.append(
                self.tr(
                    "Several folders contain {0}. Only one copy of a DLL can load; "
                    "if they are alternatives, choose Manual... and keep the one "
                    "you want."
                ).format(", ".join(duplicate_dlls))
            )
        option_folders = [] if package else self._option_folders(archive_tree)
        if option_folders:
            notice_lines.append(
                self.tr(
                    "This archive looks like it offers options ({0}). Both "
                    "layouts install all of them; choose Manual... to keep only "
                    "the one you want."
                ).format(", ".join(option_folders))
            )
        if notice_lines:
            archive_notice = notice_label("archiveGameFileNotice")
            archive_notice.setText("\n\n".join(notice_lines))
            archive_notice.setVisible(True)
            content_layout.addWidget(archive_notice)

        # --- advanced --------------------------------------------------------
        advanced_card = add_card("archiveAdvancedCard")
        advanced_button = QtWidgets.QPushButton(self.tr("Show"))
        advanced_button.setObjectName("archiveAdvancedButton")
        advanced_button.setCheckable(True)
        advanced_button.setAccessibleName(self.tr("Show advanced options"))
        add_row(
            advanced_card, 0xE713, self.tr("Advanced options"),
            self.tr("Skip text files, or keep the archive's own structure."),
            (advanced_button,),
        )
        advanced = QtWidgets.QWidget(content)
        advanced.setObjectName("archiveAdvancedOptions")
        advanced_layout = QtWidgets.QVBoxLayout(advanced)
        advanced_layout.setContentsMargins(0, 0, 0, 0)
        advanced_layout.setSpacing(10)
        advanced.setVisible(False)
        content_layout.addWidget(advanced)
        cards_layout[0] = advanced_layout

        layout_card = add_card("archiveLayoutCard", self.tr("Layout"))
        layout_group = QtWidgets.QButtonGroup(dialog)
        standard_radio = QtWidgets.QRadioButton()
        standard_radio.setObjectName("archiveStandardLayoutRadio")
        standard_radio.setAccessibleName(self.tr("Organize for MO2 (recommended)"))
        preserve_radio = QtWidgets.QRadioButton()
        preserve_radio.setObjectName("archivePreserveLayoutRadio")
        preserve_radio.setAccessibleName(self.tr("Install as it comes in the archive"))
        layout_group.addButton(standard_radio)
        layout_group.addButton(preserve_radio)
        (preserve_radio if start_preserve else standard_radio).setChecked(True)
        add_row(
            layout_card, 0xE8FD, self.tr("Organize for MO2 (recommended)"),
            self.tr(
                "Finds mod files inside wrappers and routes DLLs, settings, fonts, "
                "and game assets to MO2's standard folders."
            ),
            (standard_radio,),
        )
        add_row(
            layout_card, 0xE8B7, self.tr("Install as it comes in the archive"),
            self.tr(
                "Removes only the archive's outer folders and keeps every other path. "
                "For a mod whose instructions need its files in exact places."
            ),
            (preserve_radio,),
        )

        options_card = add_card("archiveTextFilesCard", self.tr("Text files"))
        omit_txt_box = switch(QtWidgets.QCheckBox(self.tr("Skip .txt files")))
        omit_txt_box.setObjectName("archiveSkipTxtCheck")
        omit_txt_box.setChecked(self._default_omit_txt and not single_layout)
        add_row(
            options_card, 0xE8A5, self.tr("Skip .txt files"),
            self.tr("Skips every .txt file in this archive before installation."),
            (omit_txt_box,),
        )
        omit_md_box = switch(QtWidgets.QCheckBox(self.tr("Skip .md files")))
        omit_md_box.setObjectName("archiveSkipMarkdownCheck")
        omit_md_box.setChecked(self._default_omit_md and not single_layout)
        add_row(
            options_card, 0xE8A5, self.tr("Skip .md files"),
            self.tr("Skips every Markdown .md file in this archive before installation."),
            (omit_md_box,),
        )
        if single_layout:
            advanced_card.setVisible(False)
        content_layout.addStretch(1)

        # --- the summary follows the choices ---------------------------------
        selected: dict = {"choice": None}
        summary_state: dict = {"paths": []}

        def current_choices() -> tuple[bool, bool, bool, bool]:
            preserve = preserve_radio.isChecked()
            return (
                preserve,
                omit_txt_box.isChecked(),
                omit_md_box.isChecked(),
                reforged_online_box is not None and reforged_online_box.isChecked(),
            )

        def clear_summary() -> None:
            while summary_host.count():
                widget = summary_host.takeAt(0).widget()
                if widget is not None:
                    widget.setParent(None)
                    widget.deleteLater()

        def summary_row(glyph: int, title: str, value: str, description: str = "") -> None:
            value_label = QtWidgets.QLabel(value)
            value_label.setObjectName("fluentSettingDescription")
            add_row(summary_card, glyph, title, description, (value_label,), summary_host)

        def refresh_summary() -> None:
            preserve, omit_txt, omit_md, online = current_choices()
            clear_summary()
            summary = self._layout_summary(archive_tree, preserve, omit_txt, omit_md, online)
            summary_state["paths"] = summary["paths"] if summary else []
            preview_button.setChecked(False)
            preview_state["populated"] = False
            if summary is None:
                summary_row(0xE7BA, self.tr("The summary isn't available"), "")
                regulation_notice.setVisible(False)
                layout_notice.setVisible(False)
                return
            layout_notice.setVisible(summary["misplaced"] > 0)
            if summary["misplaced"]:
                layout_notice.setText(
                    self.tr(
                        "{0} files would stay inside the folder {1}, where the game doesn't "
                        "look for them. Organize for MO2 puts them in place."
                    ).format(summary["misplaced"], summary["misplaced_in"])
                )

            def files(count: int) -> str:
                return self.tr("1 file") if count == 1 else self.tr("{0} files").format(count)

            if summary["game"]:
                summary_row(0xE8B7, self.tr("Game files"), files(summary["game"]))
            if summary["dlls"]:
                summary_row(
                    0xE943,
                    self.tr("Native DLLs"),
                    self.tr("{0} in DLLs").format(summary["dlls"])
                    if summary["dlls_routed"] == summary["dlls"]
                    else self.tr("{0} DLL files").format(summary["dlls"]),
                )
            if summary["regulation"]:
                summary_row(0xE713, "regulation.bin", self.tr("At the game root"))
            if summary["online"]:
                summary_row(
                    0xE774, self.tr("Reforged's online version"), files(summary["online"])
                )
            if summary["other"]:
                summary_row(0xE8A5, self.tr("Other files"), files(summary["other"]))
            if summary["left_out"]:
                summary_row(
                    0xE711, self.tr("Left out"), files(summary["left_out"]),
                    self.tr("Files MO2 doesn't need, such as launchers, or those you chose to skip."),
                )
            if not summary["paths"] or not (summary["game"] or summary["dlls"] or summary["regulation"]):
                summary_row(
                    0xE7BA, self.tr("No game files or DLLs found"), "",
                    self.tr("This archive may be for another game. Manual... lets you choose its files."),
                )
            others = self._other_mods_with_regulation() if summary["regulation"] else []
            regulation_notice.setVisible(bool(others))
            if others:
                regulation_notice.setText(
                    self.tr(
                        "Your active mods also bring regulation.bin ({0}). Only one can "
                        "provide it: the one with the highest priority wins."
                    ).format(", ".join(others))
                )
            refit_window()

        def refresh_name_notice() -> None:
            # a reinstall keeps its own name: that's expected
            name = mod_name_edit.text().strip()
            taken = name != (self._default_mod_name or "") and self._mod_name_taken(name)
            changed = taken != name_notice.isVisibleTo(content)
            name_notice.setVisible(taken)
            if taken:
                name_notice.setText(
                    self.tr("You already have a mod with this name. MO2 will ask whether to merge or replace it.")
                )
            if changed:
                refit_window()

        preview_state = {"populated": False}

        def populate_preview() -> None:
            preview_tree.clear()
            paths = summary_state["paths"]
            shown = paths[: self._archive_preview_entry_limit]
            folders: dict = {}
            for path in shown:
                parent_item = None
                prefix = ""
                parts = path.split("/")
                for part in parts[:-1]:
                    prefix = f"{prefix}/{part}"
                    item = folders.get(prefix)
                    if item is None:
                        item = QtWidgets.QTreeWidgetItem([part])
                        if parent_item is None:
                            preview_tree.addTopLevelItem(item)
                        else:
                            parent_item.addChild(item)
                        folders[prefix] = item
                    parent_item = item
                leaf = QtWidgets.QTreeWidgetItem([parts[-1]])
                if parent_item is None:
                    preview_tree.addTopLevelItem(leaf)
                else:
                    parent_item.addChild(leaf)
            preview_note.setText(
                self.tr("Showing the first {0} of {1} files.").format(len(shown), len(paths))
                if len(shown) < len(paths)
                else self.tr("Showing all {0} files.").format(len(paths))
            )
            preview_state["populated"] = True

        fit_state = {"wanted": 0, "placed": None}

        def fit_window() -> None:
            """As tall as the content, on the screen: the buttons stay in sight
            and the scroll area takes what doesn't fit."""
            screen = (parent.screen() if parent is not None else None) or (
                QtWidgets.QApplication.primaryScreen()
            )
            if screen is None:
                return
            available = screen.availableGeometry()
            # a card that just folded still has its old height cached
            for card in content.findChildren(QtWidgets.QFrame):
                if card.layout() is not None:
                    card.layout().invalidate()
            advanced_layout.invalidate()
            content_layout.invalidate()
            content_layout.activate()
            margins = layout.contentsMargins()
            needed = (
                content_layout.sizeHint().height()
                + margins.top()
                + margins.bottom()
                + layout.spacing()
                + buttons.sizeHint().height()
            )
            # before its first show a window has no frame to measure
            shown = dialog.isVisible()
            frame_extra = dialog.frameGeometry().height() - dialog.height() if shown else 40
            height = min(max(needed, 320), available.height() - frame_extra)
            width = min(920, available.width() - 32)
            dialog.resize(width, height)
            if shown:
                left = dialog.frameGeometry().left()
                # back where it was when it shrinks, unless the user moved it
                if dialog.frameGeometry().top() != fit_state["placed"]:
                    fit_state["wanted"] = dialog.frameGeometry().top()
            else:
                area = parent.frameGeometry() if parent is not None else available
                left = area.center().x() - width // 2
                fit_state["wanted"] = area.center().y() - (height + frame_extra) // 2
            top = min(fit_state["wanted"], available.bottom() - height - frame_extra + 1)
            top = max(available.top(), top)
            left = min(max(left, available.left()), available.right() - width + 1)
            dialog.move(left, top)
            fit_state["placed"] = top

        def refit_window() -> None:
            # once Qt has laid out what just changed
            if dialog.isVisible():
                QTimer.singleShot(0, fit_window)

        def toggle_preview(visible: bool) -> None:
            if visible and not preview_state["populated"]:
                populate_preview()
            preview_tree.setVisible(visible)
            preview_note.setVisible(visible)
            preview_button.setText(self.tr("Hide") if visible else self.tr("Show"))
            preview_button.setAccessibleName(
                self.tr("Hide all files") if visible else self.tr("Show all files")
            )
            refit_window()

        def toggle_advanced(visible: bool) -> None:
            advanced.setVisible(visible)
            advanced_button.setText(self.tr("Hide") if visible else self.tr("Show"))
            advanced_button.setAccessibleName(
                self.tr("Hide advanced options") if visible else self.tr("Show advanced options")
            )
            refit_window()

        def choose_install() -> None:
            chosen_name = mod_name_edit.text().strip()
            if not chosen_name:
                mod_name_edit.setText(mod_name)
                mod_name_edit.setFocus()
                return
            preserve, omit_txt, omit_md, online = current_choices()
            selected["choice"] = (preserve, omit_txt, omit_md, True, chosen_name)
            self._chosen_reforged_online = online
            dialog.accept()

        def choose_manual() -> None:
            selected["choice"] = "manual"
            dialog.accept()

        buttons = QtWidgets.QDialogButtonBox(
            QtWidgets.QDialogButtonBox.StandardButton.Cancel, dialog
        )
        buttons.setObjectName("archiveDialogButtons")
        buttons.setContentsMargins(0, 0, 16, 0)
        buttons.rejected.connect(dialog.reject)
        # hands the archive to MO2's manual installer, for layouts neither
        # choice fits (several options to pick from, for example)
        manual_button = buttons.addButton(
            self.tr("Manual..."), QtWidgets.QDialogButtonBox.ButtonRole.ActionRole
        )
        manual_button.setObjectName("archiveManualButton")
        manual_button.setToolTip(
            self.tr("Open MO2's manual installer to choose the files and folders yourself.")
        )
        manual_button.clicked.connect(choose_manual)
        install_button = buttons.addButton(
            self.tr("Install"), QtWidgets.QDialogButtonBox.ButtonRole.AcceptRole
        )
        install_button.setObjectName("archiveLayoutChoiceButton")
        install_button.setProperty("preferredChoice", True)
        install_button.clicked.connect(choose_install)
        layout.addWidget(buttons)

        preview_button.toggled.connect(toggle_preview)
        advanced_button.toggled.connect(toggle_advanced)
        standard_radio.toggled.connect(lambda _checked: refresh_summary())
        omit_txt_box.toggled.connect(lambda _checked: refresh_summary())
        omit_md_box.toggled.connect(lambda _checked: refresh_summary())
        mod_name_edit.textChanged.connect(lambda _text: refresh_name_notice())
        if reforged_online_box is not None:
            reforged_online_box.toggled.connect(lambda _checked: refresh_summary())
        refresh_summary()
        refresh_name_notice()

        install_button.setDefault(True)
        install_button.setFocus()
        # a mod reinstalled with its own choices shows them
        if not package and (
            self._default_preserve or self._default_omit_txt or self._default_omit_md
        ):
            advanced_button.setChecked(True)

        dialog.ensurePolished()
        fit_window()
        dialog.exec()
        accepted = dialog.result() == QtWidgets.QDialog.DialogCode.Accepted
        # a child of MO2's window: freed, not kept for every install
        dialog.deleteLater()

        if not accepted:
            return None
        return selected["choice"]

    def _nightreign_active(self) -> bool:
        try:
            game = self._organizer.managedGame() if self._organizer else None
            return game is not None and game.gameShortName().casefold() == "nightreign"
        except Exception:
            return False

    def tr(self, value: str) -> str:
        text = QtWidgets.QApplication.translate("EldenRingArchiveInstaller", value)
        # the texts name Elden Ring; a Nightreign instance gets its own name
        if self._nightreign_active():
            text = text.replace("Elden Ring", "Nightreign")
        return text
