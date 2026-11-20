import os
import stat
from pathlib import Path

import mobase
from PyQt6.QtCore import QCoreApplication


class Form43Checker(mobase.IPluginDiagnose):
    __organizer: mobase.IOrganizer

    def __init__(self):
        super().__init__()
        # MO2 Revamped: the form of each plugin by path, kept while its size and
        # modification time stay the same. Every problem check opened all the
        # plugins again (~0.5 s with a large Skyrim list, holding Python's lock)
        self.__forms: dict[str, tuple[tuple[int, int], int]] = {}

    def init(self, organizer: mobase.IOrganizer):
        self.__organizer = organizer
        return True

    def name(self):
        return "Form 43 Plugin Checker"

    def localizedName(self):
        return self.tr("Form 43 Plugin Checker")

    def author(self):
        return "AnyOldName3"

    def description(self):
        return self.tr(
            "In Skyrim Special Edition, warns about plugins still saved for the original "
            "Skyrim (form 43 or older)."
        )

    def version(self):
        return mobase.VersionInfo(1, 2, 0, mobase.ReleaseType.PRE_ALPHA)

    def requirements(self):
        return [
            mobase.PluginRequirementFactory.gameDependency("Skyrim Special Edition")
        ]

    def settings(self) -> list[mobase.PluginSetting]:
        return []

    def activeProblems(self) -> list[int]:
        if self.__scanPlugins():
            return [0]
        else:
            return []

    def shortDescription(self, key: int) -> str:
        return self.tr("Plugins made for the original Skyrim")

    def fullDescription(self, key: int) -> str:
        # MO2 Revamped: what it means and what to do, in plain words
        plugins = [Path(path).name for path in self.__listPlugins()]
        listed = "<br>".join("\u2022 " + name for name in plugins)
        return self.tr(
            "These plugins were saved for the original Skyrim (form 43 or older) and not "
            "converted for Skyrim Special Edition, which uses form 44:<br><br>{0}<br><br>"
            "Parts of mods like these often don't work. Saving a plugin again in the "
            "Skyrim Special Edition Creation Kit converts it, but a plugin like this can "
            "also mean the whole mod wasn't ported: look for the mod's Special Edition "
            "version first."
        ).format(listed)

    def hasGuidedFix(self, key: int) -> bool:
        return False

    def startGuidedFix(self, key: int) -> None:
        # Maybe we could use xEdit or something to resave the file?
        pass

    def tr(self, value: str):
        return QCoreApplication.translate("Form43Checker", value)

    def __testFile(self, path: str) -> bool | None:
        version = self.__getForm(path)
        if isinstance(version, int):
            return version < 44
        else:
            return None

    def __getForm(self, file: str) -> int | str:
        try:
            info = os.stat(file)
        except OSError:
            self.__forms.pop(file, None)
            return "invalid"
        if not stat.S_ISREG(info.st_mode):
            return "invalid"
        key = (info.st_size, info.st_mtime_ns)
        cached = self.__forms.get(file)
        if cached is not None and cached[0] == key:
            return cached[1]
        with open(file, mode="rb") as fp:
            fp.seek(20)
            form = int.from_bytes(fp.read(2), byteorder="little")
        self.__forms[file] = (key, form)
        return form

    def __listInvalidFiles(self):
        for file in self.__organizer.findFiles("", "*.es[pml]"):
            if self.__testFile(file):
                yield file

    def __scanPlugins(self):
        if self.__organizer.managedGame().gameName() != "Skyrim Special Edition":
            return False

        # Return True if there is at least one invalid file:
        return next(self.__listInvalidFiles(), False)

    def __listPlugins(self):
        return [
            "{} (form {})".format(file, self.__getForm(file))
            for file in self.__listInvalidFiles()
        ]


def createPlugin():
    return Form43Checker()
