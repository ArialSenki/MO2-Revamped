"""Manage detected native DLL order and optional exports per Elden Ring profile."""

from __future__ import annotations

import configparser
import json
import os
from pathlib import Path
import struct
import tempfile

import mobase
from PyQt6.QtCore import QCoreApplication, QEvent, QProcess, QRectF, Qt
from PyQt6.QtGui import QColor, QFont, QIcon, QPainter, QPalette, QPixmap
from PyQt6.QtWidgets import (
    QApplication,
    QMenu,
    QToolTip,
    QAbstractItemView,
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QFrame,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QTreeWidget,
    QTreeWidgetItem,
    QPushButton,
    QSplitter,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
)


# MO2 installs translations/revamped_<language>.qm; the source text is English.
_TR_CONTEXT = "EldenRingNativeProfile"
# the managed game's name, for texts that name Elden Ring; set when the tool opens
_game_name = "Elden Ring"


def _tr(text: str) -> str:
    text = QCoreApplication.translate(_TR_CONTEXT, text)
    if _game_name != "Elden Ring":
        text = text.replace("Elden Ring", _game_name).replace("eldenring.exe", "nightreign.exe")
    return text


_GLYPH_FAMILIES = ["Segoe Fluent Icons", "Segoe MDL2 Assets"]


def _glyph_font(pixels: int) -> QFont:
    font = QFont()
    font.setFamilies(_GLYPH_FAMILIES)
    font.setPixelSize(pixels)
    return font


def _glyph_icon(glyph: int, muted: bool = False) -> QIcon:
    application = QApplication.instance()
    palette = application.palette() if application is not None else None
    color = (
        palette.color(QPalette.ColorGroup.Disabled if muted else QPalette.ColorGroup.Active,
                      QPalette.ColorRole.WindowText)
        if palette is not None
        else QColor("#808080")
    )
    pixmap = QPixmap(32, 32)
    pixmap.setDevicePixelRatio(2.0)
    pixmap.fill(Qt.GlobalColor.transparent)
    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    painter.setFont(_glyph_font(16))
    painter.setPen(color)
    painter.drawText(QRectF(0, 0, 16, 16), Qt.AlignmentFlag.AlignCenter, chr(glyph))
    painter.end()
    return QIcon(pixmap)


def _make_circular_icon(parent, object_name: str, icon_path: str, size: int = 26,
                        fallback: str = "•") -> QLabel:
    icon = QLabel(parent)
    icon.setObjectName(object_name)
    icon.setAlignment(Qt.AlignmentFlag.AlignCenter)
    icon.setFixedSize(size, size)
    pixmap = QIcon(icon_path).pixmap(max(12, size // 2), max(12, size // 2))
    if pixmap.isNull():
        icon.setText(fallback)
    else:
        icon.setPixmap(pixmap)
    return icon


def _add_circular_heading(layout, parent, text: str, title_object_name: str,
                          icon_path: str, icon_object_name: str,
                          size: int = 26, fallback: str = "•") -> QLabel:
    row = QHBoxLayout()
    row.setContentsMargins(0, 0, 0, 0)
    row.setSpacing(8)
    row.addWidget(
        _make_circular_icon(parent, icon_object_name, icon_path, size, fallback),
        0,
        Qt.AlignmentFlag.AlignVCenter,
    )
    heading = QLabel(text, parent)
    heading.setObjectName(title_object_name)
    row.addWidget(heading, 1, Qt.AlignmentFlag.AlignVCenter)
    layout.addLayout(row)
    return heading


class NativeProfileTable(QTableWidget):
    """Show empty-state guidance and full-route tooltips in the profile table."""

    def __init__(self, empty_message: str, parent=None):
        super().__init__(0, 4, parent)
        # Track pointer movement so item hover affordances, including full-route
        # tooltips, work reliably in this table.
        self.setMouseTracking(True)
        self.viewport().setMouseTracking(True)
        self.viewport().installEventFilter(self)
        self._empty_state_label = QLabel(empty_message, self.viewport())
        self._empty_state_label.setObjectName("nativeTableEmptyState")
        self._empty_state_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._empty_state_label.setWordWrap(True)
        self._empty_state_label.setAttribute(
            Qt.WidgetAttribute.WA_TransparentForMouseEvents
        )
        model = self.model()
        model.rowsInserted.connect(self._update_empty_state)
        model.rowsRemoved.connect(self._update_empty_state)
        model.modelReset.connect(self._update_empty_state)
        self._update_empty_state()

    def resizeEvent(self, event) -> None:
        super().resizeEvent(event)
        if hasattr(self, "_empty_state_label"):
            self._update_empty_state()

    def _update_empty_state(self, *_args) -> None:
        self._empty_state_label.setGeometry(self.viewport().rect())
        is_empty = self.rowCount() == 0
        self._empty_state_label.setVisible(is_empty)
        if is_empty:
            self._empty_state_label.raise_()

    def eventFilter(self, watched, event) -> bool:
        if (
            watched is self.viewport()
            and event.type() == QEvent.Type.ToolTip
        ):
            item = self.itemAt(event.pos())
            if item is not None and item.column() == 2:
                tooltip = item.toolTip()
                index = self.indexFromItem(item)
                if tooltip and index.isValid():
                    QToolTip.showText(
                        event.globalPos(),
                        tooltip,
                        self.viewport(),
                        self.visualRect(index),
                    )
                    return True
        return super().eventFilter(watched, event)


class EldenRingMo2NativeProfileTool(mobase.IPluginTool):
    """Add installed mod DLLs to an ordered, profile-specific launch list."""

    ConfigFilename = "eldenring_mo2_startup.ini"
    NativeSection = "RevampedNativeProfile"
    ArchiveLayoutInstallerName = "Elden Ring Archive Layout Installer"
    InstalledNativeRouteIndexSetting = "installed_native_route_index"
    ManagedDllDirectories = {
        "dlls",
        "native",
        "natives",
        "external_dlls",
        "mo2_dlls",
        "mods",
        # Seamless Co-op keeps its DLL there; listed so it can be switched off
        # like any other DLL
        "seamlesscoop",
    }
    SeamlessDlls = {"ersc.dll", "nrsc.dll"}

    def __init__(self):
        super().__init__()
        self.__organizer = None
        self.__parent_widget = None

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self.__organizer = organizer
        return True

    def name(self) -> str:
        return "Elden Ring MO2 Native Profile"

    def localizedName(self) -> str:
        return self.name()

    def author(self) -> str:
        return "ArialSenki"

    def description(self) -> str:
        return (
            _tr("Detect installed Elden Ring native DLLs and manage their load "
            "order and optional initializer exports for each MO2 profile.")
        )

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(0, 5, 0, 80)

    def requirements(self):
        return [mobase.PluginRequirementFactory.gameDependency({"ELDEN RING", "ELDEN RING NIGHTREIGN"})]

    def settings(self) -> list[mobase.PluginSetting]:
        return []

    def enabledByDefault(self) -> bool:
        return True

    def displayName(self) -> str:
        return _tr("Native DLL Profile")

    def tooltip(self) -> str:
        return (
            _tr("Detect installed native DLLs and set their order and optional "
            "initializer for the active MO2 profile.")
        )

    def icon(self) -> QIcon:
        return QIcon()

    def setParentWidget(self, widget) -> None:
        self.__parent_widget = widget

    def display(self) -> None:
        try:
            game = self.__organizer.managedGame()
            if game is None or game.gameShortName().casefold() not in {
                "eldenring",
                "nightreign",
            }:
                return
            global _game_name
            _game_name = (
                "Nightreign"
                if game.gameShortName().casefold() == "nightreign"
                else "Elden Ring"
            )
        except Exception:
            return

        profile = self.__organizer.profile()
        if profile is None:
            self._show_notice(
                self.__parent_widget,
                _tr("No active Elden Ring profile"),
                _tr("Select an Elden Ring profile in MO2 before editing native DLL settings."),
                "warning",
            )
            return

        profile_root = Path(profile.absolutePath())
        config_path = profile_root / self.ConfigFilename
        try:
            entries = self._read_profile_entries(config_path)
            candidates = self._detect_native_dlls()
        except (
            OSError,
            UnicodeError,
            configparser.Error,
            RuntimeError,
            TypeError,
            ValueError,
        ) as error:
            self._show_notice(
                self.__parent_widget,
                _tr("Could not read native DLL profile"),
                _tr("MO2 could not inspect the active profile or installed mods.\n\n{error}").format(error=error),
                "error",
            )
            return

        dialog = QDialog(self.__parent_widget)
        dialog.setObjectName("EldenRingNativeProfileDialog")
        dialog.setWindowTitle(_tr("Elden Ring native DLL profile"))
        dialog.setMinimumSize(900, 560)
        dialog.resize(1080, 680)
        dialog.setSizeGripEnabled(True)
        layout = QVBoxLayout(dialog)
        layout.setContentsMargins(16, 16, 16, 14)
        layout.setSpacing(9)

        # a Windows 11 page title, with the profile and its counts beside it
        title_row = QHBoxLayout()
        title_row.setSpacing(12)
        titles = QVBoxLayout()
        titles.setSpacing(2)
        page_title = QLabel(_tr("Native DLL profile"), dialog)
        page_title.setProperty("wizardPageTitle", True)
        subtitle = QLabel(
            _tr("Every native DLL of your active mods, in load order, Seamless Co-op included. "
            "Switch one off to keep it from loading without turning its mod off, or move it "
            "to change the order. Without changes, MO2 loads them all in the mod list's order."),
            dialog,
        )
        subtitle.setProperty("wizardPageText", True)
        subtitle.setWordWrap(True)
        titles.addWidget(page_title)
        titles.addWidget(subtitle)
        title_row.addLayout(titles, 1)
        profile_box = QVBoxLayout()
        profile_box.setSpacing(2)
        profile_name = QLabel(_tr("MO2 profile: {profile}").format(profile=profile.name()), dialog)
        profile_name.setObjectName("fluentSettingTitle")
        profile_name.setAlignment(Qt.AlignmentFlag.AlignRight)
        profile_name.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        profile_metrics = QLabel(dialog)
        profile_metrics.setObjectName("fluentSettingDescription")
        profile_metrics.setAlignment(Qt.AlignmentFlag.AlignRight)
        profile_box.addWidget(profile_name)
        profile_box.addWidget(profile_metrics)
        title_row.addLayout(profile_box)
        layout.addLayout(title_row)

        splitter = QSplitter(Qt.Orientation.Horizontal, dialog)
        splitter.setObjectName("nativeProfileSplitter")
        splitter.setChildrenCollapsible(False)
        splitter.setHandleWidth(7)
        detected_group = QFrame(splitter)
        detected_group.setObjectName("detectedDllCard")
        detected_group.setProperty("fluentCard", True)
        detected_layout = QVBoxLayout(detected_group)
        detected_layout.setContentsMargins(12, 11, 12, 12)
        detected_layout.setSpacing(7)
        detected_heading = QLabel(_tr("Detected DLLs"), detected_group)
        detected_heading.setObjectName("fluentCardTitle")
        detected_layout.addWidget(detected_heading)
        detected_note = QLabel(
            _tr("Found in enabled MO2 mods and verified against the virtual file system."),
            detected_group,
        )
        detected_note.setObjectName("fluentSettingDescription")
        detected_note.setWordWrap(True)
        detected_layout.addWidget(detected_note)
        detected_tree = QTreeWidget(detected_group)
        detected_tree.setObjectName("detectedDllTree")
        detected_tree.setHeaderHidden(True)
        detected_tree.setRootIsDecorated(True)
        detected_tree.setAlternatingRowColors(True)
        detected_tree.setUniformRowHeights(True)
        detected_tree.setSelectionMode(
            QAbstractItemView.SelectionMode.ExtendedSelection
        )
        detected_layout.addWidget(detected_tree, 1)
        detected_status = QLabel(detected_group)
        detected_status.setObjectName("detectedDllStatus")
        detected_status.setWordWrap(True)
        detected_layout.addWidget(detected_status)
        add_button = QPushButton(_glyph_icon(0xE710), _tr("Add selected DLLs"), detected_group)
        add_button.setObjectName("addDetectedDllsButton")
        add_button.setEnabled(False)
        add_button.setToolTip(
            _tr("Add the selected DLL routes to this profile. Existing entries are skipped.")
        )
        detected_layout.addWidget(add_button)

        profile_group = QFrame(splitter)
        profile_group.setObjectName("nativeLoadOrderCard")
        profile_group.setProperty("fluentCard", True)
        profile_layout = QVBoxLayout(profile_group)
        profile_layout.setContentsMargins(12, 11, 12, 12)
        profile_layout.setSpacing(7)
        load_order_heading = QLabel(_tr("Load order"), profile_group)
        load_order_heading.setObjectName("fluentCardTitle")
        profile_layout.addWidget(load_order_heading)
        profile_note = QLabel(
            _tr("DLLs that are switched on load in the order shown. A new mod's DLLs show up "
            "here switched on. Initializers are optional named exports, for DLLs that need "
            "one."),
            profile_group,
        )
        profile_note.setObjectName("fluentSettingDescription")
        profile_note.setWordWrap(True)
        profile_layout.addWidget(profile_note)
        empty_table_message = _tr("None of your active mods has a native DLL.")
        table = NativeProfileTable(empty_table_message, profile_group)
        table.setObjectName("nativeLoadOrderTable")
        table.setHorizontalHeaderLabels(
            [_tr("Enabled"), _tr("MO2 mod"), _tr("Installed DLL route"), _tr("Initializer export")]
        )
        table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        table.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
        table.setEditTriggers(
            QAbstractItemView.EditTrigger.DoubleClicked
            | QAbstractItemView.EditTrigger.EditKeyPressed
        )
        table.verticalHeader().setVisible(False)
        table.setAlternatingRowColors(True)
        table.setShowGrid(False)
        table.setWordWrap(False)
        table.horizontalHeader().setSectionResizeMode(
            0, QHeaderView.ResizeMode.ResizeToContents
        )
        table.horizontalHeader().setSectionResizeMode(
            1, QHeaderView.ResizeMode.ResizeToContents
        )
        table.horizontalHeader().setSectionResizeMode(
            2, QHeaderView.ResizeMode.Stretch
        )
        table.horizontalHeader().setSectionResizeMode(
            3, QHeaderView.ResizeMode.Stretch
        )
        profile_layout.addWidget(table, 1)

        row_buttons = QHBoxLayout()
        row_buttons.setSpacing(7)
        remove_button = QPushButton(_glyph_icon(0xE72C), _tr("Reset"), profile_group)
        remove_button.setObjectName("resetNativeProfileButton")
        remove_button.setToolTip(
            _tr("Switch every DLL on again, in the mod list's order. Initializers are kept.")
        )
        up_button = QPushButton(_glyph_icon(0xE74A), _tr("Move up"), profile_group)
        up_button.setObjectName("moveNativeEntryUpButton")
        up_button.setEnabled(False)
        down_button = QPushButton(_glyph_icon(0xE74B), _tr("Move down"), profile_group)
        down_button.setObjectName("moveNativeEntryDownButton")
        down_button.setEnabled(False)
        exports_button = QPushButton(_glyph_icon(0xE721), _tr("Detect exports…"), profile_group)
        exports_button.setObjectName("detectNativeExportsButton")
        exports_button.setEnabled(False)
        exports_button.setToolTip(
            _tr("Read the selected DLL's named exports and choose an initializer.")
        )
        row_buttons.addWidget(remove_button)
        row_buttons.addWidget(up_button)
        row_buttons.addWidget(down_button)
        row_buttons.addStretch(1)
        row_buttons.addWidget(exports_button)
        profile_layout.addLayout(row_buttons)

        detected_group.hide()
        splitter.addWidget(profile_group)
        layout.addWidget(splitter, 1)

        footer_layout = QHBoxLayout()
        footer_layout.setSpacing(12)
        footer = QLabel(
            _tr("Entries are saved in this profile's eldenring_mo2_startup.ini. "
            "Startup, performance, and cleanup settings in that file are preserved."),
            dialog,
        )
        footer.setObjectName("fluentSettingDescription")
        footer.setWordWrap(True)
        footer_layout.addWidget(footer, 1)
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Save
            | QDialogButtonBox.StandardButton.Cancel,
            parent=dialog,
        )
        buttons.setObjectName("nativeDialogButtons")
        save_button = buttons.button(QDialogButtonBox.StandardButton.Save)
        cancel_button = buttons.button(QDialogButtonBox.StandardButton.Cancel)
        if save_button is not None:
            save_button.setObjectName("nativeSaveButton")
            save_button.setDefault(True)
        if cancel_button is not None:
            cancel_button.setObjectName("nativeCancelButton")
        footer_layout.addWidget(buttons)
        layout.addLayout(footer_layout)

        # one list: the profile's entries for active mods in their order, then
        # every other DLL of the active mods, switched on. Entries of mods that
        # are off now stay in the file for when they come back.
        active_keys = {
            (str(candidate["mod"]).casefold(), str(candidate["path"]).casefold())
            for candidate in candidates
        }
        active_mods = {str(candidate["mod"]).casefold() for candidate in candidates}
        shown = [entry for entry in entries if str(entry.get("mod", "")).casefold() in active_mods]
        table.stale_entries = [
            entry for entry in entries if str(entry.get("mod", "")).casefold() not in active_mods
        ]
        listed = {(str(e.get("mod", "")).casefold(), str(e.get("path", "")).casefold()) for e in shown}
        default_entries = [
            {"enabled": True, "mod": candidate["mod"], "path": candidate["path"], "initializer": "", "optional": False}
            for candidate in candidates
        ]
        entries = shown + [
            entry for entry in default_entries
            if (str(entry["mod"]).casefold(), str(entry["path"]).casefold()) not in listed
        ]
        del active_keys
        self._populate_detected_tree(detected_tree, candidates)
        mod_count = len({str(candidate["mod"]).casefold() for candidate in candidates})
        detected_status.setText(
            _tr("{routes} routes · {mods} enabled mod(s).").format(routes=len(candidates), mods=mod_count)
            if candidates
            else _tr("No native DLL routes found in enabled mods.")
        )
        self._populate_profile_table(table, entries)

        def update_controls(*_args) -> None:
            selected_row = table.currentRow()
            valid_row = 0 <= selected_row < table.rowCount()
            up_button.setEnabled(valid_row and selected_row > 0)
            down_button.setEnabled(
                valid_row and selected_row < table.rowCount() - 1
            )
            exports_button.setEnabled(valid_row)
            add_button.setEnabled(bool(detected_tree.selectedItems()))

        def update_profile_metrics(*_args) -> None:
            current_entries = self._entries_from_table(table)
            enabled_count = sum(entry["enabled"] for entry in current_entries)
            initializer_count = sum(bool(entry["initializer"]) for entry in current_entries)
            initializers = (
                _tr("1 initializer")
                if initializer_count == 1
                else _tr("{initializers} initializers").format(initializers=initializer_count)
            )
            profile_metrics.setText(
                _tr("{configured} configured · {enabled} enabled").format(
                    configured=len(current_entries),
                    enabled=enabled_count,
                )
                + " · "
                + initializers
            )

        def add_selected(_checked=False) -> None:
            self._add_selected_candidates(detected_tree, table)
            update_controls()
            update_profile_metrics()

        def reset_profile(_checked=False) -> None:
            initializers = {
                (entry["mod"].casefold(), entry["path"].casefold()): entry["initializer"]
                for entry in self._entries_from_table(table)
            }
            self._populate_profile_table(
                table,
                [
                    dict(entry, initializer=initializers.get(
                        (str(entry["mod"]).casefold(), str(entry["path"]).casefold()), ""
                    ))
                    for entry in default_entries
                ],
            )
            update_controls()
            update_profile_metrics()

        def move_selected_up(_checked=False) -> None:
            self._move_selected_entry(table, -1)
            update_controls()

        def move_selected_down(_checked=False) -> None:
            self._move_selected_entry(table, 1)
            update_controls()

        def choose_exports(_checked=False) -> None:
            self._choose_initializer_export(table)
            update_profile_metrics()

        add_button.clicked.connect(add_selected)
        remove_button.clicked.connect(reset_profile)
        up_button.clicked.connect(move_selected_up)
        down_button.clicked.connect(move_selected_down)
        exports_button.clicked.connect(choose_exports)
        detected_tree.itemSelectionChanged.connect(update_controls)
        detected_tree.itemDoubleClicked.connect(lambda *_args: add_selected())
        # Seamless Co-op starts itself later: its place in the order does not
        # matter, and the row says so
        for row in range(table.rowCount()):
            path_item = table.item(row, 2)
            if path_item is not None and Path(path_item.text()).name.casefold() in self.SeamlessDlls:
                path_item.setToolTip(
                    _tr("Seamless Co-op starts by itself once the game runs; its place in the "
                        "order does not matter. Switch it off here or turn its mod off.")
                )

        def show_table_menu(position) -> None:
            row = table.rowAt(position.y())
            if row < 0:
                return
            table.selectRow(row)
            entry = self._entries_from_table(table)[row]
            menu = QMenu(table)
            menu.addAction(_glyph_icon(0xE74A), _tr("Move up"), move_selected_up).setEnabled(row > 0)
            menu.addAction(_glyph_icon(0xE74B), _tr("Move down"), move_selected_down).setEnabled(
                row < table.rowCount() - 1
            )
            menu.addAction(_glyph_icon(0xE721), _tr("Detect exports…"), choose_exports)
            menu.addSeparator()
            physical = self._physical_dll_path(str(entry.get("mod", "")), str(entry.get("path", "")))
            show = menu.addAction(
                _glyph_icon(0xE838), _tr("Show in folder"),
                lambda: QProcess.startDetached("explorer.exe", ["/select,", str(physical)]),
            )
            show.setEnabled(physical is not None)
            copy = menu.addAction(
                _glyph_icon(0xE8C8), _tr("Copy route"),
                lambda: QApplication.clipboard().setText(str(entry.get("path", ""))),
            )
            copy.setEnabled(bool(entry.get("path")))
            menu.exec(table.viewport().mapToGlobal(position))

        table.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        table.customContextMenuRequested.connect(show_table_menu)
        table.itemSelectionChanged.connect(update_controls)
        table.itemChanged.connect(update_profile_metrics)
        table.model().rowsInserted.connect(update_profile_metrics)
        table.model().rowsRemoved.connect(update_profile_metrics)
        update_controls()
        update_profile_metrics()
        buttons.accepted.connect(
            lambda: self._save_and_close(dialog, profile_root, table)
        )
        buttons.rejected.connect(dialog.reject)

        layout.activate()
        dialog.ensurePolished()
        dialog.exec()
        # a child of MO2's window: freed, not kept per opening
        dialog.deleteLater()

    @staticmethod
    def _show_notice(parent, title: str, message: str, kind: str = "info") -> None:
        dialog = QDialog(parent)
        dialog.setObjectName("NativeProfileNoticeDialog")
        dialog.setWindowTitle(title)
        dialog.setMinimumWidth(440)
        dialog.setMaximumWidth(640)
        layout = QVBoxLayout(dialog)
        layout.setContentsMargins(16, 16, 16, 14)
        layout.setSpacing(12)

        header = QFrame(dialog)
        header.setObjectName("nativeNoticeHeader")
        header_layout = QHBoxLayout(header)
        header_layout.setContentsMargins(12, 10, 12, 10)
        header_layout.setSpacing(10)
        icon_text = {"info": "i", "warning": "!", "error": "×", "success": "✓"}.get(
            kind, "i"
        )
        icon = QLabel(icon_text, header)
        icon.setObjectName("nativeNoticeIcon")
        icon.setProperty("noticeKind", kind)
        icon.setAlignment(Qt.AlignmentFlag.AlignCenter)
        icon.setFixedSize(28, 28)
        heading = QLabel(title, header)
        heading.setObjectName("nativeNoticeTitle")
        heading.setWordWrap(True)
        header_layout.addWidget(icon, 0, Qt.AlignmentFlag.AlignTop)
        header_layout.addWidget(heading, 1)
        layout.addWidget(header)

        body = QLabel(message, dialog)
        body.setObjectName("nativeNoticeMessage")
        body.setWordWrap(True)
        body.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        layout.addWidget(body)

        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok, parent=dialog)
        buttons.setObjectName("nativeNoticeButtons")
        ok_button = buttons.button(QDialogButtonBox.StandardButton.Ok)
        if ok_button is not None:
            ok_button.setObjectName("nativeNoticeOkButton")
            ok_button.setDefault(True)
        buttons.accepted.connect(dialog.accept)
        layout.addWidget(buttons)
        dialog.adjustSize()
        dialog.exec()
        dialog.deleteLater()

    def _read_profile_entries(self, config_path: Path) -> list[dict]:
        config = configparser.ConfigParser(interpolation=None)
        config.read(config_path, encoding="utf-8-sig")
        if not config.has_section(self.NativeSection):
            return []

        section_items = dict(config.items(self.NativeSection))
        default_mod = section_items.get("mod", "").strip()
        entries_by_index: dict[int, dict] = {}
        supported = {"native", "mod", "enabled", "optional", "initializer"}
        for key, value in section_items.items():
            if key in {"mod", "version"}:
                continue
            prefix, separator, raw_index = key.partition("_")
            if (
                not separator
                or not raw_index.isdecimal()
                or prefix not in supported
            ):
                raise ValueError(_tr("Unsupported native profile setting '{key}'.").format(key=key))
            index = int(raw_index)
            if index <= 0:
                raise ValueError(_tr("Native DLL entry numbers must be greater than zero."))
            entry = entries_by_index.setdefault(index, {})
            if prefix == "native":
                entry["path"] = value.strip()
            elif prefix == "mod":
                entry["mod"] = value.strip()
            elif prefix == "initializer":
                entry["initializer"] = value.strip()
            else:
                if value.strip().casefold() not in {"true", "false"}:
                    raise ValueError(_tr("'{key}' must be true or false.").format(key=key))
                entry[prefix] = value.strip().casefold() == "true"

        profile_entries = []
        for index in sorted(entries_by_index):
            entry = entries_by_index[index]
            mod_name = entry.get("mod") or default_mod
            route = entry.get("path", "")
            if not mod_name or not route:
                raise ValueError(
                    _tr("Native DLL entry {index} needs both a mod and a DLL path.").format(index=index)
                )
            profile_entries.append(
                {
                    "mod": mod_name,
                    "path": self._canonical_route(mod_name, route),
                    "initializer": entry.get("initializer", ""),
                    "enabled": entry.get("enabled", True),
                    "optional": entry.get("optional", False),
                }
            )
        return profile_entries

    def _canonical_route(self, mod_name: str, route: str) -> str:
        normalized = route.replace("\\", "/").strip("/")
        mod = self.__organizer.modList().getMod(mod_name)
        if mod is None:
            return normalized
        try:
            index = mod.pluginSettings(self.ArchiveLayoutInstallerName).get(
                self.InstalledNativeRouteIndexSetting, ""
            )
            if isinstance(index, str) and index.strip():
                index = json.loads(index)
            mapping = index.get("source_to_installed", {}) if isinstance(index, dict) else {}
            if isinstance(mapping, dict):
                normalized_key = normalized.casefold()
                for source, installed in mapping.items():
                    if (
                        isinstance(source, str)
                        and isinstance(installed, str)
                        and source.replace("\\", "/").strip("/").casefold()
                        == normalized_key
                    ):
                        return installed.replace("\\", "/").strip("/")
        except (OSError, RuntimeError, TypeError, ValueError, json.JSONDecodeError):
            pass
        return normalized

    def _detect_native_dlls(self) -> list[dict]:
        mod_list = self.__organizer.modList()
        candidates = []
        seen = set()
        active_priority = {
            name.casefold(): priority
            for priority, name in enumerate(mod_list.allModsByProfilePriority())
            if mod_list.state(name) & mobase.ModState.ACTIVE
        }
        for mod_name in mod_list.allModsByProfilePriority():
            if not (mod_list.state(mod_name) & mobase.ModState.ACTIVE):
                continue
            mod = mod_list.getMod(mod_name)
            if mod is None:
                continue
            try:
                mod_root = Path(mod.absolutePath()).resolve(strict=True)
            except (OSError, RuntimeError):
                continue
            if not mod_root.is_dir():
                continue

            routes = self._installed_dll_routes(mod, mod_root)
            for route in sorted(set(routes), key=str.casefold):
                route = self._normalize_native_route(route)
                if route is None:
                    continue
                physical_path = self._safe_mod_file(mod_root, route)
                if physical_path is None:
                    continue
                try:
                    origins = list(self.__organizer.getFileOrigins(route))
                except Exception:
                    continue
                # only the mod that wins the file, as the game plugin loads it:
                # listing the overridden copies too would let the profile name
                # one game path twice, which blocks the launch
                winner = max(
                    (
                        origin.casefold()
                        for origin in origins
                        if isinstance(origin, str) and origin.casefold() in active_priority
                    ),
                    key=lambda origin: active_priority[origin],
                    default=None,
                )
                if winner != mod_name.casefold():
                    continue
                key = (mod_name.casefold(), route.casefold())
                if key in seen:
                    continue
                seen.add(key)
                candidates.append(
                    {
                        "mod": mod_name,
                        "path": route,
                        "physical_path": physical_path,
                    }
                )
        return candidates

    def _installed_dll_routes(self, mod, mod_root: Path) -> list[str]:
        routes = []
        try:
            stored = mod.pluginSettings(self.ArchiveLayoutInstallerName).get(
                self.InstalledNativeRouteIndexSetting, ""
            )
            if isinstance(stored, str) and stored.strip():
                stored = json.loads(stored)
            if isinstance(stored, dict):
                dlls = stored.get("dlls", [])
                if isinstance(dlls, list):
                    routes.extend(route for route in dlls if isinstance(route, str))
        except (OSError, TypeError, ValueError, json.JSONDecodeError):
            routes = []

        seamless = [
            f"SeamlessCoop/{name}"
            for name in sorted(self.SeamlessDlls)
            if (mod_root / "SeamlessCoop" / name).is_file()
        ]
        if routes:
            return routes + [route for route in seamless if route not in routes]

        for entry in mod_root.iterdir():
            if entry.is_file() and entry.suffix.casefold() == ".dll":
                routes.append(entry.relative_to(mod_root).as_posix())
        for directory_name in sorted(self.ManagedDllDirectories - {"seamlesscoop"}):
            directory = mod_root / directory_name
            if not directory.is_dir() or directory.is_symlink():
                continue
            for current, directories, files in os.walk(directory, followlinks=False):
                current_path = Path(current)
                directories[:] = [
                    name
                    for name in directories
                    if not (current_path / name).is_symlink()
                ]
                for filename in files:
                    if Path(filename).suffix.casefold() == ".dll":
                        routes.append(
                            (current_path / filename).relative_to(mod_root).as_posix()
                        )
        return routes + seamless

    def _normalize_native_route(self, route: str) -> str | None:
        normalized = route.replace("\\", "/").strip("/")
        relative = Path(normalized)
        if (
            not normalized
            or relative.is_absolute()
            or not relative.parts
            or any(part in {"", ".", ".."} for part in relative.parts)
            or ":" in relative.parts[0]
            or relative.suffix.casefold() != ".dll"
            or (
                len(relative.parts) > 1
                and relative.parts[0].casefold() not in self.ManagedDllDirectories
            )
        ):
            return None
        return relative.as_posix()

    @staticmethod
    def _safe_mod_file(mod_root: Path, route: str) -> Path | None:
        candidate = mod_root.joinpath(*Path(route).parts)
        try:
            if candidate.is_symlink():
                return None
            resolved = candidate.resolve(strict=True)
            resolved.relative_to(mod_root)
        except (OSError, RuntimeError, ValueError):
            return None
        return resolved if resolved.is_file() else None

    @staticmethod
    def _populate_detected_tree(widget: QTreeWidget, candidates: list[dict]) -> None:
        widget.clear()
        groups: dict[str, QTreeWidgetItem] = {}
        group_names: dict[str, str] = {}
        group_counts: dict[str, int] = {}
        for candidate in candidates:
            mod_name = str(candidate["mod"])
            key = mod_name.casefold()
            group = groups.get(key)
            if group is None:
                group = QTreeWidgetItem([mod_name])
                groups[key] = group
                group_names[key] = mod_name
                group_counts[key] = 0
                widget.addTopLevelItem(group)

            child = QTreeWidgetItem(group, [str(candidate["path"])])
            child.setData(0, Qt.ItemDataRole.UserRole, candidate)
            child.setToolTip(0, str(candidate["physical_path"]))
            group_counts[key] += 1

        for key, group in groups.items():
            count = group_counts[key]
            group.setText(
                0,
                (_tr("{group} ({count} DLL)") if count == 1 else _tr("{group} ({count} DLLs)")).format(
                    group=group_names[key], count=count
                ),
            )
            group.setToolTip(
                0,
                (
                    _tr("{group} contributes {count} detected native DLL. "
                        "Select this group and choose Add selected DLLs to add all of them.")
                    if count == 1
                    else _tr("{group} contributes {count} detected native DLLs. "
                             "Select this group and choose Add selected DLLs to add all of them.")
                ).format(group=group_names[key], count=count),
            )
            group.setExpanded(True)

    @staticmethod
    def _populate_profile_table(table: QTableWidget, entries: list[dict]) -> None:
        table.blockSignals(True)
        table.setRowCount(0)
        for entry in entries:
            row = table.rowCount()
            table.insertRow(row)
            enabled_item = QTableWidgetItem("")
            enabled_item.setFlags(
                (enabled_item.flags() | Qt.ItemFlag.ItemIsUserCheckable)
                & ~Qt.ItemFlag.ItemIsEditable
            )
            enabled_item.setCheckState(
                Qt.CheckState.Checked
                if entry.get("enabled", True)
                else Qt.CheckState.Unchecked
            )
            table.setItem(row, 0, enabled_item)
            for column, key in ((1, "mod"), (2, "path"), (3, "initializer")):
                item = QTableWidgetItem(str(entry.get(key, "")))
                if column in (1, 2):
                    item.setFlags(item.flags() & ~Qt.ItemFlag.ItemIsEditable)
                if column == 2:
                    item.setToolTip(item.text())
                    item.setData(
                        Qt.ItemDataRole.UserRole,
                        bool(entry.get("optional", False)),
                    )
                table.setItem(row, column, item)
        table.blockSignals(False)

    @staticmethod
    def _entries_from_table(table: QTableWidget) -> list[dict]:
        entries = []
        for row in range(table.rowCount()):
            enabled_item = table.item(row, 0)
            mod_item = table.item(row, 1)
            path_item = table.item(row, 2)
            initializer_item = table.item(row, 3)
            if mod_item is None or path_item is None:
                continue
            entries.append(
                {
                    "enabled": (
                        enabled_item is not None
                        and enabled_item.checkState() == Qt.CheckState.Checked
                    ),
                    "mod": mod_item.text().strip(),
                    "path": path_item.text().strip().replace("\\", "/"),
                    "initializer": (
                        initializer_item.text().strip()
                        if initializer_item is not None
                        else ""
                    ),
                    "optional": (
                        bool(path_item.data(Qt.ItemDataRole.UserRole))
                        if path_item is not None
                        else False
                    ),
                }
            )
        return entries

    def _add_selected_candidates(
        self, detected_tree: QTreeWidget, table: QTableWidget
    ) -> None:
        entries = self._entries_from_table(table)
        existing = {
            (entry["mod"].casefold(), entry["path"].casefold())
            for entry in entries
        }
        added = False
        for selected_item in detected_tree.selectedItems():
            children = [
                selected_item.child(index)
                for index in range(selected_item.childCount())
            ]
            for item in children or [selected_item]:
                candidate = item.data(0, Qt.ItemDataRole.UserRole)
                if not isinstance(candidate, dict):
                    continue
                key = (candidate["mod"].casefold(), candidate["path"].casefold())
                if key in existing:
                    continue
                entries.append(
                    {
                        "enabled": True,
                        "mod": candidate["mod"],
                        "path": candidate["path"],
                        "initializer": "",
                        "optional": False,
                    }
                )
                existing.add(key)
                added = True
        if added:
            self._populate_profile_table(table, entries)
            table.selectRow(table.rowCount() - 1)

    def _remove_selected_entry(self, table: QTableWidget) -> None:
        row = table.currentRow()
        if row < 0:
            return
        entries = self._entries_from_table(table)
        entries.pop(row)
        self._populate_profile_table(table, entries)
        if entries:
            table.selectRow(min(row, len(entries) - 1))

    def _move_selected_entry(self, table: QTableWidget, offset: int) -> None:
        row = table.currentRow()
        entries = self._entries_from_table(table)
        destination = row + offset
        if row < 0 or destination < 0 or destination >= len(entries):
            return
        entries[row], entries[destination] = entries[destination], entries[row]
        self._populate_profile_table(table, entries)
        table.selectRow(destination)

    def _choose_initializer_export(self, table: QTableWidget) -> None:
        row = table.currentRow()
        parent = table.window()
        if row < 0:
            self._show_notice(
                parent,
                _tr("Select a DLL"),
                _tr("Select a profile entry before detecting exported functions."),
                "info",
            )
            return
        entries = self._entries_from_table(table)
        entry = entries[row]
        physical_path = self._physical_dll_path(entry["mod"], entry["path"])
        if physical_path is None:
            self._show_notice(
                parent,
                _tr("DLL not found"),
                _tr("MO2 could not resolve this DLL inside its installed mod folder."),
                "warning",
            )
            return
        try:
            exports = self._read_pe_exports(physical_path)
        except (OSError, ValueError, struct.error) as error:
            self._show_notice(
                parent,
                _tr("Could not read DLL exports"),
                _tr("The DLL export table could not be read. The file was not loaded.\n\n{error}").format(error=error),
                "warning",
            )
            return
        if not exports:
            self._show_notice(
                parent,
                _tr("No initializer exports detected"),
                _tr("No named functions suitable for an initializer were found in this DLL."),
                "info",
            )
            return
        current = entry.get("initializer", "")
        choices = list(exports)
        if current and current not in choices:
            choices.insert(0, current)

        dialog = QDialog(parent)
        dialog.setObjectName("NativeProfileExportDialog")
        dialog.setWindowTitle(_tr("Choose an initializer export"))
        dialog.setMinimumWidth(540)
        layout = QVBoxLayout(dialog)
        layout.setContentsMargins(16, 16, 16, 14)
        layout.setSpacing(11)

        header = QFrame(dialog)
        header.setObjectName("nativeExportHeader")
        header_layout = QVBoxLayout(header)
        header_layout.setContentsMargins(13, 10, 13, 10)
        header_layout.setSpacing(3)
        _add_circular_heading(
            header_layout,
            header,
            _tr("Choose an initializer export"),
            "nativeExportTitle",
            ":/MO/gui/mainwindow/files/executable.svg",
            "nativeExportIcon",
            30,
            "E",
        )
        description = QLabel(
            _tr("The selected function will be called by the native bridge when "
            "Elden Ring starts. The DLL itself is never loaded by this selector."),
            header,
        )
        description.setObjectName("nativeExportDescription")
        description.setWordWrap(True)
        header_layout.addWidget(description)
        layout.addWidget(header)

        route = QLabel(f"{entry['mod']}  ·  {entry['path']}", dialog)
        route.setObjectName("nativeExportRoute")
        route.setWordWrap(True)
        route.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        layout.addWidget(route)

        export_label = QLabel(_tr("Available named exports"), dialog)
        export_label.setObjectName("nativeExportCaption")
        layout.addWidget(export_label)
        export_combo = QComboBox(dialog)
        export_combo.setObjectName("nativeExportCombo")
        export_combo.addItems(choices)
        if current in choices:
            export_combo.setCurrentIndex(choices.index(current))
        layout.addWidget(export_combo)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok
            | QDialogButtonBox.StandardButton.Cancel,
            parent=dialog,
        )
        buttons.setObjectName("nativeExportButtons")
        use_button = buttons.button(QDialogButtonBox.StandardButton.Ok)
        cancel_button = buttons.button(QDialogButtonBox.StandardButton.Cancel)
        if use_button is not None:
            use_button.setText(_tr("Use export"))
            use_button.setObjectName("nativeExportUseButton")
            use_button.setDefault(True)
        if cancel_button is not None:
            cancel_button.setObjectName("nativeExportCancelButton")
        buttons.accepted.connect(dialog.accept)
        buttons.rejected.connect(dialog.reject)
        layout.addWidget(buttons)

        accepted = dialog.exec() == QDialog.DialogCode.Accepted
        # freed once this call returns to MO2's event loop; the choice is read first
        dialog.deleteLater()
        if accepted:
            entries[row]["initializer"] = export_combo.currentText().strip()
            self._populate_profile_table(table, entries)
            table.selectRow(row)

    def _physical_dll_path(self, mod_name: str, route: str) -> Path | None:
        mod = self.__organizer.modList().getMod(mod_name)
        if mod is None:
            return None
        try:
            root = Path(mod.absolutePath()).resolve(strict=True)
        except (OSError, RuntimeError):
            return None

        normalized = route.replace("\\", "/").strip("/")
        candidates = [normalized]
        relative = Path(normalized)
        if relative.parts and relative.parts[0].casefold() in {"native", "natives"}:
            if len(relative.parts) > 1:
                tail = Path(*relative.parts[1:]).as_posix()
                candidates.extend((f"DLLs/{normalized}", f"DLLs/{tail}"))
        elif normalized:
            candidates.append(f"DLLs/{normalized}")
        for candidate in dict.fromkeys(candidates):
            resolved = self._safe_mod_file(root, candidate)
            if resolved is not None and resolved.suffix.casefold() == ".dll":
                return resolved
        return None

    @staticmethod
    def _read_pe_exports(path: Path) -> list[str]:
        file_size = path.stat().st_size
        with path.open("rb") as stream:
            def read_at(offset: int, size: int) -> bytes:
                if offset < 0 or size < 0 or size > 4 * 1024 * 1024:
                    raise ValueError(_tr("The DLL export table contains an invalid size or offset."))
                if offset + size > file_size:
                    raise ValueError(_tr("The DLL export table points outside the file."))
                stream.seek(offset)
                data = stream.read(size)
                if len(data) != size:
                    raise ValueError(_tr("The DLL ended while reading its export table."))
                return data

            dos_header = read_at(0, 64)
            if dos_header[:2] != b"MZ":
                raise ValueError(_tr("This file does not have a Windows PE header."))
            pe_offset = struct.unpack_from("<I", dos_header, 0x3C)[0]
            coff_header = read_at(pe_offset, 24)
            if coff_header[:4] != b"PE\0\0":
                raise ValueError(_tr("This file does not have a valid PE signature."))
            section_count = struct.unpack_from("<H", coff_header, 6)[0]
            optional_size = struct.unpack_from("<H", coff_header, 20)[0]
            if not 0 < section_count <= 96:
                raise ValueError(_tr("The DLL has an unsupported PE section count."))
            optional_offset = pe_offset + 24
            optional_header = read_at(optional_offset, optional_size)
            if len(optional_header) < 64:
                raise ValueError(_tr("The DLL optional header is incomplete."))
            magic = struct.unpack_from("<H", optional_header, 0)[0]
            if magic == 0x10B:
                directory_offset = 96
            elif magic == 0x20B:
                directory_offset = 112
            else:
                raise ValueError(_tr("The DLL uses an unsupported PE optional-header format."))
            if len(optional_header) < directory_offset + 8:
                raise ValueError(_tr("The DLL does not include an export data directory."))
            export_rva, export_size = struct.unpack_from(
                "<II", optional_header, directory_offset
            )
            if not export_rva or export_size < 40:
                return []
            size_of_headers = struct.unpack_from("<I", optional_header, 60)[0]
            section_table_offset = optional_offset + optional_size
            sections = []
            for index in range(section_count):
                header = read_at(section_table_offset + index * 40, 40)
                virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
                    "<IIII", header, 8
                )
                sections.append((virtual_address, virtual_size, raw_offset, raw_size))

            def rva_to_offset(rva: int, size: int = 1) -> int:
                if rva < size_of_headers and rva + size <= size_of_headers:
                    read_at(rva, size)
                    return rva
                for virtual_address, virtual_size, raw_offset, raw_size in sections:
                    span = max(virtual_size, raw_size)
                    if virtual_address <= rva < virtual_address + span:
                        delta = rva - virtual_address
                        if delta + size > raw_size:
                            break
                        offset = raw_offset + delta
                        read_at(offset, size)
                        return offset
                raise ValueError(_tr("The DLL export directory references an unmapped PE address."))

            export_offset = rva_to_offset(export_rva, 40)
            export_directory = read_at(export_offset, 40)
            number_of_functions = struct.unpack_from("<I", export_directory, 20)[0]
            number_of_names = struct.unpack_from("<I", export_directory, 24)[0]
            names_rva = struct.unpack_from("<I", export_directory, 32)[0]
            ordinals_rva = struct.unpack_from("<I", export_directory, 36)[0]
            if number_of_names > 16384:
                raise ValueError(_tr("The DLL export-name count is outside the supported range."))
            if not number_of_names or not names_rva or not ordinals_rva:
                return []
            names_offset = rva_to_offset(names_rva, number_of_names * 4)
            ordinals_offset = rva_to_offset(ordinals_rva, number_of_names * 2)
            name_rvas = read_at(names_offset, number_of_names * 4)
            ordinals = read_at(ordinals_offset, number_of_names * 2)

            exports = set()
            for index in range(number_of_names):
                ordinal = struct.unpack_from("<H", ordinals, index * 2)[0]
                if ordinal >= number_of_functions:
                    continue
                name_rva = struct.unpack_from("<I", name_rvas, index * 4)[0]
                name_offset = rva_to_offset(name_rva)
                available = min(256, file_size - name_offset)
                raw_name = read_at(name_offset, available).split(b"\0", 1)[0]
                try:
                    name = raw_name.decode("ascii")
                except UnicodeDecodeError:
                    continue
                if name and name.isidentifier() and name.isascii():
                    exports.add(name)
            return sorted(exports, key=str.casefold)

    def _save_and_close(
        self, dialog: QDialog, profile_root: Path, table: QTableWidget
    ) -> None:
        entries = self._entries_from_table(table) + [
            dict(entry) for entry in getattr(table, "stale_entries", [])
        ]
        seen = set()
        # enabled entries by game path: the game plugin refuses a profile that
        # loads one path from two mods
        enabled_paths: dict[str, str] = {}
        for index, entry in enumerate(entries, start=1):
            if not entry["mod"] or not entry["path"]:
                self._show_notice(
                    dialog,
                    _tr("Incomplete native DLL entry"),
                    _tr("Entry {index} needs both a mod name and a DLL path.").format(index=index),
                    "warning",
                )
                return
            route = self._normalize_native_route(entry["path"])
            if route is None:
                self._show_notice(
                    dialog,
                    _tr("Invalid DLL path"),
                    _tr("'{path}' is not a supported relative Elden Ring DLL path.").format(path=entry["path"]),
                    "warning",
                )
                return
            entry["path"] = route
            key = (entry["mod"].casefold(), route.casefold())
            if key in seen:
                self._show_notice(
                    dialog,
                    _tr("Duplicate DLL entry"),
                    _tr("'{path}' from '{mod}' appears more than once.").format(path=entry["path"], mod=entry["mod"]),
                    "warning",
                )
                return
            seen.add(key)
            if entry["enabled"]:
                other_mod = enabled_paths.setdefault(route.casefold(), entry["mod"])
                if other_mod.casefold() != entry["mod"].casefold():
                    self._show_notice(
                        dialog,
                        _tr("Duplicate DLL entry"),
                        _tr("'{path}' is enabled for both '{first}' and '{mod}'. MO2 "
                            "loads only the copy that wins the file; disable or "
                            "remove one of the entries.").format(
                            path=route, first=other_mod, mod=entry["mod"]
                        ),
                        "warning",
                    )
                    return
            initializer = entry["initializer"]
            if initializer and (
                not initializer.isascii() or not initializer.isidentifier()
            ):
                self._show_notice(
                    dialog,
                    _tr("Invalid initializer name"),
                    _tr("'{name}' is not a valid exported function name.").format(name=initializer),
                    "warning",
                )
                return
            if entry["enabled"] and initializer:
                physical_path = self._physical_dll_path(entry["mod"], route)
                if physical_path is not None:
                    try:
                        exports = self._read_pe_exports(physical_path)
                    except (OSError, ValueError, struct.error) as error:
                        self._show_notice(
                            dialog,
                            _tr("Could not verify initializer export"),
                            _tr("The export table for '{route}' could not be read.\n\n{error}").format(route=route, error=error),
                            "warning",
                        )
                        return
                    if initializer not in exports:
                        self._show_notice(
                            dialog,
                            _tr("Initializer export not found"),
                            _tr("'{name}' is not a named export of '{route}'. "
                                "Choose one of the detected exports or clear the initializer.").format(
                                name=initializer, route=route
                            ),
                            "warning",
                        )
                        return

        config_path = profile_root / self.ConfigFilename
        config = configparser.ConfigParser(interpolation=None)
        try:
            # the file also holds the startup options: one that cannot be
            # read is reported, not replaced by this section alone
            try:
                with config_path.open("r", encoding="utf-8-sig") as stream:
                    config.read_file(stream)
            except FileNotFoundError:
                pass
            if config.has_section(self.NativeSection):
                config.remove_section(self.NativeSection)
            if entries:
                config.add_section(self.NativeSection)
                config.set(self.NativeSection, "version", "2")
                for index, entry in enumerate(entries, start=1):
                    suffix = f"{index:02d}"
                    config.set(self.NativeSection, f"native_{suffix}", entry["path"])
                    config.set(self.NativeSection, f"mod_{suffix}", entry["mod"])
                    config.set(
                        self.NativeSection,
                        f"enabled_{suffix}",
                        "true" if entry["enabled"] else "false",
                    )
                    config.set(
                        self.NativeSection,
                        f"optional_{suffix}",
                        "true" if entry.get("optional", False) else "false",
                    )
                    if entry["initializer"]:
                        config.set(
                            self.NativeSection,
                            f"initializer_{suffix}",
                            entry["initializer"],
                        )

            profile_root.mkdir(parents=True, exist_ok=True)
            descriptor, temporary_name = tempfile.mkstemp(
                prefix=".eldenring_mo2_startup.",
                suffix=".tmp",
                dir=str(profile_root),
            )
            try:
                with os.fdopen(
                    descriptor, "w", encoding="utf-8", newline="\n"
                ) as stream:
                    config.write(stream)
                    stream.flush()
                    os.fsync(stream.fileno())
                os.replace(temporary_name, config_path)
            finally:
                if os.path.exists(temporary_name):
                    os.unlink(temporary_name)
        except (OSError, UnicodeError, configparser.Error, ValueError) as error:
            self._show_notice(
                dialog,
                _tr("Could not save native DLL profile"),
                _tr("MO2 could not save this profile's native DLL settings.\n\n{error}").format(error=error),
                "error",
            )
            return

        self._show_notice(
            dialog,
            _tr("Native DLL profile saved"),
            (
                _tr("Saved {count} ordered DLL entry for the MO2 profile file '{file}'.")
                if len(entries) == 1
                else _tr("Saved {count} ordered DLL entries for the MO2 profile file '{file}'.")
            ).format(count=len(entries), file=self.ConfigFilename),
            "success",
        )
        dialog.accept()

def createPlugin():
    return EldenRingMo2NativeProfileTool()
