"""Whether Steam saves an earlier launch hid may still be waiting to come back.

The Elden Ring plugin puts such saves back from init(), in every instance
(an instance switch must not leave them hidden). In another game's instance
basic_games gives MO2 a stub of the plugin (lazy_games.py), whose init() does
nothing: the stub asks this first, and only when it says yes loads the real
plugin to run its init(). It only looks at folders, so it costs little.
"""

from __future__ import annotations

import json
import os
from pathlib import Path

# EldenRingMo2Game.SaveRecoveryDirectoryName and the games' SaveFolderName
RECOVERY_FOLDER = ".MO2RevampedSaveRecovery"
SAVE_FOLDERS = ("EldenRing", "Nightreign")
# EldenRingMo2Game.UnroutedSaveDirectoryName and SaveRecoveryManifestName
UNROUTED_FOLDER = "unrouted-global"
MANIFEST = "session.json"


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


def session_is_pending(session: Path) -> bool:
    """SaveRecoveryMixin._session_is_pending: a session that only keeps the
    game's output from outside the profile route has nothing to put back."""
    children = [child for child in session.iterdir() if child.is_dir()]
    if not children or any(child.name != UNROUTED_FOLDER for child in children):
        return True
    try:
        swap = json.loads((session / MANIFEST).read_text(encoding="utf-8")).get("swap")
    except (OSError, UnicodeError, ValueError, AttributeError):
        return False
    return isinstance(swap, dict) and not swap.get("returned")


def pending(appdata: str | None = None) -> bool:
    """True when a recovery folder holds a session (of any install) with
    saves still to put back."""
    for root in recovery_roots(appdata):
        try:
            for install in root.iterdir():
                if not install.is_dir():
                    continue
                for session in install.iterdir():
                    if session.is_dir() and session_is_pending(session):
                        return True
        except OSError:
            continue
    return False
