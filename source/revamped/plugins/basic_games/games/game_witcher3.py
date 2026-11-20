"""The Witcher 3: Wild Hunt support for MO2 Revamped.

MO2 Revamped's own plugin, replacing basic_games' game_witcher3.py and
keeping its identity (plugin name, game name, short and Nexus names), so
instances made with it keep working.

Based on basic_games' The Witcher 3 plugin by Holt59 (Mod Organizer 2,
https://github.com/ModOrganizer2/modorganizer-basic_games, GPL-3.0). Install
layouts follow CD PROJEKT RED's REDkit documentation and Vortex's Witcher 3
extension.

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version. It is distributed WITHOUT ANY WARRANTY; see the GNU General Public
License for more details.
"""

from __future__ import annotations

from pathlib import Path

import mobase
from PyQt6.QtCore import QDateTime, QDir

from ..basic_features import BasicLocalSavegames
from ..basic_features.basic_save_game_info import BasicGameSaveGame, BasicGameSaveGameInfo
from ..basic_game import BasicGame
from .revamped_shared import documents_folder, root_builder_enabled, translator
from .witcher3.content import Witcher3ModDataContent
from .witcher3.layout import Witcher3ModDataChecker

tr = translator("Witcher3Game")


class Witcher3SaveGame(BasicGameSaveGame):
    """A .sav with its .png screenshot."""

    def __init__(self, filepath: Path):
        super().__init__(filepath)
        try:
            self._mtime = filepath.stat().st_mtime
        except OSError:
            self._mtime = 0.0

    def getName(self) -> str:
        # "ManualSave_12" -> "ManualSave 12"; the game names them so
        return self._filepath.stem.replace("_", " ")

    def getCreationTime(self) -> QDateTime:
        return QDateTime.fromSecsSinceEpoch(int(self._mtime))

    def allFiles(self) -> list[str]:
        files = [self._filepath.as_posix()]
        screenshot = self._filepath.with_suffix(".png")
        if screenshot.is_file():
            files.append(screenshot.as_posix())
        return files


class Witcher3Game(BasicGame):
    # the plugin's name is its identity: instances keep its settings under it
    Name = "Witcher 3 Support Plugin"
    Author = "MO2 Revamped"
    Version = "2.0.0"
    Description = (
        "The Witcher 3: Wild Hunt support for MO2 Revamped: mods, DLC folders, settings "
        "menus and DLLs from one mod, script conflicts for Script Merger, the next-gen "
        "menu lists and saves. Based on the work of Holt59."
    )
    DescriptionContext = "Witcher3Game"

    GameName = "The Witcher 3: Wild Hunt"
    GameShortName = "witcher3"
    GameNexusName = "witcher3"
    GameNexusId = 952
    GameSteamId = [499450, 292030]
    GameGogId = [1640424747, 1495134320, 1207664663, 1207664643]
    GameEpicId = "725a22e15ed74735bb0d6a19f3cc82d0"
    GameBinary = "bin/x64/witcher3.exe"
    # next-gen installs may ship only the DX12 build
    _DX12_BINARY = "bin/x64_dx12/witcher3.exe"
    GameDataPath = "Mods"
    GameSaveExtension = "sav"
    GameDocumentsDirectory = documents_folder() + "/The Witcher 3"
    GameSavesDirectory = "%GAME_DOCUMENTS%/gamesaves"
    GameSupportURL = "https://www.nexusmods.com/witcher3"

    _merger_names = ("WitcherScriptMerger.exe", "WitcherScriptMerger/WitcherScriptMerger.exe")

    def init(self, organizer: mobase.IOrganizer) -> bool:
        super().init(organizer)
        self._register_feature(BasicLocalSavegames(self))
        self._register_feature(BasicGameSaveGameInfo(lambda save: Path(save).with_suffix(".png")))
        self._register_feature(Witcher3ModDataChecker(self._root_reaches_game))
        self._register_feature(Witcher3ModDataContent())
        return True

    def settings(self) -> list[mobase.PluginSetting]:
        return [
            mobase.PluginSetting(
                # read by MO2 Revamped itself (OrganizerCore): each mod's root/
                # folder goes to the game folder, for DLC and menu files
                "root_folder_to_game_folder",
                tr(
                    "Map each mod's root folder to the game folder, where the game looks for "
                    "DLC folders, settings menus and DLLs (not used when Root Builder does it)"
                ),
                True,
            ),
        ]

    def _root_reaches_game(self) -> bool:
        if root_builder_enabled(self._organizer):
            return True
        value = self._organizer.pluginSetting(self.name(), "root_folder_to_game_folder")
        return value is None or bool(value)

    # IPluginGame

    def binaryName(self) -> str:
        if self._gamePath:
            game_dir = self.gameDirectory()
            if not game_dir.exists(self.GameBinary) and game_dir.exists(self._DX12_BINARY):
                return self._DX12_BINARY
        return super().binaryName()

    def looksValid(self, directory: QDir) -> bool:
        return directory.exists(self.GameBinary) or directory.exists(self._DX12_BINARY)

    def iniFiles(self) -> list[str]:
        return ["user.settings", "input.settings"]

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
        game_dir = self.gameDirectory()
        # the title of basic_games' plugin: MO2 matches executables by title
        executables = [mobase.ExecutableInfo(self.gameName(), game_dir.absoluteFilePath(self.binaryName()))]
        if self.binaryName() != self._DX12_BINARY and game_dir.exists(self._DX12_BINARY):
            executables.append(
                mobase.ExecutableInfo(f"{self.gameName()} (DX12)", game_dir.absoluteFilePath(self._DX12_BINARY))
            )
        for relative in self._merger_names:
            if game_dir.exists(relative):
                executables.append(mobase.ExecutableInfo("Script Merger", game_dir.absoluteFilePath(relative)))
                break
        return executables

    def listSaves(self, folder: QDir) -> list[mobase.ISaveGame]:
        try:
            saves = [Witcher3SaveGame(path) for path in Path(folder.absolutePath()).glob("*.sav") if path.is_file()]
        except OSError:
            return []
        saves.sort(key=lambda save: save._mtime, reverse=True)
        return saves
