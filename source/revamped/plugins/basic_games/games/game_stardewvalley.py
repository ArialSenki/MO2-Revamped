"""Stardew Valley support for MO2 Revamped.

MO2 Revamped's own plugin, replacing basic_games' game_stardewvalley.py and
keeping its identity (plugin name, game name, short and Nexus names, the
executables' titles), so instances made with it keep working.

Based on basic_games' Stardew Valley plugin by Syer10 (Mod Organizer 2,
https://github.com/ModOrganizer2/modorganizer-basic_games, GPL-3.0). Mod
layouts follow SMAPI's own mod scanner and Vortex's Stardew Valley extension.

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version. It is distributed WITHOUT ANY WARRANTY; see the GNU General Public
License for more details.
"""

from __future__ import annotations

from pathlib import Path

import mobase
from PyQt6.QtCore import QDir

from ..basic_features import BasicLocalSavegames
from ..basic_features.basic_save_game_info import BasicGameSaveGameInfo
from ..basic_game import BasicGame
from .revamped_shared import roaming_app_data_folder
from .stardewvalley.content import StardewValleyModDataContent
from .stardewvalley.layout import StardewValleyModDataChecker
from .stardewvalley.saves import list_saves, save_metadata


class StardewValleyGame(BasicGame):
    # the plugin's name is its identity: instances keep its settings under it
    Name = "Stardew Valley Support Plugin"
    Author = "MO2 Revamped"
    Version = "1.0.0"
    Description = (
        "Stardew Valley support for MO2 Revamped: SMAPI mods and content packs, their "
        "dependencies and duplicates, SMAPI's version and the saves with their farm. "
        "Based on the work of Syer10."
    )
    DescriptionContext = "StardewValleyGame"

    GameName = "Stardew Valley"
    GameShortName = "stardewvalley"
    GameNexusName = "stardewvalley"
    GameNexusId = 1303
    GameSteamId = 413150
    GameGogId = 1453375253
    GameBinary = "Stardew Valley.exe"
    GameDataPath = "mods"
    # the game keeps its saves and settings in %APPDATA%/StardewValley
    # (basic_games looked in Documents)
    GameDocumentsDirectory = roaming_app_data_folder() + "/StardewValley"
    GameSavesDirectory = "%GAME_DOCUMENTS%/Saves"
    GameSupportURL = "https://stardewvalleywiki.com/Modding:Player_Guide/Getting_Started"

    def init(self, organizer: mobase.IOrganizer) -> bool:
        super().init(organizer)
        self._register_feature(BasicLocalSavegames(self))
        self._register_feature(BasicGameSaveGameInfo(None, save_metadata))
        self._register_feature(StardewValleyModDataChecker(organizer))
        self._register_feature(StardewValleyModDataContent())
        return True

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
        # the titles of basic_games' plugin: MO2 matches executables by title
        return [
            mobase.ExecutableInfo("SMAPI", game_dir.absoluteFilePath("StardewModdingAPI.exe")),
            mobase.ExecutableInfo("Stardew Valley", game_dir.absoluteFilePath(self.binaryName())),
        ]

    def listSaves(self, folder: QDir) -> list[mobase.ISaveGame]:
        return list_saves(Path(folder.absolutePath()))
