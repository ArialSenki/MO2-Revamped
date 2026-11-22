"""MO2 Revamped: launches outside MO2's virtual files: their launch methods,
the route's saves swapped into Steam's folder for the run and taken back,
and starting the program.

Part of the Elden Ring and Nightreign game plugin (game_eldenring_mo2.py),
whose EldenRingMo2Game takes it as a mixin.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import uuid


import mobase
from PyQt6.QtCore import (
    QProcess,
)

from .common import (
    qCritical,
    qInfo,
    _tr,
)


class SaveSwapMixin:
    @classmethod
    def launch_methods_path(cls, instance_base: Path) -> Path:
        return instance_base / cls.LaunchMethodsFilename

    @classmethod
    def read_launch_methods(cls, instance_base: Path) -> dict[str, str]:
        """The executables switched by hand in Save Isolation, keyed by their
        normalised binary path."""
        try:
            data = json.loads(cls.launch_methods_path(instance_base).read_text(encoding="utf-8-sig"))
        except (OSError, UnicodeError, ValueError):
            return {}
        methods = data.get("methods") if isinstance(data, dict) else None
        if not isinstance(methods, dict):
            return {}
        return {
            str(key): value
            for key, value in methods.items()
            if value in {cls.LaunchMethodVfs, cls.LaunchMethodSwap}
        }

    @classmethod
    def reforged_online_saves_separate(cls, instance_base: Path) -> bool:
        """Whether Reforged's online play keeps its own save (the default)."""
        try:
            data = json.loads(cls.launch_methods_path(instance_base).read_text(encoding="utf-8-sig"))
        except (OSError, UnicodeError, ValueError):
            return True
        if not isinstance(data, dict):
            return True
        return data.get(cls.ReforgedOnlineSavesKey, "separate") != "shared"

    def _reforged_online_route(self, route: Path) -> Path:
        """The online save folder inside the route. Its first use starts from
        a copy of the route's saves (account folders and loose files), so the
        character carries over once; after that both go their own way."""
        online = route / self.ReforgedOnlineSavesFolder
        if self._route_save_files(online):
            return online
        online.mkdir(parents=True, exist_ok=True)
        copied = 0
        if route.is_dir():
            for child in route.iterdir():
                if child.name in {self.ReforgedOnlineSavesFolder, "save_profiles"}:
                    continue
                if child.is_dir() and self._save_directory_contains_elden_ring_saves(child):
                    shutil.copytree(child, online / child.name, dirs_exist_ok=True)
                    copied += 1
                elif child.is_file():
                    shutil.copy2(child, online / child.name)
        qInfo(
            "Elden Ring MO2: Reforged's online saves start from a copy of the "
            f"route's ({copied} account folder(s)) in '{online}'."
        )
        return online

    @classmethod
    def launch_method_for(cls, binary: str, methods: dict[str, str]) -> tuple[str, bool]:
        """(method, chosen by hand) for one executable."""
        if Path(binary).name.casefold() == cls.ReforgedOnlineLauncher:
            return cls.LaunchMethodSwap, False
        key = os.path.normcase(os.path.abspath(binary)) if binary else ""
        if key in methods:
            return methods[key], True
        automatic = Path(binary).name.casefold() in cls.AutoSwapLaunchers
        return (cls.LaunchMethodSwap if automatic else cls.LaunchMethodVfs), False

    def _launch_method(self, app_path: str) -> str:
        try:
            methods = self.read_launch_methods(Path(self._organizer.basePath()))
        except (AttributeError, RuntimeError, TypeError):
            methods = {}
        return self.launch_method_for(app_path, methods)[0]

    def _swap_route_saves_in(
        self,
        session_directory: Path,
        manifest: dict,
        global_save_root: Path,
        route: Path,
        keep_steam_saves: bool = False,
    ) -> None:
        """Puts the route's saves where the game looks without the VFS: its
        account folders into the (now empty) Steam folder, and its loose files
        (GraphicsConfig.xml) in place of the Steam ones, which wait in the
        session. The manifest records each step first, so recovery after a
        crash can take everything back.

        With keep_steam_saves (a game that plays another file, like Reforged's
        .err) Steam's own .sl2 stays visible instead of the route's: Steam Cloud
        syncs that name when the game closes, whatever started it."""
        swap = {
            "route": str(route),
            "directories": [],
            "steam_root_files": [],
            "route_root_files": [],
            # until "complete" the game has not run: _swap_route_saves_out then
            # takes nothing back to the route, and until "steam_files_held" the
            # loose files in the Steam folder are still Steam's
            "steam_files_held": False,
            "complete": False,
        }
        manifest["swap"] = swap
        self._write_save_quarantine_manifest(session_directory, manifest)
        held = session_directory / self.SwapRootFilesDirectoryName
        for child in list(global_save_root.iterdir()):
            if child.is_file():
                held.mkdir(exist_ok=True)
                os.replace(child, held / child.name)
                swap["steam_root_files"].append(child.name)
                self._write_save_quarantine_manifest(session_directory, manifest)
        swap["steam_files_held"] = True
        self._write_save_quarantine_manifest(session_directory, manifest)
        if not route.is_dir():
            if keep_steam_saves:
                self._put_steam_saves_in_place(session_directory, manifest, global_save_root)
            swap["complete"] = True
            self._write_save_quarantine_manifest(session_directory, manifest)
            return
        for child in route.iterdir():
            if child.is_file():
                shutil.copy2(child, global_save_root / child.name)
                swap["route_root_files"].append(child.name)
            elif (
                child.is_dir()
                and child.name not in {"save_profiles"}
                and self._save_directory_contains_elden_ring_saves(child)
            ):
                shutil.copytree(
                    child,
                    global_save_root / child.name,
                    ignore=(
                        (lambda folder, names, top=str(child): [
                            name for name in names if name in self.SteamCloudSaves
                        ] if folder == top else [])
                        if keep_steam_saves
                        else None
                    ),
                )
                swap["directories"].append(child.name)
            self._write_save_quarantine_manifest(session_directory, manifest)
        if keep_steam_saves:
            self._put_steam_saves_in_place(session_directory, manifest, global_save_root)
        swap["complete"] = True
        self._write_save_quarantine_manifest(session_directory, manifest)
        qInfo(
            "Elden Ring MO2: swapped the route's saves into the Steam folder for "
            f"a launch outside the VFS: {len(swap['directories'])} account folder(s), "
            f"{len(swap['route_root_files'])} loose file(s)."
        )

    def _put_steam_saves_in_place(
        self, session_directory: Path, manifest: dict, global_save_root: Path
    ) -> None:
        swap = manifest["swap"]
        placed = swap.setdefault("steam_saves_in_place", [])
        for directory in manifest.get("directories", []):
            name = str(directory.get("name", ""))
            for save in self.SteamCloudSaves:
                source = session_directory / name / save
                if not source.is_file():
                    continue
                # recorded first: a cut copy is still recognised as Steam's
                placed.append(f"{name}/{save}")
                self._write_save_quarantine_manifest(session_directory, manifest)
                target = global_save_root / name / save
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)

    def _clear_steam_saves_in_place(
        self, session_directory: Path, swap: dict, global_save_root: Path
    ) -> None:
        """Takes out the copies of Steam's saves that stayed visible, when they
        are still Steam's. One the game changed is the game's output, and goes on
        to the route like the rest."""
        placed = swap.get("steam_saves_in_place")
        if not isinstance(placed, list):
            return
        for relative in placed:
            copy = global_save_root / str(relative)
            if copy.is_file() and self._file_is_held_by(copy, session_directory / str(relative)):
                copy.unlink()
            try:
                # nothing else of this account: it mustn't block its folder's return
                if copy.parent.is_dir() and not any(copy.parent.iterdir()):
                    copy.parent.rmdir()
            except OSError:
                pass

    def _swap_route_saves_out(
        self, session_directory: Path, manifest: dict, swap: dict, global_save_root: Path
    ) -> None:
        """Takes what the game wrote in the Steam folder back to the route (the
        route was backed up before the launch), then puts the Steam folder's
        own loose files back."""
        route = Path(str(swap.get("route", "")))
        if not route.is_absolute():
            raise OSError(_tr("The save-recovery record has no valid route for the swapped saves."))
        route.mkdir(parents=True, exist_ok=True)
        # Steam's own loose files already put back by a try that failed partway are
        # not the game's to take to the route; records older than this have no list
        restored = swap.get("restored_root_files")
        if not isinstance(restored, list):
            restored = []
        swap["restored_root_files"] = restored
        self._clear_steam_saves_in_place(session_directory, swap, global_save_root)
        # a swap that stopped partway (records older than this finished theirs):
        # the game never ran, and what is here is the route's own copy, perhaps
        # cut short by the failure, so the route keeps what it has
        complete = swap.get("complete", True)
        steam_files_held = swap.get("steam_files_held", True)
        changed = 0
        if global_save_root.is_dir():
            for child in list(global_save_root.iterdir()):
                if child.is_dir() and self._save_directory_contains_elden_ring_saves(child):
                    if complete:
                        target = route / child.name
                        target.mkdir(parents=True, exist_ok=True)
                        for entry in child.rglob("*"):
                            destination = target / entry.relative_to(child)
                            if entry.is_dir():
                                destination.mkdir(parents=True, exist_ok=True)
                            else:
                                destination.parent.mkdir(parents=True, exist_ok=True)
                                changed += self._return_swapped_file(
                                    entry, destination, session_directory, route
                                )
                        shutil.rmtree(child)
                    elif self._tree_is_held_by(child, route / child.name):
                        shutil.rmtree(child)
                    else:
                        # not the route's copy: played from Steam after a crash
                        self._keep_aside(session_directory, child)
                elif child.is_file() and child.name not in restored:
                    if complete:
                        changed += self._return_swapped_file(
                            child, route / child.name, session_directory, route
                        )
                        child.unlink()
                    elif steam_files_held:
                        if self._file_is_held_by(child, route / child.name):
                            child.unlink()
                        else:
                            self._keep_aside(session_directory, child)
        held = session_directory / self.SwapRootFilesDirectoryName
        if held.is_dir():
            global_save_root.mkdir(parents=True, exist_ok=True)
            for entry in list(held.iterdir()):
                # recorded first: by now the game's copy of this name has gone to the
                # route, so whatever a retry finds under it is Steam's
                if entry.name not in restored:
                    restored.append(entry.name)
                    self._write_save_quarantine_manifest(session_directory, manifest)
                in_place = global_save_root / entry.name
                if in_place.is_file() and not self._file_is_held_by(in_place, entry):
                    self._keep_aside(session_directory, in_place)
                os.replace(entry, in_place)
            held.rmdir()
        qInfo(
            "Elden Ring MO2: the saves of the launch outside the VFS are back in "
            f"the route '{route}'; {changed} file(s) had changed."
        )
        if changed and self._recovering_earlier_session:
            self._returned_saves.append((route, changed))

    @staticmethod
    def _file_is_held_by(file: Path, other: Path) -> bool:
        """Whether every byte of the file is in the other one: the same file, or
        a copy of it cut short."""
        try:
            if not other.is_file() or file.stat().st_size > other.stat().st_size:
                return False
            with file.open("rb") as mine, other.open("rb") as theirs:
                while chunk := mine.read(1024 * 1024):
                    if theirs.read(len(chunk)) != chunk:
                        return False
        except OSError:
            return False
        return True

    @classmethod
    def _tree_is_held_by(cls, folder: Path, other: Path) -> bool:
        return all(
            cls._file_is_held_by(entry, other / entry.relative_to(folder))
            for entry in folder.rglob("*")
            if not entry.is_dir()
        )

    def _keep_aside(self, session_directory: Path, path: Path) -> None:
        """Moves something of the Steam folder into the recovery session, which
        stays for review."""
        root = session_directory / self.UnroutedSaveDirectoryName
        root.mkdir(parents=True, exist_ok=True)
        destination = root / path.name
        if os.path.lexists(destination):
            destination = root / f"{path.name}.{uuid.uuid4().hex}"
        os.replace(path, destination)
        self._kept_aside_saves.append(destination)
        qCritical(f"Elden Ring MO2: kept '{path}' in '{destination}' instead of deleting it.")

    @staticmethod
    def _copy_whole(source: Path, destination: Path) -> None:
        """A failure leaves the destination as it was, never a cut copy."""
        temporary = destination.with_name(f".{destination.name}.swap-tmp")
        shutil.copy2(source, temporary)
        os.replace(temporary, destination)

    def _return_swapped_file(
        self, entry: Path, destination: Path, session_directory: Path, route: Path
    ) -> int:
        """Copies a file of the Steam folder to the route and returns 1 if that
        changes the route. A route file newer than it (the route was written while
        the session was held) is kept aside first."""
        changed = 1
        if destination.is_file():
            mine = entry.stat()
            theirs = destination.stat()
            if mine.st_size == theirs.st_size and mine.st_mtime_ns == theirs.st_mtime_ns:
                changed = 0
            if theirs.st_mtime_ns > mine.st_mtime_ns and not self._file_is_held_by(
                destination, entry
            ):
                root = session_directory / self.UnroutedSaveDirectoryName / "route-newer"
                aside = root / destination.relative_to(route)
                aside.parent.mkdir(parents=True, exist_ok=True)
                if aside.exists() and not (
                    self._file_is_held_by(destination, aside)
                    and self._file_is_held_by(aside, destination)
                ):
                    aside = aside.with_name(f"{aside.name}.{uuid.uuid4().hex}")
                if not aside.exists():
                    self._copy_whole(destination, aside)
                if aside not in self._kept_aside_saves:
                    self._kept_aside_saves.append(aside)
                qCritical(
                    f"Elden Ring MO2: '{destination}' was newer than the copy coming "
                    f"back from the Steam folder; it was kept in '{aside}'."
                )
        self._copy_whole(entry, destination)
        return changed

    @staticmethod
    def _split_arguments(args: str) -> list[str]:
        """Splits an executable's argument line as Windows does: spaces part
        the arguments, double quotes keep one together (and are dropped), and
        \\" is a literal quote. PyQt6 has no QProcess.splitCommand."""
        arguments: list[str] = []
        current: list[str] = []
        quoted = False
        started = False
        index = 0
        while index < len(args):
            character = args[index]
            if character == "\\" and index + 1 < len(args) and args[index + 1] == '"':
                current.append('"')
                started = True
                index += 2
                continue
            if character == '"':
                quoted = not quoted
                started = True
            elif character.isspace() and not quoted:
                if started:
                    arguments.append("".join(current))
                    current, started = [], False
            else:
                current.append(character)
                started = True
            index += 1
        if started:
            arguments.append("".join(current))
        return arguments

    @classmethod
    def seamless_dll_in_mods(
        cls, mods: list[tuple[str, Path, bool]]
    ) -> tuple[str, Path] | None:
        """The Seamless Co-op DLL of the highest-priority active mod (name,
        folder, active) that holds one."""
        for name, folder, active in reversed(mods):
            dll = folder / "SeamlessCoop" / cls.SeamlessDll
            if active and dll.is_file():
                return name, dll
        return None

    def _mod_folders(self) -> list[tuple[str, Path, bool]]:
        mods = []
        try:
            mod_list = self._organizer.modList()
            for name in mod_list.allModsByProfilePriority():
                mod = mod_list.getMod(name)
                if mod is None:
                    continue
                active = bool(mod_list.state(name) & mobase.ModState.ACTIVE)
                mods.append((name, Path(mod.absolutePath()), active))
        except (AttributeError, RuntimeError):
            pass
        return mods

    def _start_outside_vfs(self, app_path: str, working_directory, args: str) -> bool:
        """Starts a launcher without MO2's hooks (QProcess runs it as a plain
        child of MO2, which itself is not hooked)."""
        try:
            directory = (
                working_directory.absolutePath()
                if hasattr(working_directory, "absolutePath")
                else str(working_directory or "")
            )
        except RuntimeError:
            directory = ""
        if not directory:
            directory = str(Path(app_path).parent)
        if self._is_console_program(app_path):
            # a console launcher (ELDEN RING Reforged's asks for its settings
            # there): QProcess gives it a console nobody sees, so it waited
            # for a key forever. It gets a window of its own.
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 1  # SW_SHOWNORMAL
            try:
                process = subprocess.Popen(
                    [app_path, *self._split_arguments(args)],
                    cwd=directory,
                    creationflags=subprocess.CREATE_NEW_CONSOLE,
                    startupinfo=startup,
                )
                started, pid = True, process.pid
            except OSError as error:
                qCritical(f"Elden Ring MO2: could not start '{app_path}': {error}")
                started, pid = False, 0
        else:
            started, pid = QProcess.startDetached(app_path, self._split_arguments(args), directory)
        if started:
            qInfo(
                f"Elden Ring MO2: started '{Path(app_path).name}' outside the VFS "
                f"(process {pid}); MO2's own run of it was cancelled."
            )
        else:
            qCritical(f"Elden Ring MO2: could not start '{app_path}' outside the VFS.")
        return bool(started)

    @staticmethod
    def _is_console_program(app_path: str) -> bool:
        """Whether the executable's PE header asks for a console."""
        try:
            with open(app_path, "rb") as image:
                header = image.read(4096)
            pe = int.from_bytes(header[0x3C:0x40], "little")
            if header[:2] != b"MZ" or header[pe : pe + 4] != b"PE\0\0":
                return False
            # the optional header's Subsystem field; 3 is IMAGE_SUBSYSTEM_WINDOWS_CUI
            return int.from_bytes(header[pe + 0x5C : pe + 0x5E], "little") == 3
        except (OSError, ValueError):
            return False
