"""How Baldur's Gate 3 mods are laid out: the installer's checker and the
mod list's "Content" column.

The checker is basic_games' (daescha) with MO2 Revamped's fixes: the folders
of Wabbajack lists made with the unofficial BG3 plugin (Root/, PAK_FILES/,
SE_CONFIG/, game folders at the top level), and readmes that don't make an
installed mod look broken.
"""

from __future__ import annotations

from enum import IntEnum
from pathlib import Path

import mobase

from ...basic_features import BasicModDataChecker, GlobPatterns, utils
from .util import (
    LOOSE_FILE_FOLDERS,
    PAK_FILES_FOLDER,
    SE_CONFIG_FOLDER,
    TOP_LEVEL_DATA_FOLDERS,
    tr,
)


class BG3ModDataChecker(BasicModDataChecker):
    def __init__(self):
        super().__init__(
            GlobPatterns(
                valid=[
                    "*.pak",
                    str(Path("Mods") / "*.pak"),  # standard mods
                    "bin",  # native mods / Script Extender / Native Mod Loader
                    "Script Extender",  # mods configured with jsons in this folder
                    "Data",  # loose file mods
                ]
                + [str(Path("*") / f) for f in LOOSE_FILE_FOLDERS],
                move={
                    "Root/": "",  # root builder not needed
                    "*.dll": "bin/",
                    # a native mod's config goes with its DLL
                    # (BG3WASD.dll + BG3WASD.toml at the archive's top level)
                    "*.toml": "bin/",
                    "ScriptExtenderSettings.json": "bin/",
                }
                | {f: "Data/" for f in LOOSE_FILE_FOLDERS},
                delete=["info.json", "*.txt"],
            )
        )

    # the layout of mods that Wabbajack lists install as they are: Root/ goes
    # to the game folder (MO2, or Root Builder), the game folders to
    # <game>/Data and SE_CONFIG/ to Script Extender's folder (the mapper)
    _mapped_folders = {f.casefold() for f in TOP_LEVEL_DATA_FOLDERS} | {
        "root",
        SE_CONFIG_FOLDER.casefold(),
    }

    def dataLooksValid(
        self, filetree: mobase.IFileTree
    ) -> mobase.ModDataChecker.CheckReturn:
        status = mobase.ModDataChecker.INVALID
        rp = self._regex_patterns
        # a readme next to valid files doesn't make an installed mod "fixable",
        # which MO2 shows as having no valid game data; docs alone aren't data
        for entry in filetree:
            name = entry.name().casefold()
            if not entry.isDir() and rp.delete.match(name):
                continue
            if rp.unfold.match(name):
                if utils.is_directory(entry):
                    status = self.dataLooksValid(entry)
                else:
                    status = mobase.ModDataChecker.INVALID
                    break
            elif rp.valid.match(name):
                if status is mobase.ModDataChecker.INVALID:
                    status = mobase.ModDataChecker.VALID
            elif utils.is_directory(entry) and (
                name in self._mapped_folders
                or (name == PAK_FILES_FOLDER.casefold() and _has_pak(entry))
            ):
                if status is mobase.ModDataChecker.INVALID:
                    status = mobase.ModDataChecker.VALID
            elif isinstance(entry, mobase.IFileTree):
                status = (
                    mobase.ModDataChecker.VALID
                    if all(rp.valid.match(e.pathFrom(filetree)) for e in entry)
                    else mobase.ModDataChecker.INVALID
                )
            elif rp.delete.match(name) or rp.move_match(name) is not None:
                status = mobase.ModDataChecker.FIXABLE
            else:
                status = mobase.ModDataChecker.INVALID
                break
        return status


def _has_pak(folder: mobase.IFileTree) -> bool:
    found = False

    def find_pak(_: str, entry: mobase.FileTreeEntry):
        nonlocal found
        if not entry.isDir() and entry.hasSuffix("pak"):
            found = True
            return mobase.IFileTree.WalkReturn.STOP
        return mobase.IFileTree.WalkReturn.CONTINUE

    folder.walk(find_pak)
    return found


# ------------------------------------------------------------------ content


class Content(IntEnum):
    # the first five keep the numbers of the original plugin's column
    PAK = 1
    WORKSPACE = 2
    NATIVE = 3
    LOOSE_FILES = 4
    SE_FILES = 5
    SCRIPT_EXTENDER = 6
    NATIVE_MOD_LOADER = 7


_CONTENTS = [
    (Content.PAK, "Pak", ":/MO/gui/content/bsa"),
    (Content.LOOSE_FILES, "Loose file override mod", ":/MO/gui/content/texture"),
    (Content.SE_FILES, "Script Extender Files", ":/MO/gui/content/inifile"),
    (Content.NATIVE, "Native DLL mod", ":/MO/gui/content/plugin"),
    (Content.SCRIPT_EXTENDER, "Script Extender", ":/MO/gui/content/skse"),
    (Content.NATIVE_MOD_LOADER, "Native Mod Loader", ":/MO/gui/content/script"),
    (Content.WORKSPACE, "Mod workspace", ":/MO/gui/content/script"),
]

_loose_folders = {f.casefold() for f in TOP_LEVEL_DATA_FOLDERS if f != "Mods"}
_loose_file_folders = {f.casefold() for f in LOOSE_FILE_FOLDERS}


class BG3DataContent(mobase.ModDataContent):
    def getAllContents(self) -> list[mobase.ModDataContent.Content]:
        return [
            mobase.ModDataContent.Content(id, tr(name), icon)
            for id, name, icon in _CONTENTS
        ]

    def getContentsFor(self, filetree: mobase.IFileTree) -> list[int]:
        contents: set[int] = set()
        for entry in filetree:
            folded = entry.name().casefold()
            if not isinstance(entry, mobase.IFileTree):
                if folded.endswith(".pak"):
                    contents.add(Content.PAK)
                continue
            if folded == "root":
                bin_tree = entry.find("bin")
                if isinstance(bin_tree, mobase.IFileTree):
                    _bin_contents(bin_tree, contents)
                data = entry.find("Data")
                if data is not None:
                    contents.add(Content.LOOSE_FILES)
            elif folded == PAK_FILES_FOLDER.casefold():
                contents.add(Content.PAK)
            elif folded in (SE_CONFIG_FOLDER.casefold(), "script extender"):
                contents.add(Content.SE_FILES)
            elif folded in _loose_folders or folded == "data":
                contents.add(Content.LOOSE_FILES)
            elif folded == "mods":
                for e in entry:
                    if e.name().casefold().endswith(".pak"):
                        contents.add(Content.PAK)
                    elif isinstance(e, mobase.IFileTree):
                        contents.add(Content.LOOSE_FILES)
            elif folded == "bin":
                _bin_contents(entry, contents)
            else:
                for e in entry:
                    if e.name().casefold() in _loose_file_folders:
                        contents.add(Content.WORKSPACE)
                        break
        return sorted(contents)


def _bin_contents(bin_tree: mobase.IFileTree, contents: set[int]) -> None:
    for e in bin_tree:
        name = e.name().casefold()
        if isinstance(e, mobase.IFileTree):
            if name == "nativemods":
                contents.add(Content.NATIVE)
            continue
        if name == "dwrite.dll":
            contents.add(Content.SCRIPT_EXTENDER)
        elif name in ("bink2w64_original.dll",):
            contents.add(Content.NATIVE_MOD_LOADER)
        elif name in ("scriptextendersettings.json", "scriptextenderupdaterconfig.json"):
            contents.add(Content.SE_FILES)
        elif name.endswith(".dll") and name != "bink2w64.dll":
            contents.add(Content.NATIVE)
