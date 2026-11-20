"""Baldur's Gate 3: MO2's problem list for the load order. Missing or
late dependencies (meta.lsx Dependencies), the Script Extender that mods
need, conflicts, one module in two mods, and the mods the game turned off on
its last start."""

from __future__ import annotations

import html

import mobase

from ..util import GAME_NAME, PLUGIN_NAME, tr

MISSING, LATE, SCRIPT_EXTENDER, DUPLICATES, CONFLICTS, DROPPED, OLDER = range(7)
LISTED = 12


def _list(lines: list[str]) -> str:
    shown = [f"• {html.escape(line)}" for line in lines[:LISTED]]
    if len(lines) > LISTED:
        shown.append(html.escape(tr("• and {0} more").replace("{0}", str(len(lines) - LISTED))))
    return "<br>".join(shown)


class BG3LoadOrderCheck(mobase.IPluginDiagnose):
    def __init__(self) -> None:
        super().__init__()
        self._plan = None
        self._fresh = False

    # IPlugin

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        mod_list = organizer.modList()
        # MO2 2.5's form: one call with every changed mod
        mod_list.onModStateChanged(self._states_changed)
        mod_list.onModMoved(lambda *_: self._changed())
        mod_list.onModInstalled(lambda *_: self._changed())
        mod_list.onModRemoved(lambda *_: self._changed())
        organizer.onProfileChanged(lambda *_: self._changed())
        return True

    def name(self) -> str:
        return "BG3 Load Order Check"

    def localizedName(self) -> str:
        return tr("Baldur's Gate 3 load order check")

    def author(self) -> str:
        return "MO2 Revamped"

    def description(self) -> str:
        return tr(
            "Warns when active Baldur's Gate 3 mods need mods that aren't active or "
            "load after them, need the Script Extender, or were turned off by the "
            "game on its last start."
        )

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(1, 0, 0, mobase.ReleaseType.FINAL)

    def requirements(self) -> list[mobase.IPluginRequirement]:
        return [mobase.PluginRequirementFactory.gameDependency(GAME_NAME)]

    def settings(self) -> list[mobase.PluginSetting]:
        return []

    # IPluginDiagnose

    def _game(self):
        game = self._organizer.managedGame()
        if game is not None and game.name() == PLUGIN_NAME and hasattr(game, "launch"):
            game._problems = self._changed
            return game
        return None

    def _current(self):
        if self._fresh:
            return self._plan
        game = self._game()
        self._plan = game.launch().quick_plan() if game is not None and game.isInstalled() else None
        self._fresh = True
        if game is not None and game.isInstalled() and game.launch().read_in_background():
            self._poll_later()
        return self._plan

    def _poll_later(self) -> None:
        from PyQt6.QtCore import QTimer

        QTimer.singleShot(300, self._poll)

    def _poll(self) -> None:
        game = self._game()
        if game is None:
            return
        background = game.launch()._background
        if background is not None and background.is_alive():
            self._poll_later()
            return
        # the new paks are in the cache now: the list again
        self._changed()

    def activeProblems(self) -> list[int]:
        plan = self._current()
        game = self._game()
        keys = []
        if plan is not None:
            for key, items in (
                (MISSING, plan.missing),
                (LATE, plan.late),
                (SCRIPT_EXTENDER, plan.needs_se),
                (DUPLICATES, plan.duplicates),
                (CONFLICTS, plan.conflicts),
                (OLDER, plan.older),
            ):
                if items:
                    keys.append(key)
        if game is not None and game._launch is not None and game._launch.dropped_last_run:
            keys.append(DROPPED)
        return keys

    def shortDescription(self, key: int) -> str:
        return {
            MISSING: tr("Mods need mods that aren't active"),
            LATE: tr("Mods load before the mods they need"),
            SCRIPT_EXTENDER: tr("Mods need the Script Extender"),
            DUPLICATES: tr("One mod is installed twice"),
            CONFLICTS: tr("Mods that conflict are both active"),
            DROPPED: tr("The game turned off mods on its last start"),
            OLDER: tr("Mods need a newer version of a mod"),
        }.get(key, "")

    def fullDescription(self, key: int) -> str:
        plan = self._current()
        if key == DROPPED:
            game = self._game()
            names = game._launch._names(game._launch.dropped_last_run) if game else []
            return tr(
                "MO2 wrote these mods in the load order, and Baldur's Gate 3 turned them "
                "off when it started:<br><br>{0}<br><br>The game does this when a mod "
                "needs a mod that isn't there, when its version doesn't match, or after "
                "a crash it blames on mods. MO2 writes them again on the next start."
            ).replace("{0}", _list(names))
        if plan is None:
            return ""
        if key == MISSING:
            lines = [tr("{0} needs {1}").replace("{0}", m).replace("{1}", d) for m, d in plan.missing]
            return tr(
                "These active mods need mods that aren't installed or are turned off "
                "(their meta.lsx lists them as dependencies):<br><br>{0}<br><br>Install "
                "or turn on the mods they need. Without them the game turns these mods "
                "off when it starts."
            ).replace("{0}", _list(lines))
        if key == LATE:
            lines = [
                tr("{0} needs {1} ({2})").replace("{0}", m).replace("{1}", d).replace("{2}", dm)
                if dm and dm.casefold() != d.casefold()
                else tr("{0} needs {1}").replace("{0}", m).replace("{1}", d)
                for m, d, dm in plan.late
            ]
            return tr(
                "These mods load before a mod they need:<br><br>{0}<br><br>Move the mod "
                "they need above them in the mod list (a lower priority loads first)."
            ).replace("{0}", _list(lines))
        if key == SCRIPT_EXTENDER:
            return tr(
                "These active mods need the Baldur's Gate 3 Script Extender, which isn't "
                "installed or is turned off:<br><br>{0}<br><br>Install it from "
                '<a href="https://www.nexusmods.com/baldursgate3/mods/2172">'
                "nexusmods.com/baldursgate3/mods/2172</a> as a mod (its DWrite.dll goes "
                "to bin/). Without it these mods don't work."
            ).replace("{0}", _list(plan.needs_se))
        if key == DUPLICATES:
            lines = [
                tr("{0}: in {1} and {2}; {2}'s is used")
                .replace("{0}", n)
                .replace("{1}", a)
                .replace("{2}", b)
                for n, a, b in plan.duplicates
            ]
            return tr(
                "These mods bring the same module (the same UUID), usually two versions "
                "of one mod:<br><br>{0}<br><br>Only the one with the higher priority "
                "goes in the load order. Turn the other off."
            ).replace("{0}", _list(lines))
        if key == CONFLICTS:
            lines = [tr("{0} and {1}").replace("{0}", a).replace("{1}", b) for a, b in plan.conflicts]
            return tr(
                "These active mods say in their meta.lsx that they conflict:<br><br>{0}"
                "<br><br>Turn one of each pair off."
            ).replace("{0}", _list(lines))
        if key == OLDER:
            lines = [
                tr("{0} needs {1} {2}, found {3}")
                .replace("{0}", m)
                .replace("{1}", d)
                .replace("{2}", need)
                .replace("{3}", have)
                for m, d, need, have in plan.older
            ]
            return tr(
                "These mods need a newer version of a mod than the one installed:"
                "<br><br>{0}<br><br>Update the mods they need."
            ).replace("{0}", _list(lines))
        return ""

    def hasGuidedFix(self, key: int) -> bool:
        return False

    def startGuidedFix(self, key: int) -> None:
        pass

    def _states_changed(self, states: dict) -> None:
        self._changed()

    def _changed(self) -> None:
        self._fresh = False
        self._plan = None
        try:
            self._invalidate()
        except Exception:  # noqa: BLE001 - MO2 not ready yet
            pass
