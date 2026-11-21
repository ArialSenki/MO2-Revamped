"""Baldur's Gate 3 support for MO2 Revamped.

MO2 Revamped's own plugin, replacing basic_games' game_baldursgate3.py and
keeping its identity (plugin name, game name, short names, settings), so
instances, Nexus collections and Wabbajack lists made with it keep working.

Based on basic_games' Baldur's Gate 3 plugin by daescha (Mod Organizer 2,
https://github.com/ModOrganizer2/modorganizer-basic_games, GPL-3.0): the
file mapper, the mod data checker, the LSLib download and the BG3 tools
started there. References: BG3 Mod Manager (LaughingLeader), Vortex's
Baldur's Gate 3 extension, LSLib (Norbyte) and the Script Extender (Norbyte).

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version. It is distributed WITHOUT ANY WARRANTY; see the GNU General Public
License for more details.

Loading this module costs little: the load order, LSLib and the mapper are
imported the first time an instance of Baldur's Gate 3 needs them.
"""

from __future__ import annotations

import os
from pathlib import Path
from typing import Any

import mobase
from PyQt6.QtCore import QDir

from ..basic_game import BasicGame
from .baldursgate3.util import (
    GAME_NAME,
    PLUGIN_NAME,
    log_debug,
    log_info,
    root_builder_enabled,
    tr,
)

_local_app_data = (os.environ.get("LOCALAPPDATA") or "%USERPROFILE%/AppData/Local").replace(
    "\\", "/"
)


class BG3Game(BasicGame, mobase.IPluginFileMapper):
    # the plugin's name is its identity: instances keep its settings under it
    Name = PLUGIN_NAME
    Author = "MO2 Revamped"
    Version = "1.0.0"
    Description = (
        "Baldur's Gate 3 support for MO2 Revamped: pak mods and their load order, "
        "loose files, the Script Extender, native mods and saves. Based on the plugin "
        "by daescha."
    )
    DescriptionContext = "BaldursGate3Game"
    GameName = GAME_NAME
    GameShortName = "baldursgate3"
    GameNexusName = "baldursgate3"
    GameValidShortNames = ["bg3"]
    GameLauncher = "Launcher/LariLauncher.exe"
    GameBinary = "bin/bg3.exe"
    GameDataPath = ""
    GameDocumentsDirectory = _local_app_data + "/Larian Studios/Baldur's Gate 3"
    GameSavesDirectory = "%GAME_DOCUMENTS%/PlayerProfiles/Public/Savegames/Story"
    GameSaveExtension = "lsv"
    GameNexusId = 3474
    GameSteamId = 1086940
    GameGogId = 1456460669
    GameSupportURL = "https://wiki.bg3.community"

    def __init__(self) -> None:
        BasicGame.__init__(self)
        mobase.IPluginFileMapper.__init__(self)
        self.main_window = None
        self._launch = None
        self._mapper = None
        self._lslib = None
        self._base_dlls: set[str] | None = None
        self._problems = None

    # ------------------------------------------------------------- IPlugin

    def init(self, organizer: mobase.IOrganizer) -> bool:
        super().init(organizer)
        from .baldursgate3.layout import BG3DataContent, BG3ModDataChecker
        from .baldursgate3.saves import preview_of, save_metadata

        from ..basic_features import BasicLocalSavegames
        from ..basic_features.basic_save_game_info import BasicGameSaveGameInfo

        self._register_feature(BG3ModDataChecker())
        self._register_feature(BG3DataContent())
        self._register_feature(
            BasicGameSaveGameInfo(lambda p: preview_of(Path(p)), save_metadata)
        )
        self._register_feature(BasicLocalSavegames(self))
        organizer.onAboutToRun(self._on_about_to_run)
        organizer.onFinishedRun(self._on_finished_run)
        organizer.onUserInterfaceInitialized(self._on_ui)
        organizer.modList().onModInstalled(self._on_mod_installed)
        return True

    def _on_ui(self, window: Any) -> None:
        self.main_window = window

    def settings(self) -> list[mobase.PluginSetting]:
        # the keys and defaults of basic_games' plugin, so instances keep theirs
        settings = super().settings() + [
            mobase.PluginSetting(
                "force_load_dlls",
                "Force load all dlls detected in active mods. Removes the need for "
                "'Native Mod Loader' and similar mods.",
                True,
            ),
            mobase.PluginSetting(
                "log_diff",
                "Log a diff of modsettings.lsx before and after the game runs "
                "(debug log level).",
                False,
            ),
            mobase.PluginSetting(
                "delete_levelcache_folders_older_than_x_days",
                "Maximum number of days a folder in overwrite/LevelCache is kept "
                "after the game closes. Negative to keep them.",
                3,
            ),
            mobase.PluginSetting(
                "autobuild_paks",
                "Pack mod workspaces (unpacked mods with Mods/<name>/meta.lsx) into "
                "paks when the game starts. Needs LSLib.",
                True,
            ),
            mobase.PluginSetting(
                "convert_yamls_to_json",
                "Convert YAMLs to JSONs when the game starts, so Script Extender "
                "mods can be configured with YAML files.",
                False,
            ),
            mobase.PluginSetting(
                "keep_external_mods",
                "Keep in the load order the mods enabled outside MO2 (the in-game "
                "mod manager, paks put in the documents' Mods folder) while their "
                "pak is there.",
                True,
            ),
            mobase.PluginSetting(
                "warn_missing_dependencies",
                "Before the game starts, ask when an active mod needs a mod that "
                "isn't active (the game turns such mods off).",
                True,
            ),
            mobase.PluginSetting(
                "clear_mod_crash_check",
                "Clear the game's ModCrashSanityCheck folder before it starts, so a "
                "crash doesn't make the game turn all mods off (as BG3 Mod Manager "
                "does).",
                True,
            ),
        ]
        for setting in settings:
            setting.description = tr(setting.description)
        return settings

    def setting(self, key: str) -> mobase.MoVariant:
        return self._organizer.pluginSetting(self.name(), key)

    # ---------------------------------------------------------- IPluginGame

    def gameVersion(self) -> str:
        """bin/bg3.exe's product version (4.1.1.x); its file version is
        always 1.0.0.0."""
        if not self.isInstalled():
            return ""
        exe = self.gameDirectory().absoluteFilePath(self.binaryName())
        for read in ("getProductVersion", "getFileVersion"):
            try:
                version = getattr(mobase, read)(exe)
            except Exception:  # noqa: BLE001 - missing or unreadable exe
                continue
            if version and version != "1.0.0.0":
                return version
        return ""

    def listSaves(self, folder: QDir) -> list[mobase.ISaveGame]:
        from .baldursgate3.saves import list_saves

        return list_saves(Path(folder.absolutePath()))

    def executables(self) -> list[mobase.ExecutableInfo]:
        if not self.isInstalled():
            return []
        game_dir = self.gameDirectory()
        # the titles of basic_games' plugin: MO2 matches an instance's
        # executables by title
        execs = [
            mobase.ExecutableInfo(
                f"{self.gameName()}: DX11", game_dir.absoluteFilePath("bin/bg3_dx11.exe")
            ),
            mobase.ExecutableInfo(
                f"{self.gameName()}: Vulkan", game_dir.absoluteFilePath(self.binaryName())
            ),
        ]
        launcher = game_dir.absoluteFilePath(self.getLauncherName())
        if Path(launcher).exists():
            execs.append(mobase.ExecutableInfo("Larian Launcher", launcher))
        return execs

    def executableForcedLoads(self) -> list[mobase.ExecutableForcedLoadSetting]:
        if not self.isInstalled() or not self.setting("force_load_dlls"):
            return []
        dlls = self.forced_dlls()
        if not dlls:
            return []
        log_debug(f"dlls to force load: {dlls}")
        exes = ("bg3.exe", "bg3_dx11.exe")
        return [
            mobase.ExecutableForcedLoadSetting(exe, dll).withEnabled(True)
            for dll in dlls
            for exe in exes
        ]

    def forced_dlls(self) -> list[str]:
        """The DLLs the game should load from the mods, relative to bin/:
        the Script Extender's DWrite.dll and the native mods (NativeMods/).

        The game imports bink2w64.dll as it starts, before the VFS is there,
        so it always gets its own: Native Mod Loader's proxy never runs under
        MO2, and the native mods it would load are loaded here instead (as
        the original plugin did). The DLLs with the name of one of the
        game's (that proxy) and NML's renamed original are never forced."""
        tree = self._organizer.virtualFileTree()
        libs: dict[str, str] = {}
        base = self.base_dlls()
        def add(relpath: str) -> None:
            folded = relpath.casefold()
            if folded not in base and folded != "bink2w64_original.dll":
                libs.setdefault(folded, relpath)

        folder = tree.find("bin")
        if isinstance(folder, mobase.IFileTree):

            def visit(_: Any, entry: mobase.FileTreeEntry, folder=folder) -> mobase.IFileTree.WalkReturn:
                relpath = entry.pathFrom(folder).replace("\\", "/")
                if relpath and entry.hasSuffix("dll"):
                    add(relpath)
                return mobase.IFileTree.WalkReturn.CONTINUE

            folder.walk(visit)

        # a mod's Root/bin (Wabbajack lists made with Root Builder): read from
        # the active mods' folders, highest priority first. The data tree
        # leaves Root/ out where the data folder is the game folder
        if not root_builder_enabled(self._organizer):
            mods = self._organizer.modList()
            for name in reversed(list(mods.allModsByProfilePriority())):
                if not mods.state(name) & mobase.ModState.ACTIVE:
                    continue
                mod = mods.getMod(name)
                if mod is None:
                    continue
                root_bin = Path(mod.absolutePath()) / "Root" / "bin"
                if not root_bin.is_dir():
                    continue
                for dll in root_bin.rglob("*.dll"):
                    add(dll.relative_to(root_bin).as_posix())
        return list(libs.values())

    def base_dlls(self) -> set[str]:
        if self._base_dlls is None:
            base_bin = Path(self.gameDirectory().absoluteFilePath("bin"))
            try:
                self._base_dlls = {f.name.casefold() for f in base_bin.glob("*.dll")}
            except OSError:
                self._base_dlls = set()
        return self._base_dlls

    def has_script_extender(self) -> bool:
        if not self.isInstalled():
            return False
        if Path(self.gameDirectory().absoluteFilePath("bin/DWrite.dll")).exists():
            return True
        tree = self._organizer.virtualFileTree()
        if tree.find("bin/DWrite.dll") is not None:
            return True
        # a mod's Root/bin, from its folder: the data tree leaves Root/ out
        # where the data folder is the game folder
        mods = self._organizer.modList()
        for name in mods.allModsByProfilePriority():
            if not mods.state(name) & mobase.ModState.ACTIVE:
                continue
            mod = mods.getMod(name)
            if mod is not None and (Path(mod.absolutePath()) / "Root" / "bin" / "DWrite.dll").is_file():
                return True
        return False

    def loadOrderMechanism(self) -> mobase.LoadOrderMechanism:
        return mobase.LoadOrderMechanism.PLUGINS_TXT

    # ----------------------------------------------------- IPluginFileMapper

    def mappings(self) -> list[mobase.Mapping]:
        if not self.isInstalled():
            return []
        return self.mapper().mappings()

    # ---------------------------------------------------- parts, made lazily

    def mapper(self):
        if self._mapper is None:
            from .baldursgate3.mapper import FileMapper

            self._mapper = FileMapper(self)
        return self._mapper

    def launch(self):
        if self._launch is None:
            from .baldursgate3.launch import Launch

            self._launch = Launch(self)
        return self._launch

    def lslib(self):
        if self._lslib is None:
            from .baldursgate3.lslib import LSLibRetriever

            self._lslib = LSLibRetriever(self)
        return self._lslib

    def divine(self):
        from .baldursgate3.lslib import Divine

        return Divine(self.tools_dir)

    # ------------------------------------------------------------- paths

    @property
    def plugin_data_path(self) -> Path:
        return Path(self._organizer.pluginDataPath(), self.name()).absolute()

    @property
    def tools_dir(self) -> Path:
        return self.plugin_data_path / "tools"

    @property
    def overwrite_path(self) -> Path:
        return Path(self._organizer.overwritePath())

    @property
    def log_dir(self) -> Path:
        return Path(self._organizer.basePath()) / "logs"

    @property
    def modsettings_path(self) -> Path:
        # it follows the profile
        return Path(self._organizer.profilePath()) / "modsettings.lsx"

    @property
    def modsettings_backups(self) -> Path:
        return Path(self._organizer.profilePath()) / "modsettings-backups"

    @property
    def real_modsettings_path(self) -> Path:
        """The game's own copy, outside the VFS (what it wrote when it last
        ran without MO2, or the in-game mod manager)."""
        return Path(self.documentsDirectory().absolutePath()) / "PlayerProfiles/Public/modsettings.lsx"

    def root_builder_enabled(self) -> bool:
        return root_builder_enabled(self._organizer)

    def active_mods(self) -> list[mobase.IModInterface]:
        modlist = self._organizer.modList()
        mods = []
        for name in modlist.allModsByProfilePriority():
            if not modlist.state(name) & mobase.ModState.ACTIVE:
                continue
            mod = modlist.getMod(name)
            if mod is None or mod.isSeparator() or mod.isForeign() or mod.isOverwrite():
                continue
            mods.append(mod)
        return mods

    # ------------------------------------------------------------- windows

    def progress_window(self, title: str, maximum: int, cancelable: bool = True):
        from PyQt6.QtCore import Qt
        from PyQt6.QtWidgets import QProgressDialog

        progress = QProgressDialog(
            tr(title), tr("Cancel") if cancelable else None, 0, maximum, self.main_window
        )
        progress.setWindowTitle(tr("Baldur's Gate 3"))
        progress.setWindowModality(Qt.WindowModality.ApplicationModal)
        progress.setMinimumDuration(400)
        return progress

    def create_progress_window(
        self, title: str, max_progress: int, msg: str = "", cancelable: bool = True
    ):
        # for LSLibRetriever
        progress = self.progress_window(msg or title, max_progress, cancelable)
        progress.show()
        return progress

    def tr(self, text: str) -> str:
        return tr(text)

    @staticmethod
    def process_events() -> None:
        from PyQt6.QtWidgets import QApplication

        QApplication.processEvents()

    def invalidate_problems(self) -> None:
        if self._problems is not None:
            self._problems()

    # ---------------------------------------------------------- callbacks

    def _on_about_to_run(self, exec_path: str, working_dir: QDir | None = None, args: str = "") -> bool:
        from .baldursgate3.launch import check_dotnet_before_run

        if not check_dotnet_before_run(self, exec_path):
            return False
        return self.launch().before_run(exec_path)

    def _on_finished_run(self, exec_path: str, exit_code: int) -> None:
        self.launch().after_run(exec_path)

    def _on_mod_installed(self, mod: mobase.IModInterface) -> None:
        """The new mod's paks are read now (cheap), so the problem list and the
        next start know them; a pak that can't be read doesn't make the
        installation look failed."""
        try:
            from .baldursgate3.paks import scan_mod_paks

            # in a worker thread: a big pak read on the interface's thread
            # froze MO2 right after the installation
            launch = self.launch()
            launch.unread.extend(scan_mod_paks(Path(mod.absolutePath())))
            launch.read_in_background()
        except Exception as e:  # noqa: BLE001
            log_info(f"couldn't read the paks of the new mod {mod.name()}: {e}")
