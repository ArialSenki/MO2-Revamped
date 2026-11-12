from pathlib import Path

import mobase
from PyQt6.QtCore import QCoreApplication


class Form43Checker(mobase.IPluginDiagnose):
    __organizer: mobase.IOrganizer

    def __init__(self):
        super().__init__()

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
        path = Path(file)
        if path.is_file():
            with path.open(mode="rb") as fp:
                fp.seek(20)
                return int.from_bytes(fp.read(2), byteorder="little")
        else:
            return "invalid"

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
