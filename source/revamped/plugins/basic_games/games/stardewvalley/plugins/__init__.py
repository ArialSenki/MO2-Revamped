"""Stardew Valley: MO2's problem report for SMAPI and the mods' dependencies."""

from __future__ import annotations

import html
from pathlib import Path

import mobase

from ...revamped_shared import log_warning, translator
from ...revamped_shared.mod_cache import PerModCache
from .. import manifest as manifests
from ..checks import ManifestCache, Report, analyze, display_name, game_folder_manifests, mod_manifests

GAME_NAME = "Stardew Valley"
tr = translator("StardewValleyGame")
NO_SMAPI, MISSING, TOO_OLD, SMAPI_TOO_OLD, DUPLICATES = range(5)
LISTED = 12
SMAPI_FILES = ("StardewModdingAPI.dll", "StardewModdingAPI.exe")


def smapi_version(game_dir: Path) -> str | None:
    """The installed SMAPI's version ("" when unreadable), None without SMAPI."""
    for name in SMAPI_FILES:
        path = game_dir / name
        if path.is_file():
            try:
                return mobase.getFileVersion(str(path)) or ""
            except Exception:  # noqa: BLE001
                return ""
    return None


def nexus_link(unique_id: str) -> str:
    known = manifests.KNOWN.get(unique_id)
    if known is None:
        return ""
    url = f"https://www.nexusmods.com/stardewvalley/mods/{known[1]}"
    return f' (<a href="{url}">{url}</a>)'


def bullet(items: list[str], more: int) -> str:
    lines = [f"• {item}" for item in items[:LISTED]]
    if more > 0:
        lines.append(tr("• and {0} more").format(more))
    return "<br>".join(lines)


class StardewValleyCheck(mobase.IPluginDiagnose):
    def __init__(self) -> None:
        super().__init__()
        self._report: Report | None = None
        self._known: dict = {}
        self._cache = ManifestCache()
        # each mod's manifests, read again only when its folder changes (see
        # PerModCache: the check runs on the interface thread)
        self._mods: PerModCache = PerModCache()

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        mod_list = organizer.modList()
        mod_list.onModStateChanged(lambda states: self._changed())
        mod_list.onModInstalled(lambda mod, *_: self._mod_files_changed(mod))
        mod_list.onModRemoved(lambda name, *_: self._mod_files_changed(name))
        organizer.onProfileChanged(lambda *_: self._changed())
        organizer.onFinishedRun(lambda *_: self._changed())
        # the mod order decides which version of a mod SMAPI sees
        mod_list.onModMoved(lambda *_: self._changed())
        return True

    def name(self) -> str:
        return "Stardew Valley Mod Check"

    def localizedName(self) -> str:
        return tr("Stardew Valley Mod Check")

    def author(self) -> str:
        return "MO2 Revamped"

    def description(self) -> str:
        return tr(
            "Warns when Stardew Valley mods need SMAPI, a framework or another mod that "
            "isn't there or is too old, and about mods SMAPI skips as duplicates."
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
        for key, present in (
            (NO_SMAPI, report.no_smapi),
            (MISSING, report.missing),
            (TOO_OLD, report.too_old),
            (SMAPI_TOO_OLD, report.smapi_too_old),
            (DUPLICATES, report.duplicates),
        ):
            if present:
                keys.append(key)
        return keys

    def shortDescription(self, key: int) -> str:
        return {
            NO_SMAPI: tr("SMAPI isn't installed"),
            MISSING: tr("Mods need other mods that aren't there"),
            TOO_OLD: tr("Mods need newer versions of other mods"),
            SMAPI_TOO_OLD: tr("Mods need a newer SMAPI"),
            DUPLICATES: tr("The same mod is installed twice"),
        }.get(key, "")

    def fullDescription(self, key: int) -> str:
        report = self._current()
        esc = html.escape
        if key == NO_SMAPI:
            names = [esc(n) for n in report.no_smapi]
            return tr(
                "These active mods are SMAPI mods, and SMAPI isn't in the game folder:<br><br>{0}"
                "<br><br>Install SMAPI with its own installer from <a href=\"{1}\">{1}</a> (into "
                "the game folder, not as a mod), then start the game with the SMAPI program in MO2."
            ).format(bullet(names, len(names) - LISTED), "https://www.nexusmods.com/stardewvalley/mods/2400")
        if key == MISSING:
            items = [
                "{0}{1}: {2}".format(
                    esc(display_name(uid, self._known)), nexus_link(uid), esc(", ".join(users))
                )
                for uid, users in sorted(report.missing.items())
            ]
            return tr(
                "These mods are needed, and no active mod has them (needed mod: the mods that "
                "need it):<br><br>{0}<br><br>Install them or turn them on in the mod list. SMAPI "
                "doesn't load a mod whose required mods are missing."
            ).format(bullet(items, len(items) - LISTED))
        if key == TOO_OLD:
            items = [
                esc(tr("{0} needs {1} {2} or newer; {3} is installed").format(mod, need, minimum, have))
                for mod, need, have, minimum in report.too_old
            ]
            return tr(
                "These mods need a newer version of another mod:<br><br>{0}<br><br>Update the "
                "needed mods. SMAPI skips a mod whose required mods are too old."
            ).format(bullet(items, len(items) - LISTED))
        if key == SMAPI_TOO_OLD:
            items = [
                esc(tr("{0} needs SMAPI {1} or newer").format(mod, minimum))
                for mod, minimum, _have in report.smapi_too_old
            ]
            have = report.smapi_too_old[0][2] if report.smapi_too_old else ""
            return tr(
                "The installed SMAPI ({0}) is older than these mods need:<br><br>{1}<br><br>Update "
                "SMAPI with its installer from <a href=\"{2}\">{2}</a>."
            ).format(esc(have), bullet(items, len(items) - LISTED), "https://www.nexusmods.com/stardewvalley/mods/2400")
        items = [
            "{0}: {1}".format(
                esc(display_name(uid, self._known)),
                esc(", ".join(name or tr("the game's Mods folder") for name in names)),
            )
            for uid, names in sorted(report.duplicates.items())
        ]
        return tr(
            "The same mod (the same UniqueID in manifest.json) is in more than one folder of the "
            "game's Mods folder:<br><br>{0}<br><br>SMAPI then loads none of the copies. Keep one "
            "of them: turn the other mods off or remove the duplicate folder."
        ).format(bullet(items, len(items) - LISTED))

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

    def _smapi_stamp(self):
        """SMAPI's files as they are now: a report made before SMAPI was
        installed, updated or removed (outside MO2) is made again."""
        game = self._organizer.managedGame()
        if game is None or not game.isInstalled():
            return None
        stamp = []
        for name in SMAPI_FILES:
            try:
                info = (Path(game.gameDirectory().absolutePath()) / name).stat()
                stamp.append((name, info.st_mtime_ns, info.st_size))
            except OSError:
                stamp.append((name, None, None))
        return tuple(stamp)

    def _current(self) -> Report:
        stamp = self._smapi_stamp()
        if self._report is not None and stamp == getattr(self, "_stamp", None):
            return self._report
        self._stamp = stamp
        report = Report()
        game = self._organizer.managedGame()
        if game is not None and game.gameName() == GAME_NAME and game.isInstalled():
            try:
                game_dir = Path(game.gameDirectory().absolutePath())
                mods = self._mods.active(
                    self._organizer,
                    lambda name, path, mod: mod_manifests(name, path, mod.fileTree(), self._cache),
                )
                game_mods = game_folder_manifests(game_dir / "Mods", self._cache)
                self._known = {m.unique_id: m for entry in [game_mods, *mods] for m in entry.manifests}
                report = analyze(mods, game_mods, smapi_version(game_dir))
            except Exception as error:  # noqa: BLE001 - never break MO2's problem list
                log_warning(f"Stardew Valley mod check failed: {error}")
        self._report = report
        return report


def createPlugins() -> list[mobase.IPlugin]:
    return [StardewValleyCheck()]
