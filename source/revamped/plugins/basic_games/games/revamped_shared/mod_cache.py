"""MO2 Revamped: what a problem check reads from each active mod, kept while
the mod's folder doesn't change.

MO2 runs the Python plugins' problem checks on its interface thread, after
every change to the mod list (a deadlock between a check on a worker and a
mod refresh made it so). A check that reads every active mod's files each
time stops the window at each checkbox with a few hundred mods. With this, a
mod is read again only when its folder's mtime changed or it was installed
again (an install can write into an existing folder without changing it).
"""

from __future__ import annotations

import os
from pathlib import Path
from typing import Any, Callable, Generic, TypeVar

import mobase

T = TypeVar("T")


class PerModCache(Generic[T]):
    def __init__(self) -> None:
        # (name, folder) -> (the folder's mtime when read, what was read)
        self._entries: dict[tuple[str, str], tuple[int, T]] = {}

    def forget(self, mod: Any) -> None:
        """A mod installed again or removed (an IModInterface or its name)."""
        name = mod if isinstance(mod, str) else mod.name()
        for key in [key for key in self._entries if key[0] == name]:
            del self._entries[key]

    def active(
        self,
        organizer: mobase.IOrganizer,
        read: Callable[[str, Path, Any], T | None],
    ) -> list[T]:
        """read(name, folder, mod) for each active mod, in priority order
        (separators, foreign mods and Overwrite left out), or what it gave
        the last time when the mod's folder is as it was. A mod whose read
        gives None (or raises) is left out and read again next time."""
        mod_list = organizer.modList()
        mods = []
        for name in mod_list.allModsByProfilePriority():
            if not mod_list.state(name) & mobase.ModState.ACTIVE:
                continue
            mod = mod_list.getMod(name)
            if mod is None or mod.isSeparator() or mod.isForeign() or mod.isOverwrite():
                continue
            mods.append((name, mod.absolutePath(), mod))
        stamps = _folder_stamps(os.path.dirname(os.path.normpath(folder)) for _, folder, _ in mods)

        results: list[T] = []
        for name, folder, mod in mods:
            key = (name, folder)
            stamp = stamps.get(os.path.normcase(os.path.normpath(folder)))
            if stamp is None:
                try:
                    stamp = os.stat(folder).st_mtime_ns
                except OSError:
                    self._entries.pop(key, None)
                    continue
            path = Path(folder)
            cached = self._entries.get(key)
            if cached is not None and cached[0] == stamp:
                results.append(cached[1])
                continue
            try:
                value = read(name, path, mod)
            except Exception:  # noqa: BLE001 - a mod being removed
                value = None
            if value is None:
                self._entries.pop(key, None)
                continue
            self._entries[key] = (stamp, value)
            results.append(value)
        return results


def _folder_stamps(parents) -> dict[str, int]:
    """The mtime of every folder in the given folders (the mods folder,
    usually one), by its normalized path. One listing gives them all: on
    Windows a stat of each of ~400 mod folders took ~150 ms, more than the
    check this saves."""
    stamps: dict[str, int] = {}
    for parent in set(parents):
        try:
            with os.scandir(parent) as entries:
                for entry in entries:
                    if entry.is_dir(follow_symlinks=False):
                        stamps[os.path.normcase(os.path.normpath(entry.path))] = entry.stat(
                            follow_symlinks=False
                        ).st_mtime_ns
        except OSError:
            continue
    return stamps
