# MO2 Python plugin for the Bethesda golden tests (copied into a test bed only,
# never shipped): once the interface is up and the plugin list has refreshed,
# copies the profile's plugins.txt and loadorder.txt to MO2R_GOLDEN_OUT.
import os
import shutil

import mobase
from PyQt6.QtCore import QTimer

OUT = os.environ.get("MO2R_GOLDEN_OUT", "")


class GoldenDump(mobase.IPlugin):
    def __init__(self):
        super().__init__()
        self._org = None

    def init(self, organizer):
        self._org = organizer
        if OUT:
            organizer.onUserInterfaceInitialized(lambda w: QTimer.singleShot(5000, self._dump))
        return True

    def _dump(self):
        prof = self._org.profilePath()
        os.makedirs(OUT, exist_ok=True)
        for name in ("plugins.txt", "loadorder.txt"):
            if os.path.exists(os.path.join(prof, name)):
                shutil.copyfile(os.path.join(prof, name), os.path.join(OUT, name))
        with open(os.path.join(OUT, "done"), "w") as f:
            f.write(str(len(self._org.pluginList().pluginNames())))

    def name(self):
        return "MO2R Golden Dump"

    def author(self):
        return "MO2 Revamped"

    def description(self):
        return "Test-only plugin."

    def version(self):
        return mobase.VersionInfo(1, 0, 0)

    def settings(self):
        return []


def createPlugin():
    return GoldenDump()
