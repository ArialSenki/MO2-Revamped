"""Logging, translation and small helpers for the Baldur's Gate 3 plugin.

Only Qt's core module is imported here, so the game plugin loads fast in
instances of other games.
"""

from __future__ import annotations

import os
from pathlib import Path

from PyQt6.QtCore import QCoreApplication, qCritical, qDebug, qInfo, qWarning

# logging's levels: logging itself is imported only for a message that needs it
_DEBUG, _INFO, _WARNING, _CRITICAL = 10, 20, 30, 50

PLUGIN_NAME = "Baldur's Gate 3 Plugin"
GAME_NAME = "Baldur's Gate 3"

# MO2 installs translations/revamped_<language>.qm; the source text is English
TR_CONTEXT = "BaldursGate3Game"


def _safe(qt_function, level):
    # PyQt hands Qt's log functions ASCII only: a path with an accent (a user
    # named José, a mod called "Ánimo") raised UnicodeEncodeError and stopped
    # the code that logged it. Such messages go through Python's logging,
    # which MO2's Python plugin writes to the same log.
    def log(message) -> None:
        text = "" if message is None else str(message)
        if text.isascii():
            qt_function(text)
        else:
            import logging

            logging.getLogger("mo2revamped").log(level, text)

    return log


log_debug = _safe(qDebug, _DEBUG)
log_info = _safe(qInfo, _INFO)
log_warning = _safe(qWarning, _WARNING)
log_critical = _safe(qCritical, _CRITICAL)


def debug_enabled() -> bool:
    from PyQt6.QtCore import QLoggingCategory

    cat = QLoggingCategory.defaultCategory()
    return cat is not None and cat.isDebugEnabled()


def tr(text: str) -> str:
    return QCoreApplication.translate(TR_CONTEXT, text)


def root_builder_enabled(organizer) -> bool:
    """Whether Root Builder deploys the mods' Root/ folders itself; without
    it, MO2 Revamped maps them to the game folder. Its plugin name is
    RootBuilder; "Root Builder" is its display name. Kept here, not taken from
    basic_features, so an older basic_games doesn't stop this plugin loading."""
    try:
        return any(
            organizer.isPluginEnabled(name) for name in ("RootBuilder", "Root Builder")
        )
    except Exception:  # noqa: BLE001
        return False


def subfolders(path: Path) -> list[Path]:
    try:
        with os.scandir(path) as it:
            return [Path(e.path) for e in it if e.is_dir()]
    except OSError:
        return []


def write_atomically(path: Path, data: bytes) -> None:
    """The whole file under another name first, then swapped in: a crash or
    a full disk never leaves half a file behind."""
    path.parent.mkdir(parents=True, exist_ok=True)
    part = path.with_name(path.name + ".part")
    try:
        with open(part, "wb") as out:
            out.write(data)
            out.flush()
            os.fsync(out.fileno())
        os.replace(part, path)
    finally:
        try:
            part.unlink()
        except OSError:
            pass


# The game's own folders inside Data/ that mods override with loose files.
# A mod has them at its top level (a Wabbajack list made with the unofficial
# BG3 plugin) or under Data/.
LOOSE_FILE_FOLDERS = {"Public", "Mods", "Generated", "Localization", "ScriptExtender"}
TOP_LEVEL_DATA_FOLDERS = LOOSE_FILE_FOLDERS | {"Video"}

# The unofficial BG3 plugin's folders that Wabbajack lists keep:
# SE_CONFIG/<mod>/... goes to <documents>/Script Extender/<mod>/... and
# PAK_FILES/*.pak to <documents>/Mods
SE_CONFIG_FOLDER = "SE_CONFIG"
PAK_FILES_FOLDER = "PAK_FILES"

# The programs that start the game with the mods' load order; the Larian
# Launcher starts bin/bg3*.exe itself
GAME_BINARIES = {"bg3.exe", "bg3_dx11.exe", "larilauncher.exe"}


def runs_the_game(exec_path: str) -> bool:
    return Path(exec_path).name.casefold() in GAME_BINARIES
