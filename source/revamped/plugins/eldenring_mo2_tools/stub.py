"""MO2 Revamped: the Elden Ring tools' stand-ins for the other games.

MO2 loads plugins/eldenring_mo2_*.py in every instance, and evaluates each
from its source every time. Their code is in this folder (which MO2 doesn't
load as a plugin): each file imports it only when the instance manages
ELDEN RING or ELDEN RING NIGHTREIGN, or when MO2 doesn't say which game it
manages. In any other instance it gives MO2 these stand-ins: they answer what
MO2 asks every plugin when it loads them (name, version, requirements,
settings), and their requirement keeps them inactive there, as the real tools
are.
"""

from __future__ import annotations

import importlib
import re
import sys
from pathlib import Path

import mobase
from PyQt6.QtCore import QCoreApplication
from PyQt6.QtGui import QIcon

GAMES = {"ELDEN RING", "ELDEN RING NIGHTREIGN"}


def instance_needs_tools() -> bool:
    """True for an Elden Ring or Nightreign instance, or when MO2 didn't say."""
    app = QCoreApplication.instance()
    game = app.property("revampedInstanceGame") if app is not None else None
    if not isinstance(game, str) or not game:
        return True
    return game.casefold() in {g.casefold() for g in GAMES}


def tool_module(name: str):
    """eldenring_mo2_tools.<name>, evaluated again on each plugin load as the
    file plugin was (MO2 restarting in the same process reloads plugins)."""
    full = f"eldenring_mo2_tools.{name}"
    if full in sys.modules:
        return importlib.reload(sys.modules[full])
    return importlib.import_module(full)


def file_version(name: str, default: tuple[int, ...]) -> mobase.VersionInfo:
    """The tool's version as its version() states it (VersionInfo(0, 5, 0, n)),
    read from its file so that the two can't drift apart."""
    try:
        text = (Path(__file__).parent / name).read_text(encoding="utf-8")
        match = re.search(r"VersionInfo\((\d+), (\d+), (\d+), (\d+)\)", text)
        if match:
            return mobase.VersionInfo(*(int(g) for g in match.groups()))
    except OSError:
        pass
    return mobase.VersionInfo(*default)


def _requirements() -> list:
    return [mobase.PluginRequirementFactory.gameDependency(set(GAMES))]


class ToolStub(mobase.IPluginTool):
    def __init__(self, name, context, display_name, description, tooltip, version):
        super().__init__()
        self._name, self._context = name, context
        self._display, self._description, self._tooltip = display_name, description, tooltip
        self._version = version

    def _tr(self, text: str) -> str:
        return QCoreApplication.translate(self._context, text)

    def init(self, organizer: mobase.IOrganizer) -> bool:
        return True

    def name(self) -> str:
        return self._name

    def localizedName(self) -> str:
        return self.displayName()

    def author(self) -> str:
        return "ArialSenki"

    def description(self) -> str:
        return self._tr(self._description)

    def version(self) -> mobase.VersionInfo:
        return self._version

    def requirements(self):
        return _requirements()

    def settings(self) -> list[mobase.PluginSetting]:
        return []

    def enabledByDefault(self) -> bool:
        return True

    def displayName(self) -> str:
        return self._tr(self._display)

    def tooltip(self) -> str:
        return self._tr(self._tooltip)

    def icon(self) -> QIcon:
        return QIcon()

    def setParentWidget(self, widget) -> None:
        pass

    def display(self) -> None:
        pass


class DiagnoseStub(mobase.IPluginDiagnose):
    def __init__(self, name, context, localized_name, description, version):
        super().__init__()
        self._name, self._context = name, context
        self._localized, self._description, self._version = localized_name, description, version

    def init(self, organizer: mobase.IOrganizer) -> bool:
        return True

    def name(self) -> str:
        return self._name

    def localizedName(self) -> str:
        return QCoreApplication.translate(self._context, self._localized)

    def author(self) -> str:
        return "ArialSenki"

    def description(self) -> str:
        return QCoreApplication.translate(self._context, self._description)

    def version(self) -> mobase.VersionInfo:
        return self._version

    def requirements(self) -> list:
        return _requirements()

    def settings(self) -> list[mobase.PluginSetting]:
        return []

    def activeProblems(self) -> list[int]:
        return []

    def shortDescription(self, key: int) -> str:
        return ""

    def fullDescription(self, key: int) -> str:
        return ""

    def hasGuidedFix(self, key: int) -> bool:
        return False

    def startGuidedFix(self, key: int) -> None:
        pass
