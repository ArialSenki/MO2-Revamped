"""Logging and Windows folders for the Cyberpunk 2077 plugin."""

from __future__ import annotations

import logging
import os

from PyQt6.QtCore import QCoreApplication, qCritical, qInfo, qWarning

_logger = logging.getLogger("mo2revamped")


def _safe(qt_function, level):
    # PyQt hands Qt's log functions ASCII only: a path with an accent (a user
    # named José) raised UnicodeEncodeError and stopped the code that logged
    # it. Such messages go through Python's logging, which MO2's Python plugin
    # writes to the same log.
    def log(message) -> None:
        text = "" if message is None else str(message)
        if text.isascii():
            qt_function(text)
        else:
            _logger.log(level, text)

    return log


log_info = _safe(qInfo, logging.INFO)
log_warning = _safe(qWarning, logging.WARNING)
log_critical = _safe(qCritical, logging.CRITICAL)

# MO2 installs translations/revamped_<language>.qm; the source text is English
TR_CONTEXT = "Cyberpunk2077Game"


def tr(text: str) -> str:
    return QCoreApplication.translate(TR_CONTEXT, text)


def root_builder_enabled(organizer) -> bool:
    """Whether Root Builder deploys the mods' root/ folders itself; without
    it, MO2 Revamped maps them to the game folder. Its plugin name is
    RootBuilder; "Root Builder" is its display name. Kept here, not taken from
    basic_features, so an older basic_games doesn't stop this plugin loading."""
    try:
        return any(organizer.isPluginEnabled(name) for name in ("RootBuilder", "Root Builder"))
    except Exception:  # noqa: BLE001
        return False


def known_folder(guid: str, fallback: str) -> str:
    """A Windows known folder, also when the user moved it (Saved Games on
    another drive)."""
    try:
        import ctypes
        import uuid
        from ctypes import wintypes

        folder_id = uuid.UUID(guid)
        raw = (ctypes.c_byte * 16).from_buffer_copy(folder_id.bytes_le)
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


def saved_games_folder() -> str:
    return known_folder("{4C5C32FF-BB9D-43B0-B5B4-2D72E54EAAA4}", "%USERPROFILE%/Saved Games")


def local_app_data_folder() -> str:
    return known_folder("{F1B32785-6FBA-4FCF-9D55-7B8E7F157091}", "%LOCALAPPDATA%")
