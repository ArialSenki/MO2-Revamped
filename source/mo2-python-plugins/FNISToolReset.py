# MO2 Revamped: resets the FNIS integration's choices, rewritten for Revamped.
# It keeps the plugin name of Mod Organizer 2's FNIS Integration Tool Reset by
# LostDragonist (GNU GPL version 3) and what it does: the next run of FNIS asks
# for its setup again.

from __future__ import annotations

from PyQt6.QtGui import QIcon
from PyQt6.QtWidgets import QMessageBox, QWidget

import mobase

from FNISTool import TOOL_NAME, setup, tr


class FNISToolReset(mobase.IPluginTool):

    def __init__(self):
        super().__init__()
        self._organizer: mobase.IOrganizer | None = None
        self._parent: QWidget | None = None

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        return True

    def name(self) -> str:
        return "FNIS Integration Tool Reset"

    def localizedName(self) -> str:
        return tr("FNIS Integration Tool Reset")

    def author(self) -> str:
        return "LostDragonist, MO2 Revamped"

    def description(self) -> str:
        return tr("Changes where FNIS is and where its files and logs go.")

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(2, 0, 0, mobase.ReleaseType.FINAL)

    def master(self) -> str:
        return TOOL_NAME

    def settings(self) -> list:
        return []

    def displayName(self) -> str:
        return tr("FNIS/Change FNIS settings")

    def tooltip(self) -> str:
        return self.description()

    def icon(self) -> QIcon:
        return QIcon()

    def setParentWidget(self, widget: QWidget) -> None:
        self._parent = widget

    def display(self) -> None:
        # the setup window shows the current choices; Cancel leaves them as they are
        setup(self._organizer, self._parent, False)


def createPlugin() -> mobase.IPlugin:
    return FNISToolReset()
