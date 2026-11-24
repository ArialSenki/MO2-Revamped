"""MO2 Revamped: Tools > Reforged UI images, and its notifications.

ELDEN RING Reforged's tutorial and item pictures come from an archive its own
tool, Gideon, makes from the game's (see
basic_games/games/eldenring/reforged_ui.py). The tool makes them on demand;
the diagnose says when they are missing, made for an older Reforged, or
hidden by another mod's 00_Solo.
"""

from __future__ import annotations

from pathlib import Path

import mobase
from PyQt6.QtGui import QIcon
from PyQt6.QtWidgets import QMessageBox

from basic_games.games.eldenring import reforged_ui
from basic_games.games.eldenring.common import _tr


def _mod_folders(organizer) -> list[tuple[str, Path, bool]]:
    mods = []
    try:
        mod_list = organizer.modList()
        for name in mod_list.allModsByProfilePriority():
            mod = mod_list.getMod(name)
            if mod is None:
                continue
            active = bool(mod_list.state(name) & mobase.ModState.ACTIVE)
            mods.append((name, Path(mod.absolutePath()), active))
    except (AttributeError, RuntimeError):
        pass
    return mods


def _manages_elden_ring(organizer) -> bool:
    try:
        game = organizer.managedGame() if organizer is not None else None
        return game is not None and game.gameShortName().casefold() == "eldenring"
    except Exception:
        return False


def _game_directory(organizer) -> Path | None:
    try:
        return Path(organizer.managedGame().gameDirectory().absolutePath())
    except (AttributeError, RuntimeError):
        return None


class EldenRingMo2ReforgedUiTool(mobase.IPluginTool):
    def __init__(self):
        super().__init__()
        self._organizer = None
        self._parent = None
        self.diagnose = None

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        return True

    def name(self) -> str:
        return "Elden Ring MO2 Reforged UI Images"

    def localizedName(self) -> str:
        return self.displayName()

    def author(self) -> str:
        return "MO2 Revamped"

    def description(self) -> str:
        return _tr("Makes ELDEN RING Reforged's UI images (tutorials, item pictures) with its tool, Gideon.")

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(1, 0, 0, mobase.ReleaseType.FINAL)

    def requirements(self):
        return [mobase.PluginRequirementFactory.gameDependency({"ELDEN RING"})]

    def settings(self) -> list[mobase.PluginSetting]:
        return []

    def enabledByDefault(self) -> bool:
        return True

    def displayName(self) -> str:
        return _tr("Reforged UI images...")

    def tooltip(self) -> str:
        return _tr("Make the tutorial and item pictures of ELDEN RING Reforged from your game files.")

    def icon(self) -> QIcon:
        return QIcon()

    def setParentWidget(self, widget) -> None:
        self._parent = widget

    def _reforged(self) -> tuple[str, Path] | None:
        mods = _mod_folders(self._organizer)
        found = reforged_ui.active_reforged(mods)
        if found is not None:
            return found
        # a Reforged turned off still can have its images made
        for name, root, _active in reversed(mods):
            if reforged_ui.is_reforged_mod(root):
                return name, root
        return None

    def display(self) -> None:
        if not _manages_elden_ring(self._organizer):
            return
        title = _tr("Reforged UI images")
        found = self._reforged()
        if found is None:
            QMessageBox.information(
                self._parent, title,
                _tr("No ELDEN RING Reforged mod is installed. Install Reforged from its archive "
                    "and MO2 makes its UI images after the install."),
            )
            return
        name, root = found
        state = reforged_ui.status(root)
        if state == "no-tool":
            QMessageBox.warning(self._parent, title, reforged_ui.check_before_run(root, None))
            return
        texts = {
            "ready": _tr("{0} has its UI images. Make them again if Reforged's tutorials or "
                         "item pictures are missing in the game."),
            "missing": _tr("{0} doesn't have its UI images yet: its tutorials show \"You are "
                           "missing Reforged UI images\" and its item pictures are missing."),
            "outdated": _tr("{0}'s UI images were made for an older version of Reforged."),
        }
        box = QMessageBox(self._parent)
        box.setIcon(QMessageBox.Icon.Question if state == "ready" else QMessageBox.Icon.Warning)
        box.setWindowTitle(title)
        box.setText(texts[state].format(name))
        details = _tr(
            "MO2 runs Reforged's own tool, Gideon: it copies the game's UI archive (00_Solo) "
            "from your game files and adds Reforged's pictures, about 2 GB in the mod's "
            "folder, in under a minute. The game folder isn't changed."
        )
        others, winner = reforged_ui.other_providers(_mod_folders(self._organizer), name)
        if others:
            details += "\n\n" + reforged_ui.conflict_text(others, winner, name)
        box.setInformativeText(details)
        create = box.addButton(
            _tr("Make them again") if state == "ready" else _tr("Create them"),
            QMessageBox.ButtonRole.AcceptRole,
        )
        box.addButton(_tr("Cancel"), QMessageBox.ButtonRole.RejectRole)
        box.setDefaultButton(create)
        box.exec()
        box.deleteLater()
        if box.clickedButton() is not create:
            return
        if reforged_ui.create_images(
            self._parent, name, root, _game_directory(self._organizer), self._organizer
        ):
            QMessageBox.information(self._parent, title, _tr("Reforged's UI images are ready."))
        if self.diagnose is not None:
            try:
                self.diagnose._invalidate()
            except (AttributeError, RuntimeError):
                pass


class EldenRingMo2ReforgedUiDiagnose(mobase.IPluginDiagnose):
    """Says when the active Reforged's UI images are missing, out of date,
    or hidden by another mod's."""

    Missing = 0
    NoTool = 1
    Conflict = 2

    def __init__(self, tool: EldenRingMo2ReforgedUiTool) -> None:
        super().__init__()
        self._organizer = None
        self._tool = tool

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        return True

    def name(self) -> str:
        return "Elden Ring MO2 Reforged UI Diagnose"

    def localizedName(self) -> str:
        return _tr("Reforged UI image checks")

    def author(self) -> str:
        return "MO2 Revamped"

    def description(self) -> str:
        return _tr("Warns when ELDEN RING Reforged's UI images are missing or hidden by another mod.")

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(1, 0, 0, mobase.ReleaseType.FINAL)

    def requirements(self) -> list:
        return [mobase.PluginRequirementFactory.gameDependency({"ELDEN RING"})]

    def settings(self) -> list[mobase.PluginSetting]:
        return []

    def _state(self):
        if not _manages_elden_ring(self._organizer):
            return None
        mods = _mod_folders(self._organizer)
        found = reforged_ui.active_reforged(mods)
        if found is None:
            return None
        name, root = found
        others, winner = reforged_ui.other_providers(mods, name)
        return name, root, reforged_ui.status(root), others, winner

    def activeProblems(self) -> list[int]:
        state = self._state()
        if state is None:
            return []
        name, _root, status, others, winner = state
        problems = []
        if status in {"missing", "outdated"}:
            problems.append(self.Missing)
        elif status == "no-tool":
            problems.append(self.NoTool)
        # another mod's archive over Reforged's: Reforged's pictures don't show
        if others and winner != name:
            problems.append(self.Conflict)
        return problems

    def shortDescription(self, key: int) -> str:
        if key == self.Conflict:
            return _tr("Another mod's UI images hide ELDEN RING Reforged's")
        return _tr("ELDEN RING Reforged's UI images are missing")

    def fullDescription(self, key: int) -> str:
        state = self._state()
        if state is None:
            return ""
        name, root, status, others, winner = state
        if key == self.Conflict:
            return "<p>" + reforged_ui.conflict_text(others, winner, name) + "</p>"
        if key == self.NoTool:
            return "<p>" + reforged_ui.check_before_run(root, None) + "</p>"
        first = (
            _tr("{0}'s UI images were made for an older version of Reforged.")
            if status == "outdated"
            else _tr("{0} doesn't have its UI images yet: its tutorials show \"You are "
                     "missing Reforged UI images\" and its item pictures are missing.")
        ).format(name)
        return "<p>" + first + "</p><p>" + _tr(
            "Fix makes them with Reforged's own tool, Gideon, from your game files (about "
            "2 GB, under a minute). Tools > Reforged UI images... does the same."
        ) + "</p>"

    def hasGuidedFix(self, key: int) -> bool:
        return key == self.Missing

    def startGuidedFix(self, key: int) -> None:
        if key == self.Missing:
            self._tool.display()


def createPlugins() -> list:
    tool = EldenRingMo2ReforgedUiTool()
    diagnose = EldenRingMo2ReforgedUiDiagnose(tool)
    tool.diagnose = diagnose
    return [tool, diagnose]
