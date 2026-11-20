"""Stardew Valley mod layouts.

MO2's data folder is the game's Mods folder, so a mod holds SMAPI mod folders
("ModName/manifest.json"). SMAPI looks into a folder for mods only when it has
no files of its own (documents apart) and has subfolders, so a wrapper around
mod folders ("Pack/[CP] A/manifest.json") loads as it is: it is valid here too.
"""

from __future__ import annotations

import mobase

from ..revamped_shared import is_dir, name_of, remove_junk, unwrap

MANIFEST = "manifest.json"
# how deep SMAPI finds a manifest under one top folder (as deep as any real
# mod pack goes)
MAX_DEPTH = 4
# the files SMAPI's mod scanner ignores when it decides whether a folder is a
# mod (ModScanner: documents, images, archives, backups, shortcuts, OS junk)
SMAPI_IGNORED_SUFFIXES = (
    ".txt", ".md", ".doc", ".docx", ".rtf", ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp",
    ".zip", ".rar", ".7z", ".tar", ".gz", ".bak", ".old", ".backup", ".url", ".lnk",
)  # fmt: skip
SMAPI_IGNORED_NAMES = frozenset({"desktop.ini", "thumbs.db", ".ds_store"})


def smapi_ignores(entry) -> bool:
    """Whether SMAPI's scanner skips this entry: a file it ignores, or a
    folder whose name starts with a dot (or macOS's __MACOSX)."""
    name = name_of(entry)
    if is_dir(entry):
        return name.startswith(".") or name == "__macosx"
    return name.endswith(SMAPI_IGNORED_SUFFIXES) or name in SMAPI_IGNORED_NAMES or name.startswith("._")


def has_manifest(folder: mobase.IFileTree) -> bool:
    return folder.exists(MANIFEST, mobase.FileTreeEntry.FileTypes.FILE)


def holds_mods(folder: mobase.IFileTree, depth: int = 0) -> bool:
    """Whether SMAPI finds a mod in the folder: a manifest in it, or (when it
    has no files of its own but documents) in one of its subfolders."""
    if has_manifest(folder):
        return True
    if depth >= MAX_DEPTH:
        return False
    if any(not is_dir(e) and not smapi_ignores(e) for e in folder):
        return False
    return any(is_dir(e) and not smapi_ignores(e) and holds_mods(e, depth + 1) for e in folder)


class StardewValleyModDataChecker(mobase.ModDataChecker):
    def __init__(self, organizer: mobase.IOrganizer | None = None):
        super().__init__()
        # to see the mods already installed: a translation or a set of
        # config.json files goes over a mod's folder ("ModName/i18n/de.json")
        self._organizer = organizer

    def _installed_mod(self, name: str) -> bool:
        """Whether the game's Mods folder, as MO2 shows it, has a mod in a
        folder of that name."""
        if self._organizer is None:
            return False
        try:
            entry = self._organizer.virtualFileTree().find(name, mobase.FileTreeEntry.FileTypes.DIRECTORY)
        except Exception:  # noqa: BLE001 - MO2 not ready yet
            return False
        return is_dir(entry) and holds_mods(entry)

    def dataLooksValid(self, filetree: mobase.IFileTree) -> mobase.ModDataChecker.CheckReturn:
        parent = filetree.parent()
        if parent is not None and self.dataLooksValid(parent) is self.FIXABLE:
            return self.FIXABLE
        return self._check(filetree)

    def fix(self, filetree: mobase.IFileTree) -> mobase.IFileTree:
        remove_junk(filetree)
        mods = self._mods_folder(filetree)
        if mods is not None:
            unwrap(filetree, mods)
        return filetree

    def _check(self, tree: mobase.IFileTree) -> mobase.ModDataChecker.CheckReturn:
        if self._mods_folder(tree) is not None:
            return self.FIXABLE
        return self._own_status(tree)

    @staticmethod
    def _mods_folder(tree: mobase.IFileTree) -> mobase.IFileTree | None:
        """The game's Mods folder packed in the archive ("Mods/ModName/...")."""
        for entry in tree:
            if is_dir(entry) and name_of(entry) == "mods" and not has_manifest(entry) and holds_mods(entry):
                folders = [e for e in tree if is_dir(e)]
                if len(folders) == 1:
                    return entry
        return None

    def _own_status(self, tree: mobase.IFileTree) -> mobase.ModDataChecker.CheckReturn:
        if has_manifest(tree):
            # a mod's own files at the top: the folder needs a name the
            # checker doesn't know (the manual installer asks)
            return self.INVALID
        if any(is_dir(entry) and not smapi_ignores(entry) and holds_mods(entry) for entry in tree):
            return self.VALID
        # files for mods already installed (translations, config.json sets)
        folders = [entry for entry in tree if is_dir(entry)]
        if folders and all(self._installed_mod(entry.name()) for entry in folders):
            return self.VALID
        return self.INVALID
