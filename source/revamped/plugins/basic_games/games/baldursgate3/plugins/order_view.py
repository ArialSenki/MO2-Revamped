"""BG3 Tools > Load Order: the load order the game will get, one module per
row with what's wrong with it, and a button that moves the mods others need
above them in MO2's list (only when the user asks)."""

from __future__ import annotations

import html

import mobase
from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QDialog,
    QDialogButtonBox,
    QHeaderView,
    QLabel,
    QMessageBox,
    QPushButton,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
)

from ..loadorder import _mod_label, fix_order
from ..paks import version64_text
from ..util import tr


def rows(plan) -> list[tuple[str, str, str, str]]:
    """(module, mod, version, notes) for each module, in load order."""
    notes: dict[str, list[str]] = {}

    def note(label: str, text: str) -> None:
        notes.setdefault(label, []).append(text)

    for mod, dep in plan.missing:
        note(mod, tr("needs {0}, which isn't active").replace("{0}", dep))
    for mod, dep, _ in plan.late:
        note(mod, tr("loads before {0}, which it needs").replace("{0}", dep))
    for mod, dep, need, have in plan.older:
        note(mod, tr("needs {0} {1}, found {2}").replace("{0}", dep).replace("{1}", need).replace("{2}", have))
    for mod, other in plan.conflicts:
        note(mod, tr("conflicts with {0}").replace("{0}", other))
    needs_se = set(plan.needs_se)
    out = []
    for module in plan.base:
        out.append((module.get("name", ""), tr("the game"), version64_text(int(module.get("version64") or 0)), ""))
    for module in plan.modules:
        label = _mod_label(module)
        extra = list(notes.get(label, []))
        if module.get("mod") in needs_se:
            extra.append(tr("needs the Script Extender"))
        out.append(
            (
                module.get("name", ""),
                module.get("mod", ""),
                version64_text(int(module.get("version64") or 0)),
                "; ".join(extra),
            )
        )
    for module in plan.external:
        out.append(
            (module.get("name", ""), tr("outside MO2"), version64_text(int(module.get("version64") or 0)), "")
        )
    return out


class LoadOrderDialog(QDialog):
    def __init__(self, game, organizer: mobase.IOrganizer, parent=None):
        super().__init__(parent)
        self._game, self._organizer = game, organizer
        self.setWindowTitle(tr("Baldur's Gate 3 load order"))
        self.resize(900, 600)
        layout = QVBoxLayout(self)
        self._summary = QLabel()
        self._summary.setWordWrap(True)
        layout.addWidget(self._summary)
        self._table = QTableWidget(0, 4)
        self._table.setHorizontalHeaderLabels([tr("Module"), tr("Mod"), tr("Version"), tr("Problems")])
        self._table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self._table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        header = self._table.horizontalHeader()
        header.setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
        layout.addWidget(self._table)
        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Close)
        self._fix = QPushButton(tr("Move needed mods up"))
        self._fix.setToolTip(
            tr("Moves each mod that others need just above them in the mod list, so it loads first.")
        )
        buttons.addButton(self._fix, QDialogButtonBox.ButtonRole.ActionRole)
        self._fix.clicked.connect(self._move_up)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)
        self._refresh()

    def _refresh(self) -> None:
        launch = self._game.launch()
        self._plan = launch.quick_plan()
        reading = launch.read_in_background()
        plan = self._plan
        if plan is None:
            self._summary.setText(tr("The load order couldn't be worked out; see MO2's log."))
            self._fix.setEnabled(False)
            return
        data = rows(plan)
        self._table.setRowCount(len(data))
        for r, values in enumerate(data):
            for c, value in enumerate(values):
                item = QTableWidgetItem(value)
                if c == 3 and value:
                    item.setForeground(Qt.GlobalColor.darkYellow)
                self._table.setItem(r, c, item)
        self._table.resizeColumnsToContents()
        problems = sum(1 for row in data if row[3])
        text = tr("{0} modules in the order the game loads them (top first); {1} with problems.")
        text = text.replace("{0}", str(len(data))).replace("{1}", str(problems))
        if reading:
            text += " " + tr("Some new paks are still being read: open this again in a moment.")
        self._summary.setText(text)
        self._fix.setEnabled(bool(plan.late_mods))

    def _move_up(self) -> None:
        pairs = list(self._plan.late_mods)
        lines = "".join(
            f"<br>• {html.escape(dep)} → {html.escape(tr('above'))} {html.escape(mod)}" for mod, dep in pairs[:15]
        )
        answer = QMessageBox.question(
            self,
            tr("Move needed mods up"),
            tr("These mods will move up in the mod list, each just above the mod that needs it:")
            + lines,
        )
        if answer != QMessageBox.StandardButton.Yes:
            return
        mod_list = self._organizer.modList()
        fix_order(pairs, mod_list.priority, mod_list.setPriority)
        self._refresh()
