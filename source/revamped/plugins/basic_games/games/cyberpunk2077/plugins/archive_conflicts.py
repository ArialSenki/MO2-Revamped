"""Cyberpunk 2077: a tool that shows which .archive wins where two hold the
same game files (MO2's own conflict view only sees whole files)."""

from __future__ import annotations

from pathlib import Path

import mobase
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QIcon
from PyQt6.QtWidgets import (
    QApplication,
    QDialog,
    QDialogButtonBox,
    QHeaderView,
    QLabel,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from ..archives import Archive, ArchiveError, conflicts, read_hashes
from ..util import log_warning, tr

GAME_NAME = "Cyberpunk 2077"
ARCHIVE_FOLDER = "archive/pc/mod"


def active_archives(organizer: mobase.IOrganizer) -> tuple[list[Archive], list[str]]:
    """The archives the game sees in archive/pc/mod (each name's winning
    file), and the lines of the modlist.txt it sees."""
    archives: list[Archive] = []
    for real in organizer.findFiles(ARCHIVE_FOLDER, "*.archive"):
        path = Path(real)
        origins = organizer.getFileOrigins(f"{ARCHIVE_FOLDER}/{path.name}")
        origin = origins[0] if origins else ""
        archive = Archive(path.name, path, "" if origin == "data" else origin)
        try:
            archive.hashes = set(read_hashes(path))
        except (OSError, ArchiveError) as error:
            archive.error = str(error)
        archives.append(archive)
    modlist: list[str] = []
    found = organizer.findFiles(ARCHIVE_FOLDER, "modlist.txt")
    if found:
        try:
            modlist = Path(found[0]).read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            pass
    return archives, modlist


class ArchiveConflictsTool(mobase.IPluginTool):
    def __init__(self) -> None:
        super().__init__()
        self._parent: QWidget | None = None

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        return True

    def name(self) -> str:
        return "Cyberpunk 2077 Archive Conflicts"

    def localizedName(self) -> str:
        return tr("Cyberpunk 2077 Archive Conflicts")

    def author(self) -> str:
        return "ArialSenki"

    def description(self) -> str:
        return tr("Shows which .archive wins where two mods' archives hold the same game files.")

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(1, 0, 0, mobase.ReleaseType.FINAL)

    def requirements(self) -> list[mobase.IPluginRequirement]:
        return [mobase.PluginRequirementFactory.gameDependency(GAME_NAME)]

    def settings(self) -> list[mobase.PluginSetting]:
        return []

    def displayName(self) -> str:
        return tr("Archive conflicts")

    def tooltip(self) -> str:
        return self.description()

    def icon(self) -> QIcon:
        return QIcon(":/MO/gui/content/bsa")

    def setParentWidget(self, widget: QWidget) -> None:
        self._parent = widget

    def display(self) -> None:
        QApplication.setOverrideCursor(Qt.CursorShape.WaitCursor)
        try:
            archives, modlist = active_archives(self._organizer)
            found = conflicts([a for a in archives if not a.error], modlist)
        except Exception as error:  # noqa: BLE001 - never break MO2's tool menu
            log_warning(f"Cyberpunk 2077: archive conflicts not read: {error}")
            archives, modlist, found = [], [], []
        finally:
            QApplication.restoreOverrideCursor()
        ConflictsDialog(archives, modlist, found, self._parent).exec()


class ConflictsDialog(QDialog):
    def __init__(self, archives, modlist, found, parent=None) -> None:
        super().__init__(parent)
        self.setWindowTitle(tr("Archive conflicts"))
        self.resize(1100, 600)
        layout = QVBoxLayout(self)
        unreadable = [a for a in archives if a.error]
        if modlist:
            order = tr("The game loads the archives listed in modlist.txt first, in its order, then the rest alphabetically.")
        else:
            order = tr("Without modlist.txt the game loads the archives alphabetically by file name.")
        text = tr("{0} archives, {1} pairs share game files. Where two do, the archive loaded first wins. {2}").format(
            len(archives), len(found), order
        )
        if unreadable:
            text += " " + tr("{0} archives could not be read.").format(len(unreadable))
        summary = QLabel(text)
        summary.setWordWrap(True)
        layout.addWidget(summary)

        tree = QTreeWidget()
        tree.setHeaderLabels([tr("Files"), tr("Wins"), tr("Mod"), tr("Loses"), tr("Mod")])
        tree.setRootIsDecorated(False)
        tree.setUniformRowHeights(True)
        tree.setAlternatingRowColors(True)
        for conflict in found:
            item = QTreeWidgetItem(
                [
                    "",
                    conflict.winner.name,
                    conflict.winner.origin or tr("game folder"),
                    conflict.loser.name,
                    conflict.loser.origin or tr("game folder"),
                ]
            )
            item.setData(0, Qt.ItemDataRole.DisplayRole, conflict.files)
            item.setTextAlignment(0, Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
            for column in range(1, 5):
                item.setToolTip(column, item.text(column))
            tree.addTopLevelItem(item)
        tree.setSortingEnabled(True)
        tree.sortByColumn(0, Qt.SortOrder.DescendingOrder)
        header = tree.header()
        header.setStretchLastSection(True)
        header.setSectionResizeMode(0, QHeaderView.ResizeMode.ResizeToContents)
        # long archive and mod names: a column never takes more than this
        widest = 260
        for column in range(1, 4):
            header.setSectionResizeMode(column, QHeaderView.ResizeMode.Interactive)
            tree.resizeColumnToContents(column)
            tree.setColumnWidth(column, min(tree.columnWidth(column), widest))
        layout.addWidget(tree)

        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Close)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)
