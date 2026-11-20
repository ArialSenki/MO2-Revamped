"""Valheim mod layouts: BepInEx's (revamped_shared.bepinex), plus the older
InSlimVML loader's mods, Advanced Builder's builds and replaced game assets.
"""

from __future__ import annotations

import mobase

from ..revamped_shared import name_of
from ..revamped_shared.bepinex import BEPINEX_VALID, BepInExModDataChecker

# what the game folder takes from a mod as it is
VALID = BEPINEX_VALID | frozenset({
    "unstripped_corlib", "unstripped_managed", "start_game_bepinex.sh", "start_server_bepinex.sh",
    "inslimvml", "inslimvml.ini", "valheim_data", "advancedbuilder",
})  # fmt: skip


class ValheimModDataChecker(BepInExModDataChecker):
    VALID_NAMES = VALID
    PACK_WRAPPERS = frozenset({"bepinexpack_valheim"})
    # folders a plugin brings next to its DLL
    PLUGIN_FOLDERS = frozenset({"jotunn", "translations", "customtextures", "assets"})

    def extra_file_target(self, name: str) -> str | None:
        if name.endswith("_vml.dll"):
            return "InSlimVML/Mods/"
        if name.endswith(".vbuild"):
            return "AdvancedBuilder/Builds/"
        if name.endswith(".assets"):
            return "valheim_Data/"
        return None

    def extra_folder_target(self, folder: mobase.IFileTree) -> str | None:
        if name_of(folder) == "builds":
            return "AdvancedBuilder/"
        return None
