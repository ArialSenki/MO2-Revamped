"""Valheim: MO2's problem report for BepInEx, Jotunn and InSlimVML."""

from __future__ import annotations

import html
from pathlib import Path

import mobase

from ...revamped_shared import log_warning, translator
from ...revamped_shared.dotnet import SHARED, plugin_dlls
from ...revamped_shared.mod_cache import PerModCache
from ..checks import KNOWN, ModFolder, Report, analyze

GAME_NAME = "Valheim"
tr = translator("ValheimGame")
NO_BEPINEX, MISSING, NO_INSLIMVML = range(3)
LISTED = 12
BEPINEX_URL = "https://thunderstore.io/c/valheim/p/denikson/BepInExPack_Valheim/"


def active_mods(organizer: mobase.IOrganizer) -> list[ModFolder]:
    mod_list = organizer.modList()
    mods = []
    for name in mod_list.allModsByProfilePriority():
        if not mod_list.state(name) & mobase.ModState.ACTIVE:
            continue
        mod = mod_list.getMod(name)
        if mod is None or mod.isSeparator() or mod.isForeign() or mod.isOverwrite():
            continue
        mods.append(ModFolder(name, Path(mod.absolutePath())))
    return mods


def bullet(names: list[str]) -> str:
    lines = [f"• {html.escape(n)}" for n in names[:LISTED]]
    if len(names) > LISTED:
        lines.append(tr("• and {0} more").format(len(names) - LISTED))
    return "<br>".join(lines)


class ValheimCheck(mobase.IPluginDiagnose):
    def __init__(self) -> None:
        super().__init__()
        self._report: Report | None = None
        # each mod's plugin DLLs, walked again only when its folder changes
        # (see PerModCache: the check runs on the interface thread)
        self._mods: PerModCache[ModFolder] = PerModCache()

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        mod_list = organizer.modList()
        mod_list.onModStateChanged(lambda states: self._changed())
        mod_list.onModInstalled(lambda mod, *_: self._mod_files_changed(mod))
        mod_list.onModRemoved(lambda name, *_: self._mod_files_changed(name))
        # a refresh (F5, a program's end, files hidden in a mod) forgets every
        # mod: a change below a mod's top folder keeps the folder's date
        self._mods.watch(organizer, self.name(), self._changed)
        organizer.onProfileChanged(lambda *_: self._changed())
        mod_list.onModMoved(lambda *_: self._changed())
        # BepInEx or a plugin installed into the game folder by hand
        organizer.onFinishedRun(lambda *_: self._changed())
        return True

    def name(self) -> str:
        return "Valheim Mod Check"

    def localizedName(self) -> str:
        return tr("Valheim Mod Check")

    def author(self) -> str:
        return "MO2 Revamped"

    def description(self) -> str:
        return tr(
            "Warns when Valheim mods need BepInEx, another plugin (Jotunn...) or the InSlimVML "
            "loader and it isn't there."
        )

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(1, 0, 0, mobase.ReleaseType.FINAL)

    def requirements(self) -> list[mobase.IPluginRequirement]:
        return [mobase.PluginRequirementFactory.gameDependency(GAME_NAME)]

    def settings(self) -> list[mobase.PluginSetting]:
        return []

    def activeProblems(self) -> list[int]:
        report = self._current()
        keys = []
        if report.without_bepinex:
            keys.append(NO_BEPINEX)
        if report.missing:
            keys.append(MISSING)
        if report.without_inslimvml:
            keys.append(NO_INSLIMVML)
        return keys

    def shortDescription(self, key: int) -> str:
        return {
            NO_BEPINEX: tr("BepInEx isn't installed"),
            MISSING: tr("Plugins need other plugins that aren't there"),
            NO_INSLIMVML: tr("InSlimVML mods without their loader"),
        }.get(key, "")

    def fullDescription(self, key: int) -> str:
        report = self._current()
        if key == NO_BEPINEX:
            return tr(
                "These active mods are BepInEx plugins, and BepInEx (BepInEx/core and winhttp.dll) "
                "isn't in an active mod or the game folder:<br><br>{0}<br><br>Install "
                "BepInExPack_Valheim as a mod from <a href=\"{1}\">{1}</a> or Nexus Mods; without "
                "it the game starts with no mods."
            ).format(bullet(report.without_bepinex), BEPINEX_URL)
        if key == MISSING:
            items = []
            for guid, users in sorted(report.missing.items()):
                name, url = KNOWN.get(guid, (guid, ""))
                link = f' (<a href="{url}">{url}</a>)' if url else ""
                items.append(f"{html.escape(name)}{link}: {html.escape(', '.join(users))}")
            lines = [f"\u2022 {item}" for item in items[:LISTED]]
            if len(items) > LISTED:
                lines.append(tr("\u2022 and {0} more").format(len(items) - LISTED))
            return tr(
                "These plugins are required (their [BepInDependency]), and no active mod has "
                "them (required plugin: the mods that need it):<br><br>{0}<br><br>Install them "
                "or turn them on; BepInEx doesn't load a plugin whose required plugins are missing."
            ).format("<br>".join(lines))
        return tr(
            "These active mods are for the InSlimVML loader (InSlimVML/Mods/*_VML.dll), and "
            "its inslimvml.ini isn't there:<br><br>{0}<br><br>Install InSlimVML, or a BepInEx "
            "version of these mods."
        ).format(bullet(report.without_inslimvml))

    def hasGuidedFix(self, key: int) -> bool:
        return False

    def startGuidedFix(self, key: int) -> None:
        pass

    def _changed(self) -> None:
        self._report = None
        try:
            self._invalidate()
        except Exception:  # noqa: BLE001 - MO2 not ready yet
            pass

    def _mod_files_changed(self, mod) -> None:
        self._mods.forget(mod)
        self._changed()

    def _current(self) -> Report:
        if self._report is not None:
            return self._report
        report = Report()
        game = self._organizer.managedGame()
        if game is not None and game.gameName() == GAME_NAME:
            try:
                game_dir = Path(game.gameDirectory().absolutePath()) if game.isInstalled() else None
                mods = self._mods.active(
                    self._organizer, lambda name, path, mod: ModFolder(name, path, plugin_dlls(path))
                )
                report = analyze(mods, game_dir, SHARED)
            except Exception as error:  # noqa: BLE001 - never break MO2's problem list
                log_warning(f"Valheim mod check failed: {error}")
        self._report = report
        return report


def createPlugins() -> list[mobase.IPlugin]:
    return [ValheimCheck()]
