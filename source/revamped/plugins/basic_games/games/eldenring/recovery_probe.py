"""Whether Steam saves an earlier launch hid may still be waiting to come back.

The Elden Ring plugin puts such saves back from init(), in every instance
(an instance switch must not leave them hidden). In another game's instance
basic_games gives MO2 a stub of the plugin (lazy_games.py), whose init() does
nothing: the stub asks this first, and only when it says yes loads the real
plugin to run its init(). It only looks at folders, so it costs little.
"""

from __future__ import annotations

import os
from pathlib import Path

# EldenRingMo2Game.SaveRecoveryDirectoryName and the games' SaveFolderName
RECOVERY_FOLDER = ".MO2RevampedSaveRecovery"
SAVE_FOLDERS = ("EldenRing", "Nightreign")


def recovery_roots(appdata: str | None = None) -> list[Path]:
    """Where EldenRingMo2Game._save_quarantine_parent() puts the recovery
    folder: beside the game's save folder, or beside its target when that
    folder links to another drive."""
    appdata = appdata or os.environ.get("APPDATA") or str(Path.home() / "AppData" / "Roaming")
    roots = []
    for name in SAVE_FOLDERS:
        saves = Path(appdata) / name
        parent = saves.parent
        try:
            target = saves.resolve()
            if os.path.splitdrive(str(target))[0].casefold() != os.path.splitdrive(
                str(parent.resolve())
            )[0].casefold():
                parent = target.parent
        except (OSError, RuntimeError):
            pass
        root = parent / RECOVERY_FOLDER
        if root not in roots:
            roots.append(root)
    return roots


def pending(appdata: str | None = None) -> bool:
    """True when a recovery folder holds any session (of any install)."""
    for root in recovery_roots(appdata):
        try:
            for install in root.iterdir():
                if install.is_dir() and any(child.is_dir() for child in install.iterdir()):
                    return True
        except OSError:
            continue
    return False
