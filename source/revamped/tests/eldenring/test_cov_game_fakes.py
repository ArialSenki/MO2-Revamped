"""Shared stand-ins for the test_cov_game_* tests: a message box that answers
by itself, a QTimer that only records, and a mod with plugin settings. No
tests here."""

from __future__ import annotations

import contextlib
import tempfile
from pathlib import Path
from unittest import mock

from PyQt6.QtWidgets import QMessageBox

from fake_organizer import FakeMod, FakeModList, FakeOrganizer


class FakeButton:
    def __init__(self, text=""):
        self.text = text


class FakeBox:
    """QMessageBox that never shows. `answer` picks the clicked button by its
    index in the order they were added (None: closed with no button);
    `check` ticks the box's checkbox; `shown` collects every box and static
    message as (kind, title, text)."""

    Icon = QMessageBox.Icon
    ButtonRole = QMessageBox.ButtonRole
    StandardButton = QMessageBox.StandardButton
    answer: int | None = 0
    check = False
    shown: list = []

    def __init__(self, parent=None):
        self._buttons: list = []
        self._checkbox = None
        self._clicked = None
        self.title = self.text = self.informative = ""

    @classmethod
    def reset(cls, answer: int | None = 0, check: bool = False) -> None:
        cls.answer, cls.check, cls.shown = answer, check, []

    def setIcon(self, _icon):
        pass

    def setWindowTitle(self, title):
        self.title = title

    def setText(self, text):
        self.text = text

    def setInformativeText(self, text):
        self.informative = text

    def setCheckBox(self, checkbox):
        self._checkbox = checkbox

    def addButton(self, *args):
        button = FakeButton(args[0] if args else "")
        self._buttons.append(button)
        return button

    def setDefaultButton(self, _button):
        pass

    def setWindowModality(self, _modality):
        pass

    def setAttribute(self, *_args):
        pass

    def exec(self):
        if self._checkbox is not None:
            self._checkbox.setChecked(type(self).check)
        answer = type(self).answer
        self._clicked = self._buttons[answer] if answer is not None and answer < len(self._buttons) else None
        type(self).shown.append(("box", self.title, self.text + "\n" + self.informative))
        return 0

    def show(self):
        type(self).shown.append(("show", self.title, self.text))

    def close(self):
        pass

    def deleteLater(self):
        pass

    def clickedButton(self):
        return self._clicked

    @classmethod
    def _static(cls, kind):
        def show(_parent, title, text, *_rest):
            cls.shown.append((kind, title, text))
            return QMessageBox.StandardButton.Ok

        return show

    @classmethod
    def warning(cls, parent, title, text, *rest):
        return cls._static("warning")(parent, title, text, *rest)

    @classmethod
    def critical(cls, parent, title, text, *rest):
        return cls._static("critical")(parent, title, text, *rest)

    @classmethod
    def information(cls, parent, title, text, *rest):
        return cls._static("information")(parent, title, text, *rest)


class FakeTimer:
    """QTimer.singleShot that keeps the calls instead of running them."""

    calls: list = []

    class _Signal:
        def __init__(self):
            self.slots = []

        def connect(self, slot):
            self.slots.append(slot)

    def __init__(self, *_parent):
        self.timeout = self._Signal()
        self.interval = None
        self.active = False

    def setInterval(self, interval):
        self.interval = interval

    def start(self):
        self.active = True

    def stop(self):
        self.active = False

    @classmethod
    def singleShot(cls, interval, callback):
        cls.calls.append((interval, callback))

    @classmethod
    def run_all(cls) -> None:
        calls, cls.calls = cls.calls, []
        for _interval, callback in calls:
            callback()


@contextlib.contextmanager
def quiet_ui(*modules, answer: int | None = 0, check: bool = False):
    """FakeBox and FakeTimer in place of the modules' QMessageBox and QTimer."""
    FakeBox.reset(answer, check)
    FakeTimer.calls = []
    with contextlib.ExitStack() as stack:
        for module in modules:
            if hasattr(module, "QMessageBox"):
                stack.enter_context(mock.patch.object(module, "QMessageBox", FakeBox))
            if hasattr(module, "QTimer"):
                stack.enter_context(mock.patch.object(module, "QTimer", FakeTimer))
        yield FakeBox


@contextlib.contextmanager
def no_game_running(game, launcher: str | None = None):
    """The game closed (and an optional launcher open) whatever runs on this PC."""
    cls = type(game)
    with mock.patch.object(cls, "_elden_ring_process_state", return_value=False), \
            mock.patch.object(cls, "_open_launcher", return_value=launcher):
        yield


@contextlib.contextmanager
def private_temp():
    """tempfile.gettempdir() is a new folder: bridge logs and copies land there,
    never in the user's own temporary folder."""
    folder = Path(tempfile.mkdtemp(prefix="er-tmp-"))
    with mock.patch.object(tempfile, "tempdir", str(folder)):
        yield folder


class SettingsMod(FakeMod):
    """A FakeMod whose archive installer settings can be set."""

    def __init__(self, name, path, settings=None, tree_error=None):
        super().__init__(name, path)
        self._settings = settings or {}
        self._tree_error = tree_error

    def pluginSettings(self, _plugin):
        return dict(self._settings)

    def fileTree(self):
        if self._tree_error is not None:
            raise self._tree_error
        return super().fileTree()


def use_mod_settings(organizer: FakeOrganizer, settings: dict[str, dict], tree_errors=None):
    """Makes getMod() of the fake organizer's mod list return SettingsMod."""
    tree_errors = tree_errors or {}

    def get_mod(self, name):
        if name not in self._organizer.mod_order:
            return None
        return SettingsMod(name, self._organizer.mods / name, settings.get(name), tree_errors.get(name))

    return mock.patch.object(FakeModList, "getMod", get_mod)
