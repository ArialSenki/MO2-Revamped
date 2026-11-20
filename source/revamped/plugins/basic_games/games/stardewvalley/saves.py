"""Stardew Valley saves: %APPDATA%/StardewValley/Saves/<Farm>_<id>/, with the
save itself (a file named like its folder), SaveGameInfo (the farmer, for
the load menu) and the game's _old backups."""

from __future__ import annotations

import xml.etree.ElementTree as ElementTree
from pathlib import Path

import mobase
from PyQt6.QtCore import QDateTime

from ...basic_features.basic_save_game_info import BasicGameSaveGame

SEASONS = ("Spring", "Summer", "Fall", "Winter")
# SaveGameInfo is a few KB; a bigger file isn't one
_INFO_LIMIT = 4 * 1024 * 1024


class StardewSaveGame(BasicGameSaveGame):
    def __init__(self, folder: Path):
        super().__init__(folder)
        self._info = read_info(folder / "SaveGameInfo")
        try:
            self._mtime = (folder / folder.name).stat().st_mtime
        except OSError:
            self._mtime = 0.0

    def getName(self) -> str:
        farmer, farm = self._info.get("Farmer", ""), self._info.get("Farm", "")
        if farmer and farm:
            return f"{farmer} ({farm})"
        return self._filepath.name.rsplit("_", 1)[0] or self._filepath.name

    def getCreationTime(self) -> QDateTime:
        return QDateTime.fromSecsSinceEpoch(int(self._mtime))

    def getSaveGroupIdentifier(self) -> str:
        return self._filepath.name

    def allFiles(self) -> list[str]:
        try:
            return [p.as_posix() for p in self._filepath.iterdir() if p.is_file()]
        except OSError:
            return []


def _text(root, tag: str) -> str:
    element = root.find(tag)
    return (element.text or "").strip() if element is not None else ""


def read_info(path: Path) -> dict[str, str]:
    """The farmer, farm, date, money and play time from SaveGameInfo; empty
    when it can't be read (a save the game is still writing)."""
    try:
        if path.stat().st_size > _INFO_LIMIT:
            return {}
        root = ElementTree.parse(path).getroot()
    except (OSError, ElementTree.ParseError, ValueError):
        return {}
    info: dict[str, str] = {}
    for key, tag in (("Farmer", "name"), ("Farm", "farmName"), ("Money", "money"), ("Game version", "gameVersion")):
        value = _text(root, tag)
        if value:
            info[key] = value
    day, season, year = (_text(root, t) for t in ("dayOfMonthForSaveGame", "seasonForSaveGame", "yearForSaveGame"))
    try:
        info["Date"] = f"{SEASONS[int(season) % 4]} {int(day)}, Year {int(year)}"
    except (ValueError, IndexError):
        pass
    played = _text(root, "millisecondsPlayed")
    if played.isdigit():
        minutes = int(played) // 60000
        info["Play Time"] = f"{minutes // 60:02}:{minutes % 60:02}"
    return info


def list_saves(folder: Path) -> list[mobase.ISaveGame]:
    saves = []
    try:
        candidates = [p for p in folder.iterdir() if p.is_dir()]
    except OSError:
        return []
    for candidate in candidates:
        if (candidate / candidate.name).is_file():
            saves.append(StardewSaveGame(candidate))
    saves.sort(key=lambda save: save._mtime, reverse=True)
    return saves


def save_metadata(path: Path, save: mobase.ISaveGame):
    """For the save details: SaveGameInfo's values."""
    info = getattr(save, "_info", None)
    if info is None:
        info = read_info(Path(path) / "SaveGameInfo")
    return dict(info) or None
