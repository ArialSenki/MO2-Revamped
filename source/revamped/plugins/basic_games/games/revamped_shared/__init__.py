"""Helpers MO2 Revamped's own game plugins share (Witcher 3, Stardew Valley,
Valheim...): logging that survives non-ASCII paths, translation by context,
Windows' known folders, and small file tree helpers.

Not a game: basic_games only loads games/game_*.py and games/*/plugins.
"""

from __future__ import annotations

import os
from typing import Callable

import mobase
from PyQt6.QtCore import QCoreApplication, qCritical, qInfo, qWarning

_INFO, _WARNING, _CRITICAL = 20, 30, 50


def _safe(qt_function, level):
    # PyQt hands Qt's log functions ASCII only: a path with an accent raised
    # UnicodeEncodeError and stopped the code that logged it. Such messages go
    # through Python's logging, which MO2's Python plugin writes to the same log.
    def log(message) -> None:
        text = "" if message is None else str(message)
        if text.isascii():
            qt_function(text)
        else:
            import logging

            logging.getLogger("mo2revamped").log(level, text)

    return log


log_info = _safe(qInfo, _INFO)
log_warning = _safe(qWarning, _WARNING)
log_critical = _safe(qCritical, _CRITICAL)


def translator(context: str) -> Callable[[str], str]:
    """tr() for a context of translations/revamped_<language>.qm."""

    def tr(text: str) -> str:
        return QCoreApplication.translate(context, text)

    return tr


def known_folder(guid: str, fallback: str) -> str:
    """A Windows known folder, also when the user moved it (Documents or Saved
    Games on another drive)."""
    try:
        import ctypes
        import uuid
        from ctypes import wintypes

        raw = (ctypes.c_byte * 16).from_buffer_copy(uuid.UUID(guid).bytes_le)
        path = ctypes.c_wchar_p()
        shell32 = ctypes.windll.shell32
        shell32.SHGetKnownFolderPath.argtypes = [
            ctypes.c_void_p,
            wintypes.DWORD,
            wintypes.HANDLE,
            ctypes.POINTER(ctypes.c_wchar_p),
        ]
        if shell32.SHGetKnownFolderPath(ctypes.byref(raw), 0, None, ctypes.byref(path)) == 0:
            result = path.value
            ctypes.windll.ole32.CoTaskMemFree(path)
            if result:
                return result.replace("\\", "/")
    except Exception:  # noqa: BLE001 - not Windows, or no shell32
        pass
    return os.path.expandvars(fallback).replace("\\", "/")


def documents_folder() -> str:
    return known_folder("{FDD39AD0-238F-46AF-ADB4-6C85480369C7}", "%USERPROFILE%/Documents")


def roaming_app_data_folder() -> str:
    return known_folder("{3EB685DB-65F9-4CF6-A03A-E3EF65729F3D}", "%APPDATA%")


def local_low_app_data_folder() -> str:
    return known_folder("{A520A1A4-1780-4FF6-BD18-167343C5AF16}", "%USERPROFILE%/AppData/LocalLow")


def root_builder_enabled(organizer) -> bool:
    """Whether Root Builder deploys the mods' root/ folders itself."""
    try:
        return any(organizer.isPluginEnabled(name) for name in ("RootBuilder", "Root Builder"))
    except Exception:  # noqa: BLE001
        return False


# files that may sit next to the folder that holds a mod
DOC_SUFFIXES = (
    ".txt", ".md", ".pdf", ".url", ".html", ".htm", ".png", ".jpg", ".jpeg",
    ".webp", ".gif", ".jxl", ".bmp", ".rtf", ".nfo", ".docx",
)  # fmt: skip


# what macOS and Windows leave in archives: never part of a mod
JUNK_FILES = frozenset({".ds_store", "thumbs.db", "desktop.ini"})
JUNK_FOLDERS = frozenset({"__macosx"})


def is_dir(entry) -> bool:
    return isinstance(entry, mobase.IFileTree)


def name_of(entry) -> str:
    return entry.name().casefold()


def is_doc(entry) -> bool:
    """A readme, licence or picture (LICENSE, README with no suffix too)."""
    if is_dir(entry):
        return False
    name = name_of(entry)
    return name.endswith(DOC_SUFFIXES) or "." not in name or name in JUNK_FILES or name.startswith("._")


def find_dir(tree: mobase.IFileTree, path: str) -> mobase.IFileTree | None:
    entry = tree.find(path, mobase.FileTreeEntry.FileTypes.DIRECTORY)
    return entry if is_dir(entry) else None


def walk_files(tree: mobase.IFileTree | None, visit: Callable[[str, mobase.FileTreeEntry], bool]) -> None:
    """Calls visit(path, entry) for each file; visit returns True to stop."""
    if tree is None:
        return

    def step(path: str, entry: mobase.FileTreeEntry) -> mobase.IFileTree.WalkReturn:
        if not is_dir(entry) and visit(path, entry):
            return mobase.IFileTree.WalkReturn.STOP
        return mobase.IFileTree.WalkReturn.CONTINUE

    tree.walk(step, "/")


def contains_suffix(tree: mobase.IFileTree | None, suffixes: tuple[str, ...]) -> bool:
    found = []
    walk_files(tree, lambda _p, e: bool(name_of(e).endswith(suffixes) and not found.append(e)))
    return bool(found)


def unwrap(tree: mobase.IFileTree, inner: mobase.IFileTree) -> None:
    """Takes a folder's contents up into its parent, and the folder away.

    Never IFileTree.merge: merging a folder that holds an entry of its own
    name into its parent crashes MO2 (an access violation in uibase). The
    folder is renamed first, then its entries are moved up one by one."""
    temporary = "__mo2_revamped_unwrap__"
    while tree.exists(temporary):
        temporary += "_"
    if not tree.move(inner, temporary):
        return
    holder = find_dir(tree, temporary)
    if holder is None:
        return
    for entry in list(holder):
        tree.insert(entry, mobase.IFileTree.InsertPolicy.MERGE)
    holder.detach()


def remove_junk(tree: mobase.IFileTree) -> None:
    """Removes what macOS and Windows leave in archives (.DS_Store, ._ files,
    __MACOSX, Thumbs.db, desktop.ini), at any depth."""
    junk = []

    def visit(_path: str, entry) -> mobase.IFileTree.WalkReturn:
        name = name_of(entry)
        if is_dir(entry):
            if name in JUNK_FOLDERS:
                junk.append(entry)
                return mobase.IFileTree.WalkReturn.SKIP
        elif name in JUNK_FILES or name.startswith("._"):
            junk.append(entry)
        return mobase.IFileTree.WalkReturn.CONTINUE

    tree.walk(visit, "/")
    for entry in junk:
        entry.detach()


def prune_empty(tree: mobase.IFileTree | None) -> None:
    """Removes a folder left empty by a move, and parents it leaves empty."""
    while tree is not None and len(tree) == 0 and tree.parent() is not None:
        parent = tree.parent()
        tree.detach()
        tree = parent


def lone_wrapper(tree: mobase.IFileTree, keep: frozenset[str] = frozenset()) -> mobase.IFileTree | None:
    """The one folder holding a mod, when only documents sit next to it and
    its name isn't one of `keep` (a folder the game itself uses)."""
    folders = []
    for entry in tree:
        if is_dir(entry):
            if name_of(entry) not in JUNK_FOLDERS:
                folders.append(entry)
        elif not is_doc(entry):
            return None
    if len(folders) != 1 or name_of(folders[0]) in keep:
        return None
    return folders[0]
