"""MO2 Revamped: what the parts of the Elden Ring and Nightreign game plugin
share: the log functions, translation, the process list and a few small
widgets.
"""

from __future__ import annotations

import ctypes
from ctypes import wintypes
import os
from pathlib import Path


from PyQt6.QtCore import (
    QEvent,
    QObject,
    Qt,
    qCritical,
    qInfo,
    qWarning,
)
from PyQt6.QtWidgets import (
    QAbstractButton,
    QToolButton,
)

from PyQt6.QtCore import QCoreApplication


# PyQt passes Qt's log functions ASCII only: a message with any other
# character (a path with an accent, a Windows error in Spanish) raised
# UnicodeEncodeError and stopped the code that logged it. Such messages go
# through Python's logging, which MO2's Python plugin writes to the same log.
import logging  # noqa: E402
import sys  # noqa: E402

_QT_LOG_CALLS: dict = {}


# the game this MO2 manages, for the log; set once its interface is up
_log_game_name = "Elden Ring"


def set_log_game_name(name: str) -> None:
    global _log_game_name
    _log_game_name = name


def _safe_qt_log(qt_function, level):
    def log(message) -> None:
        text = "" if message is None else str(message)
        text = text.replace("Elden Ring MO2", f"{_log_game_name} MO2")
        caller = sys._getframe(1)
        filename, line = caller.f_code.co_filename, caller.f_lineno
        if not text.isascii():
            logging.getLogger("mo2revamped").log(
                level, f"[{os.path.basename(filename)}:{line}] {text}"
            )
            return
        # Qt logs the file and line of the code that calls it; a call compiled
        # at the caller's place keeps the log pointing there instead of here
        code = _QT_LOG_CALLS.get((filename, line))
        if code is None:
            code = compile("\n" * (line - 1) + "_qt(_text)", filename, "exec")
            _QT_LOG_CALLS[(filename, line)] = code
        exec(code, {"_qt": qt_function, "_text": text})

    return log


qCritical = _safe_qt_log(qCritical, logging.CRITICAL)
qInfo = _safe_qt_log(qInfo, logging.INFO)
qWarning = _safe_qt_log(qWarning, logging.WARNING)
# for code that shadows the names to collect its lines first
_module_qinfo = qInfo
_module_qwarning = qWarning


def _with_true_case(path) -> str:
    """The path as the disk spells it. Steam's registry value can be all lower
    case, and MO2 shows and stores the game folder as it gets it."""
    original = Path(path)
    if not original.is_absolute():
        return str(path)
    drive = original.drive
    result = Path(original.anchor.upper() if len(drive) == 2 else original.anchor)
    for part in original.parts[1:]:
        try:
            names = os.listdir(result)
            match = (
                part
                if part in names
                else next((name for name in names if name.casefold() == part.casefold()), None)
            )
        except OSError:
            return str(path)
        if match is None:
            return str(path)
        result = result / match
    return str(result)


# MO2 installs translations/revamped_<language>.qm; the source text is English.
_TR_CONTEXT = "EldenRingGame"


def _tr(text: str) -> str:
    return QCoreApplication.translate(_TR_CONTEXT, text)


class _ProcessEntry32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD),
        ("cntUsage", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD),
        ("th32DefaultHeapID", ctypes.c_size_t),
        ("th32ModuleID", wintypes.DWORD),
        ("cntThreads", wintypes.DWORD),
        ("th32ParentProcessID", wintypes.DWORD),
        ("pcPriClassBase", ctypes.c_long),
        ("dwFlags", wintypes.DWORD),
        ("szExeFile", ctypes.c_wchar * 260),
    ]


def _running_process_names() -> set[str] | None:
    """The lowercased executable names of every running process, read from
    Windows directly (a few milliseconds, no console process), or None when
    the list can't be read."""
    processes = _running_processes()
    return None if processes is None else {name for _pid, name in processes}


def _running_processes() -> list[tuple[int, str]] | None:
    """(process id, lowercased executable name) of every running process, or
    None when the list can't be read."""
    try:
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        snapshot_fn = kernel32.CreateToolhelp32Snapshot
        snapshot_fn.argtypes = (wintypes.DWORD, wintypes.DWORD)
        snapshot_fn.restype = wintypes.HANDLE
        first = kernel32.Process32FirstW
        first.argtypes = (wintypes.HANDLE, ctypes.POINTER(_ProcessEntry32W))
        first.restype = wintypes.BOOL
        following = kernel32.Process32NextW
        following.argtypes = (wintypes.HANDLE, ctypes.POINTER(_ProcessEntry32W))
        following.restype = wintypes.BOOL
        close = kernel32.CloseHandle
        close.argtypes = (wintypes.HANDLE,)
        snapshot = snapshot_fn(0x00000002, 0)  # TH32CS_SNAPPROCESS
        if not snapshot or snapshot == wintypes.HANDLE(-1).value:
            return None
        try:
            entry = _ProcessEntry32W()
            entry.dwSize = ctypes.sizeof(_ProcessEntry32W)
            processes: list[tuple[int, str]] = []
            ok = first(snapshot, ctypes.byref(entry))
            while ok:
                processes.append((entry.th32ProcessID, entry.szExeFile.casefold()))
                ok = following(snapshot, ctypes.byref(entry))
            # the walk ends with ERROR_NO_MORE_FILES; anything else cut it short
            if ctypes.get_last_error() != 18:
                return None
            return processes or None
        finally:
            close(snapshot)
    except (OSError, AttributeError, ValueError):
        return None


def _process_image_path(pid: int) -> str | None:
    """The full path of a running process's executable, or None when Windows
    doesn't give it."""
    try:
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        open_process = kernel32.OpenProcess
        open_process.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
        open_process.restype = wintypes.HANDLE
        query = kernel32.QueryFullProcessImageNameW
        query.argtypes = (
            wintypes.HANDLE,
            wintypes.DWORD,
            wintypes.LPWSTR,
            ctypes.POINTER(wintypes.DWORD),
        )
        query.restype = wintypes.BOOL
        close = kernel32.CloseHandle
        close.argtypes = (wintypes.HANDLE,)
        handle = open_process(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
        if not handle:
            return None
        try:
            size = wintypes.DWORD(32768)
            buffer = ctypes.create_unicode_buffer(size.value)
            if not query(handle, 0, buffer, ctypes.byref(size)):
                return None
            return buffer.value or None
        finally:
            close(handle)
    except (OSError, AttributeError, ValueError):
        return None


class _RunButtonEnabledMirror(QObject):
    """Mirror MO2's launch-button enabled state onto the visible split button."""

    def __init__(self, source: QAbstractButton, replacement: QToolButton) -> None:
        super().__init__(source)
        self._source = source
        self._replacement = replacement

    def eventFilter(self, watched, event) -> bool:
        if watched == self._source and event.type() == QEvent.Type.EnabledChange:
            self._replacement.setEnabled(self._source.isEnabled())
        return False


class _ProgramListLock(QObject):
    """Keeps a combo box on its program while it still looks and reads as usual;
    disabled, it would lose its colors and icon."""

    Blocked = (
        QEvent.Type.MouseButtonPress,
        QEvent.Type.MouseButtonDblClick,
        QEvent.Type.Wheel,
        QEvent.Type.KeyPress,
    )

    def __init__(self, parent) -> None:
        super().__init__(parent)
        self.locked = False

    def eventFilter(self, _watched, event) -> bool:
        if not self.locked or event.type() not in self.Blocked:
            return False
        # the keyboard can still leave the list
        return not (
            event.type() == QEvent.Type.KeyPress
            and event.key() in (Qt.Key.Key_Tab, Qt.Key.Key_Backtab)
        )


class _MainWindowCloseWatcher(QObject):
    """Calls back when MO2's main window closes (MO2 quits or switches
    instance), before the plugin's timers stop."""

    def __init__(self, window, on_close) -> None:
        super().__init__(window)
        self._on_close = on_close

    def eventFilter(self, watched, event) -> bool:
        # the window hides once MO2 has accepted the close: a Close event
        # comes first, also when the user then cancels MO2's "still running"
        # question. Minimizing hides it too, but spontaneously.
        if event.type() == QEvent.Type.Hide and not event.spontaneous():
            self._on_close()
        return False



# the folder of game_eldenring_mo2.py, where the bridge and the me3
# compatibility DLL are installed beside it
GAMES_DIR = Path(__file__).resolve().parent.parent

__all__ = ['_QT_LOG_CALLS', '_log_game_name', 'set_log_game_name', '_safe_qt_log', 'qCritical', 'qInfo', 'qWarning', '_module_qinfo', '_module_qwarning', '_with_true_case', '_TR_CONTEXT', '_tr', '_ProcessEntry32W', '_running_process_names', '_running_processes', '_process_image_path', '_RunButtonEnabledMirror', '_ProgramListLock', '_MainWindowCloseWatcher', 'GAMES_DIR']
