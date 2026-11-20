"""Cyberpunk 2077: MO2's problem report for missing frameworks."""

from __future__ import annotations

import html
import threading
from pathlib import Path

import mobase

from PyQt6.QtCore import QTimer

from ..frameworks import FRAMEWORKS, CodewareCandidate, ModFiles, Report, analyze, scan_codeware
from ..util import log_warning, tr

GAME_NAME = "Cyberpunk 2077"
# problem keys: a missing framework is its index in FRAMEWORKS
REDMOD_DLC = 100
OUTSIDE_DEPLOY = 101
# mods named in a problem's text
LISTED_MODS = 12


def active_mod_files(organizer: mobase.IOrganizer) -> list[ModFiles]:
    mod_list = organizer.modList()
    mods: list[ModFiles] = []
    for name in mod_list.allModsByProfilePriority():
        if not mod_list.state(name) & mobase.ModState.ACTIVE:
            continue
        mod = mod_list.getMod(name)
        if mod is None or mod.isSeparator() or mod.isForeign() or mod.isOverwrite():
            continue
        try:
            tree = mod.fileTree()
        except Exception:  # noqa: BLE001 - a mod being removed
            continue
        mods.append(ModFiles(name, Path(mod.absolutePath()), tree))
    return mods


class CyberpunkFrameworkCheck(mobase.IPluginDiagnose):
    def __init__(self) -> None:
        super().__init__()
        self._report: Report | None = None
        # which mods import Codeware, by candidate key; filled in the
        # background (reading many scripts can take seconds)
        self._codeware: dict[tuple, bool] = {}
        self._scan: threading.Thread | None = None

    # IPlugin

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        mod_list = organizer.modList()
        # MO2 2.5's form: one call with every changed mod
        mod_list.onModStateChanged(self._states_changed)
        mod_list.onModInstalled(lambda *_: self._changed())
        mod_list.onModRemoved(lambda *_: self._changed())
        organizer.onProfileChanged(lambda *_: self._changed())
        return True

    def name(self) -> str:
        return "Cyberpunk 2077 Framework Check"

    def localizedName(self) -> str:
        return tr("Cyberpunk 2077 Framework Check")

    def author(self) -> str:
        return "ArialSenki"

    def description(self) -> str:
        return tr(
            "Warns when active Cyberpunk 2077 mods need a framework (RED4ext, Cyber Engine "
            "Tweaks, redscript, ArchiveXL, TweakXL, Codeware...) or the REDmod DLC that "
            "isn't installed."
        )

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(1, 0, 0, mobase.ReleaseType.FINAL)

    def requirements(self) -> list[mobase.IPluginRequirement]:
        return [mobase.PluginRequirementFactory.gameDependency(GAME_NAME)]

    def settings(self) -> list[mobase.PluginSetting]:
        return []

    # IPluginDiagnose

    def activeProblems(self) -> list[int]:
        report = self._current_report()
        keys = [index for index, framework in enumerate(FRAMEWORKS) if framework.key in report.missing]
        if report.redmod_without_dlc:
            keys.append(REDMOD_DLC)
        if report.outside_redmod_deploy:
            keys.append(OUTSIDE_DEPLOY)
        return keys

    def shortDescription(self, key: int) -> str:
        if key == REDMOD_DLC:
            return tr("REDmod mods need the REDmod DLC")
        if key == OUTSIDE_DEPLOY:
            return tr("REDmod files deployed outside MO2")
        return tr("Missing framework: {0}").format(FRAMEWORKS[key].name)

    def fullDescription(self, key: int) -> str:
        report = self._current_report()
        if key == REDMOD_DLC:
            return tr(
                "These active mods are REDmods, which the game loads only with the free "
                "REDmod DLC (its tools/redmod folder isn't in the game folder):<br><br>{0}"
                "<br><br>Install the REDmod DLC from your store's DLC list (Steam, GOG or "
                "Epic). Until then MO2 starts the game without deploying them."
            ).format(self._mod_list(report.redmod_without_dlc))
        if key == OUTSIDE_DEPLOY:
            return tr(
                "The game folder has REDmod files deployed by something else than MO2 "
                "(r6/cache/modded), and no REDmod is active in MO2. The game loads them "
                "when started with -modded, so mods MO2 doesn't manage can still change "
                "the game.<br><br>If you only use MO2, remove them by verifying the game "
                "files in your store, or start “Cyberpunk 2077 - skip REDmod "
                "deploy”, which starts the game without -modded."
            )
        framework = FRAMEWORKS[key]
        users = report.missing.get(framework.key, [])
        return tr(
            "These active mods need {0}, which isn't installed or is turned off in the mod "
            "list:<br><br>{1}<br><br>Install {0} from <a href=\"{2}\">{2}</a> or turn it "
            "on. Without it these mods don't work, and some stop the game from starting."
        ).format(framework.name, self._mod_list(users), framework.url)

    def hasGuidedFix(self, key: int) -> bool:
        return False

    def startGuidedFix(self, key: int) -> None:
        pass

    # the report

    def _states_changed(self, states: dict) -> None:
        self._changed()

    def _changed(self) -> None:
        self._report = None
        try:
            self._invalidate()
        except Exception:  # noqa: BLE001 - MO2 not ready yet
            pass

    def _current_report(self) -> Report:
        if self._report is not None:
            return self._report
        report = Report()
        game = self._organizer.managedGame()
        if game is not None and game.gameName() == GAME_NAME:
            try:
                game_dir = Path(game.gameDirectory().absolutePath()) if game.isInstalled() else None
                report = analyze(
                    active_mod_files(self._organizer),
                    self._organizer.virtualFileTree(),
                    game_dir,
                    self._codeware_users,
                )
            except Exception as error:  # noqa: BLE001 - never break MO2's problem list
                log_warning(f"Cyberpunk 2077 framework check failed: {error}")
        self._report = report
        return report

    def _codeware_users(self, candidates: list[CodewareCandidate]) -> list[str]:
        """The candidates known to import Codeware; the unknown ones are read
        in a background thread, and the report is made again when it ends."""
        unknown = [c for c in candidates if c.key not in self._codeware]
        if unknown and self._scan is None:
            results: dict[tuple, bool] = {}

            def work() -> None:
                try:
                    results.update(scan_codeware(unknown))
                except Exception:  # noqa: BLE001 - never let a read stop MO2
                    pass

            def finished() -> None:
                if self._scan is not None and self._scan.is_alive():
                    QTimer.singleShot(250, finished)
                    return
                self._codeware.update(results)
                # a mod the scan could not read counts as not using Codeware
                for candidate in unknown:
                    self._codeware.setdefault(candidate.key, False)
                self._scan = None
                self._changed()

            self._scan = threading.Thread(target=work, name="Cyberpunk 2077 Codeware scan", daemon=True)
            self._scan.start()
            QTimer.singleShot(250, finished)
        return [c.name for c in candidates if self._codeware.get(c.key)]

    @staticmethod
    def _mod_list(names: list[str]) -> str:
        shown = [f"• {html.escape(name)}" for name in names[:LISTED_MODS]]
        if len(names) > LISTED_MODS:
            shown.append(tr("• and {0} more").format(len(names) - LISTED_MODS))
        return "<br>".join(shown)


def createPlugins() -> list[mobase.IPlugin]:
    from .archive_conflicts import ArchiveConflictsTool

    return [CyberpunkFrameworkCheck(), ArchiveConflictsTool()]

