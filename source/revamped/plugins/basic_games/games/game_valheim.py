"""Valheim support for MO2 Revamped.

MO2 Revamped's own plugin, replacing basic_games' game_valheim.py and keeping
its identity (plugin name, game name, short and Nexus names, settings), so
instances made with it keep working.

Based on basic_games' Valheim plugin by Zash (Mod Organizer 2,
https://github.com/ModOrganizer2/modorganizer-basic_games, GPL-3.0). Mod
layouts follow BepInEx, Thunderstore's package format and Vortex's Valheim
extension.

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version. It is distributed WITHOUT ANY WARRANTY; see the GNU General Public
License for more details.
"""

from __future__ import annotations

import os
import threading
from pathlib import Path

import mobase
from PyQt6.QtCore import QDir, QTimer

from ..basic_features import BasicLocalSavegames
from ..basic_features.basic_save_game_info import BasicGameSaveGameInfo
from ..basic_game import BasicGame
from .revamped_shared import local_low_app_data_folder, log_warning, translator
from .revamped_shared.dotnet import SHARED
from .valheim import configs
from .valheim.content import ValheimModDataContent
from .valheim.layout import ValheimModDataChecker
from .valheim.saves import list_saves, save_metadata

tr = translator("ValheimGame")


class ValheimGame(BasicGame):
    # the plugin's name is its identity: instances keep its settings under it
    Name = "Valheim Support Plugin"
    Author = "MO2 Revamped"
    Version = "2.0.0"
    Description = (
        "Valheim support for MO2 Revamped: BepInEx and its plugins, Thunderstore packages, "
        "settings files back to their mods by plugin GUID, Jotunn and the saves with their "
        "world. Based on the work of Zash."
    )
    DescriptionContext = "ValheimGame"

    GameName = "Valheim"
    GameShortName = "valheim"
    GameNexusName = "valheim"
    GameNexusId = 3667
    GameSteamId = [892970, 896660, 1223920]
    GameBinary = "valheim.exe"
    GameDataPath = ""
    GameSavesDirectory = local_low_app_data_folder() + "/IronGate/Valheim"
    GameSupportURL = "https://www.nexusmods.com/valheim"

    # BepInEx's Doorstop: Valheim loads it at start, before the VFS is there
    _forced_libraries = ("winhttp.dll",)

    def init(self, organizer: mobase.IOrganizer) -> bool:
        super().init(organizer)
        self._register_feature(BasicLocalSavegames(self))
        self._register_feature(BasicGameSaveGameInfo(None, save_metadata))
        self._register_feature(ValheimModDataChecker())
        self._register_feature(ValheimModDataContent())
        self._sync_thread: threading.Thread | None = None
        organizer.onUserInterfaceInitialized(lambda window: self._sync_configs())
        organizer.onFinishedRun(self._on_finished_run)
        return True

    def settings(self) -> list[mobase.PluginSetting]:
        # the keys and defaults of basic_games' plugin, so instances keep theirs
        return [
            mobase.PluginSetting(
                "sync_overwrite",
                tr(
                    "Move the settings files BepInEx plugins write to Overwrite into the mod of "
                    "their plugin (by its GUID), when MO2 starts and after the game closes"
                ),
                True,
            ),
            mobase.PluginSetting(
                "search_overwrite_file_content",
                tr("Also read the plugin GUID from the settings file's header, not only from its name"),
                True,
            ),
        ]

    # IPluginGame

    def gameVersion(self) -> str:
        if not self.isInstalled():
            return ""
        try:
            return super().gameVersion()
        except Exception:  # noqa: BLE001 - the exe is missing or unreadable
            return ""

    def executables(self) -> list[mobase.ExecutableInfo]:
        if not self.isInstalled():
            return []
        # the title of basic_games' plugin (BasicGame's default)
        return [mobase.ExecutableInfo(self.gameName(), self.gameDirectory().absoluteFilePath(self.binaryName()))]

    def executableForcedLoads(self) -> list[mobase.ExecutableForcedLoadSetting]:
        if not self.isInstalled():
            return []
        loads = []
        try:
            tree = self._organizer.virtualFileTree()
            for library in self._forced_libraries:
                # only when BepInEx is installed: a forced load of a file that
                # isn't there fails the launch
                if tree.find(library) is not None:
                    loads.append(mobase.ExecutableForcedLoadSetting(self.binaryName(), library).withEnabled(True))
        except Exception as error:  # noqa: BLE001 - never stop a launch here
            log_warning(f"Valheim: forced loads not set: {error}")
        return loads

    def listSaves(self, folder: QDir) -> list[mobase.ISaveGame]:
        return list_saves(Path(folder.absolutePath()))

    # settings files from Overwrite back to their mods

    def _setting(self, key: str) -> bool:
        return self._organizer.pluginSetting(self.name(), key) is not False

    def _on_finished_run(self, app_path: str, exit_code: int) -> None:
        if os.path.normcase(os.path.normpath(app_path)) == os.path.normcase(
            os.path.normpath(self.gameDirectory().absoluteFilePath(self.binaryName()))
        ):
            self._sync_configs()

    def _sync_configs(self) -> None:
        if not self.isActive() or not self._setting("sync_overwrite") or self._sync_thread is not None:
            return
        try:
            mod_list = self._organizer.modList()
            mods = []
            for name in mod_list.allModsByProfilePriority():
                if not mod_list.state(name) & mobase.ModState.ACTIVE:
                    continue
                mod = mod_list.getMod(name)
                if mod is None or mod.isSeparator() or mod.isForeign() or mod.isOverwrite():
                    continue
                mods.append((name, Path(mod.absolutePath())))
            overwrite = Path(self._organizer.overwritePath())
            read_header = self._setting("search_overwrite_file_content")
        except Exception as error:  # noqa: BLE001 - never break MO2's start
            log_warning(f"Valheim: settings files not synced: {error}")
            return
        # reading the plugins' DLLs can take a while on a big list: in a
        # background thread; the files are moved in MO2's thread
        result: list = []

        def work() -> None:
            try:
                result.extend(configs.plan(overwrite, mods, read_header, SHARED))
            except Exception as error:  # noqa: BLE001
                log_warning(f"Valheim: settings files not synced: {error}")

        def finished() -> None:
            if self._sync_thread is not None and self._sync_thread.is_alive():
                QTimer.singleShot(250, finished)
                return
            self._sync_thread = None
            if result and configs.apply(result):
                self._organizer.refresh()

        self._sync_thread = threading.Thread(target=work, name="Valheim settings sync", daemon=True)
        self._sync_thread.start()
        QTimer.singleShot(250, finished)
