"""Valheim saves in %USERPROFILE%/AppData/LocalLow/IronGate/Valheim:
characters_local/*.fch and worlds_local/*.fwl (+ .db) for local saves, and
characters/ and worlds/ for Steam Cloud ones (the folders older versions
used for every save)."""

from __future__ import annotations

import struct
from pathlib import Path

import mobase
from PyQt6.QtCore import QDateTime

from ...basic_features.basic_save_game_info import BasicGameSaveGame

FOLDERS = (
    ("characters_local", ".fch", "Character"),
    ("worlds_local", ".fwl", "World"),
    ("characters", ".fch", "Character (cloud)"),
    ("worlds", ".fwl", "World (cloud)"),
)
# world files are small: a bigger one isn't read
_FWL_LIMIT = 1024 * 1024


class _Reader:
    def __init__(self, data: bytes) -> None:
        self.data, self.at = data, 0

    def int32(self) -> int:
        value = struct.unpack_from("<i", self.data, self.at)[0]
        self.at += 4
        return value

    def string(self) -> str:
        # .NET BinaryWriter: the length as a 7-bit encoded int, then UTF-8
        length, shift = 0, 0
        while True:
            byte = self.data[self.at]
            self.at += 1
            length |= (byte & 0x7F) << shift
            if not byte & 0x80:
                break
            shift += 7
            if shift > 28:
                raise ValueError("bad string length")
        if length > len(self.data) - self.at:
            raise ValueError("string past the end")
        text = self.data[self.at:self.at + length].decode("utf-8", errors="replace")
        self.at += length
        return text


def read_world(path: Path) -> dict[str, str]:
    """A .fwl's world name, seed and version; empty when unreadable."""
    try:
        if path.stat().st_size > _FWL_LIMIT:
            return {}
        reader = _Reader(path.read_bytes())
        reader.int32()  # the size of what follows
        version = reader.int32()
        name = reader.string()
        seed_name = reader.string()
        return {"World": name, "Seed": seed_name, "World version": str(version)}
    except (OSError, ValueError, IndexError, struct.error):
        return {}


class ValheimSaveGame(BasicGameSaveGame):
    def __init__(self, path: Path, kind: str):
        super().__init__(path)
        self._kind = kind
        self._info = read_world(path) if path.suffix.casefold() == ".fwl" else {}
        try:
            self._mtime = path.stat().st_mtime
        except OSError:
            self._mtime = 0.0

    def getName(self) -> str:
        return f"[{self._kind}] {self._info.get('World') or self._filepath.stem}"

    def getCreationTime(self) -> QDateTime:
        return QDateTime.fromSecsSinceEpoch(int(self._mtime))

    def getSaveGroupIdentifier(self) -> str:
        return self._filepath.parent.name

    def allFiles(self) -> list[str]:
        # the save, the world's .db, and the game's .old backups of both
        base = self._filepath.with_suffix("")
        suffixes = [self._filepath.suffix, self._filepath.suffix + ".old"]
        if self._filepath.suffix.casefold() == ".fwl":
            suffixes += [".db", ".db.old"]
        files = []
        for suffix in suffixes:
            candidate = base.with_name(base.name + suffix)
            if candidate.is_file():
                files.append(candidate.as_posix())
        return files


def list_saves(root: Path) -> list[mobase.ISaveGame]:
    saves = []
    for folder, suffix, kind in FOLDERS:
        directory = root / folder
        try:
            files = [p for p in directory.iterdir() if p.is_file() and p.suffix.casefold() == suffix]
        except OSError:
            continue
        saves.extend(ValheimSaveGame(path, kind) for path in files)
    saves.sort(key=lambda save: save._mtime, reverse=True)
    return saves


def save_metadata(path: Path, save: mobase.ISaveGame):
    info = dict(getattr(save, "_info", {}) or {})
    kind = getattr(save, "_kind", "")
    if kind:
        info["Type"] = kind
    return info or None
