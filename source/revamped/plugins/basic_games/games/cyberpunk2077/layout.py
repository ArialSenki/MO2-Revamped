"""Cyberpunk 2077 mod layouts: what an installed mod must look like, and the
fixes for archives packed another way.

Mod Organizer's data folder for Cyberpunk is the game folder, so a mod is
valid when it has one of the game's top folders (archive, bin, engine, r6,
red4ext, mods; root for Root Builder). Layouts the game can't load are moved
where it looks for them; anything ambiguous (two folders that look like
options, a CET or REDmod without its folder name) is left for the manual
installer rather than guessed.
"""

from __future__ import annotations

from typing import Callable

import mobase

# the game folder's own top folders a mod may add to
TOP_FOLDERS = frozenset({"archive", "bin", "engine", "r6", "red4ext", "mods", "root"})

ARCHIVE_MOD = "archive/pc/mod/"
CET_MODS = "bin/x64/plugins/cyber_engine_tweaks/mods/"
ARCHIVE_SUFFIXES = (".archive", ".xl")
TWEAK_SUFFIXES = (".yaml", ".yml", ".tweak")
AUDIO_SUFFIXES = (".wav", ".ogg", ".mp3", ".flac")
# files that may sit next to the folder that holds the mod
DOC_SUFFIXES = (
    ".txt", ".md", ".pdf", ".url", ".html", ".htm", ".png", ".jpg", ".jpeg",
    ".webp", ".gif", ".jxl", ".bmp", ".rtf", ".nfo", ".docx",
)  # fmt: skip
# folders holding the mod inside one more folder: at most this many levels
MAX_WRAPPERS = 3

Action = Callable[[], None]


def _is_dir(entry: mobase.FileTreeEntry) -> bool:
    return isinstance(entry, mobase.IFileTree)


def _name(entry: mobase.FileTreeEntry) -> str:
    return entry.name().casefold()


def _has_suffix(entry: mobase.FileTreeEntry, suffixes: tuple[str, ...]) -> bool:
    return not _is_dir(entry) and _name(entry).endswith(suffixes)


def contains_suffix(tree: mobase.IFileTree | None, suffixes: tuple[str, ...]) -> bool:
    """Whether a file with one of the suffixes is anywhere in the tree."""
    if tree is None or not _is_dir(tree):
        return False
    found = False

    def visit(_path: str, entry: mobase.FileTreeEntry) -> mobase.IFileTree.WalkReturn:
        nonlocal found
        if _has_suffix(entry, suffixes):
            found = True
            return mobase.IFileTree.WalkReturn.STOP
        return mobase.IFileTree.WalkReturn.CONTINUE

    tree.walk(visit)
    return found


def _only_suffix(tree: mobase.IFileTree, suffixes: tuple[str, ...]) -> bool:
    """Whether the tree has files and all of them, documents apart, have one of
    the suffixes."""
    files = 0
    other = False

    def visit(_path: str, entry: mobase.FileTreeEntry) -> mobase.IFileTree.WalkReturn:
        nonlocal files, other
        if _is_dir(entry):
            return mobase.IFileTree.WalkReturn.CONTINUE
        if _has_suffix(entry, suffixes):
            files += 1
        elif not _is_doc(entry):
            other = True
            return mobase.IFileTree.WalkReturn.STOP
        return mobase.IFileTree.WalkReturn.CONTINUE

    tree.walk(visit)
    return files > 0 and not other


def _is_doc(entry: mobase.FileTreeEntry) -> bool:
    if _is_dir(entry):
        return False
    name = _name(entry)
    # LICENSE, README, CHANGELOG...
    return name.endswith(DOC_SUFFIXES) or "." not in name


def _find_dir(tree: mobase.IFileTree, path: str) -> mobase.IFileTree | None:
    entry = tree.find(path, mobase.FileTreeEntry.FileTypes.DIRECTORY)
    return entry if entry is not None and _is_dir(entry) else None


def prune_empty(tree: mobase.IFileTree | None) -> None:
    """Remove a folder left empty by a move, and parents it leaves empty."""
    while tree is not None and len(tree) == 0 and tree.parent() is not None:
        parent = tree.parent()
        tree.detach()
        tree = parent


class CyberpunkModDataChecker(mobase.ModDataChecker):
    # Root Builder (an add-on MO2 Revamped doesn't include) deploys root/ as
    # real files. Without it MO2 Revamped serves a mod's top-level files and
    # its root/ in the game folder through the VFS, and the game plugin
    # force-loads CET's and RED4ext's loaders (executableForcedLoads)
    _root_builder_moves = {
        # RED4ext: only its loader DLLs
        "red4ext/RED4ext.dll": "root/red4ext/",
        "bin/x64/winmm.dll": "root/bin/x64/",
        # CET: its loader; its framework folder is moved by _root_builder_cet
        "bin/x64/version.dll": "root/bin/x64/",
        "bin/x64/global.ini": "root/bin/x64/",
        "bin/x64/plugins/cyber_engine_tweaks.asi": "root/bin/x64/plugins/",
    }
    _cet_folder = "bin/x64/plugins/cyber_engine_tweaks"

    def __init__(self, use_root_builder: Callable[[], bool] = lambda: False):
        super().__init__()
        self._use_root_builder = use_root_builder

    # mobase.ModDataChecker

    def dataLooksValid(self, filetree: mobase.IFileTree) -> mobase.ModDataChecker.CheckReturn:
        # the quick installer looks at a lone top folder's contents too: a
        # layout fixable from its parent stays fixable there
        parent = filetree.parent()
        if parent is not None and self.dataLooksValid(parent) is self.FIXABLE:
            return self.FIXABLE
        return self._check(filetree, 0)

    def fix(self, filetree: mobase.IFileTree) -> mobase.IFileTree:
        for _ in range(MAX_WRAPPERS):
            status, _ = self._plan(filetree)
            if status is not self.INVALID:
                break
            inner = self._wrapper(filetree)
            if inner is None or self._check(inner, 1) is self.INVALID:
                break
            filetree.merge(inner)
            inner.detach()
        _, actions = self._plan(filetree)
        for action in actions:
            action()
        return filetree

    # the checks

    def _check(self, tree: mobase.IFileTree, depth: int) -> mobase.ModDataChecker.CheckReturn:
        status, _ = self._plan(tree)
        if status is self.INVALID and depth < MAX_WRAPPERS:
            inner = self._wrapper(tree)
            if inner is not None and self._check(inner, depth + 1) is not self.INVALID:
                return self.FIXABLE
        return status

    def _wrapper(self, tree: mobase.IFileTree) -> mobase.IFileTree | None:
        """The one folder holding the mod, when only documents sit next to it.
        Two folders may be options of the mod (the manual installer asks)."""
        folders = []
        for entry in tree:
            if _is_dir(entry):
                folders.append(entry)
            elif not _is_doc(entry):
                return None
        if len(folders) != 1 or _name(folders[0]) in TOP_FOLDERS:
            return None
        return folders[0]

    def _plan(self, tree: mobase.IFileTree) -> tuple[mobase.ModDataChecker.CheckReturn, list[Action]]:
        """Whether the tree is valid, fixable or invalid, and the moves that
        fix it."""
        valid = False
        actions: list[Action] = []
        entries = list(tree)
        has_audio = any(_has_suffix(entry, AUDIO_SUFFIXES) for entry in entries)

        def move(entry: mobase.FileTreeEntry, target: str) -> Action:
            def run() -> None:
                parent = entry.parent()
                tree.move(entry, target, mobase.IFileTree.InsertPolicy.MERGE)
                prune_empty(parent)

            return run

        for entry in entries:
            name = _name(entry)
            if _is_dir(entry):
                if name in TOP_FOLDERS:
                    valid = True
                    actions.extend(self._inner_fixes(tree, entry, move))
                elif entry.exists("info.json", mobase.FileTreeEntry.FileTypes.FILE):
                    # a REDmod's folder
                    actions.append(move(entry, "mods/"))
                elif entry.exists("init.lua", mobase.FileTreeEntry.FileTypes.FILE):
                    # a Cyber Engine Tweaks mod's folder
                    actions.append(move(entry, CET_MODS))
                elif name == "x64":
                    actions.append(move(entry, "bin/"))
                elif name == "pc" and _find_dir(entry, "mod") is not None:
                    actions.append(move(entry, "archive/"))
                elif name == "plugins" and (
                    _find_dir(entry, "cyber_engine_tweaks") is not None
                    or any(_has_suffix(child, (".asi",)) for child in entry)
                ):
                    actions.append(move(entry, "bin/x64/"))
                elif name == "cyber_engine_tweaks":
                    actions.append(move(entry, "bin/x64/plugins/"))
                elif name == "scripts" and contains_suffix(entry, (".reds",)):
                    actions.append(move(entry, "r6/"))
                elif name == "tweaks" and _only_suffix(entry, TWEAK_SUFFIXES):
                    actions.append(move(entry, "r6/"))
                # any other folder: kept as it is
            elif name.endswith(ARCHIVE_SUFFIXES):
                actions.append(move(entry, ARCHIVE_MOD))
            elif name.endswith(".reds"):
                actions.append(move(entry, "r6/scripts/"))
            elif name.endswith(TWEAK_SUFFIXES) and not has_audio:
                # next to audio files they are an Audioware manifest
                actions.append(move(entry, "r6/tweaks/"))
            elif name.endswith(".asi"):
                actions.append(move(entry, "bin/x64/plugins/"))
            # any other file (readmes, licenses): kept as it is

        if self._use_root_builder() and any(tree.exists(path) for path in self._root_builder_moves):
            actions.append(lambda: self._root_builder_layout(tree))

        if actions:
            return self.FIXABLE, actions
        return (self.VALID if valid else self.INVALID), actions

    def _inner_fixes(self, tree: mobase.IFileTree, top: mobase.IFileTree, move) -> list[Action]:
        """Fixes inside the game's top folders."""
        actions: list[Action] = []
        name = _name(top)
        if name == "mods":
            # mods/<name>/ is REDmod's; one with init.lua and no info.json is
            # a CET mod packed relative to CET's folder
            for child in top:
                if (
                    _is_dir(child)
                    and not child.exists("info.json", mobase.FileTreeEntry.FileTypes.FILE)
                    and child.exists("init.lua", mobase.FileTreeEntry.FileTypes.FILE)
                ):
                    actions.append(move(child, CET_MODS))
        elif name == "archive":
            # archive/pc/patch is the layout of the game's first versions
            patch = _find_dir(top, "pc/patch")
            if patch is not None and contains_suffix(patch, ARCHIVE_SUFFIXES):
                for child in list(patch):
                    actions.append(move(child, ARCHIVE_MOD))
        elif name == "red4ext":
            # RED4ext loads a plugin from its own folder in red4ext/plugins
            plugins = _find_dir(top, "plugins")
            if plugins is not None:
                for child in plugins:
                    if _has_suffix(child, (".dll",)):
                        stem = child.name().rsplit(".", 1)[0]
                        actions.append(move(child, f"red4ext/plugins/{stem}/"))
        return actions

    def _root_builder_layout(self, tree: mobase.IFileTree) -> None:
        """Root Builder's layout: the loaders to root/, and the CET framework
        with them, its mods apart (only CET 1.27+ works under the VFS)."""
        for source, target in self._root_builder_moves.items():
            entry = tree.find(source)
            if entry is not None:
                parent = entry.parent()
                tree.move(entry, target, mobase.IFileTree.InsertPolicy.MERGE)
                prune_empty(parent)
        cet = _find_dir(tree, self._cet_folder)
        if cet is None:
            return
        root_cet = f"root/{self._cet_folder}"
        if cet.find("mods") is None:
            parent = cet.parent()
            tree.move(cet, root_cet, mobase.IFileTree.InsertPolicy.MERGE)
            prune_empty(parent)
        else:
            for entry in list(cet):
                if _name(entry) != "mods":
                    tree.move(entry, root_cet + "/", mobase.IFileTree.InsertPolicy.MERGE)
