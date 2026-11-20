"""The Cyberpunk 2077 frameworks: which ones the active mods need, and which
are there (in an active mod, or installed in the game folder itself)."""

from __future__ import annotations

import os
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

import mobase

from .layout import ARCHIVE_SUFFIXES, TWEAK_SUFFIXES, contains_suffix


@dataclass(frozen=True)
class Framework:
    key: str
    name: str
    # any of these files means it is installed
    markers: tuple[str, ...]
    nexus_id: int
    # frameworks it needs itself (its own scripts are redscript)
    requires: tuple[str, ...] = ()

    @property
    def url(self) -> str:
        return f"https://www.nexusmods.com/cyberpunk2077/mods/{self.nexus_id}"


FRAMEWORKS: tuple[Framework, ...] = (
    Framework("red4ext", "RED4ext", ("red4ext/RED4ext.dll",), 2380),
    Framework("cet", "Cyber Engine Tweaks", ("bin/x64/plugins/cyber_engine_tweaks.asi",), 107),
    Framework("redscript", "redscript", ("engine/tools/scc.exe", "engine/config/base/scripts.ini"), 1511),
    Framework("archivexl", "ArchiveXL", ("red4ext/plugins/ArchiveXL/ArchiveXL.dll",), 4198, ("red4ext", "redscript")),
    Framework("tweakxl", "TweakXL", ("red4ext/plugins/TweakXL/TweakXL.dll",), 4197, ("red4ext", "redscript")),
    Framework("codeware", "Codeware", ("red4ext/plugins/Codeware/Codeware.dll",), 7780, ("red4ext", "redscript")),
    Framework("input_loader", "Input Loader", ("red4ext/plugins/input_loader/input_loader.dll",), 4575, ("red4ext",)),
    Framework("audioware", "Audioware", ("red4ext/plugins/audioware/audioware.dll",), 12001, ("red4ext", "redscript")),
)
BY_KEY = {framework.key: framework for framework in FRAMEWORKS}

# REDmod is a free DLC of the game, not a mod
REDMOD_BINARY = "tools/redmod/bin/redMod.exe"

# a mod's files may be in its root/ folder too (Root Builder's layout, which
# MO2 Revamped maps to the game folder)
PREFIXES = ("", "root/")

_CODEWARE_IMPORT = re.compile(rb"^\s*import\s+Codeware\b", re.MULTILINE)
# bytes read of one script: redscript puts its imports at the top of a file,
# before any declaration
_CODEWARE_READ_LIMIT = 8 * 1024
_CODEWARE_FILE_LIMIT = 4000  # scripts read in all


def _dir(tree: mobase.IFileTree, path: str) -> mobase.IFileTree | None:
    entry = tree.find(path, mobase.FileTreeEntry.FileTypes.DIRECTORY)
    return entry if isinstance(entry, mobase.IFileTree) else None


def _not_empty(tree: mobase.IFileTree | None) -> bool:
    return tree is not None and len(tree) > 0


def provided_by_tree(tree: mobase.IFileTree) -> set[str]:
    """The frameworks a file tree (a mod, or the virtual game folder) holds."""
    found = set()
    for framework in FRAMEWORKS:
        if any(tree.exists(prefix + marker) for prefix in PREFIXES for marker in framework.markers):
            found.add(framework.key)
    return found


def needed_by_tree(tree: mobase.IFileTree) -> set[str]:
    """The frameworks a mod's files need, from where they are (Codeware apart:
    only its scripts tell, see codeware_users)."""
    needs: set[str] = set()
    for prefix in PREFIXES:
        plugins = _dir(tree, prefix + "red4ext/plugins")
        if _not_empty(plugins):
            needs.add("red4ext")
        if _not_empty(_dir(tree, prefix + "bin/x64/plugins/cyber_engine_tweaks/mods")):
            needs.add("cet")
        if contains_suffix(_dir(tree, prefix + "r6/scripts"), (".reds",)):
            needs.add("redscript")
        if contains_suffix(_dir(tree, prefix + "r6/tweaks"), TWEAK_SUFFIXES):
            needs.add("tweakxl")
        if contains_suffix(_dir(tree, prefix + "archive/pc/mod"), (".xl",)):
            needs.add("archivexl")
        if contains_suffix(_dir(tree, prefix + "r6/input"), (".xml",)):
            needs.add("input_loader")
        if _not_empty(_dir(tree, prefix + "r6/audioware")):
            needs.add("audioware")
    # a REDmod's archives can need ArchiveXL too
    mods = _dir(tree, "mods")
    if mods is not None:
        for child in mods:
            if isinstance(child, mobase.IFileTree) and contains_suffix(_dir(child, "archives"), (".xl",)):
                needs.add("archivexl")
    # what a framework needs itself
    for key in provided_by_tree(tree):
        needs.update(BY_KEY[key].requires)
    return needs


def uses_redmod(tree: mobase.IFileTree) -> bool:
    mods = _dir(tree, "mods")
    return mods is not None and any(
        isinstance(child, mobase.IFileTree)
        and child.exists("info.json", mobase.FileTreeEntry.FileTypes.FILE)
        for child in mods
    )


def reds_files(tree: mobase.IFileTree) -> list[str]:
    """The redscript files of a mod, relative to it."""
    files: list[str] = []
    for prefix in PREFIXES:
        for folder in ("r6/scripts", "red4ext/plugins"):
            start = _dir(tree, prefix + folder)
            if start is None:
                continue

            def visit(path: str, entry: mobase.FileTreeEntry, base=prefix + folder):
                if not isinstance(entry, mobase.IFileTree) and entry.name().casefold().endswith(".reds"):
                    files.append(f"{base}/{path}{entry.name()}".replace("\\", "/"))
                return mobase.IFileTree.WalkReturn.CONTINUE

            start.walk(visit, "/")
    return files


def imports_codeware(mod_path: Path, files: Iterable[str], budget: list[int]) -> bool:
    """Whether one of the mod's scripts imports Codeware. budget[0] is the
    number of scripts that may still be read. Reads files only (no mobase),
    so it can run outside the main thread."""
    for relative in files:
        if budget[0] <= 0:
            return False
        budget[0] -= 1
        try:
            with open(mod_path / relative, "rb") as script:
                if _CODEWARE_IMPORT.search(script.read(_CODEWARE_READ_LIMIT)):
                    return True
        except OSError:
            continue
    return False


@dataclass(frozen=True)
class CodewareCandidate:
    """A mod with redscript files, which may import Codeware."""

    name: str
    path: Path
    files: tuple[str, ...]

    @property
    def key(self) -> tuple:
        return (self.name, str(self.path), self.files)


def codeware_candidates(mods: list["ModFiles"]) -> list[CodewareCandidate]:
    return [
        CodewareCandidate(mod.name, mod.path, tuple(files))
        for mod in mods
        if (files := reds_files(mod.tree))
    ]


def scan_codeware(candidates: Iterable[CodewareCandidate]) -> dict[tuple, bool]:
    """Which candidates import Codeware, by their key."""
    budget = [_CODEWARE_FILE_LIMIT]
    return {c.key: imports_codeware(c.path, c.files, budget) for c in candidates}


@dataclass
class Report:
    """Missing frameworks and the active mods that need them."""

    missing: dict[str, list[str]] = field(default_factory=dict)
    # REDmods are active but the REDmod DLC isn't installed
    redmod_without_dlc: list[str] = field(default_factory=list)
    # REDmod files deployed in the game folder by something else than MO2,
    # loaded with -modded while no REDmod is active in MO2
    outside_redmod_deploy: bool = False


@dataclass
class ModFiles:
    name: str
    path: Path
    tree: mobase.IFileTree


def analyze(
    mods: list[ModFiles],
    virtual_tree: mobase.IFileTree | None,
    game_dir: Path | None,
    codeware_users=None,
) -> Report:
    """mods: the active mods, in priority order. codeware_users: given the
    candidates, the names of those that import Codeware (as far as known);
    by default their scripts are read here."""
    provided: set[str] = set()
    if virtual_tree is not None:
        provided |= provided_by_tree(virtual_tree)
    if game_dir is not None:
        for framework in FRAMEWORKS:
            if any((game_dir / marker).is_file() for marker in framework.markers):
                provided.add(framework.key)

    needs: dict[str, list[str]] = {}
    redmod_users: list[str] = []
    for mod in mods:
        own = provided_by_tree(mod.tree)
        provided |= own
        for key in needed_by_tree(mod.tree) - own:
            needs.setdefault(key, []).append(mod.name)
        if uses_redmod(mod.tree):
            redmod_users.append(mod.name)

    report = Report()
    if "codeware" not in provided:
        candidates = codeware_candidates(mods)
        if codeware_users is None:
            found = scan_codeware(candidates)
            users = [c.name for c in candidates if found[c.key]]
        else:
            users = list(codeware_users(candidates))
        if users:
            needs["codeware"] = users
    for framework in FRAMEWORKS:
        users = needs.get(framework.key)
        if users and framework.key not in provided:
            report.missing[framework.key] = users

    if game_dir is not None:
        if redmod_users and not (game_dir / REDMOD_BINARY).is_file():
            report.redmod_without_dlc = redmod_users
        modded = game_dir / "r6/cache/modded"
        if not redmod_users and modded.is_dir():
            try:
                report.outside_redmod_deploy = any(entry.is_file() for entry in os.scandir(modded))
            except OSError:
                pass
    return report
