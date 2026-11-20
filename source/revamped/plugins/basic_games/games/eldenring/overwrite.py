"""MO2 Revamped: the profile's Overwrite cleanup before and after a game run.

Part of the Elden Ring and Nightreign game plugin (game_eldenring_mo2.py),
whose EldenRingMo2Game takes it as a mixin.
"""

from __future__ import annotations

import os
from pathlib import Path
import stat

from .common import (
    qCritical,
    qInfo,
    _tr,
)


def _game_class():
    """EldenRingMo2Game, looked up when called (it is built from this mixin)."""
    from ..game_eldenring_mo2 import EldenRingMo2Game

    return EldenRingMo2Game


class OverwriteMixin:
    def _clear_overwrite_logs_before_game(self) -> None:
        # a running game (or a tool) may still be writing its logs there
        if self._elden_ring_process_state() is not False:
            qInfo(
                "Elden Ring MO2: Elden Ring is already running; old logs in "
                "Overwrite were left in place."
            )
            return
        try:
            overwrite_path = Path(self._organizer.overwritePath())
            if overwrite_path.name.casefold() != "overwrite":
                raise ValueError(_tr("MO2 returned a path that is not named Overwrite"))

            root_stat = os.lstat(overwrite_path)
            reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
            if getattr(root_stat, "st_file_attributes", 0) & reparse_flag:
                raise ValueError(_tr("the Overwrite directory is a reparse point"))
            if not stat.S_ISDIR(root_stat.st_mode):
                raise ValueError(_tr("the Overwrite path is not a directory"))

            removed = self._remove_overwrite_logs(overwrite_path)
            qInfo(
                "Elden Ring MO2: removed "
                f"{removed} old .log file(s) from Overwrite before launch."
            )
        except FileNotFoundError:
            qInfo(
                "Elden Ring MO2: Overwrite was missing or changed before launch; "
                "no old logs were cleared."
            )
        except (OSError, TypeError, ValueError) as error:
            qCritical(
                "Elden Ring MO2: could not clear old .log files from Overwrite "
                f"before launch: {error}. The game will still start."
            )

    @staticmethod
    def _remove_overwrite_logs(overwrite_path: Path) -> int:
        removed = 0
        with os.scandir(overwrite_path) as entries:
            for entry in entries:
                path = Path(entry.path)
                entry_stat = os.lstat(path)
                reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
                if getattr(entry_stat, "st_file_attributes", 0) & reparse_flag:
                    continue
                if stat.S_ISDIR(entry_stat.st_mode):
                    removed += _game_class()._remove_overwrite_logs(path)
                elif path.suffix.casefold() == ".log":
                    try:
                        os.unlink(path)
                    except OSError as error:
                        # a log still open elsewhere: the others are removed
                        qInfo(f"Elden Ring MO2: kept '{path.name}' in Overwrite: {error}")
                        continue
                    removed += 1
        return removed

    def _arm_overwrite_cleanup(self) -> None:
        try:
            overwrite_path = Path(self._organizer.overwritePath())
        except (OSError, TypeError, ValueError) as error:
            qCritical(
                f"Elden Ring MO2: automatic Overwrite cleanup could not get "
                f"MO2's Overwrite path: {error}. The game will still start."
            )
            return

        self._overwrite_cleanup_path = overwrite_path
        self._overwrite_cleanup_armed = True
        qInfo("Elden Ring MO2: Overwrite cleanup will run after Elden Ring closes.")

    @staticmethod
    def _remove_overwrite_entry(path: Path, kept: list[str] | None = None) -> int:
        """Removes one Overwrite entry and returns how many were removed. With
        `kept`, an entry that can't be removed (a file still open elsewhere)
        is listed there and the others still go."""
        entry_stat = os.lstat(path)
        reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
        is_reparse_point = bool(
            getattr(entry_stat, "st_file_attributes", 0) & reparse_flag
        )

        # Never follow symlinks, junctions, or other Windows reparse points
        # outside MO2's Overwrite directory.
        if is_reparse_point:
            if stat.S_ISDIR(entry_stat.st_mode):
                os.rmdir(path)
            else:
                os.unlink(path)
            return 1

        if stat.S_ISDIR(entry_stat.st_mode):
            removed = 0
            kept_before = len(kept) if kept is not None else 0
            with os.scandir(path) as entries:
                for entry in entries:
                    try:
                        removed += _game_class()._remove_overwrite_entry(
                            Path(entry.path), kept
                        )
                    except OSError as error:
                        if kept is None:
                            raise
                        kept.append(f"{entry.path}: {error}")
            if kept is not None and len(kept) > kept_before:
                # the folder still holds what could not be removed
                return removed
            os.rmdir(path)
            return removed + 1

        os.unlink(path)
        return 1

    def _clear_overwrite_contents(self, overwrite_path: Path) -> None:
        try:
            if overwrite_path.name.casefold() != "overwrite":
                raise ValueError(_tr("MO2 returned a path that is not named Overwrite"))

            root_stat = os.lstat(overwrite_path)
            reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
            if getattr(root_stat, "st_file_attributes", 0) & reparse_flag:
                raise ValueError(_tr("the Overwrite directory is a reparse point"))
            if not stat.S_ISDIR(root_stat.st_mode):
                raise ValueError(_tr("the Overwrite path is not a directory"))

            removed = 0
            kept: list[str] = []
            with os.scandir(overwrite_path) as entries:
                for entry in entries:
                    try:
                        removed += self._remove_overwrite_entry(Path(entry.path), kept)
                    except OSError as error:
                        kept.append(f"{entry.path}: {error}")
            qInfo(
                "Elden Ring MO2: cleared "
                f"{removed} file(s) and folder(s) from Overwrite after the game closed."
            )
            if kept:
                # one locked file must not keep everything else in Overwrite
                qCritical(
                    f"Elden Ring MO2: kept {len(kept)} Overwrite entr(y/ies) that "
                    "could not be removed: " + "; ".join(kept[:10])
                )
        except FileNotFoundError:
            qInfo("Elden Ring MO2: Overwrite was already empty after the game closed.")
        except (OSError, ValueError) as error:
            qCritical(
                f"Elden Ring MO2: could not fully clear Overwrite after the game "
                f"closed: {error}"
            )
