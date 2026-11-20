"""The Witcher 3: MO2's problem report for script conflicts and menus the
next-gen game won't load."""

from __future__ import annotations

import html
from pathlib import Path

import mobase

from ...revamped_shared import log_warning, translator
from ..checks import ModTree, menu_files, overwrite_merged_scripts, script_conflicts, unlisted_menus
from ..layout import MENU_FOLDER

GAME_NAME = "The Witcher 3: Wild Hunt"
tr = translator("Witcher3Game")
SCRIPT_CONFLICTS = 0
UNLISTED_MENUS = 1
LISTED = 12  # items named in a problem's text
FILELISTS = ("dx11filelist.txt", "dx12filelist.txt")


def active_mods(organizer: mobase.IOrganizer) -> list[tuple[ModTree, Path]]:
    """The active mods, lowest priority first, with their folders."""
    mod_list = organizer.modList()
    mods = []
    for name in mod_list.allModsByProfilePriority():
        if not mod_list.state(name) & mobase.ModState.ACTIVE:
            continue
        mod = mod_list.getMod(name)
        if mod is None or mod.isSeparator() or mod.isForeign() or mod.isOverwrite():
            continue
        try:
            mods.append((ModTree(name, mod.fileTree()), Path(mod.absolutePath())))
        except Exception:  # noqa: BLE001 - a mod being removed
            continue
    return mods


def winning_filelists(mods: list[tuple[ModTree, Path]], game_dir: Path) -> list[str]:
    """dx11filelist.txt and dx12filelist.txt as the game sees them: from the
    highest mod's root/ that has one, else the game folder's."""
    texts = []
    for name in FILELISTS:
        relative = f"{MENU_FOLDER}/{name}"
        candidates = [path / "root" / relative for _mod, path in reversed(mods)] + [game_dir / relative]
        for candidate in candidates:
            try:
                if candidate.is_file():
                    texts.append(candidate.read_text(encoding="utf-8-sig", errors="replace"))
                    break
            except OSError:
                continue
    return texts


class Witcher3Check(mobase.IPluginDiagnose):
    def __init__(self) -> None:
        super().__init__()
        self._conflicts = None
        self._menus = None

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        mod_list = organizer.modList()
        mod_list.onModStateChanged(lambda states: self._changed())
        mod_list.onModInstalled(lambda *_: self._changed())
        mod_list.onModRemoved(lambda *_: self._changed())
        organizer.onProfileChanged(lambda *_: self._changed())
        # the mod order decides the winning file lists; Script Merger writes
        # its output while it runs
        mod_list.onModMoved(lambda *_: self._changed())
        organizer.onFinishedRun(lambda *_: self._changed())
        return True

    def name(self) -> str:
        return "The Witcher 3 Mod Check"

    def localizedName(self) -> str:
        return tr("The Witcher 3 Mod Check")

    def author(self) -> str:
        return "MO2 Revamped"

    def description(self) -> str:
        return tr(
            "Warns about scripts two Witcher 3 mods change (Script Merger can combine "
            "them) and about menus the next-gen game won't load."
        )

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(1, 0, 0, mobase.ReleaseType.FINAL)

    def requirements(self) -> list[mobase.IPluginRequirement]:
        return [mobase.PluginRequirementFactory.gameDependency(GAME_NAME)]

    def settings(self) -> list[mobase.PluginSetting]:
        return []

    def activeProblems(self) -> list[int]:
        self._compute()
        keys = []
        if self._conflicts:
            keys.append(SCRIPT_CONFLICTS)
        if self._menus:
            keys.append(UNLISTED_MENUS)
        return keys

    def shortDescription(self, key: int) -> str:
        if key == SCRIPT_CONFLICTS:
            return tr("Mods change the same scripts")
        return tr("Menus the next-gen game won't load")

    def fullDescription(self, key: int) -> str:
        self._compute()
        if key == SCRIPT_CONFLICTS:
            lines = [
                "• {0}: {1}".format(
                    html.escape(conflict.script),
                    ", ".join(html.escape(f"{folder} ({mod})") for folder, mod in conflict.folders),
                )
                for conflict in self._conflicts[:LISTED]
            ]
            if len(self._conflicts) > LISTED:
                lines.append(tr("• and {0} more").format(len(self._conflicts) - LISTED))
            return tr(
                "These scripts are changed by more than one mod. The game uses only one "
                "copy of each, so the other mod's changes are lost or the game stops with "
                "a script error:<br><br>{0}<br><br>Run Script Merger (WitcherScriptMerger) "
                "from MO2 to combine them into mod0000_MergedFiles, and keep its output as "
                "a mod. Run it again after adding, removing or updating these mods."
            ).format("<br>".join(lines))
        menus = list(self._menus.items())
        lines = [f"• {html.escape(name)} ({html.escape(mod)})" for name, mod in menus[:LISTED]]
        if len(menus) > LISTED:
            lines.append(tr("• and {0} more").format(len(menus) - LISTED))
        return tr(
            "The next-gen version of the game loads a mod's settings menu only when "
            "bin/config/r4game/user_config_matrix/pc/dx11filelist.txt or dx12filelist.txt "
            "lists it, and these menus aren't listed:<br><br>{0}<br><br>Add a line "
            "“name.xml;” for each to both files (in a mod of their own, so the "
            "game's files stay as they are), or install the mods' versions made for the "
            "next-gen game."
        ).format("<br>".join(lines))

    def hasGuidedFix(self, key: int) -> bool:
        return False

    def startGuidedFix(self, key: int) -> None:
        pass

    def _changed(self) -> None:
        self._conflicts = None
        self._menus = None
        try:
            self._invalidate()
        except Exception:  # noqa: BLE001 - MO2 not ready yet
            pass

    def _compute(self) -> None:
        if self._conflicts is not None:
            return
        self._conflicts, self._menus = [], {}
        game = self._organizer.managedGame()
        if game is None or game.gameName() != GAME_NAME:
            return
        try:
            mods = active_mods(self._organizer)
            merged = overwrite_merged_scripts(Path(self._organizer.overwritePath()))
            self._conflicts = script_conflicts([tree for tree, _ in mods], merged)
            if game.isInstalled():
                game_dir = Path(game.gameDirectory().absolutePath())
                # only the next-gen game (4.0, with its DX12 build) needs the lists
                if (game_dir / "bin/x64_dx12").is_dir():
                    menus = menu_files([tree for tree, _ in mods])
                    if menus:
                        self._menus = unlisted_menus(menus, winning_filelists(mods, game_dir))
        except Exception as error:  # noqa: BLE001 - never break MO2's problem list
            log_warning(f"The Witcher 3 mod check failed: {error}")


def createPlugins() -> list[mobase.IPlugin]:
    return [Witcher3Check()]
