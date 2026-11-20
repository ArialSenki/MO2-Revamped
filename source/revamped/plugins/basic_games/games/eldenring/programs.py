"""MO2 Revamped: eLDEN RING Reforged's online launcher as an MO2 program, kept
in step with its mod.

Part of the Elden Ring and Nightreign game plugin (game_eldenring_mo2.py),
whose EldenRingMo2Game takes it as a mixin.
"""

from __future__ import annotations

from pathlib import Path

import mobase
from PyQt6.QtCore import (
    QFileInfo,
    QDir,
    QTimer,
)
from PyQt6.QtWidgets import (
    QComboBox,
    QMessageBox,
)

from .common import (
    _tr,
    _ProgramListLock,
)
from .checker import EldenRingModDataChecker


class OnlineProgramsMixin:
    def _online_launchers(self) -> list[Path]:
        """Reforged's launchers in the mods folder. The folder is searched only
        when a mod was installed or removed, MO2 asks for its programs, or the
        mods it holds have changed (a refresh that found a new folder)."""
        if not self.AutoSwapLaunchers:
            return []
        try:
            mod_names = frozenset(self._organizer.modList().allMods())
            mods_path = Path(self._organizer.modsPath())
        except (AttributeError, RuntimeError, TypeError):
            return []
        if self._online_launchers_found is None or mod_names != self._online_launchers_mods:
            online = EldenRingModDataChecker.ReforgedOnlineDirectory
            try:
                self._online_launchers_found = sorted(
                    mods_path.glob(f"*/{online}/{EldenRingModDataChecker.ReforgedLauncherPath}")
                )
            except OSError:
                return []
            self._online_launchers_mods = mod_names
        return self._online_launchers_found

    def _reforged_online_executables(self) -> list[mobase.ExecutableInfo]:
        """Reforged's own launcher, for its online mode, from each installed
        ELDEN RING Reforged that kept its release (archive installer choice).
        It starts outside the VFS (AutoSwapLaunchers), as online needs."""
        executables = []
        for index, launcher in enumerate(self._online_launchers()):
            title = "ELDEN RING Reforged (online)"
            if index:
                title += f" ({launcher.parents[3].name})"
            executables.append(
                mobase.ExecutableInfo(title, QFileInfo(str(launcher)))
                .withWorkingDirectory(QDir(str(launcher.parents[2])))
                .withSteamAppId(str(self.GameSteamId))
            )
        return executables

    def _inactive_reforged_online_programs(
        self, executables: list[mobase.ExecutableInfo]
    ) -> set[str]:
        """The online programs whose Reforged mod is off in this profile."""
        inactive = set()
        try:
            mod_list = self._organizer.modList()
            for executable in executables:
                mod_name = Path(executable.binary().absoluteFilePath()).parents[3].name
                if not mod_list.state(mod_name) & mobase.ModState.ACTIVE:
                    inactive.add(executable.title())
        except (AttributeError, IndexError, RuntimeError):
            return set()
        return inactive

    def _follow_reforged_online_programs(self, selector: QComboBox) -> None:
        """The program list offers Reforged's online mode only with its mod on."""
        if self._online_programs_selector is selector:
            return
        self._online_programs_selector = selector
        self._online_programs_tooltip = selector.toolTip()
        self._online_program_lock = _ProgramListLock(selector)
        selector.installEventFilter(self._online_program_lock)

        def sync(*_args) -> None:
            # several changes in a row need one look at the list
            if not self._online_programs_sync_queued:
                self._online_programs_sync_queued = True
                QTimer.singleShot(0, self._sync_reforged_online_programs)

        selector.model().rowsInserted.connect(sync)
        # the wheel and the arrows can reach a row hidden from the popup
        selector.currentIndexChanged.connect(lambda _row: sync())

        def mods_toggled(changes) -> None:
            # only a mod holding one of Reforged's launchers changes the list
            online_mods = {launcher.parents[3].name for launcher in self._online_launchers()}
            if not online_mods.isdisjoint(changes):
                sync()

        def mods_replaced(*_args) -> None:
            self._online_launchers_found = None
            sync()

        mod_list = self._organizer.modList()
        mod_list.onModStateChanged(mods_toggled)
        mod_list.onModInstalled(mods_replaced)
        mod_list.onModRemoved(mods_replaced)
        self._organizer.onProfileChanged(sync)
        sync()

    def _sync_reforged_online_programs(self) -> None:
        """A Reforged mod installed as the online version decides the program:
        while it is on, the list stays on its launcher."""
        self._online_programs_sync_queued = False
        selector = self._online_programs_selector
        if selector is None or not self.isActive():
            return
        executables = self._reforged_online_executables()
        hidden = self._inactive_reforged_online_programs(executables)
        # the program of a Reforged mod that was removed stays saved: not offered
        offered = {executable.title() for executable in executables}
        for row in range(selector.count()):
            text = selector.itemText(row)
            if text.startswith("ELDEN RING Reforged (online)") and text not in offered:
                hidden.add(text)
        view = selector.view()
        for row in range(selector.count()):
            view.setRowHidden(row, selector.itemText(row) in hidden)
        online = [
            executable.title()
            for executable in executables
            if executable.title() not in hidden
        ]
        row = selector.findText(online[0]) if online else -1
        if row >= 0:
            if selector.currentIndex() != row:
                selector.setCurrentIndex(row)
            self._online_program_lock.locked = True
            selector.setToolTip(
                _tr("Reforged's online version is on. Turn that mod off to choose another program.")
            )
            self._online_program_locked = True
            return
        if self._online_program_locked:
            self._online_program_locked = False
            self._online_program_lock.locked = False
            selector.setToolTip(self._online_programs_tooltip)
        # MO2's first program is the game
        if selector.currentText() in hidden and selector.count() > 1:
            selector.setCurrentIndex(1)

    def _reforged_online_mod_ready(self, app_path: str) -> bool:
        """A shortcut or the toolbar can still start the online program with its
        mod off: it would run a Reforged the profile has turned off."""
        launcher = Path(app_path)
        if (
            launcher.name.casefold() not in self.AutoSwapLaunchers
            or len(launcher.parents) < 4
            or launcher.parents[2].name != EldenRingModDataChecker.ReforgedOnlineDirectory
        ):
            return True
        mod_name = launcher.parents[3].name
        mod_list = self._organizer.modList()
        try:
            if mod_list.state(mod_name) & mobase.ModState.ACTIVE:
                return True
        except (AttributeError, RuntimeError):
            return True
        box = QMessageBox(self._message_parent())
        box.setIcon(QMessageBox.Icon.Question)
        box.setWindowTitle("ELDEN RING Reforged")
        box.setText(_tr("Reforged is off in this profile"))
        box.setInformativeText(
            _tr("Its online mode starts from the mod {0}. Turn the mod on and start it?").format(mod_name)
        )
        start = box.addButton(_tr("Turn on and start"), QMessageBox.ButtonRole.AcceptRole)
        box.addButton(QMessageBox.StandardButton.Cancel)
        box.setDefaultButton(start)
        box.exec()
        # a child of MO2's window: freed once MO2's event loop runs again,
        # after the choice below is read
        box.deleteLater()
        if box.clickedButton() is not start:
            return False
        mod_list.setActive(mod_name, True)
        self._mod_turned_on_for_launch = mod_name
        return True
