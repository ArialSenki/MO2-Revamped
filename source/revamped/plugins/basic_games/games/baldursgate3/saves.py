"""Baldur's Gate 3 saves: <saves>/<Leader>-<id>__<title>/<title>.lsv with its
<title>.WebP screenshot. A .lsv is an LSPK package; its SaveInfo.json has the
save's name, the party and the game version. Current saves compress it with
Zstandard, which MO2's Python can't read, so the folder's name gives the
leader and the title then."""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any

import mobase

from ...basic_features.basic_save_game_info import BasicGameSaveGame, format_date


_folder_re = re.compile(r"(?P<leader>.+?)-\d+__(?P<title>.+)")


def folder_parts(lsv: Path) -> tuple[str, str]:
    """(leader, title) from the save's folder name, or ("", stem)."""
    match = _folder_re.fullmatch(lsv.parent.name)
    if match:
        return match["leader"], match["title"]
    return "", lsv.stem


def preview_of(lsv: Path) -> Path | None:
    """The save's screenshot, by its real name (the game writes .WebP)."""
    try:
        for file in lsv.parent.iterdir():
            if file.stem == lsv.stem and file.suffix.casefold() == ".webp":
                return file
    except OSError:
        pass
    return None


class BG3SaveGame(BasicGameSaveGame):
    def getName(self) -> str:
        info = save_info(self._filepath)
        name = info.get("Save Name") if info else None
        return str(name) if name else folder_parts(self._filepath)[1]

    def allFiles(self) -> list[str]:
        # the screenshot goes with the save when MO2 moves or deletes it
        files = [self.getFilepath()]
        preview = preview_of(self._filepath)
        if preview is not None:
            files.append(preview.as_posix())
        return files


def list_saves(folder: Path) -> list[mobase.ISaveGame]:
    """Every .lsv one or two folders down (Story/<save>/, or <save>/ when the
    saves folder is Story itself), newest first. A save the game is still
    writing, or an odd file, doesn't stop the list."""
    found: list[tuple[float, Path]] = []
    for pattern in ("*/*.lsv", "*/*/*.lsv"):
        for lsv in folder.glob(pattern):
            try:
                found.append((lsv.stat().st_mtime, lsv))
            except OSError:
                continue
    found.sort(key=lambda t: t[0], reverse=True)
    seen: set[Path] = set()
    saves: list[mobase.ISaveGame] = []
    for _, lsv in found:
        if lsv not in seen:
            seen.add(lsv)
            saves.append(BG3SaveGame(lsv))
    return saves


_info_cache: dict[str, tuple[float, dict[str, Any] | None]] = {}


def save_info(lsv: Path) -> dict[str, Any] | None:
    """SaveInfo.json from the .lsv, or None (another format, Zstandard, a
    save being written)."""
    try:
        mtime = lsv.stat().st_mtime
    except OSError:
        return None
    key = str(lsv)
    cached = _info_cache.get(key)
    if cached and cached[0] == mtime:
        return cached[1]
    info = None
    try:
        from . import lspk

        data = lspk.Package(lsv).read_file("SaveInfo.json")
        if data:
            info = json.loads(data.decode("utf-8-sig", "replace"))
            if not isinstance(info, dict):
                info = None
    except Exception:  # noqa: BLE001 - any odd save just shows less
        info = None
    _info_cache[key] = (mtime, info)
    return info


def save_metadata(lsv: Path, save: mobase.ISaveGame) -> dict[str, Any]:
    from .util import tr

    meta: dict[str, Any] = {tr("Saved"): format_date(save.getCreationTime())}
    info = save_info(Path(lsv))
    if not info:
        leader = folder_parts(Path(lsv))[0]
        if leader:
            meta[tr("Character")] = leader
        return meta
    party = info.get("Active Party") or {}
    characters = party.get("Characters") if isinstance(party, dict) else None
    if isinstance(characters, list) and characters:
        leader = characters[0] if isinstance(characters[0], dict) else {}
        classes = leader.get("Classes")
        if isinstance(classes, list):
            classes = ", ".join(
                str(c.get("Main") or c.get("Name") or "")
                for c in classes
                if isinstance(c, dict)
            )
        text = " ".join(
            str(v)
            for v in (leader.get("Origin") or leader.get("Race"), classes)
            if v
        )
        if leader.get("Level"):
            text = f"{text} ({tr('level')} {leader['Level']})".strip()
        if text:
            meta[tr("Character")] = text
    for label, key in (
        ("Location", "Current Level"),
        ("Difficulty", "Difficulty"),
        ("Game version", "Game Version"),
    ):
        value = info.get(key)
        if isinstance(value, list):
            value = ", ".join(str(v) for v in value)
        if value:
            meta[tr(label)] = str(value)
    return meta
