"""Valheim mod layouts.

MO2's data folder for Valheim is the game folder: a mod holds BepInEx/...
(plugins, config, patchers), BepInEx's own files (winhttp.dll, Doorstop) or
the older InSlimVML loader's. Mods packed for Thunderstore (manifest.json,
icon.png, README.md and the plugin's files at the top) and loose DLLs are
moved where BepInEx loads them.
"""

from __future__ import annotations

from typing import Callable

import mobase

from ..revamped_shared import is_dir, lone_wrapper, name_of, prune_empty, remove_junk, unwrap

# what the game folder takes from a mod as it is
VALID = frozenset({
    "bepinex", "doorstop_libs", "unstripped_corlib", "unstripped_managed", ".doorstop_version",
    "doorstop_config.ini", "start_game_bepinex.sh", "start_server_bepinex.sh", "winhttp.dll",
    "inslimvml", "inslimvml.ini", "valheim_data", "advancedbuilder",
})  # fmt: skip
# Thunderstore's package files: not for the game folder
THUNDERSTORE = frozenset({"manifest.json", "icon.png", "readme.md", "changelog.md", "license", "license.md", "license.txt"})
# folders a plugin brings next to its DLL
PLUGIN_FOLDERS = frozenset({"jotunn", "translations", "customtextures", "assets"})
MAX_WRAPPERS = 3

Action = Callable[[], None]


def _has_dll(folder: mobase.IFileTree) -> bool:
    found = []

    def visit(_path: str, entry) -> mobase.IFileTree.WalkReturn:
        if not is_dir(entry) and name_of(entry).endswith(".dll"):
            found.append(entry)
            return mobase.IFileTree.WalkReturn.STOP
        return mobase.IFileTree.WalkReturn.CONTINUE

    folder.walk(visit, "/")
    return bool(found)


def _plugin_folder(folder: mobase.IFileTree) -> bool:
    """A plugin's own folder: DLLs (at any depth) and none of BepInEx's or
    the game folder's own structure (then it's a wrapper)."""
    structure = VALID | {"plugins", "config", "patchers", "bepinexpack_valheim"}
    return _has_dll(folder) and not any(name_of(e) in structure for e in folder)


class ValheimModDataChecker(mobase.ModDataChecker):
    def dataLooksValid(self, filetree: mobase.IFileTree) -> mobase.ModDataChecker.CheckReturn:
        parent = filetree.parent()
        if parent is not None and self.dataLooksValid(parent) is self.FIXABLE:
            return self.FIXABLE
        return self._check(filetree, 0)

    def fix(self, filetree: mobase.IFileTree) -> mobase.IFileTree:
        remove_junk(filetree)
        for _ in range(MAX_WRAPPERS):
            status, _ = self._plan(filetree)
            if status is not self.INVALID:
                break
            inner = lone_wrapper(filetree, VALID)
            if inner is None or self._check(inner, 1) is self.INVALID:
                break
            unwrap(filetree, inner)
        for _ in range(MAX_WRAPPERS):
            _, actions = self._plan(filetree)
            if not actions:
                break
            for action in actions:
                action()
        return filetree

    def _check(self, tree: mobase.IFileTree, depth: int) -> mobase.ModDataChecker.CheckReturn:
        status, _ = self._plan(tree)
        if status is self.INVALID and depth < MAX_WRAPPERS:
            inner = lone_wrapper(tree, VALID)
            if inner is not None and self._check(inner, depth + 1) is not self.INVALID:
                return self.FIXABLE
        return status

    def _plan(self, tree: mobase.IFileTree) -> tuple[mobase.ModDataChecker.CheckReturn, list[Action]]:
        valid = False
        actions: list[tuple[str, Action]] = []
        entries = list(tree)
        names = {name_of(e) for e in entries}
        # the archive's only folder (documents and Thunderstore's files apart):
        # with a DLL of its own it is the plugin's folder; two may be options
        folders = [e for e in entries if is_dir(e)]
        lone = folders[0] if len(folders) == 1 else None
        loose_dlls = any(
            not is_dir(e) and name_of(e).endswith(".dll") and not name_of(e).endswith("_vml.dll")
            and name_of(e) not in VALID
            for e in entries
        )
        # a loose DLL next to folders that hold DLLs of their own: options
        if loose_dlls and any(name_of(f) not in VALID and _has_dll(f) for f in folders):
            return self.INVALID, []
        # a Thunderstore package: its manifest and icon next to the mod's files
        thunderstore = "manifest.json" in names and (
            "icon.png" in names or any(n.endswith(".dll") or n in ("plugins", "config", "patchers") for n in names)
        )

        def move(entry: mobase.FileTreeEntry, target: str) -> Action:
            def run() -> None:
                parent = entry.parent()
                tree.move(entry, target, mobase.IFileTree.InsertPolicy.MERGE)
                prune_empty(parent)

            return run

        for entry in entries:
            name = name_of(entry)
            if name in VALID:
                valid = True
            elif is_dir(entry):
                if name == "bepinexpack_valheim":
                    # BepInEx's own pack, wrapped
                    actions.append(("move", lambda entry=entry: unwrap(tree, entry)))
                elif name in ("plugins", "config", "patchers"):
                    actions.append(("move", move(entry, "BepInEx/")))
                elif name in PLUGIN_FOLDERS:
                    actions.append(("move", move(entry, "BepInEx/plugins/")))
                elif name == "builds":
                    actions.append(("move", move(entry, "AdvancedBuilder/")))
                elif (entry is lone or (lone is not None and name == name_of(lone))) and _plugin_folder(entry):
                    # the plugin with its data files, as BepInEx loads it
                    actions.append(("move", move(entry, "BepInEx/plugins/")))
                elif loose_dlls and not _has_dll(entry):
                    # a data folder next to a loose plugin DLL goes with it
                    actions.append(("move", move(entry, "BepInEx/plugins/")))
            elif name.endswith("_vml.dll"):
                actions.append(("move", move(entry, "InSlimVML/Mods/")))
            elif name.endswith(".dll"):
                actions.append(("move", move(entry, "BepInEx/plugins/")))
            elif name.endswith(".cfg"):
                actions.append(("move", move(entry, "BepInEx/config/")))
            elif name.endswith(".vbuild"):
                actions.append(("move", move(entry, "AdvancedBuilder/Builds/")))
            elif name.endswith(".assets"):
                actions.append(("move", move(entry, "valheim_Data/")))
            elif thunderstore and name in THUNDERSTORE:
                actions.append(("drop", lambda entry=entry: entry.detach()))
        # removing Thunderstore's files is done along a real fix, never alone:
        # an installed mod keeping its manifest.json is still valid
        if any(kind == "move" for kind, _ in actions):
            return self.FIXABLE, [action for _, action in actions]
        return (self.VALID if valid else self.INVALID), []
