"""The mod data checker of BepInEx games (Valheim, Subnautica...).

MO2's data folder for these games is the game folder: a mod holds BepInEx/...
(plugins, config, patchers) or BepInEx's own files (winhttp.dll, Doorstop).
Mods packed for Thunderstore (manifest.json, icon.png, README.md and the
plugin's files at the top), BepInEx packs in a wrapper folder, loose DLLs and
a plugin's own folder are moved where BepInEx loads them. A game adds its own
folders and rules by subclassing.
"""

from __future__ import annotations

from typing import Callable

import mobase

from . import JUNK_FOLDERS, is_dir, lone_wrapper, name_of, prune_empty, remove_junk, unwrap

# what every BepInEx game folder takes from a mod as it is
BEPINEX_VALID = frozenset({
    "bepinex", "doorstop_libs", ".doorstop_version", "doorstop_config.ini", "winhttp.dll",
    "changelog.txt", "libdoorstop.dylib", "libdoorstop.so",
})  # fmt: skip
# BepInEx's own folders, which a mod may pack without BepInEx/
BEPINEX_FOLDERS = frozenset({"plugins", "config", "patchers"})
# Thunderstore's package files: not for the game folder
THUNDERSTORE = frozenset({
    "manifest.json", "icon.png", "readme.md", "changelog.md", "license", "license.md", "license.txt",
})  # fmt: skip
MAX_WRAPPERS = 3

Action = Callable[[], None]


def has_dll(folder: mobase.IFileTree) -> bool:
    """Whether a DLL is anywhere in the folder."""
    found = []

    def visit(_path: str, entry) -> mobase.IFileTree.WalkReturn:
        if not is_dir(entry) and name_of(entry).endswith(".dll"):
            found.append(entry)
            return mobase.IFileTree.WalkReturn.STOP
        return mobase.IFileTree.WalkReturn.CONTINUE

    folder.walk(visit, "/")
    return bool(found)


def has_bepinex_folder(folder: mobase.IFileTree) -> bool:
    """Whether a BepInEx folder is anywhere in the folder (then the folder is
    a wrapper, however deep: "Mod v1/Mod/BepInEx/plugins/x.dll")."""
    found = []

    def visit(_path: str, entry) -> mobase.IFileTree.WalkReturn:
        if is_dir(entry) and name_of(entry) == "bepinex":
            found.append(entry)
            return mobase.IFileTree.WalkReturn.STOP
        return mobase.IFileTree.WalkReturn.CONTINUE

    folder.walk(visit, "/")
    return bool(found)


class BepInExModDataChecker(mobase.ModDataChecker):
    # the game folder's own files and folders a mod may add to
    VALID_NAMES: frozenset[str] = BEPINEX_VALID
    # the folders BepInEx packs come wrapped in ("BepInExPack_Valheim")
    PACK_WRAPPERS: frozenset[str] = frozenset()
    # folders a plugin brings next to its DLL, for BepInEx/plugins
    PLUGIN_FOLDERS: frozenset[str] = frozenset()

    def plugins_target(self) -> str:
        """Where a loose plugin goes."""
        return "BepInEx/plugins/"

    def extra_file_target(self, name: str) -> str | None:
        """A game's own place for a loose file (checked before the DLL rule)."""
        return None

    def extra_folder_target(self, folder: mobase.IFileTree) -> str | None:
        """A game's own place for a top folder (checked before the plugin
        folder rules)."""
        return None

    # mobase.ModDataChecker

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
            inner = lone_wrapper(filetree, self.VALID_NAMES)
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

    # the checks

    def _check(self, tree: mobase.IFileTree, depth: int) -> mobase.ModDataChecker.CheckReturn:
        status, _ = self._plan(tree)
        if status is self.INVALID and depth < MAX_WRAPPERS:
            inner = lone_wrapper(tree, self.VALID_NAMES)
            if inner is not None and self._check(inner, depth + 1) is not self.INVALID:
                return self.FIXABLE
        return status

    def _plugin_folder(self, folder: mobase.IFileTree) -> bool:
        """A plugin's own folder: DLLs (at any depth) and none of BepInEx's or
        the game folder's own structure (then it's a wrapper)."""
        structure = self.VALID_NAMES | BEPINEX_FOLDERS | self.PACK_WRAPPERS
        return (
            has_dll(folder)
            and not any(name_of(e) in structure for e in folder)
            and not has_bepinex_folder(folder)
        )

    def _loose_plugin(self, entry) -> bool:
        name = name_of(entry)
        return (
            not is_dir(entry)
            and name.endswith(".dll")
            and name not in self.VALID_NAMES
            and self.extra_file_target(name) is None
        )

    def _plan(self, tree: mobase.IFileTree) -> tuple[mobase.ModDataChecker.CheckReturn, list[Action]]:
        valid = False
        actions: list[tuple[str, Action]] = []
        entries = list(tree)
        names = {name_of(e) for e in entries}
        # the archive's only folder (documents and Thunderstore's files apart):
        # with a DLL of its own it is the plugin's folder; two may be options
        # (what macOS leaves, __MACOSX/._Mod.dll, isn't a folder of the mod's:
        # fix() removes it first)
        folders = [e for e in entries if is_dir(e) and name_of(e) not in JUNK_FOLDERS]
        lone = folders[0] if len(folders) == 1 else None
        loose_dlls = any(self._loose_plugin(e) for e in entries)
        # a loose DLL next to folders that hold DLLs of their own: options
        if loose_dlls and any(
            name_of(f) not in self.VALID_NAMES and self.extra_folder_target(f) is None and has_dll(f) for f in folders
        ):
            return self.INVALID, []
        # a Thunderstore package: its manifest and icon next to the mod's files
        thunderstore = "manifest.json" in names and (
            "icon.png" in names or any(n.endswith(".dll") or n in BEPINEX_FOLDERS for n in names)
        )

        def move(entry: mobase.FileTreeEntry, target: str) -> Action:
            def run() -> None:
                parent = entry.parent()
                tree.move(entry, target, mobase.IFileTree.InsertPolicy.MERGE)
                prune_empty(parent)

            return run

        for entry in entries:
            name = name_of(entry)
            if name in self.VALID_NAMES:
                valid = True
            elif is_dir(entry):
                target = self.extra_folder_target(entry)
                if target is not None:
                    actions.append(("move", move(entry, target)))
                elif name in self.PACK_WRAPPERS:
                    # BepInEx's own pack, wrapped
                    actions.append(("move", lambda entry=entry: unwrap(tree, entry)))
                elif name in BEPINEX_FOLDERS:
                    actions.append(("move", move(entry, "BepInEx/")))
                elif name in self.PLUGIN_FOLDERS:
                    actions.append(("move", move(entry, self.plugins_target())))
                elif (entry is lone or (lone is not None and name == name_of(lone))) and self._plugin_folder(entry):
                    # the plugin with its data files, as BepInEx loads it
                    actions.append(("move", move(entry, self.plugins_target())))
                elif loose_dlls and not has_dll(entry):
                    # a data folder next to a loose plugin DLL goes with it
                    actions.append(("move", move(entry, self.plugins_target())))
            else:
                target = self.extra_file_target(name)
                if target is not None:
                    actions.append(("move", move(entry, target)))
                elif name.endswith(".dll"):
                    actions.append(("move", move(entry, self.plugins_target())))
                elif name.endswith(".cfg"):
                    actions.append(("move", move(entry, "BepInEx/config/")))
                elif thunderstore and name in THUNDERSTORE:
                    actions.append(("drop", lambda entry=entry: entry.detach()))
        # removing Thunderstore's files is done along a real fix, never alone:
        # an installed mod keeping its manifest.json is still valid
        if any(kind == "move" for kind, _ in actions):
            return self.FIXABLE, [action for _, action in actions]
        return (self.VALID if valid else self.INVALID), []
