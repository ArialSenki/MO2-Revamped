"""What a Valheim mod holds, for MO2's Content column and filter."""

from __future__ import annotations

from enum import IntEnum, auto

import mobase

from ..revamped_shared import contains_suffix, find_dir, is_dir, name_of


class Content(IntEnum):
    BEPINEX = auto()
    PLUGINS = auto()
    PATCHERS = auto()
    CONFIG = auto()
    INSLIMVML = auto()
    ASSEMBLIES = auto()
    BUILDS = auto()


CONTENTS = (
    (Content.BEPINEX, "BepInEx", ":/MO/gui/content/skse"),
    (Content.PLUGINS, "BepInEx plugins", ":/MO/gui/content/plugin"),
    (Content.PATCHERS, "BepInEx patchers", ":/MO/gui/content/script"),
    (Content.CONFIG, "Settings (.cfg)", ":/MO/gui/content/inifile"),
    (Content.INSLIMVML, "InSlimVML mods", ":/MO/gui/content/modgroup"),
    (Content.ASSEMBLIES, "Game assemblies or assets", ":/MO/gui/content/bsa"),
    (Content.BUILDS, "Advanced Builder builds", ":/MO/gui/content/mesh"),
)


def contents_of(tree: mobase.IFileTree) -> set[Content]:
    found: set[Content] = set()
    top = {name_of(e): e for e in tree}
    if "winhttp.dll" in top or tree.exists("BepInEx/core/BepInEx.dll"):
        found.add(Content.BEPINEX)
    if contains_suffix(find_dir(tree, "BepInEx/plugins"), (".dll",)):
        found.add(Content.PLUGINS)
    if contains_suffix(find_dir(tree, "BepInEx/patchers"), (".dll",)):
        found.add(Content.PATCHERS)
    config = find_dir(tree, "BepInEx/config")
    if config is not None and len(config) > 0:
        found.add(Content.CONFIG)
    if "inslimvml" in top or "inslimvml.ini" in top:
        found.add(Content.INSLIMVML)
    if any(name in top for name in ("unstripped_corlib", "unstripped_managed", "valheim_data")):
        found.add(Content.ASSEMBLIES)
    if "advancedbuilder" in top and is_dir(top["advancedbuilder"]):
        found.add(Content.BUILDS)
    return found


class ValheimModDataContent(mobase.ModDataContent):
    def getAllContents(self) -> list[mobase.ModDataContent.Content]:
        return [mobase.ModDataContent.Content(int(key), name, icon) for key, name, icon in CONTENTS]

    def getContentsFor(self, filetree: mobase.IFileTree) -> list[int]:
        try:
            return [int(content) for content in contents_of(filetree)]
        except Exception:  # noqa: BLE001 - a mod's files changing under it
            return []
