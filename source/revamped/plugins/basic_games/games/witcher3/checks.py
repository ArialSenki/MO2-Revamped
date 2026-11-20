"""The Witcher 3 checks behind the problem report: script conflicts between
mods, and menus the next-gen game won't load."""

from __future__ import annotations

from dataclasses import dataclass

import mobase

from ..revamped_shared import find_dir, is_dir, name_of
from .content import MERGED_MOD
from .layout import MENU_FOLDER

# the game's own menu files: never listed by a mod
VANILLA_MENUS = frozenset(
    name + ".xml"
    for name in (
        "audio", "display", "gameplay", "gamma", "graphics", "graphicsdx11", "graphicsdx12",
        "hdr", "hidden", "hud", "input", "localization", "rendering", "postprocess",
    )
)  # fmt: skip


@dataclass(frozen=True)
class ModTree:
    name: str  # the MO2 mod
    tree: mobase.IFileTree


@dataclass
class ScriptConflict:
    script: str  # relative to content/scripts, as the first mod spells it
    folders: list[tuple[str, str]]  # (game mod folder, MO2 mod), in MO2's order


def _scripts(folder: mobase.IFileTree) -> list[str]:
    scripts = find_dir(folder, "content/scripts")
    found: list[str] = []
    if scripts is None:
        return found

    def visit(path: str, entry) -> mobase.IFileTree.WalkReturn:
        if not is_dir(entry) and name_of(entry).endswith(".ws"):
            found.append(f"{path}{entry.name()}".replace("\\", "/"))
        return mobase.IFileTree.WalkReturn.CONTINUE

    scripts.walk(visit, "/")
    return found


def script_conflicts(mods: list[ModTree], merged_elsewhere: frozenset[str] | set[str] = frozenset()) -> list[ScriptConflict]:
    """Scripts that two game mod folders (mod...) both change, unless Script
    Merger's output (mod0000_MergedFiles) has a merged copy. Mods: the active
    ones, lowest priority first. Two MO2 mods with the same folder name are
    one folder in the game (the higher one wins), not a conflict."""
    owners: dict[str, dict[str, str]] = {}  # script -> game folder -> MO2 mod
    spelling: dict[str, str] = {}
    merged: set[str] = {script.casefold() for script in merged_elsewhere}
    for mod in mods:
        for entry in mod.tree:
            if not is_dir(entry) or not name_of(entry).startswith("mod"):
                continue
            folder = entry.name()
            for script in _scripts(entry):
                key = script.casefold()
                if name_of(entry) == MERGED_MOD:
                    merged.add(key)
                    continue
                spelling.setdefault(key, script)
                owners.setdefault(key, {})[folder.casefold()] = mod.name
    conflicts = []
    for key, folders in owners.items():
        if len(folders) > 1 and key not in merged:
            ordered = sorted(folders.items())
            conflicts.append(ScriptConflict(spelling[key], [(f, m) for f, m in ordered]))
    conflicts.sort(key=lambda c: c.script.casefold())
    return conflicts


def overwrite_merged_scripts(overwrite) -> set[str]:
    """The scripts of a Script Merger output still in Overwrite (it writes
    there through the VFS until the user makes a mod of it)."""
    import os

    root = overwrite / "mod0000_MergedFiles" / "content" / "scripts"
    found: set[str] = set()
    if not root.is_dir():
        return found
    for folder, _dirs, files in os.walk(root):
        for name in files:
            if name.casefold().endswith(".ws"):
                relative = os.path.relpath(os.path.join(folder, name), root)
                found.add(relative.replace(os.sep, "/").casefold())
    return found


def menu_files(mods: list[ModTree]) -> dict[str, str]:
    """The menu files the mods add (file name, casefolded -> MO2 mod)."""
    found: dict[str, str] = {}
    for mod in mods:
        folder = find_dir(mod.tree, "root/" + MENU_FOLDER)
        if folder is None:
            continue
        for entry in folder:
            name = name_of(entry)
            if not is_dir(entry) and name.endswith(".xml") and name not in VANILLA_MENUS:
                found[name] = mod.name
    return found


def listed_menus(filelist: str) -> set[str]:
    """The menu files a dx11filelist.txt or dx12filelist.txt lists ("name.xml;")."""
    names = set()
    for line in filelist.splitlines():
        entry = line.strip().rstrip(";").strip().casefold()
        if entry.endswith(".xml"):
            names.add(entry.rsplit("/", 1)[-1].rsplit("\\", 1)[-1])
    return names


def unlisted_menus(menus: dict[str, str], filelists: list[str]) -> dict[str, str]:
    """The menus no file list names: the next-gen game doesn't load them."""
    listed: set[str] = set()
    for text in filelists:
        listed |= listed_menus(text)
    return {name: mod for name, mod in sorted(menus.items()) if name not in listed}
