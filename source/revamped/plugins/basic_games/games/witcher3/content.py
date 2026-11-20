"""What a Witcher 3 mod holds, for MO2's Content column and filter."""

from __future__ import annotations

from enum import IntEnum, auto

import mobase

from ..revamped_shared import contains_suffix, find_dir, is_dir, name_of
from .layout import MENU_FOLDER

MERGED_MOD = "mod0000_mergedfiles"


class Content(IntEnum):
    SCRIPTS = auto()
    ASSETS = auto()
    DLC = auto()
    MENU = auto()
    NATIVE = auto()
    MERGED = auto()


CONTENTS = (
    (Content.SCRIPTS, "Scripts", ":/MO/gui/content/script"),
    (Content.ASSETS, "Bundles and textures", ":/MO/gui/content/bsa"),
    (Content.DLC, "DLC", ":/MO/gui/content/modgroup"),
    (Content.MENU, "Settings menu", ":/MO/gui/content/menu"),
    (Content.NATIVE, "DLL or ASI", ":/MO/gui/content/plugin"),
    (Content.MERGED, "Script Merger output", ":/MO/gui/content/skse"),
)

_ASSET_SUFFIXES = (".bundle", ".cache", ".store")


def _scan_content(folder: mobase.IFileTree, found: set[Content]) -> None:
    content = find_dir(folder, "content")
    if content is None:
        return
    # the game reads bundles and caches from content/ itself, and scripts from
    # content/scripts: no walk over a texture mod's every file
    if any(not is_dir(e) and name_of(e).endswith(_ASSET_SUFFIXES) for e in content):
        found.add(Content.ASSETS)
    if Content.SCRIPTS not in found and contains_suffix(find_dir(content, "scripts"), (".ws",)):
        found.add(Content.SCRIPTS)


def contents_of(tree: mobase.IFileTree) -> set[Content]:
    found: set[Content] = set()
    for entry in tree:
        if not is_dir(entry):
            continue
        name = name_of(entry)
        if name == "root":
            dlc = find_dir(entry, "dlc")
            if dlc is not None and len(dlc) > 0:
                found.add(Content.DLC)
            if find_dir(entry, MENU_FOLDER) is not None:
                found.add(Content.MENU)
            for native in ("bin/x64", "bin/x64_dx12"):
                folder = find_dir(entry, native)
                if folder is not None and any(
                    name_of(e).endswith((".dll", ".asi")) for e in folder if not is_dir(e)
                ):
                    found.add(Content.NATIVE)
        elif name.startswith("mod"):
            if name == MERGED_MOD:
                found.add(Content.MERGED)
            _scan_content(entry, found)
    return found


class Witcher3ModDataContent(mobase.ModDataContent):
    def getAllContents(self) -> list[mobase.ModDataContent.Content]:
        return [mobase.ModDataContent.Content(int(key), name, icon) for key, name, icon in CONTENTS]

    def getContentsFor(self, filetree: mobase.IFileTree) -> list[int]:
        try:
            return [int(content) for content in contents_of(filetree)]
        except Exception:  # noqa: BLE001 - a mod's files changing under it
            return []
