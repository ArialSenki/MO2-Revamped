"""What a Stardew Valley mod holds, for MO2's Content column and filter.

MO2 hands only the file tree, so the kind comes from the files next to each
manifest.json: a .dll (SMAPI mod), content.json (Content Patcher pack)...
"""

from __future__ import annotations

from enum import IntEnum, auto

import mobase

from ..revamped_shared import is_dir, name_of
from .layout import MAX_DEPTH, has_manifest, smapi_ignores


class Content(IntEnum):
    SMAPI_MOD = auto()
    CONTENT_PATCHER = auto()
    CONTENT_PACK = auto()
    TRANSLATIONS = auto()
    CONFIG = auto()


CONTENTS = (
    (Content.SMAPI_MOD, "SMAPI mod (code)", ":/MO/gui/content/plugin"),
    (Content.CONTENT_PATCHER, "Content Patcher pack", ":/MO/gui/content/texture"),
    (Content.CONTENT_PACK, "Other content pack", ":/MO/gui/content/modgroup"),
    (Content.TRANSLATIONS, "Translations (i18n)", ":/MO/gui/content/interface"),
    (Content.CONFIG, "Settings (config.json)", ":/MO/gui/content/inifile"),
)


def mod_folders(tree: mobase.IFileTree, depth: int = 0) -> list[mobase.IFileTree]:
    """The folders with a manifest.json, as SMAPI finds them."""
    found = []
    for entry in tree:
        if not is_dir(entry) or smapi_ignores(entry):
            continue
        if has_manifest(entry):
            found.append(entry)
        elif depth < MAX_DEPTH:
            found.extend(mod_folders(entry, depth + 1))
    return found


def contents_of(tree: mobase.IFileTree) -> set[Content]:
    found: set[Content] = set()
    for folder in mod_folders(tree):
        names = {name_of(e): e for e in folder}
        if any(n.endswith(".dll") for n, e in names.items() if not is_dir(e)):
            found.add(Content.SMAPI_MOD)
        elif "content.json" in names:
            found.add(Content.CONTENT_PATCHER)
        else:
            found.add(Content.CONTENT_PACK)
        if "i18n" in names and is_dir(names["i18n"]):
            found.add(Content.TRANSLATIONS)
        if "config.json" in names:
            found.add(Content.CONFIG)
    return found


class StardewValleyModDataContent(mobase.ModDataContent):
    def getAllContents(self) -> list[mobase.ModDataContent.Content]:
        return [mobase.ModDataContent.Content(int(key), name, icon) for key, name, icon in CONTENTS]

    def getContentsFor(self, filetree: mobase.IFileTree) -> list[int]:
        try:
            return [int(content) for content in contents_of(filetree)]
        except Exception:  # noqa: BLE001 - a mod's files changing under it
            return []
