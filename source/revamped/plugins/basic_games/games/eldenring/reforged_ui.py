"""MO2 Revamped: ELDEN RING Reforged's UI images (Gideon).

Reforged ships its tutorial pictures and item illustrations as loose textures
(``mod/menu/deploy/projects/ELDENRINGReforged/solo``) and a tool, Gideon, that
merges them into a copy of the game's own ``menu/hi/00_Solo.tpfbhd/.tpfbdt``
(about 2 GB). Reforged's launcher runs it; MO2 runs it itself:

- Gideon finds the game through Steam (its registry key and
  libraryfolders.vdf), reads ``Data0.bhd/.bdt`` there and never writes in the
  game folder;
- it writes ``menu/hi/00_Solo.*`` two folders above its own (Gideon's "mod
  folder"), taking the copy already there as its base when there is one;
- ``--user --project ELDENRINGReforged`` is what Reforged's shortcut runs; it
  needs a console, so MO2 gives it a hidden pseudoconsole and reads its text.

The installer keeps the tool in ``_ReforgedTools/menu/deploy`` (or in the
release kept for Reforged's online mode), which the game never reads. The
archives Gideon writes there are moved to the mod's ``menu/hi``, where MO2's
VFS and the bridge's loose-file hook give them to the game.
"""

from __future__ import annotations

import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import re
import shutil
import subprocess
import threading
import time

from PyQt6.QtCore import QEventLoop, Qt, QTimer
from PyQt6.QtWidgets import QMessageBox, QProgressDialog

from .common import qInfo, qWarning, _tr

Project = "ELDENRINGReforged"
ToolsDirectory = "_ReforgedTools"
OnlineDirectory = "_ReforgedOnline"
SoloFiles = ("00_Solo.tpfbhd", "00_Solo.tpfbdt")
# what MO2 writes beside Gideon's project once the images are made: the
# project's own last-update.txt then, to tell an update that changed them
StampFile = "mo2-ui-images.txt"
# the archives are ~2.2 GB; Gideon writes the game's copy first, then its own
NeededSpace = 4 * 1024**3
# a 00_Solo.tpfbdt smaller than this is not a finished one
_MinimumArchive = 64 * 1024**2


def tool_folders(mod_root: Path) -> list[Path]:
    """Where an installed Reforged keeps Gideon (menu/deploy), in order."""
    return [
        mod_root / ToolsDirectory / "menu" / "deploy",
        mod_root / OnlineDirectory / "mod" / "menu" / "deploy",
        # a release installed as it came (Keep original layout)
        mod_root / "mod" / "menu" / "deploy",
    ]


def find_gideon(mod_root: Path) -> Path | None:
    """Gideon's folder in this mod, with Reforged's project; None without."""
    for folder in tool_folders(mod_root):
        try:
            if (folder / "Gideon.exe").is_file() and (
                folder / "projects" / Project / "project.ini"
            ).is_file():
                return folder
        except OSError:
            continue
    return None


def is_reforged_mod(mod_root: Path) -> bool:
    try:
        return (mod_root / "DLLs" / "reforged.dll").is_file() or find_gideon(mod_root) is not None
    except OSError:
        return False


def images_folder(mod_root: Path, tool: Path | None = None) -> Path:
    """Where the game reads the images from in this mod."""
    if tool is not None and tool.parent.parent.name.casefold() == "mod" and (
        tool.parent.parent.parent == mod_root
    ):
        # kept as it came: its game files are under mod/ too
        return mod_root / "mod" / "menu" / "hi"
    return mod_root / "menu" / "hi"


def _archive_ready(folder: Path) -> bool:
    try:
        return (folder / SoloFiles[0]).is_file() and (
            folder / SoloFiles[1]
        ).stat().st_size >= _MinimumArchive
    except OSError:
        return False


def _project_version(tool: Path) -> str:
    try:
        return (tool / "projects" / Project / "last-update.txt").read_text(
            encoding="utf-8", errors="replace"
        ).strip()
    except OSError:
        return ""


def _stamp(tool: Path) -> str | None:
    try:
        return (tool / "projects" / Project / StampFile).read_text(
            encoding="utf-8", errors="replace"
        ).strip()
    except OSError:
        return None


def status(mod_root: Path) -> str:
    """"ready", "missing" (Gideon can make them), "outdated" (made for an
    older Reforged), or "no-tool" (missing and no Gideon to make them)."""
    tool = find_gideon(mod_root)
    if not _archive_ready(images_folder(mod_root, tool)):
        return "missing" if tool is not None else "no-tool"
    if tool is not None:
        stamp = _stamp(tool)
        # images MO2 didn't make (copied by hand) are taken as they are
        if stamp is not None and stamp != _project_version(tool):
            return "outdated"
    return "ready"


def up_to_date(mod_root: Path) -> bool:
    """Whether MO2 made this mod's images with its current Gideon project."""
    tool = find_gideon(mod_root)
    return (
        tool is not None
        and _archive_ready(images_folder(mod_root, tool))
        and _stamp(tool) == _project_version(tool)
    )


def other_providers(
    mods: list[tuple[str, Path, bool]], reforged_name: str
) -> tuple[list[str], str | None]:
    """The other active mods bringing menu/hi/00_Solo, and which active mod
    the game gets it from (the highest priority; mods come lowest first)."""
    providers = []
    for name, root, active in mods:
        if not active:
            continue
        folder = root / "menu" / "hi"
        try:
            if any((folder / file).is_file() for file in SoloFiles):
                providers.append(name)
        except OSError:
            continue
    others = [name for name in providers if name != reforged_name]
    return others, (providers[-1] if providers else None)


def active_reforged(mods: list[tuple[str, Path, bool]]) -> tuple[str, Path] | None:
    """The highest-priority active Reforged mod."""
    for name, root, active in reversed(mods):
        if active and is_reforged_mod(root):
            return name, root
    return None


# --------------------------------------------------------------- running it

_k32 = ctypes.WinDLL("kernel32", use_last_error=True) if os.name == "nt" else None


class _Coord(ctypes.Structure):
    _fields_ = [("X", wintypes.SHORT), ("Y", wintypes.SHORT)]


class _StartupInfo(ctypes.Structure):
    _fields_ = [
        ("cb", wintypes.DWORD), ("lpReserved", wintypes.LPWSTR),
        ("lpDesktop", wintypes.LPWSTR), ("lpTitle", wintypes.LPWSTR),
        ("dwX", wintypes.DWORD), ("dwY", wintypes.DWORD),
        ("dwXSize", wintypes.DWORD), ("dwYSize", wintypes.DWORD),
        ("dwXCountChars", wintypes.DWORD), ("dwYCountChars", wintypes.DWORD),
        ("dwFillAttribute", wintypes.DWORD), ("dwFlags", wintypes.DWORD),
        ("wShowWindow", wintypes.WORD), ("cbReserved2", wintypes.WORD),
        ("lpReserved2", ctypes.c_void_p), ("hStdInput", wintypes.HANDLE),
        ("hStdOutput", wintypes.HANDLE), ("hStdError", wintypes.HANDLE),
    ]


class _StartupInfoEx(ctypes.Structure):
    _fields_ = [("StartupInfo", _StartupInfo), ("lpAttributeList", ctypes.c_void_p)]


class _ProcessInformation(ctypes.Structure):
    _fields_ = [
        ("hProcess", wintypes.HANDLE), ("hThread", wintypes.HANDLE),
        ("dwProcessId", wintypes.DWORD), ("dwThreadId", wintypes.DWORD),
    ]


_ANSI = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]|\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)|\x1b[@-_]")
# Gideon's texts that end a run: the last one, and those of a failure or of
# a question MO2 can't answer for the user (a folder to pick, a name)
_DONE = "process complete"
_FAILED = (
    "something went wrong", "unhandled exception", "could not locate your game folder",
    "game folder could not be located", "could not find", "could not open",
    "please select", "please enter", "please provide", "malformed", "error while",
)


class GideonRun:
    """One run of Gideon, in a pseudoconsole MO2 reads (a hidden console
    where Windows has none). poll() gives what it has said so far."""

    def __init__(self, tool: Path):
        self.tool = tool
        self.text = ""
        self.finished = False
        self.exit_code: int | None = None
        self._lock = threading.Lock()
        self._process = None
        self._input = None
        self._console = None
        self._handles: list = []
        self._last_output = time.monotonic()

    def command(self, silent: bool = False) -> list[str]:
        # --silent: no text and no key to press at the end (nor any failure)
        return [
            str(self.tool / "Gideon.exe"), "--user", *(("--silent",) if silent else ()),
            "--project", Project,
        ]

    def start(self) -> None:
        if _k32 is not None and hasattr(_k32, "CreatePseudoConsole"):
            try:
                self._start_pseudoconsole()
                return
            except OSError as error:
                qWarning(f"Elden Ring MO2: no pseudoconsole for Gideon ({error}); using a hidden console.")
        # its console hidden; MO2 sees only how it ended and the files
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        self._process = subprocess.Popen(
            self.command(silent=True), cwd=str(self.tool), startupinfo=startup,
            creationflags=subprocess.CREATE_NEW_CONSOLE, stdin=subprocess.DEVNULL,
        )

    def _start_pseudoconsole(self) -> None:
        k32 = _k32
        k32.CreatePseudoConsole.argtypes = [
            _Coord, wintypes.HANDLE, wintypes.HANDLE, wintypes.DWORD,
            ctypes.POINTER(ctypes.c_void_p),
        ]
        k32.CreatePseudoConsole.restype = ctypes.c_long
        k32.ClosePseudoConsole.argtypes = [ctypes.c_void_p]
        k32.CreatePipe.argtypes = [
            ctypes.POINTER(wintypes.HANDLE), ctypes.POINTER(wintypes.HANDLE),
            ctypes.c_void_p, wintypes.DWORD,
        ]
        k32.InitializeProcThreadAttributeList.argtypes = [
            ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, ctypes.POINTER(ctypes.c_size_t),
        ]
        k32.UpdateProcThreadAttribute.argtypes = [
            ctypes.c_void_p, wintypes.DWORD, ctypes.c_size_t, ctypes.c_void_p,
            ctypes.c_size_t, ctypes.c_void_p, ctypes.c_void_p,
        ]
        k32.DeleteProcThreadAttributeList.argtypes = [ctypes.c_void_p]
        k32.CreateProcessW.argtypes = [
            wintypes.LPCWSTR, wintypes.LPWSTR, ctypes.c_void_p, ctypes.c_void_p,
            wintypes.BOOL, wintypes.DWORD, ctypes.c_void_p, wintypes.LPCWSTR,
            ctypes.c_void_p, ctypes.c_void_p,
        ]
        k32.ReadFile.argtypes = [
            wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
            ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p,
        ]
        k32.WriteFile.argtypes = [
            wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
            ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p,
        ]
        k32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        k32.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
        k32.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
        k32.CloseHandle.argtypes = [wintypes.HANDLE]

        in_read, in_write = wintypes.HANDLE(), wintypes.HANDLE()
        out_read, out_write = wintypes.HANDLE(), wintypes.HANDLE()
        if not k32.CreatePipe(ctypes.byref(in_read), ctypes.byref(in_write), None, 0):
            raise ctypes.WinError(ctypes.get_last_error())
        if not k32.CreatePipe(ctypes.byref(out_read), ctypes.byref(out_write), None, 0):
            k32.CloseHandle(in_read)
            k32.CloseHandle(in_write)
            raise ctypes.WinError(ctypes.get_last_error())
        console = ctypes.c_void_p()
        result = k32.CreatePseudoConsole(_Coord(160, 50), in_read, out_write, 0, ctypes.byref(console))
        # the pseudoconsole holds its own ends
        k32.CloseHandle(in_read)
        k32.CloseHandle(out_write)
        if result != 0:
            k32.CloseHandle(in_write)
            k32.CloseHandle(out_read)
            raise OSError(f"CreatePseudoConsole failed: {result:#x}")
        size = ctypes.c_size_t()
        k32.InitializeProcThreadAttributeList(None, 1, 0, ctypes.byref(size))
        attributes = ctypes.create_string_buffer(size.value)
        info = _StartupInfoEx()
        info.StartupInfo.cb = ctypes.sizeof(_StartupInfoEx)
        # no standard handles of MO2's: the pseudoconsole's are Gideon's
        info.StartupInfo.dwFlags = 0x100
        process = _ProcessInformation()
        created = False
        try:
            if not k32.InitializeProcThreadAttributeList(attributes, 1, 0, ctypes.byref(size)):
                raise ctypes.WinError(ctypes.get_last_error())
            # PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
            if not k32.UpdateProcThreadAttribute(
                attributes, 0, 0x00020016, console, ctypes.sizeof(ctypes.c_void_p), None, None
            ):
                raise ctypes.WinError(ctypes.get_last_error())
            info.lpAttributeList = ctypes.cast(attributes, ctypes.c_void_p)
            command = ctypes.create_unicode_buffer(subprocess.list2cmdline(self.command()))
            # EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT
            created = bool(k32.CreateProcessW(
                None, command, None, None, False, 0x00080000 | 0x00000400, None,
                str(self.tool), ctypes.byref(info), ctypes.byref(process),
            ))
            if not created:
                raise ctypes.WinError(ctypes.get_last_error())
        except OSError:
            k32.ClosePseudoConsole(console)
            k32.CloseHandle(in_write)
            k32.CloseHandle(out_read)
            raise
        finally:
            k32.DeleteProcThreadAttributeList(attributes)
        k32.CloseHandle(process.hThread)
        self._process = process.hProcess
        self._console = console
        self._input = in_write
        self._handles = [in_write, out_read, process.hProcess]
        threading.Thread(target=self._read, args=(out_read,), daemon=True).start()
        threading.Thread(target=self._wait, daemon=True).start()

    def _read(self, handle) -> None:
        buffer = ctypes.create_string_buffer(16384)
        count = wintypes.DWORD()
        pending = b""
        while _k32.ReadFile(handle, buffer, len(buffer), ctypes.byref(count), None) and count.value:
            pending += buffer.raw[: count.value]
            # a character split between two reads waits for its other bytes
            try:
                chunk, pending = pending.decode("utf-8"), b""
            except UnicodeDecodeError as error:
                chunk, pending = pending[: error.start].decode("utf-8", "replace"), pending[error.start:]
                if len(pending) > 8:
                    chunk, pending = chunk + pending.decode("utf-8", "replace"), b""
            with self._lock:
                self.text += _ANSI.sub("", chunk).replace("\r", "")
                self._last_output = time.monotonic()

    def _wait(self) -> None:
        _k32.WaitForSingleObject(self._process, 0xFFFFFFFF)
        code = wintypes.DWORD()
        _k32.GetExitCodeProcess(self._process, ctypes.byref(code))
        # its last text is read before the pseudoconsole closes
        time.sleep(0.3)
        _k32.ClosePseudoConsole(self._console)
        with self._lock:
            self.exit_code = code.value
            self.finished = True

    def poll(self) -> tuple[str, float]:
        """What Gideon has said, and for how long it has said nothing."""
        if isinstance(self._process, subprocess.Popen):
            code = self._process.poll()
            if code is not None:
                self.exit_code, self.finished = code, True
        with self._lock:
            return self.text, time.monotonic() - self._last_output

    def answer(self) -> None:
        """Enter, for the key Gideon waits for after its last line."""
        if self._input is not None and not self.finished:
            written = wintypes.DWORD()
            _k32.WriteFile(self._input, b"\r", 1, ctypes.byref(written), None)

    def stop(self) -> None:
        if self.finished or self._process is None:
            return
        try:
            if isinstance(self._process, subprocess.Popen):
                self._process.kill()
            else:
                _k32.TerminateProcess(self._process, 1)
        except OSError:
            pass

    def close(self) -> None:
        if not isinstance(self._process, subprocess.Popen) and self.finished:
            for handle in self._handles:
                _k32.CloseHandle(handle)
            self._handles = []


def failure_text(text: str) -> str | None:
    """Gideon's lines from the first that says it failed, if one does."""
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    for index, line in enumerate(lines):
        lowered = line.casefold()
        if any(word in lowered for word in _FAILED):
            return "\n".join(lines[index : index + 8])
    return None


def stage(text: str) -> str:
    lowered = text.casefold()
    if _DONE in lowered:
        return _tr("Finishing...")
    if "packing the solo archive" in lowered:
        return _tr("Adding Reforged's pictures to the archive...")
    if "unpacking files" in lowered:
        return _tr("Copying the game's UI archive (00_Solo) from your game files...")
    return _tr("Starting Gideon, Reforged's UI image tool...")


def _place(tool: Path, mod_root: Path) -> None:
    """Moves Gideon's archives to where the game reads them in the mod; in
    the release kept for Reforged's launcher they stay as hard links."""
    made = tool.parent.parent / "menu" / "hi"
    target = images_folder(mod_root, tool)
    if made == target:
        return
    target.mkdir(parents=True, exist_ok=True)
    for name in SoloFiles:
        for existing in list(target.iterdir()):
            # another spelling of the same file (00_solo) would stay beside it
            if existing.name.casefold() == name.casefold() and existing.name != name:
                existing.unlink()
        os.replace(made / name, target / name)
        if tool.parent.parent.parent.name == OnlineDirectory:
            try:
                os.link(target / name, made / name)
            except OSError as error:
                qInfo(f"Elden Ring MO2: Reforged's launcher copy of {name} not linked: {error}")


def check_before_run(mod_root: Path, game_directory: Path | None) -> str | None:
    """Why Gideon can't run now, as the text to show; None when it can."""
    tool = find_gideon(mod_root)
    if tool is None:
        return _tr(
            "This copy of ELDEN RING Reforged doesn't have its UI image tool (Gideon): "
            "MO2 versions before 1.1.0-beta.18 left it out when installing. Reinstall "
            "Reforged from its archive (right-click the mod, Reinstall), and MO2 creates "
            "the images after the install."
        )
    # MO2's game folder holds Game\, where the archives are
    if game_directory is None or not any(
        (folder / "Data0.bhd").is_file() for folder in (game_directory, game_directory / "Game")
    ):
        return _tr(
            "MO2 couldn't find the game folder with Data0.bhd, where Gideon reads the "
            "game's UI images. Check the game's folder in MO2's instance settings, and "
            "that Steam has the game installed."
        )
    try:
        free = shutil.disk_usage(tool).free
    except OSError:
        free = NeededSpace
    if free < NeededSpace:
        return _tr(
            "There isn't enough free space on the drive of your mods: the UI images "
            "need about {0} GB, and {1} GB are free. Free some space and try again."
        ).format(NeededSpace // 1024**3, f"{free / 1024**3:.1f}")
    return None


def create_images(
    parent, mod_name: str, mod_root: Path, game_directory: Path | None, organizer=None
) -> bool:
    """Runs Gideon for this Reforged mod with a progress window. True when
    the images are in place; any failure has been shown to the user. With
    the organizer, MO2 then reads the mod's files again, for its VFS."""
    title = _tr("Reforged UI images")
    problem = check_before_run(mod_root, game_directory)
    if problem is not None:
        QMessageBox.warning(parent, title, problem)
        return False
    tool = find_gideon(mod_root)
    made = tool.parent.parent / "menu" / "hi"
    # Gideon builds on the archive it finds there: the game's own, fresh
    for name in SoloFiles:
        try:
            (made / name).unlink()
        except FileNotFoundError:
            pass
        except OSError as error:
            QMessageBox.warning(
                parent, title,
                _tr("MO2 couldn't remove the old {0}: {1}").format(made / name, error),
            )
            return False

    progress = QProgressDialog(parent)
    progress.setWindowTitle(title)
    progress.setLabelText(stage(""))
    progress.setCancelButtonText(_tr("Cancel"))
    progress.setRange(0, 0)
    progress.setMinimumDuration(0)
    progress.setMinimumWidth(460)
    progress.setWindowModality(Qt.WindowModality.ApplicationModal)
    progress.setAutoClose(False)
    progress.setAutoReset(False)

    run = GideonRun(tool)
    qInfo(f"Elden Ring MO2: running Gideon for {mod_name}: {subprocess.list2cmdline(run.command())}")
    try:
        run.start()
    except OSError as error:
        progress.deleteLater()
        QMessageBox.warning(parent, title, _tr("Gideon couldn't start: {0}").format(error))
        return False
    progress.show()

    loop = QEventLoop()
    state = {"canceled": False, "answered": 0.0, "stopped": False}
    started = time.monotonic()

    def tick() -> None:
        text, quiet = run.poll()
        if run.finished:
            loop.quit()
            return
        lowered = text.casefold()
        if state["canceled"] or progress.wasCanceled():
            state["canceled"] = True
            run.stop()
        elif _DONE in lowered:
            # it waits for a key after its last line
            if quiet > 1.0 and time.monotonic() - state["answered"] > 3.0:
                state["answered"] = time.monotonic()
                run.answer()
            if quiet > 15.0:
                run.stop()
        elif failure_text(text) is not None and quiet > 3.0:
            # a failure, or a question MO2 can't answer for the user
            state["stopped"] = True
            run.stop()
        elif quiet > 900.0:
            state["stopped"] = True
            run.stop()
        seconds = int(time.monotonic() - started)
        progress.setLabelText(
            stage(text) + "\n\n" + _tr("{0} s - Reforged's tutorials and item pictures, about 2 GB.").format(seconds)
        )

    timer = QTimer()
    timer.timeout.connect(tick)
    timer.start(200)
    loop.exec()
    timer.stop()
    progress.close()
    progress.deleteLater()
    text, _quiet = run.poll()
    run.close()
    lowered = text.casefold()
    succeeded = _DONE in lowered or (
        run.exit_code == 0 and not text.strip() and _archive_ready(made)
    )
    log = " | ".join(line.strip() for line in text.splitlines() if line.strip())
    qInfo(f"Elden Ring MO2: Gideon ended with {run.exit_code}: {log[-1500:]}")
    if state["canceled"]:
        _remove_partial(made)
        return False
    if not succeeded or not _archive_ready(made):
        _remove_partial(made)
        details = failure_text(text) or "\n".join(text.strip().splitlines()[-8:])
        box = QMessageBox(parent)
        box.setIcon(QMessageBox.Icon.Warning)
        box.setWindowTitle(title)
        box.setText(_tr("Gideon couldn't create Reforged's UI images"))
        informative = _tr(
            "Gideon finds the game through Steam: if Steam doesn't have ELDEN RING "
            "installed, run the game from Steam once or verify its files, then try again "
            "from Tools > Reforged UI images..."
        )
        if details:
            informative += "\n\n" + _tr("Gideon said:") + "\n" + details
        box.setInformativeText(informative)
        box.exec()
        box.deleteLater()
        return False
    try:
        _place(tool, mod_root)
        (tool / "projects" / Project / StampFile).write_text(_project_version(tool), encoding="utf-8")
    except OSError as error:
        QMessageBox.warning(
            parent, title, _tr("MO2 couldn't put the UI images in the mod: {0}").format(error)
        )
        return False
    qInfo(f"Elden Ring MO2: Reforged's UI images are ready in {images_folder(mod_root, tool)}.")
    if organizer is not None:
        try:
            organizer.refresh(True)
        except (AttributeError, RuntimeError, TypeError) as error:
            qWarning(f"Elden Ring MO2: could not refresh after the UI images: {error}")
    return True


def _remove_partial(folder: Path) -> None:
    for name in SoloFiles:
        try:
            (folder / name).unlink()
        except OSError:
            pass


def conflict_text(others: list[str], winner: str | None, reforged_name: str) -> str:
    if winner == reforged_name:
        return _tr(
            "{0} also bring their own menu\\hi\\00_Solo (UI images: loading screens, "
            "item pictures). The game reads only one, Reforged's, which has the highest "
            "priority, so the pictures those mods add won't show."
        ).format(", ".join(others))
    return _tr(
        "{0} has a higher priority than {1} and brings its own menu\\hi\\00_Solo (UI "
        "images). The game reads only that one, so Reforged's tutorials show "
        "\"You are missing Reforged UI images\" and its item pictures are missing. Give "
        "{1} the higher priority, or turn off the mods you don't need."
    ).format(winner, reforged_name)
