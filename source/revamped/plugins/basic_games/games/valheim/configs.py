"""Valheim: BepInEx config files that land in Overwrite go back to their mods.

A BepInEx plugin writes BepInEx/config/<GUID>.cfg the first time it runs;
under MO2 that new file goes to Overwrite. It belongs to the mod whose plugin
declares that GUID in its [BepInPlugin] attribute, read from the DLL's
metadata (revamped_shared.dotnet), so a plugin that only names another one
(a dependency, a compatibility check) is never taken for it. BepInEx also
writes the GUID in the file's header ("## Plugin GUID:"), which counts first.

A file goes back only to the one mod that has the plugin, and never over a
file that mod already has.
"""

from __future__ import annotations

import re
import shutil
from dataclasses import dataclass
from pathlib import Path

from ..revamped_shared import log_info, log_warning
from ..revamped_shared.dotnet import AssemblyIndex, plugin_dlls

_GUID_LINE = re.compile(r"^##\s*Plugin GUID:\s*(\S+)", re.IGNORECASE | re.MULTILINE)


@dataclass(frozen=True)
class Move:
    source: Path
    target: Path
    mod: str


def config_guid(path: Path, read_header: bool) -> str:
    """The plugin GUID a config is for: from its header when allowed and
    there, else its file name."""
    if read_header:
        try:
            with open(path, encoding="utf-8", errors="replace") as file:
                head = file.read(2048)
            match = _GUID_LINE.search(head)
            if match:
                return match.group(1)
        except OSError:
            pass
    return path.stem


def plan(overwrite: Path, mods: list[tuple[str, Path]], read_header: bool, index: AssemblyIndex) -> list[Move]:
    """Which configs in Overwrite go to which mod. mods: the active ones.
    Reads files only (no mobase), so it can run outside the main thread."""
    config_dir = overwrite / "BepInEx" / "config"
    if not config_dir.is_dir():
        return []
    configs = [
        p for p in config_dir.iterdir()
        if p.is_file() and p.suffix.casefold() == ".cfg" and p.name.casefold() != "bepinex.cfg"
    ]  # fmt: skip
    if not configs:
        return []
    # the plugin GUIDs each mod's DLLs declare
    owners: dict[str, list[tuple[str, Path]]] = {}
    for name, folder in mods:
        for dll in plugin_dlls(folder):
            for guid in index.info(dll).plugin_guids:
                holders = owners.setdefault(guid.casefold(), [])
                if (name, folder) not in holders:
                    holders.append((name, folder))
    moves = []
    for path in configs:
        found = owners.get(config_guid(path, read_header).casefold(), [])
        if len(found) != 1:
            continue  # nobody's, or several mods': left where it is
        name, folder = found[0]
        target = folder / "BepInEx" / "config" / path.name
        if not target.exists():
            moves.append(Move(path, target, name))
    return moves


def apply(moves: list[Move]) -> int:
    done = 0
    for move in moves:
        try:
            if move.target.exists() or not move.source.is_file():
                continue
            move.target.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(move.source), str(move.target))
            log_info(f"Valheim: {move.source.name} moved from Overwrite to {move.mod}")
            done += 1
        except OSError as error:
            log_warning(f"Valheim: {move.source.name} not moved to {move.mod}: {error}")
    return done
