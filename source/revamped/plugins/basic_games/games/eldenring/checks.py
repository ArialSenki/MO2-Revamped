"""MO2 Revamped: checks of the game and the mods before a launch: the game
version against the bridge's code searches, several regulation.bin, Steam
Cloud, the save extension and Seamless Co-op's password.

Part of the Elden Ring and Nightreign game plugin (game_eldenring_mo2.py),
whose EldenRingMo2Game takes it as a mixin.
"""

from __future__ import annotations

import configparser
import re
from pathlib import Path
import struct


from PyQt6.QtCore import (
    QUrl,
)
from PyQt6.QtGui import QDesktopServices
from PyQt6.QtWidgets import (
    QCheckBox,
    QMessageBox,
)

from .common import (
    qInfo,
    qWarning,
    _tr,
)


def _game_class():
    """EldenRingMo2Game, looked up when called (it is built from this mixin)."""
    from ..game_eldenring_mo2 import EldenRingMo2Game

    return EldenRingMo2Game


class LaunchChecksMixin:
    @staticmethod
    def _read_game_executable(executable: Path) -> bytes:
        reads = _game_class()._executable_reads
        if reads is None:
            return executable.read_bytes()
        info = executable.stat()
        key = (str(executable), info.st_size, info.st_mtime_ns)
        if key not in reads:
            reads[key] = executable.read_bytes()
        return reads[key]

    @classmethod
    def file_loader_found(cls, executable: Path) -> bool | None:
        """Whether the bridge will find the game's file loader in this executable,
        by the rules of its own search; None when the file can't be read."""
        try:
            data = _game_class()._read_game_executable(executable)
            header = struct.unpack_from("<I", data, 0x3C)[0]
            count = struct.unpack_from("<H", data, header + 6)[0]
            optional_size = struct.unpack_from("<H", data, header + 20)[0]
            sections = []
            for index in range(count):
                entry = header + 24 + optional_size + index * 40
                virtual_size, address, raw_size, raw = struct.unpack_from("<IIII", data, entry + 8)
                if struct.unpack_from("<I", data, entry + 36)[0] & 0x20000000:
                    sections.append((address, min(virtual_size or raw_size, raw_size), raw))
        except (OSError, struct.error):
            return None

        def file_offset(address: int) -> int | None:
            for start, size, raw in sections:
                if start <= address < start + size:
                    return raw + address - start
            return None

        def follow_jumps(address: int) -> int | None:
            for _jump in range(9):
                offset = file_offset(address)
                if offset is None:
                    return None
                if data[offset] != 0xE9:
                    return address
                address += 5 + int.from_bytes(data[offset + 1:offset + 5], "little", signed=True)
            return None

        found = None
        for start, size, raw in sections:
            for before, after in cls.ResolverCallSites:
                first = 5 + len(before)
                match = data.find(after, raw + first, raw + size)
                while match >= 0:
                    if data[match - 5] == 0xE8 and data[match - first:match - 5] == before:
                        target = follow_jumps(
                            start + match - raw
                            + int.from_bytes(data[match - 4:match], "little", signed=True)
                        )
                        if target is not None:
                            if found is not None and found != target:
                                return False
                            found = target
                    match = data.find(after, match + 1, raw + size)
        if found is not None:
            return True
        # the bridge's second way (find_resolver_by_argument_setup): the
        # arguments set up for the call, two calls or more agreeing
        calls = 0
        for start, size, raw in sections:
            for pattern in cls.ResolverArgumentSetups:
                for match in pattern.finditer(data, raw, raw + size):
                    call = match.end() - 1
                    if call + 5 > raw + size:
                        continue
                    target = follow_jumps(
                        start + call + 5 - raw
                        + int.from_bytes(data[call + 1:call + 5], "little", signed=True)
                    )
                    if target is None:
                        continue
                    if found is not None and found != target:
                        return False
                    found = target
                    calls += 1
        return calls >= 2

    @staticmethod
    def game_version_text(executable: Path) -> str:
        """The version as the game shows it (Elden Ring keeps it in the text
        "Gr v1.17.2#1131") with the file's own, or whichever of them is there."""
        try:
            data = _game_class()._read_game_executable(executable)
        except OSError:
            return ""
        file_version = ""
        fixed_info = data.find(struct.pack("<I", 0xFEEF04BD))
        if fixed_info >= 0 and fixed_info + 16 <= len(data):
            high, low = struct.unpack_from("<II", data, fixed_info + 8)
            file_version = f"{high >> 16}.{high & 0xFFFF}.{low >> 16}.{low & 0xFFFF}"
        shown = re.search(rb"Gr v(\d+\.\d+\.\d+)#\d+\0dantelion", data)
        if shown is None:
            return file_version
        version = shown.group(1).decode().removesuffix(".0")
        return f"{version} ({file_version})" if file_version else version

    def _file_loader_ready(self, app_path: str) -> bool:
        """A game update can change the code the bridge looks for: said before
        the launch, and again only if the game's own run shows what this missed."""
        if self._runs_outside_vfs(app_path) or not self._active_mods_have_game_files():
            return True
        executable = Path(self.gameDirectory().absolutePath()) / self.GameBinary
        try:
            info = executable.stat()
        except OSError:
            return True
        stamp = f"{info.st_size}:{info.st_mtime_ns}"
        if self._file_loader_scan[0] != stamp:
            self._file_loader_scan = (stamp, self.file_loader_found(executable))
        if self._file_loader_scan[1] is not False:
            return True
        version = self.game_version_text(executable)
        qWarning(f"Elden Ring MO2: the game's file loader wasn't found in '{executable}' ({version}).")
        self._loose_files_warning_shown = True
        key = "file_loader_accepted"
        try:
            if self._organizer.persistent(self.name(), key, "") == stamp:
                return True
        except (AttributeError, RuntimeError):
            pass
        box = QMessageBox(self._message_parent())
        box.setIcon(QMessageBox.Icon.Warning)
        box.setWindowTitle(_tr("Mods may not load"))
        box.setText(self._game_text(_tr("MO2 doesn't recognize this version of Elden Ring")))
        details = _tr("The game files of your mods won't be used, though their DLLs aren't affected. "
                      "This usually follows a game update: look for a new version of MO2 Revamped.")
        if version:
            details += "\n\n" + _tr("Game version: {0}").format(version)
        box.setInformativeText(details)
        remember = QCheckBox(_tr("Don't warn again for this version of the game"))
        box.setCheckBox(remember)
        start = box.addButton(_tr("Start anyway"), QMessageBox.ButtonRole.AcceptRole)
        box.addButton(_tr("Cancel"), QMessageBox.ButtonRole.RejectRole)
        box.setDefaultButton(start)
        box.exec()
        # a child of MO2's window: freed once MO2's event loop runs again,
        # after the choice below is read
        box.deleteLater()
        if box.clickedButton() is not start:
            return False
        if remember.isChecked():
            try:
                self._organizer.setPersistent(self.name(), key, stamp)
            except (AttributeError, RuntimeError) as error:
                qWarning(f"Elden Ring MO2: could not save the file loader choice: {error}")
        return True

    def _regulation_conflict_ready(self, app_path: str) -> bool:
        """Of several active regulation.bin the game gets only the one with the
        highest priority, and nothing else tells the user."""
        if self._runs_outside_vfs(app_path):
            # no mod's regulation.bin reaches it
            return True
        mods = [
            name
            for name, root, active in self._mod_folders()
            if active and (root / "regulation.bin").is_file()
        ]
        if len(mods) < 2:
            return True
        combination = "|".join(sorted(name.casefold() for name in mods))
        profile = self._organizer.profile()
        key = f"regulation_accepted_{profile.name() if profile is not None else ''}"
        try:
            if self._organizer.persistent(self.name(), key, "") == combination:
                return True
        except (AttributeError, RuntimeError):
            pass
        qInfo(f"Elden Ring MO2: several active mods have regulation.bin: {', '.join(mods)}.")
        box = QMessageBox(self._message_parent())
        box.setIcon(QMessageBox.Icon.Warning)
        box.setWindowTitle("regulation.bin")
        box.setText(_tr("Several active mods bring their own regulation.bin"))
        box.setInformativeText(
            _tr("The game uses only one: {0}, the one with the highest priority. The rule "
                "changes of the others ({1}) won't apply, and they may not work properly. "
                "To use another one, change its priority in the mod list or turn off the "
                "mods you don't need.").format(mods[-1], ", ".join(mods[:-1]))
        )
        remember = QCheckBox(_tr("Don't warn again for these mods"))
        box.setCheckBox(remember)
        start = box.addButton(_tr("Start anyway"), QMessageBox.ButtonRole.AcceptRole)
        box.addButton(_tr("Cancel"), QMessageBox.ButtonRole.RejectRole)
        box.setDefaultButton(start)
        box.exec()
        # a child of MO2's window: freed once MO2's event loop runs again,
        # after the choice below is read
        box.deleteLater()
        if box.clickedButton() is not start:
            return False
        if remember.isChecked():
            try:
                self._organizer.setPersistent(self.name(), key, combination)
            except (AttributeError, RuntimeError) as error:
                qWarning(f"Elden Ring MO2: could not save the regulation.bin choice: {error}")
        return True

    @staticmethod
    def save_extension_literal_found(executable: Path) -> bool | None:
        """Whether apply_save_extension_setting in the bridge will find the one
        .sl2 literal it replaces, by the rules of its own search: constant data
        only, at even offsets. None when the file can't be read."""
        pattern = ".sl2".encode("utf-16-le")
        try:
            data = _game_class()._read_game_executable(executable)
            header = struct.unpack_from("<I", data, 0x3C)[0]
            count = struct.unpack_from("<H", data, header + 6)[0]
            optional_size = struct.unpack_from("<H", data, header + 20)[0]
            matches = 0
            for index in range(count):
                entry = header + 24 + optional_size + index * 40
                virtual_size, _address, raw_size, raw = struct.unpack_from("<IIII", data, entry + 8)
                flags = struct.unpack_from("<I", data, entry + 36)[0]
                # not executable or writable, initialized data
                if flags & 0xA0000000 or not flags & 0x00000040:
                    continue
                end = raw + min(virtual_size or raw_size, raw_size)
                match = data.find(pattern, raw, end)
                while match >= 0:
                    matches += (match - raw) % 2 == 0
                    match = data.find(pattern, match + 2, end)
        except (OSError, struct.error):
            return None
        return matches == 1

    def _steam_cloud_swap_ready(self, app_path: str) -> bool:
        """A program started outside the VFS plays the route's ER0000.sl2 in the
        Steam folder, and Steam Cloud can upload it as it closes. Reforged online
        plays its own file, and Steam's stays in place."""
        if (
            not self._runs_outside_vfs(app_path)
            or Path(app_path).name.casefold() == self.ReforgedOnlineLauncher
        ):
            return True
        key = "steam_cloud_swap_accepted"
        try:
            if self._organizer.persistent(self.name(), key, False):
                return True
        except (AttributeError, RuntimeError):
            pass
        box = QMessageBox(self._message_parent())
        box.setIcon(QMessageBox.Icon.Warning)
        box.setWindowTitle(_tr("Steam Cloud"))
        box.setText(_tr("This program can send this profile's save to Steam Cloud"))
        box.setInformativeText(
            _tr("It starts outside MO2's virtual files, with this profile's saves in the Steam "
                "folder. If Steam Cloud is on for this game, Steam may upload them when the game "
                "closes, in place of your own Steam save. Your files stay on this PC.")
        )
        remember = QCheckBox(_tr("Don't warn again"))
        box.setCheckBox(remember)
        start = box.addButton(_tr("Start anyway"), QMessageBox.ButtonRole.AcceptRole)
        box.setDefaultButton(box.addButton(_tr("Cancel"), QMessageBox.ButtonRole.RejectRole))
        box.exec()
        # a child of MO2's window: freed once MO2's event loop runs again,
        # after the choice below is read
        box.deleteLater()
        if box.clickedButton() is not start:
            return False
        if remember.isChecked():
            try:
                self._organizer.setPersistent(self.name(), key, True)
            except (AttributeError, RuntimeError) as error:
                qWarning(f"Elden Ring MO2: could not remember the Steam Cloud choice: {error}")
        return True

    def _save_extension_ready(self, app_path: str) -> bool:
        """Without its own save extension ELDEN RING Reforged or The
        Convergence uses the save of the other mods: said before the launch,
        as for the file loader."""
        self._save_extension_accepted = False
        if self._runs_outside_vfs(app_path):
            return True
        own_save = self._mod_save_extension()
        if own_save is None:
            return True
        extension, mod_name = own_save
        executable = Path(self.gameDirectory().absolutePath()) / self.GameBinary
        try:
            info = executable.stat()
        except OSError:
            return True
        stamp = f"{info.st_size}:{info.st_mtime_ns}"
        if self._save_extension_scan[0] != stamp:
            self._save_extension_scan = (stamp, self.save_extension_literal_found(executable))
        if self._save_extension_scan[1] is not False:
            return True
        version = self.game_version_text(executable)
        qWarning(f"Elden Ring MO2: the game's save extension wasn't found in '{executable}' ({version}).")
        key = "save_extension_accepted"
        try:
            if self._organizer.persistent(self.name(), key, "") == stamp:
                self._save_extension_accepted = True
                return True
        except (AttributeError, RuntimeError):
            pass
        box = QMessageBox(self._message_parent())
        box.setIcon(QMessageBox.Icon.Warning)
        box.setWindowTitle(_tr("Saves may mix"))
        box.setText(_tr("MO2 can't give {0} its own save file in this version of the game").format(mod_name))
        details = _tr("{0} would use ER0000.sl2, the save of your other mods, instead of "
                      "ER0000.{1}, and could overwrite it. This usually follows a game update: "
                      "look for a new version of MO2 Revamped.").format(mod_name, extension)
        if version:
            details += "\n\n" + _tr("Game version: {0}").format(version)
        box.setInformativeText(details)
        remember = QCheckBox(_tr("Don't warn again for this version of the game"))
        box.setCheckBox(remember)
        start = box.addButton(_tr("Start anyway"), QMessageBox.ButtonRole.AcceptRole)
        box.setDefaultButton(box.addButton(_tr("Cancel"), QMessageBox.ButtonRole.RejectRole))
        box.exec()
        # a child of MO2's window: freed once MO2's event loop runs again,
        # after the choice below is read
        box.deleteLater()
        if box.clickedButton() is not start:
            return False
        self._save_extension_accepted = True
        if remember.isChecked():
            try:
                self._organizer.setPersistent(self.name(), key, stamp)
            except (AttributeError, RuntimeError) as error:
                qWarning(f"Elden Ring MO2: could not save the save extension choice: {error}")
        return True

    def _convergence_seamless_ready(self, app_path: str) -> bool:
        """With The Convergence, Seamless Co-op runs from a copy that saves
        apart (see _seamless_load_dll). When that copy can't be made, the game
        isn't started: Seamless would otherwise write Convergence's characters
        into the co-op save it uses without Convergence."""
        if self._runs_outside_vfs(app_path):
            return True
        self._seamless_load_dll()
        error = getattr(self, "_convergence_seamless_error", None)
        if not error:
            return True
        qWarning(f"Elden Ring MO2: Seamless Co-op's copy for The Convergence couldn't be made "
                 f"({error}); launch stopped.")
        QMessageBox.warning(
            self._message_parent(),
            _tr("Seamless Co-op"),
            _tr("Seamless Co-op couldn't be set up for The Convergence, so the game was not "
                "started: it would save Convergence's characters into the co-op save it uses "
                "without Convergence.\n\n{0}").format(error),
        )
        return False

    def _seamless_password_ready(self, app_path: str) -> bool:
        """Seamless Co-op quits the game at start without a session password
        in its settings file: MO2 says so before the launch instead."""
        if self._runs_outside_vfs(app_path):
            # started outside the VFS, no mod's DLL joins it
            return True
        seamless = self._active_seamless_dll()
        if seamless is None:
            return True
        settings = seamless.with_name(f"{seamless.stem}_settings.ini")
        if not settings.is_file():
            return True
        try:
            config = configparser.ConfigParser(interpolation=None, strict=False)
            config.read(settings, encoding="utf-8-sig")
            password = config.get("PASSWORD", "cooppassword", fallback="").strip()
        except (OSError, UnicodeError, configparser.Error):
            return True
        if password:
            return True
        qWarning(f"Elden Ring MO2: Seamless Co-op has no session password in '{settings}'; launch stopped.")
        box = QMessageBox(self._message_parent())
        box.setIcon(QMessageBox.Icon.Warning)
        box.setWindowTitle(_tr("Seamless Co-op"))
        box.setText(_tr("Seamless Co-op needs a session password"))
        box.setInformativeText(
            _tr("Set cooppassword in its settings file; everyone who plays together uses the "
                "same one. Without it Seamless Co-op closes the game as it starts, so the "
                "game was not started.\n\n{0}").format(settings)
        )
        open_file = box.addButton(_tr("Open the settings file"), QMessageBox.ButtonRole.AcceptRole)
        box.addButton(_tr("Cancel"), QMessageBox.ButtonRole.RejectRole)
        box.setDefaultButton(open_file)
        box.exec()
        # a child of MO2's window: freed once MO2's event loop runs again,
        # after the choice below is read
        box.deleteLater()
        if box.clickedButton() is open_file:
            QDesktopServices.openUrl(QUrl.fromLocalFile(str(settings)))
        return False
