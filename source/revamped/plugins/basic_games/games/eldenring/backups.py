"""MO2 Revamped: the automatic backups of a save route before each launch.

Part of the Elden Ring and Nightreign game plugin (game_eldenring_mo2.py),
whose EldenRingMo2Game takes it as a mixin.
"""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import stat
import time

from .common import (
    qCritical,
    qInfo,
)


def _game_class():
    """EldenRingMo2Game, looked up when called (it is built from this mixin)."""
    from ..game_eldenring_mo2 import EldenRingMo2Game

    return EldenRingMo2Game


class BackupsMixin:
    @classmethod
    def save_backup_root(cls, route_root: Path, instance_base: Path, is_global: bool) -> Path:
        """Where the automatic backups of a save route live: beside the route
        ("<folder> Backups/auto"), or inside the MO2 instance for the global
        Steam route, so nothing is ever written next to the Steam saves."""
        if is_global:
            return instance_base / cls.GlobalBackupsFolderName / "auto"
        return route_root.parent / f"{route_root.name} Backups" / "auto"

    @classmethod
    def _route_save_files(cls, route_root: Path) -> list[Path]:
        """The ER0000.* files of a route: in its account folders and loose in
        its root, without following links."""
        files: list[Path] = []
        if not route_root.is_dir():
            return files
        for entry in route_root.iterdir():
            try:
                if entry.is_symlink() or _game_class()._is_link_or_junction(entry):
                    continue
                if entry.is_dir():
                    for child in entry.iterdir():
                        if child.is_file() and not child.is_symlink() and child.name.casefold().startswith(cls.SavePrefix):
                            files.append(child)
                elif entry.is_file() and entry.name.casefold().startswith(cls.SavePrefix):
                    files.append(entry)
            except OSError:
                continue
        return files

    @staticmethod
    def _is_link_or_junction(path: Path) -> bool:
        try:
            attributes = os.lstat(path).st_file_attributes
        except (OSError, AttributeError):
            return False
        return bool(attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT)

    @staticmethod
    def _backup_age_key(entry: Path) -> tuple[int, str, int]:
        # oldest first: by when the copy was made (the names use local time,
        # which repeats an hour when summer time ends), then by name with the
        # "-N" suffix as a number, so "-10" comes after "-2"
        try:
            made = entry.stat().st_mtime_ns
        except OSError:
            made = 0
        _stamp, _separator, suffix = entry.name[15:].partition("-")
        return made, entry.name[:15], int(suffix) if suffix.isdigit() else 1

    @staticmethod
    def _file_signature(path: Path) -> tuple[int, int]:
        info = path.stat()
        return info.st_size, info.st_mtime_ns

    def _backup_route_saves(self, route_root: Path, backup_root: Path, keep: int) -> None:
        """Copies the route's saves into backup_root/<timestamp>, unless they
        match the newest backup (size and time; a copy keeps the file's time,
        so nothing is read twice), then keeps only the newest `keep` copies.
        Failures are logged; they never stop the launch."""
        if keep <= 0:
            return
        staging = None
        try:
            files = self._route_save_files(route_root)
            if not files:
                return
            current = {
                file.relative_to(route_root).as_posix(): self._file_signature(file)
                for file in files
            }
            backup_root.mkdir(parents=True, exist_ok=True)
            # copies a failed run left behind
            for leftover in backup_root.glob(".*.copying"):
                shutil.rmtree(leftover, ignore_errors=True)
            backups = sorted(
                (
                    entry
                    for entry in backup_root.iterdir()
                    if entry.is_dir() and self.BackupNamePattern.match(entry.name)
                ),
                key=self._backup_age_key,
            )
            if backups:
                newest = backups[-1]
                previous = {
                    file.relative_to(newest).as_posix(): self._file_signature(file)
                    for file in newest.rglob("*")
                    if file.is_file()
                }
                if previous == current:
                    qInfo("Elden Ring MO2: saves unchanged since the last automatic backup.")
                    return

            stamp = time.strftime("%Y%m%d-%H%M%S")
            target = backup_root / stamp
            suffix = 2
            while target.exists():
                target = backup_root / f"{stamp}-{suffix}"
                suffix += 1
            staging = backup_root / f".{target.name}.copying"
            for file in files:
                destination = staging / file.relative_to(route_root)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(file, destination)
            os.replace(staging, target)
            staging = None
            backups.append(target)
            qInfo(
                f"Elden Ring MO2: backed up {len(files)} save file(s) to '{target}'."
            )

            for old in backups[:-keep]:
                shutil.rmtree(old, ignore_errors=True)
        except Exception as error:  # a backup must never stop the launch
            qCritical(f"Elden Ring MO2: the automatic save backup failed: {error}")
        finally:
            if staging is not None:
                shutil.rmtree(staging, ignore_errors=True)

    def _save_backups_kept(self) -> int:
        try:
            value = int(
                self._organizer.pluginSetting(self.name(), self.SaveBackupsSetting)
            )
        except (TypeError, ValueError):
            value = self.SaveBackupsDefault
        return max(0, min(value, 50))
