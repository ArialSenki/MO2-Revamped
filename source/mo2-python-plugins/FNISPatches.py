# MO2 Revamped: FNIS's patches, rewritten for Revamped. It keeps the plugin
# name and the files of Mod Organizer 2's FNIS Patches Tool by AnyOldName3
# (GNU GPL version 3): FNIS's own patch list next to GenerateFNISforUsers.exe,
# and MyPatches.txt, one patch ID per line, for the patches that are on.

from __future__ import annotations

import os
import tempfile

from PyQt6.QtCore import QEvent, QObject, Qt
from PyQt6.QtGui import QIcon
from PyQt6.QtWidgets import QLineEdit, QListWidget, QListWidgetItem, QMessageBox, QWidget

import mobase

import revamped_fluent as fluent
from FNISTool import TOOL_NAME, FnisSettings, setup, tr

PATCH_LISTS = {
    "Skyrim": "PatchList.txt",
    "Skyrim Special Edition": "PatchListSE.txt",
    "Skyrim VR": "PatchListVR.txt",
}
MY_PATCHES = "MyPatches.txt"


class Patch:
    """One line of FNIS's patch list: id#hidden#bones#behaviors#text[#check]."""

    def __init__(self, fields: list[str]):
        self.id = fields[0].strip()
        self.hidden = fields[1].strip() == "1"
        self.text = fields[4].strip()


def read_patch_list(path: str) -> list[Patch]:
    patches = []
    # FNIS writes UTF-8 with a byte order mark; the first line is a header
    with open(path, encoding="utf-8-sig", errors="replace") as file:
        next(file, None)
        for line in file:
            line = line.rstrip("\r\n")
            if not line.strip() or line.startswith("'"):
                continue
            fields = line.split("#")
            if len(fields) >= 5 and fields[0].strip():
                patches.append(Patch(fields))
    return patches


def read_enabled(path: str) -> list[str]:
    if not os.path.isfile(path):
        return []
    with open(path, encoding="utf-8-sig", errors="replace") as file:
        return [line.strip() for line in file if line.strip()]


def write_enabled(path: str, ids: list[str]) -> None:
    """Writes MyPatches.txt in one step: a failure leaves the old file whole."""
    folder = os.path.dirname(path)
    handle, temporary = tempfile.mkstemp(prefix="MyPatches.", suffix=".tmp", dir=folder)
    try:
        with os.fdopen(handle, "w", encoding="utf-8", newline="\n") as file:
            for patch_id in ids:
                file.write(patch_id + "\n")
        os.replace(temporary, path)
    except BaseException:
        try:
            os.remove(temporary)
        except OSError:
            pass
        raise


class _KeepEnter(QObject):
    """Enter in the search box doesn't press Save and close the window."""

    def eventFilter(self, watched: QObject, event: QEvent) -> bool:
        return event.type() == QEvent.Type.KeyPress and event.key() in (Qt.Key.Key_Return, Qt.Key.Key_Enter)


class PatchesDialog(fluent.ContentDialog):

    def __init__(self, parent: QWidget | None, patches: list[Patch], enabled: set[str]):
        super().__init__(
            parent, "FnisPatchesDialog", tr("FNIS patches"),
            tr("The patches FNIS applies when it runs. FNIS turns some of them on or off by "
               "itself, so its own list can differ from this one."),
        )
        self.setMinimumSize(560, 520)
        self._search = QLineEdit(self)
        self._search.setPlaceholderText(tr("Search patches"))
        self._search.setClearButtonEnabled(True)
        self._search.setAccessibleName(tr("Search patches"))
        self._search.installEventFilter(_KeepEnter(self._search))
        self.body.addWidget(self._search)

        self._list = QListWidget(self)
        self._list.setAccessibleName(tr("Patches"))
        self._list.setUniformItemSizes(True)
        self._list.setVerticalScrollMode(QListWidget.ScrollMode.ScrollPerPixel)
        enabled = {patch_id.casefold() for patch_id in enabled}
        for patch in patches:
            if patch.hidden:
                continue
            item = QListWidgetItem(patch.text, self._list)
            item.setToolTip(patch.id)
            item.setFlags(item.flags() | Qt.ItemFlag.ItemIsUserCheckable)
            item.setData(Qt.ItemDataRole.UserRole, patch.id)
            item.setCheckState(Qt.CheckState.Checked if patch.id.casefold() in enabled else Qt.CheckState.Unchecked)
        self.body.addWidget(self._list, 1)

        self._count = fluent.note("", self)
        self.body.addWidget(self._count)

        # not the default: Enter in the list mustn't save and close
        save = self.addButton(tr("Save"), True, default=False)
        cancel = self.addButton(tr("Cancel"))
        save.clicked.connect(self.accept)
        cancel.clicked.connect(self.reject)
        self._search.textChanged.connect(self._filter)
        self._list.itemChanged.connect(self._update_count)
        self._update_count()
        if self._list.count() == 0:
            self._count.setText(tr("FNIS's patch list has no patches to choose."))
            save.setEnabled(False)

    def _filter(self, text: str) -> None:
        needle = text.strip().casefold()
        for row in range(self._list.count()):
            item = self._list.item(row)
            haystack = f"{item.text()} {item.data(Qt.ItemDataRole.UserRole)}".casefold()
            item.setHidden(bool(needle) and needle not in haystack)

    def _update_count(self) -> None:
        on = sum(self._list.item(row).checkState() == Qt.CheckState.Checked
                 for row in range(self._list.count()))
        self._count.setText(tr("{on} of {total} patches on").format(on=on, total=self._list.count()))

    def choices(self) -> dict[str, bool]:
        return {self._list.item(row).data(Qt.ItemDataRole.UserRole):
                self._list.item(row).checkState() == Qt.CheckState.Checked
                for row in range(self._list.count())}


class FNISPatches(mobase.IPluginTool):

    def __init__(self):
        super().__init__()
        self._organizer: mobase.IOrganizer | None = None
        self._parent: QWidget | None = None

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        return True

    def name(self) -> str:
        return "FNIS Patches Tool"

    def localizedName(self) -> str:
        return tr("FNIS Patches Tool")

    def author(self) -> str:
        return "AnyOldName3, MO2 Revamped"

    def description(self) -> str:
        return tr("Chooses the patches FNIS applies.")

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(2, 0, 0, mobase.ReleaseType.FINAL)

    def master(self) -> str:
        return TOOL_NAME

    def settings(self) -> list:
        return []

    def displayName(self) -> str:
        return tr("FNIS/Choose FNIS patches")

    def tooltip(self) -> str:
        return self.description()

    def icon(self) -> QIcon:
        return QIcon()

    def setParentWidget(self, widget: QWidget) -> None:
        self._parent = widget

    def _warn(self, text: str) -> None:
        QMessageBox.warning(self._parent, tr("FNIS patches"), text)

    def display(self) -> None:
        settings = FnisSettings(self._organizer)
        if settings.executable_problem():
            answer = QMessageBox.question(
                self._parent, tr("FNIS is not set up"),
                tr("FNIS isn't set up yet, so MO2 can't read its patches. Set it up now?"),
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.Cancel,
            )
            if answer != QMessageBox.StandardButton.Yes:
                return
            settings = setup(self._organizer, self._parent, False)
            if settings is None or settings.executable_problem():
                return

        folder = os.path.dirname(settings.executable)
        list_path = os.path.join(folder, PATCH_LISTS.get(self._organizer.managedGame().gameName(),
                                                         "PatchListSE.txt"))
        my_patches = os.path.join(folder, MY_PATCHES)
        try:
            patches = read_patch_list(list_path)
        except OSError as error:
            self._warn(tr("FNIS's patch list could not be read:\n{path}\n\n{error}").format(path=list_path, error=error))
            return
        try:
            enabled = read_enabled(my_patches)
        except OSError as error:
            self._warn(tr("The patches that are on could not be read:\n{path}\n\n{error}").format(
                path=my_patches, error=error))
            return

        dialog = PatchesDialog(self._parent, patches, set(enabled))
        if dialog.exec() != PatchesDialog.DialogCode.Accepted:
            return

        choices = dialog.choices()
        shown = {patch_id.casefold() for patch_id in choices}
        # patches this window doesn't show (hidden ones, or ones no longer in
        # FNIS's list) keep the state they had
        kept = [patch_id for patch_id in enabled if patch_id.casefold() not in shown]
        chosen = [patch_id for patch_id, on in choices.items() if on]
        try:
            write_enabled(my_patches, kept + chosen)
        except OSError as error:
            self._warn(tr("The patches could not be saved:\n{path}\n\n{error}").format(path=my_patches, error=error))


def createPlugin() -> mobase.IPlugin:
    return FNISPatches()
