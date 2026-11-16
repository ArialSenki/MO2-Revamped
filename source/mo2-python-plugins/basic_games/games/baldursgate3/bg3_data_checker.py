from pathlib import Path

import mobase

from ...basic_features import BasicModDataChecker, GlobPatterns, utils
from . import bg3_utils


class BG3ModDataChecker(BasicModDataChecker):
    def __init__(self):
        super().__init__(
            GlobPatterns(
                valid=[
                    "*.pak",
                    str(Path("Mods") / "*.pak"),  # standard mods
                    "bin",  # native mods / Script Extender
                    "Script Extender",  # mods which are configured via jsons in this folder
                    "Data",  # loose file mods
                ]
                + [str(Path("*") / f) for f in bg3_utils.loose_file_folders],
                move={
                    "Root/": "",  # root builder not needed
                    "*.dll": "bin/",
                    # MO2 Revamped: a native mod's config goes with its DLL
                    # (BG3WASD.dll + BG3WASD.toml at the archive's top level)
                    "*.toml": "bin/",
                    "ScriptExtenderSettings.json": "bin/",
                }
                | {f: "Data/" for f in bg3_utils.loose_file_folders},
                delete=["info.json", "*.txt"],
            )
        )

    # MO2 Revamped: the layout of mods that Wabbajack lists install as they are
    # (made with the unofficial BG3 plugin): Root/ goes to the game folder
    # (OrganizerCore::fileMapping, or Root Builder), the game folders to
    # <game>/Data and SE_CONFIG/ to Script Extender's folder (BG3FileMapper)
    _mapped_folders = {
        f.casefold() for f in bg3_utils.top_level_data_folders
    } | {"root", bg3_utils.se_config_folder.casefold()}

    def dataLooksValid(
        self, filetree: mobase.IFileTree
    ) -> mobase.ModDataChecker.CheckReturn:
        status = mobase.ModDataChecker.INVALID
        rp = self._regex_patterns
        # MO2 Revamped: a readme next to valid files doesn't make an installed
        # mod "fixable", which MO2 shows as having no valid game data; docs
        # alone aren't game data
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
                or (
                    name == bg3_utils.pak_files_folder.casefold()
                    and self._has_pak(entry)
                )
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

    @staticmethod
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
