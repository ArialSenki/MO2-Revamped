from enum import IntEnum, auto

import mobase

from . import bg3_utils


class Content(IntEnum):
    PAK = auto()
    WORKSPACE = auto()
    NATIVE = auto()
    LOOSE_FILES = auto()
    SE_FILES = auto()


# MO2 Revamped: the game folders at a mod's top level, except Mods/ (paks or
# loose folders, told apart below)
_loose_folders = {
    f.casefold() for f in bg3_utils.top_level_data_folders if f != "Mods"
}


class BG3DataContent(mobase.ModDataContent):
    BG3_CONTENTS: list[tuple[Content, str, str, bool] | tuple[Content, str, str]] = [
        (Content.WORKSPACE, "Mod workspace", ":/MO/gui/content/script"),
        (Content.PAK, "Pak", ":/MO/gui/content/bsa"),
        (Content.LOOSE_FILES, "Loose file override mod", ":/MO/gui/content/texture"),
        (Content.SE_FILES, "Script Extender Files", ":/MO/gui/content/inifile"),
        (Content.NATIVE, "Native DLL mod", ":/MO/gui/content/plugin"),
    ]

    def getAllContents(self) -> list[mobase.ModDataContent.Content]:
        return [
            mobase.ModDataContent.Content(id, name, icon, *filter_only)
            for id, name, icon, *filter_only in self.BG3_CONTENTS
        ]

    def getContentsFor(self, filetree: mobase.IFileTree) -> list[int]:
        contents: set[int] = set()
        for entry in filetree:
            if isinstance(entry, mobase.IFileTree):
                # MO2 Revamped: also the folders of the unofficial BG3 plugin
                # that Wabbajack lists keep (see BG3ModDataChecker)
                folded = entry.name().casefold()
                if folded == "root":
                    if entry.find("bin") is not None:
                        contents.add(Content.NATIVE)
                    continue
                if folded == bg3_utils.pak_files_folder.casefold():
                    contents.add(Content.PAK)
                    continue
                if folded == bg3_utils.se_config_folder.casefold():
                    contents.add(Content.SE_FILES)
                    continue
                if folded in _loose_folders:
                    contents.add(Content.LOOSE_FILES)
                    continue
                match folded:
                    case "script extender":
                        contents.add(Content.SE_FILES)
                    case "data":
                        contents.add(Content.LOOSE_FILES)
                    case "mods":
                        for e in entry:
                            if e.name().casefold().endswith(".pak"):
                                contents.add(Content.PAK)
                            elif isinstance(e, mobase.IFileTree):
                                contents.add(Content.LOOSE_FILES)
                    case "bin":
                        contents.add(Content.NATIVE)
                    case _:
                        for e in entry:
                            if e.name() in bg3_utils.loose_file_folders:
                                contents.add(Content.WORKSPACE)
                                break
            elif entry.name().casefold().endswith(".pak"):
                contents.add(Content.PAK)
        return list(contents)
