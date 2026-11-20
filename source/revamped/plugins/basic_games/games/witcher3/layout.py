"""The Witcher 3 mod layouts.

MO2's data folder for The Witcher 3 is the game's Mods folder, so a mod holds
the game's mod folders ("modName/content/..."); the game loads only folders
whose name starts with "mod". What goes to the game folder itself (DLC/dlcX,
menu files in bin/config/r4game/user_config_matrix/pc, DLLs in bin/x64) sits
in the mod's root/, which MO2 Revamped maps to the game folder.
"""

from __future__ import annotations

from typing import Callable

import mobase

from ..revamped_shared import find_dir, is_dir, lone_wrapper, name_of, prune_empty, remove_junk, unwrap

ROOT = "root"
# the game folder's own folders a mod may add to, through root/
GAME_FOLDERS = frozenset({"dlc", "bin"})
MENU_FOLDER = "bin/config/r4game/user_config_matrix/pc"
MAX_WRAPPERS = 3

Action = Callable[[], None]


def is_mod_folder(entry) -> bool:
    """A folder the game loads from Mods: "mod..." with a content folder."""
    return is_dir(entry) and name_of(entry).startswith("mod") and find_dir(entry, "content") is not None


def is_dlc_folder(entry) -> bool:
    return is_dir(entry) and name_of(entry).startswith("dlc") and find_dir(entry, "content") is not None


class Witcher3ModDataChecker(mobase.ModDataChecker):
    def __init__(self, use_root: Callable[[], bool] = lambda: True):
        super().__init__()
        # whether the mods' root/ reaches the game folder (MO2 Revamped's
        # mapping, or Root Builder)
        self._use_root = use_root

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
            inner = self._wrapper(filetree)
            if inner is None or self._check(inner, 1) is self.INVALID:
                break
            unwrap(filetree, inner)
        # a fix can show the next one ("Mods/Foo/content": out of Mods, then
        # the "mod" prefix), so until nothing is left, a few times at most
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
            inner = self._wrapper(tree)
            if inner is not None and self._check(inner, depth + 1) is not self.INVALID:
                return self.FIXABLE
        return status

    @staticmethod
    def _wrapper(tree: mobase.IFileTree) -> mobase.IFileTree | None:
        inner = lone_wrapper(tree, frozenset({ROOT, "content"}) | GAME_FOLDERS)
        if inner is None or is_mod_folder(inner) or is_dlc_folder(inner):
            return None
        return inner

    def _plan(self, tree: mobase.IFileTree) -> tuple[mobase.ModDataChecker.CheckReturn, list[Action]]:
        valid = False
        actions: list[Action] = []
        entries = list(tree)

        def move(entry: mobase.FileTreeEntry, target: str) -> Action:
            def run() -> None:
                parent = entry.parent()
                tree.move(entry, target, mobase.IFileTree.InsertPolicy.MERGE)
                prune_empty(parent)

            return run

        def rename(entry: mobase.FileTreeEntry, name: str) -> Action:
            return lambda: tree.move(entry, name, mobase.IFileTree.InsertPolicy.MERGE)

        for entry in entries:
            if not is_dir(entry):
                continue
            name = name_of(entry)
            if name == ROOT:
                # unused in Mods/root when nothing maps it to the game folder
                valid = valid or self._use_root()
            elif name == "mods" and any(is_mod_folder(child) or is_dir(child) and find_dir(child, "content") for child in entry):
                # the game's own Mods folder, one level too many
                actions.append(lambda entry=entry: unwrap(tree, entry))
            elif name in GAME_FOLDERS and self._use_root():
                # DLC/dlcX or bin/...: the game folder's, through root/
                actions.append(move(entry, ROOT + "/"))
            elif is_mod_folder(entry):
                valid = True
            elif is_dlc_folder(entry):
                # a DLC: the game folder's, never renamed into a mod
                if self._use_root():
                    actions.append(move(entry, ROOT + "/DLC/"))
            elif find_dir(entry, "content") is not None:
                # a mod folder without the "mod" the game needs to load it
                actions.append(rename(entry, "mod" + entry.name()))
        if actions:
            return self.FIXABLE, actions
        return (self.VALID if valid else self.INVALID), actions
