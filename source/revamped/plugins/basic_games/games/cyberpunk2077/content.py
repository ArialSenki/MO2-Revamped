"""What a Cyberpunk 2077 mod holds, for MO2's Content column and filter.

MO2 asks this of every mod in the list once (and again when a mod's files
change), so it looks only at the few folders that tell the kinds apart.
"""

from __future__ import annotations

from enum import IntEnum, auto

import mobase

from .frameworks import FRAMEWORKS, PREFIXES
from .layout import TWEAK_SUFFIXES, contains_suffix, suffixes_in


class Content(IntEnum):
    ARCHIVE = auto()
    ARCHIVEXL = auto()
    REDMOD = auto()
    RED4EXT = auto()
    CET = auto()
    REDSCRIPT = auto()
    TWEAKS = auto()
    CONFIG = auto()
    AUDIO = auto()
    FRAMEWORK = auto()


CONTENTS = (
    (Content.ARCHIVE, "Archives", ":/MO/gui/content/bsa"),
    (Content.ARCHIVEXL, "ArchiveXL files", ":/MO/gui/content/mesh"),
    (Content.REDMOD, "REDmod", ":/MO/gui/content/modgroup"),
    (Content.RED4EXT, "RED4ext plugin", ":/MO/gui/content/plugin"),
    (Content.CET, "Cyber Engine Tweaks mod", ":/MO/gui/content/interface"),
    (Content.REDSCRIPT, "redscript", ":/MO/gui/content/script"),
    (Content.TWEAKS, "TweakXL tweaks", ":/MO/gui/content/inifile"),
    (Content.CONFIG, "Game settings and input", ":/MO/gui/content/menu"),
    (Content.AUDIO, "Audio", ":/MO/gui/content/sound"),
    (Content.FRAMEWORK, "Framework (RED4ext, CET, redscript...)", ":/MO/gui/content/skse"),
)

_framework_markers = tuple(
    prefix + marker for framework in FRAMEWORKS for marker in framework.markers for prefix in PREFIXES
)


def _dir(tree: mobase.IFileTree, path: str) -> mobase.IFileTree | None:
    entry = tree.find(path, mobase.FileTreeEntry.FileTypes.DIRECTORY)
    return entry if isinstance(entry, mobase.IFileTree) else None


def contents_of(tree: mobase.IFileTree) -> set[Content]:
    found: set[Content] = set()
    # each lookup crosses into MO2's C++ tree: only the folders this mod has
    top = {entry.name().casefold() for entry in tree if isinstance(entry, mobase.IFileTree)}
    if any(marker.split("/", 1)[0] in top and tree.exists(marker) for marker in _framework_markers):
        found.add(Content.FRAMEWORK)
    roots = [tree]
    if "root" in top:
        root = _dir(tree, "root")
        if root is not None:
            roots.append(root)
    for base in roots:
        names = top if base is tree else {e.name().casefold() for e in base if isinstance(e, mobase.IFileTree)}
        if "archive" in names:
            archives = _dir(base, "archive/pc/mod")
            kinds = suffixes_in(archives, (".archive", ".xl"))
            if ".archive" in kinds:
                found.add(Content.ARCHIVE)
            if ".xl" in kinds:
                found.add(Content.ARCHIVEXL)
        if "red4ext" in names:
            plugins = _dir(base, "red4ext/plugins")
            if plugins is not None and len(plugins) > 0:
                found.add(Content.RED4EXT)
        if "bin" in names:
            cet = _dir(base, "bin/x64/plugins/cyber_engine_tweaks/mods")
            if cet is not None and len(cet) > 0:
                found.add(Content.CET)
        if "r6" in names:
            r6 = _dir(base, "r6")
            for entry in r6 if r6 is not None else ():
                name = entry.name().casefold()
                if name == "scripts":
                    found.add(Content.REDSCRIPT)
                elif name == "tweaks" and contains_suffix(entry, TWEAK_SUFFIXES):
                    found.add(Content.TWEAKS)
                elif name in ("config", "input"):
                    found.add(Content.CONFIG)
                elif name == "audioware":
                    found.add(Content.AUDIO)
        if "engine" in names and _dir(base, "engine/config") is not None:
            found.add(Content.CONFIG)
    if "mods" in top:
        mods = _dir(tree, "mods")
        for child in mods if mods is not None else ():
            if isinstance(child, mobase.IFileTree) and child.exists(
                "info.json", mobase.FileTreeEntry.FileTypes.FILE
            ):
                found.add(Content.REDMOD)
                if _dir(child, "customSounds") is not None:
                    found.add(Content.AUDIO)
    return found


class CyberpunkModDataContent(mobase.ModDataContent):
    def getAllContents(self) -> list[mobase.ModDataContent.Content]:
        return [mobase.ModDataContent.Content(int(key), name, icon) for key, name, icon in CONTENTS]

    def getContentsFor(self, filetree: mobase.IFileTree) -> list[int]:
        try:
            return [int(content) for content in contents_of(filetree)]
        except Exception:  # noqa: BLE001 - a mod's files changing under it
            return []
