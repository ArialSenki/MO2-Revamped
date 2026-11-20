"""MO2 Revamped: what the plugin shows in MO2: the start-up recovery notices,
the messages about saves, the run button and the window's title bar.

Part of the Elden Ring and Nightreign game plugin (game_eldenring_mo2.py),
whose EldenRingMo2Game takes it as a mixin.
"""

from __future__ import annotations

import ctypes
import os
from pathlib import Path


from PyQt6.QtCore import (
    QTimer,
    Qt,
)
from PyQt6.QtWidgets import (
    QAbstractButton,
    QApplication,
    QBoxLayout,
    QComboBox,
    QMessageBox,
    QSizePolicy,
    QPushButton,
    QToolButton,
)

from .common import (
    set_log_game_name,
    qCritical,
    qInfo,
    _tr,
    _RunButtonEnabledMirror,
    _MainWindowCloseWatcher,
)


class InterfaceMixin:
    def _initialize_elden_ring_ui(self, _main_window) -> None:
        if not self.isActive():
            # MO2 switched to the other game's instance with this game's launcher
            # still open: its saves still come back once it closes
            if self._recovery_waiting:
                self._retry_recovery_later()
            return

        qInfo(
            "Elden Ring MO2 support "
            f"{self.Version} initialized; native bridge component "
            f"{self.NativeBridgeVersion}."
        )

        self._main_window = _main_window
        if _main_window is not None:
            # MO2 deletes its window itself: no message may use it after that
            _main_window.destroyed.connect(self._forget_main_window)

        set_log_game_name(self.DisplayName)

        if not self._recover_stale_global_save_quarantines():
            if self._recovery_waiting:
                # MO2 started while the game (or another MO2 window's game)
                # still runs: the saves come back once it closes
                QMessageBox.information(
                    _main_window,
                    _tr("Steam saves still hidden"),
                    _tr("An earlier launch hid the Steam save folders, and the "
                    "game or one of its launchers is still running, or another "
                    "MO2 window is waiting for its game. MO2 puts the saves "
                    "back as soon as it closes. Don't start the game from "
                    "Steam until then."),
                )
                self._retry_recovery_later()
            else:
                self._show_recovery_failed(_main_window)
        self._report_displaced_saves(_main_window)

        # the game-close watcher checks every few seconds: MO2 closed right
        # after the game would leave the saves set aside until its next start
        # (and block other installs' launches meanwhile)
        if _main_window is not None:
            self._close_watcher = _MainWindowCloseWatcher(
                _main_window, self._restore_saves_on_close
            )
            _main_window.installEventFilter(self._close_watcher)

        self._organizer.setPluginSetting(
            self.name(), self.ArchiveLayoutOverrideSetting, False
        )
        self._start_refined_ui_alignment()

    def _restore_saves_on_close(self) -> None:
        """MO2 is closing: Steam saves this launch set aside come back now,
        when neither the game nor a launcher of it runs any more; otherwise
        they wait for the next MO2 start, as after a crash."""
        self._close_launcher_notice()
        session = self._active_save_quarantine
        if session is None:
            return
        state = self._elden_ring_process_state()
        launcher = self._open_launcher()
        if state is not False or launcher:
            qInfo(
                "Elden Ring MO2: MO2 closes while the game or its launcher runs; "
                "the Steam saves come back at the next MO2 start."
            )
            still_open = self.GameProcess if state is True else launcher
            if still_open:
                QMessageBox.information(
                    # closed from the taskbar while minimized, MO2's window
                    # would hide this box and MO2 would seem not to close
                    self._message_parent(),
                    _tr("Steam saves still hidden"),
                    _tr("{0} is still open as MO2 closes. The Steam saves come back the "
                        "next time MO2 runs, once it has closed. Don't start the game "
                        "from Steam until then.").format(still_open),
                )
            return
        try:
            self._restore_global_save_quarantine(
                session, Path(self.savesDirectory().absolutePath())
            )
            self._active_save_quarantine = None
            qInfo("Elden Ring MO2: MO2 closes; the Steam saves are back in their folder.")
            self._report_displaced_saves()
        except (OSError, RuntimeError, ValueError) as error:
            qCritical(
                "Elden Ring MO2: the Steam saves could not be restored as MO2 "
                f"closes; they come back at the next MO2 start: {error}"
            )

    def _game_text(self, text: str) -> str:
        # a translated text that names Elden Ring, for the game this plugin manages
        name = self.DisplayName
        if name == "Elden Ring":
            return text
        # German joins the name with hyphens (Elden-Ring-Spielstand); French
        # elides before it (d'Elden Ring, qu'Elden Ring)
        for old, new in (
            ("d'Elden Ring", f"de {name}"),
            ("d’Elden Ring", f"de {name}"),
            ("qu'Elden Ring", f"que {name}"),
            ("qu’Elden Ring", f"que {name}"),
            ("Elden-Ring", name),
            ("Elden Ring", name),
        ):
            text = text.replace(old, new)
        return text

    def _show_recovery_failed(self, parent=None) -> None:
        QMessageBox.critical(
            parent or self._message_parent(),
            self._game_text(_tr("Elden Ring save recovery needed")),
            self._game_text(_tr("MO2 found a previous save-isolation session that it could not "
            "restore automatically. The original saves remain in the "
            "MO2 Revamped recovery folder. Do not launch Elden Ring or Steam "
            "until the saved folders have been restored.")),
        )

    def _show_launch_recovery_blocked(self) -> None:
        if self._recovery_waiting:
            launcher = self._open_launcher()
            if launcher and self._elden_ring_process_state() is False:
                QMessageBox.warning(
                    self._message_parent(),
                    _tr("Steam saves still hidden"),
                    _tr("The Steam save folders an earlier launch hid can't be put "
                        "back yet: {0} is still open. Close it and try again. The game "
                        "was not started.").format(launcher),
                )
                return
            QMessageBox.warning(
                self._message_parent(),
                _tr("Steam saves still hidden"),
                _tr("The Steam save folders an earlier launch hid can't be put "
                "back yet: the game or one of its launchers is still running, "
                "or another MO2 window is waiting for its game. Close it and "
                "try again. The game was not started."),
            )
            return
        QMessageBox.critical(
            self._message_parent(),
            self._game_text(_tr("Elden Ring save recovery needed")),
            _tr("MO2 could not restore an earlier global save folder. "
            "The game was not started. Restore the files from the "
            "MO2 Revamped recovery folder before continuing."),
        )

    def _report_displaced_saves(self, parent=None) -> None:
        """Tells the user about save folders with newer saves that a restore
        had to move into the recovery folder, and what a swap's return did."""
        parent = parent or self._message_parent()
        if self._displaced_newer_saves:
            folders = "\n".join(str(folder) for folder in self._displaced_newer_saves)
            self._displaced_newer_saves = []
            QMessageBox.warning(
                parent,
                _tr("Newer saves set aside"),
                _tr("MO2 put back the Steam saves an earlier launch had hidden, but "
                "a save folder had appeared in their place with newer saves (the "
                "game started from Steam meanwhile, or a save written outside the "
                "MO2 route). MO2 did not delete it; it is kept here:\n\n{0}\n\n"
                "If it holds progress you want, close the game and copy its files "
                "back.").format(folders),
            )
        if self._kept_aside_saves:
            kept = "\n".join(str(path) for path in self._kept_aside_saves)
            self._kept_aside_saves = []
            QMessageBox.warning(
                parent,
                _tr("Saves kept aside"),
                _tr("MO2 found saves that differ from, or are newer than, the copy "
                    "that takes their place. Nothing was deleted; they are kept "
                    "here:\n\n{0}\n\nIf one holds progress you want, close the game "
                    "and copy it back.").format(kept),
            )
        if self._returned_saves:
            returned = "\n".join(
                _tr("{0} changed file(s) to {1}").format(count, route)
                for route, count in self._returned_saves
            )
            self._returned_saves = []
            QMessageBox.information(
                parent,
                _tr("Saves moved back to their route"),
                _tr("A launch outside MO2's virtual file system ended while MO2 wasn't "
                    "watching. What was played from the Steam folder went to the route, "
                    "and the Steam saves are back:\n\n{0}").format(returned),
            )

    def _retry_recovery_later(self) -> None:
        # one retry chain at a time
        if self._recovery_retry_pending:
            return
        self._recovery_retry_pending = True
        # Steam Cloud looks at the save folder a second or two after the game
        # closes: while this install's recovery waits for the game itself the
        # saves must be back by then. Another MO2 window that holds the
        # session restores it from its own fast watch.
        interval = (
            self.SwapWatchInterval
            if self._recovery_waiting_for_game
            else self.GameWatchInterval
        )
        QTimer.singleShot(interval, self._retry_recovery)

    def _retry_recovery(self) -> None:
        self._recovery_retry_pending = False
        # while it waits for the game or a launcher to close, the sessions are
        # scanned again only once they have: the process check comes first
        if (
            self._recovery_waiting
            and self._recovery_waiting_for_game
            and (self._elden_ring_process_state() is not False or self._launcher_running())
        ):
            self._retry_recovery_later()
            return
        if self._recover_stale_global_save_quarantines():
            if self._recovered_sessions:
                qInfo(
                    "Elden Ring MO2: the game closed; the Steam saves an earlier "
                    "launch hid are back in their folder."
                )
            self._report_displaced_saves()
        elif self._recovery_waiting:
            self._retry_recovery_later()
        else:
            self._show_recovery_failed()

    def _start_refined_ui_alignment(self) -> None:
        """Align the executable selector and match the native title bar to MO2's theme."""
        if os.name != "nt":
            return
        application = QApplication.instance()
        if application is None:
            return
        self._refined_ui_attempts = 0
        self._refined_ui_timer = QTimer(application)
        self._refined_ui_timer.setInterval(250)
        self._refined_ui_timer.timeout.connect(self._align_refined_main_window)
        QTimer.singleShot(0, self._align_refined_main_window)
        self._refined_ui_timer.start()

    def _align_refined_main_window(self) -> None:
        self._refined_ui_attempts += 1
        application = QApplication.instance()
        if application is not None:
            for window in application.topLevelWidgets():
                selector = window.findChild(QComboBox, "executablesListBox")
                run_button = window.findChild(QAbstractButton, "startButton")
                if selector is None or run_button is None:
                    continue
                # The Fluent v4 window (recognized by its single "more" button)
                # sizes and centers the picker and Run itself.
                fluent_layout = window.findChild(QToolButton, "moreActionsBtn") is not None
                selector_parent = selector.parentWidget()
                layout = selector_parent.layout() if selector_parent is not None else None
                if layout is not None and not fluent_layout:
                    layout.setAlignment(selector, Qt.AlignmentFlag.AlignTop)
                selector.setMinimumContentsLength(32)
                selector.setSizeAdjustPolicy(
                    QComboBox.SizeAdjustPolicy.AdjustToMinimumContentsLengthWithIcon
                )
                selector.setMaxVisibleItems(8)
                selector.view().setMinimumWidth(max(320, selector.width()))
                self._follow_reforged_online_programs(selector)
                if not self._integrate_shortcut_menu(window, run_button):
                    continue
                self._set_native_title_bar(window, self._uses_dark_theme(application, window))
                self._refined_ui_timer.stop()
                return
        if self._refined_ui_attempts >= 40:
            self._refined_ui_timer.stop()

    def _integrate_shortcut_menu(self, window, run_button: QAbstractButton) -> bool:
        # Source builds with the split-button change already have a single
        # QToolButton. Keep that native control and its auto-connected Run slot.
        if isinstance(run_button, QToolButton) and run_button.menu() is not None:
            run_button.setPopupMode(QToolButton.ToolButtonPopupMode.MenuButtonPopup)
            if window.findChild(QToolButton, "moreActionsBtn") is None:
                run_button.setMinimumHeight(44)
            return True

        if window.findChild(QToolButton, "refinedRunButton") is not None:
            return True

        shortcut = window.findChild(QPushButton, "linkButton")
        if shortcut is None:
            return False

        # MO2's compiled 2.5.2 UI names this QVBoxLayout verticalLayout_12.
        # The fork source renames it runButtonsLayout; support both layouts.
        layout = window.findChild(QBoxLayout, "runButtonsLayout")
        if layout is None:
            layout = window.findChild(QBoxLayout, "verticalLayout_12")

        if (
            not isinstance(layout, QBoxLayout)
            or layout.indexOf(run_button) < 0
            or layout.indexOf(shortcut) < 0
        ):
            # Find the containing layout by its actual child buttons if the
            # UI uses a different layout object name.
            layout = None
            parent = shortcut.parentWidget()
            while parent is not None:
                candidate = parent.layout()
                if (
                    isinstance(candidate, QBoxLayout)
                    and candidate.indexOf(run_button) >= 0
                    and candidate.indexOf(shortcut) >= 0
                ):
                    layout = candidate
                    break
                parent = parent.parentWidget()

        if not isinstance(layout, QBoxLayout):
            return False

        shortcut_menu = shortcut.menu()
        if shortcut_menu is None:
            return False

        run_index = layout.indexOf(run_button)
        shortcut_index = layout.indexOf(shortcut)
        layout.setDirection(QBoxLayout.Direction.LeftToRight)
        layout.setSpacing(0)
        layout.setContentsMargins(0, 0, 0, 0)
        split_button = QToolButton(layout.parentWidget() or run_button.parentWidget())
        split_button.setObjectName("refinedRunButton")
        split_button.setText(run_button.text())
        split_button.setIcon(run_button.icon())
        split_button.setIconSize(run_button.iconSize())
        split_button.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextBesideIcon)
        split_button.setPopupMode(QToolButton.ToolButtonPopupMode.MenuButtonPopup)
        split_button.setMenu(shortcut_menu)
        split_button.setToolTip(_tr("Run the selected program; use the arrow for shortcut options."))
        split_button.setAccessibleName(_tr("Run"))
        split_button.setAccessibleDescription(
            _tr("Run the selected program. Use the arrow section for shortcut options.")
        )
        split_button.setSizePolicy(
            QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed
        )
        split_button.setMinimumWidth(max(132, run_button.minimumWidth()))
        split_button.setFixedHeight(44)
        split_button.setEnabled(run_button.isEnabled())
        split_button.setProperty("mo2RefinedSplitRun", True)

        # Route the main button area through MO2's existing auto-connected
        # slot. The menu area still refreshes shortcut state before opening.
        split_button.clicked.connect(
            lambda _checked=False, original=run_button: original.click()
        )
        split_button.pressed.connect(shortcut.pressed.emit)
        enable_mirror = _RunButtonEnabledMirror(run_button, split_button)
        run_button.installEventFilter(enable_mirror)
        self._refined_ui_filters.append(enable_mirror)

        layout.removeWidget(run_button)
        layout.removeWidget(shortcut)
        layout.insertWidget(min(run_index, shortcut_index), split_button, 1)
        layout.setStretch(layout.indexOf(split_button), 1)
        run_button.hide()
        shortcut.hide()
        split_button.show()
        return True

    @staticmethod
    def _uses_dark_theme(application, window) -> bool:
        """Detect the selected MO2 stylesheet so Windows matches its title bar."""
        return application is not None and bool(application.property("revampedDark"))

    @staticmethod
    def _set_native_title_bar(window, dark_mode: bool) -> None:
        """Ask Windows to draw this window's native title bar in the MO2 theme."""
        try:
            dwm = ctypes.WinDLL("dwmapi", use_last_error=True)
            set_attribute = dwm.DwmSetWindowAttribute
            set_attribute.argtypes = [
                ctypes.c_void_p,
                ctypes.c_uint,
                ctypes.c_void_p,
                ctypes.c_uint,
            ]
            set_attribute.restype = ctypes.c_long
            hwnd = ctypes.c_void_p(int(window.winId()))
            use_dark_mode = ctypes.c_int(1 if dark_mode else 0)
            for attribute in (20, 19):
                result = set_attribute(
                    hwnd,
                    attribute,
                    ctypes.byref(use_dark_mode),
                    ctypes.sizeof(use_dark_mode),
                )
                if result == 0:
                    break
        except (AttributeError, OSError, TypeError, ValueError):
            # Older Windows versions may not expose either title-bar attribute.
            pass

    def _forget_main_window(self, *_args) -> None:
        self._main_window = None

    def _message_parent(self):
        """MO2's main window, so the plugin's messages open centred on it."""
        window = getattr(self, "_main_window", None)
        if window is not None:
            try:
                # minimized, it would hide a box it owns (a message after the
                # game closed): such a box gets a taskbar button of its own
                if window.isMinimized():
                    return None
                return window
            except RuntimeError:
                # a window Qt has already deleted can't be a parent
                self._main_window = None
        return QApplication.activeWindow()
