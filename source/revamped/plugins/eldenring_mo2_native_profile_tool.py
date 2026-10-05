"""Manage detected native DLL order and optional exports per Elden Ring profile."""

from __future__ import annotations

import configparser
import json
import os
from pathlib import Path
import struct
import tempfile

import mobase
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QIcon
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QDialog,
    QDialogButtonBox,
    QGroupBox,
    QHBoxLayout,
    QHeaderView,
    QInputDialog,
    QLabel,
    QTreeWidget,
    QTreeWidgetItem,
    QMessageBox,
    QPushButton,
    QSplitter,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
)


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
    }

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
            "Detect installed Elden Ring native DLLs and manage their load "
            "order and optional initializer exports for each MO2 profile."
        )

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(0, 5, 0, 75)

    def requirements(self):
        return [mobase.PluginRequirementFactory.gameDependency({"ELDEN RING"})]

    def settings(self) -> list[mobase.PluginSetting]:
        return []

    def enabledByDefault(self) -> bool:
        return True

    def displayName(self) -> str:
        return "Elden Ring / Native DLL Profile"

    def tooltip(self) -> str:
        return (
            "Detect installed native DLLs and set their order and optional "
            "initializer for the active MO2 profile."
        )

    def icon(self) -> QIcon:
        return QIcon()

    def setParentWidget(self, widget) -> None:
        self.__parent_widget = widget

    def display(self) -> None:
        try:
            game = self.__organizer.managedGame()
            if game is None or game.gameShortName().casefold() != "eldenring":
                return
        except Exception:
            return

        profile = self.__organizer.profile()
        if profile is None:
            QMessageBox.warning(
                self.__parent_widget,
                "No active Elden Ring profile",
                "Select an Elden Ring profile in MO2 before editing native DLL settings.",
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
            QMessageBox.critical(
                self.__parent_widget,
                "Could not read native DLL profile",
                f"MO2 could not inspect the active profile or installed mods.\n\n{error}",
            )
            return

        dialog = QDialog(self.__parent_widget)
        dialog.setWindowTitle("Elden Ring native DLL profile")
        dialog.setMinimumSize(900, 560)
        dialog.resize(1080, 660)
        dialog.setSizeGripEnabled(True)
        layout = QVBoxLayout(dialog)
        layout.setContentsMargins(18, 16, 18, 14)
        layout.setSpacing(10)

        heading = QLabel(
            f"<b>MO2 profile:</b> {profile.name()}<br>"
            "Detected entries come from enabled mods. The list order controls "
            "the priority of entries here; an initializer is optional."
        )
        heading.setWordWrap(True)
        layout.addWidget(heading)

        splitter = QSplitter(Qt.Orientation.Horizontal, dialog)
        detected_group = QGroupBox("Detected DLLs by MO2 mod", splitter)
        detected_layout = QVBoxLayout(detected_group)
        detected_layout.setContentsMargins(12, 12, 12, 12)
        detected_note = QLabel(
            "MO2's installed route index is used when available; older mods are "
            "scanned in their native DLL folders. Each DLL stays under its source mod."
        )
        detected_note.setWordWrap(True)
        detected_layout.addWidget(detected_note)
        detected_tree = QTreeWidget(detected_group)
        detected_tree.setHeaderHidden(True)
        detected_tree.setRootIsDecorated(True)
        detected_tree.setAlternatingRowColors(True)
        detected_tree.setUniformRowHeights(True)
        detected_tree.setSelectionMode(
            QAbstractItemView.SelectionMode.ExtendedSelection
        )
        detected_layout.addWidget(detected_tree, 1)
        detected_status = QLabel(detected_group)
        detected_status.setWordWrap(True)
        detected_layout.addWidget(detected_status)
        add_button = QPushButton("Add selected DLLs", detected_group)
        detected_layout.addWidget(add_button)

        profile_group = QGroupBox("Profile load order and initializers", splitter)
        profile_layout = QVBoxLayout(profile_group)
        profile_layout.setContentsMargins(12, 12, 12, 12)
        profile_note = QLabel(
            "Move entries to change their order. Initializer names must be "
            "named exports from that DLL; use Detect exports to choose one. "
            "This profile setting does not enable or "
            "disable the mod itself."
        )
        profile_note.setWordWrap(True)
        profile_layout.addWidget(profile_note)
        table = QTableWidget(0, 4, profile_group)
        table.setHorizontalHeaderLabels(
            ["Use entry", "MO2 mod", "Installed DLL path", "Initializer export (optional)"]
        )
        table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        table.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
        table.setEditTriggers(
            QAbstractItemView.EditTrigger.DoubleClicked
            | QAbstractItemView.EditTrigger.EditKeyPressed
        )
        table.verticalHeader().setVisible(False)
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
        remove_button = QPushButton("Remove mapping", profile_group)
        up_button = QPushButton("Move up", profile_group)
        down_button = QPushButton("Move down", profile_group)
        exports_button = QPushButton("Detect exports…", profile_group)
        for button in (remove_button, up_button, down_button, exports_button):
            row_buttons.addWidget(button)
        profile_layout.addLayout(row_buttons)

        splitter.addWidget(detected_group)
        splitter.addWidget(profile_group)
        splitter.setStretchFactor(0, 1)
        splitter.setStretchFactor(1, 2)
        layout.addWidget(splitter, 1)

        footer = QLabel(
            "Entries are saved in this profile's eldenring_mo2_startup.ini. "
            "Startup, performance, and cleanup settings in that file are preserved."
        )
        footer.setWordWrap(True)
        layout.addWidget(footer)
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Save
            | QDialogButtonBox.StandardButton.Cancel,
            parent=dialog,
        )
        layout.addWidget(buttons)

        self._populate_detected_tree(detected_tree, candidates)
        mod_count = len({str(candidate["mod"]).casefold() for candidate in candidates})
        detected_status.setText(
            f"{len(candidates)} winning native DLL route(s) across "
            f"{mod_count} MO2 mod(s). Select a mod group or individual DLLs."
            if candidates
            else "No native DLL routes were detected in enabled mods."
        )
        self._populate_profile_table(table, entries)
        add_button.clicked.connect(
            lambda _checked=False: self._add_selected_candidates(
                detected_tree, table
            )
        )
        remove_button.clicked.connect(
            lambda _checked=False: self._remove_selected_entry(table)
        )
        up_button.clicked.connect(
            lambda _checked=False: self._move_selected_entry(table, -1)
        )
        down_button.clicked.connect(
            lambda _checked=False: self._move_selected_entry(table, 1)
        )
        exports_button.clicked.connect(
            lambda _checked=False: self._choose_initializer_export(table)
        )
        buttons.accepted.connect(
            lambda: self._save_and_close(dialog, profile_root, table)
        )
        buttons.rejected.connect(dialog.reject)

        layout.activate()
        dialog.ensurePolished()
        dialog.exec()

    def _read_profile_entries(self, config_path: Path) -> list[dict]:
        config = configparser.ConfigParser(interpolation=None)
        config.read(config_path, encoding="utf-8")
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
                raise ValueError(f"Unsupported native profile setting '{key}'.")
            index = int(raw_index)
            if index <= 0:
                raise ValueError("Native DLL entry numbers must be greater than zero.")
            entry = entries_by_index.setdefault(index, {})
            if prefix == "native":
                entry["path"] = value.strip()
            elif prefix == "mod":
                entry["mod"] = value.strip()
            elif prefix == "initializer":
                entry["initializer"] = value.strip()
            else:
                if value.strip().casefold() not in {"true", "false"}:
                    raise ValueError(f"'{key}' must be true or false.")
                entry[prefix] = value.strip().casefold() == "true"

        profile_entries = []
        for index in sorted(entries_by_index):
            entry = entries_by_index[index]
            mod_name = entry.get("mod") or default_mod
            route = entry.get("path", "")
            if not mod_name or not route:
                raise ValueError(
                    f"Native DLL entry {index} needs both a mod and a DLL path."
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
                if not any(
                    isinstance(origin, str)
                    and origin.casefold() == mod_name.casefold()
                    for origin in origins
                ):
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

        if routes:
            return routes

        for entry in mod_root.iterdir():
            if entry.is_file() and entry.suffix.casefold() == ".dll":
                routes.append(entry.relative_to(mod_root).as_posix())
        for directory_name in sorted(self.ManagedDllDirectories):
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
        return routes

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
            suffix = "DLL" if count == 1 else "DLLs"
            group.setText(0, f"{group_names[key]} ({count} {suffix})")
            group.setToolTip(
                0,
                f"{group_names[key]} contributes {count} detected native {suffix}. "
                "Select this group and choose Add selected DLLs to add all of them.",
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
        if row < 0:
            QMessageBox.information(
                self.__parent_widget,
                "Select a DLL",
                "Select a profile entry before detecting exported functions.",
            )
            return
        entries = self._entries_from_table(table)
        entry = entries[row]
        physical_path = self._physical_dll_path(entry["mod"], entry["path"])
        if physical_path is None:
            QMessageBox.warning(
                self.__parent_widget,
                "DLL not found",
                "MO2 could not resolve this DLL inside its installed mod folder.",
            )
            return
        try:
            exports = self._read_pe_exports(physical_path)
        except (OSError, ValueError, struct.error) as error:
            QMessageBox.warning(
                self.__parent_widget,
                "Could not read DLL exports",
                f"The DLL export table could not be read. The file was not loaded.\n\n{error}",
            )
            return
        if not exports:
            QMessageBox.information(
                self.__parent_widget,
                "No initializer exports detected",
                "No named functions suitable for an initializer were found in this DLL.",
            )
            return
        current = entry.get("initializer", "")
        choices = list(exports)
        if current and current not in choices:
            choices.insert(0, current)
        selected, accepted = QInputDialog.getItem(
            self.__parent_widget,
            "Choose an exported initializer",
            "This function will be called by the native bridge when Elden Ring starts:",
            choices,
            max(0, choices.index(current)) if current in choices else 0,
            False,
        )
        if accepted:
            entries[row]["initializer"] = selected.strip()
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
                    raise ValueError("The DLL export table contains an invalid size or offset.")
                if offset + size > file_size:
                    raise ValueError("The DLL export table points outside the file.")
                stream.seek(offset)
                data = stream.read(size)
                if len(data) != size:
                    raise ValueError("The DLL ended while reading its export table.")
                return data

            dos_header = read_at(0, 64)
            if dos_header[:2] != b"MZ":
                raise ValueError("This file does not have a Windows PE header.")
            pe_offset = struct.unpack_from("<I", dos_header, 0x3C)[0]
            coff_header = read_at(pe_offset, 24)
            if coff_header[:4] != b"PE\0\0":
                raise ValueError("This file does not have a valid PE signature.")
            section_count = struct.unpack_from("<H", coff_header, 6)[0]
            optional_size = struct.unpack_from("<H", coff_header, 20)[0]
            if not 0 < section_count <= 96:
                raise ValueError("The DLL has an unsupported PE section count.")
            optional_offset = pe_offset + 24
            optional_header = read_at(optional_offset, optional_size)
            if len(optional_header) < 64:
                raise ValueError("The DLL optional header is incomplete.")
            magic = struct.unpack_from("<H", optional_header, 0)[0]
            if magic == 0x10B:
                directory_offset = 96
            elif magic == 0x20B:
                directory_offset = 112
            else:
                raise ValueError("The DLL uses an unsupported PE optional-header format.")
            if len(optional_header) < directory_offset + 8:
                raise ValueError("The DLL does not include an export data directory.")
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
                raise ValueError("The DLL export directory references an unmapped PE address.")

            export_offset = rva_to_offset(export_rva, 40)
            export_directory = read_at(export_offset, 40)
            number_of_functions = struct.unpack_from("<I", export_directory, 20)[0]
            number_of_names = struct.unpack_from("<I", export_directory, 24)[0]
            names_rva = struct.unpack_from("<I", export_directory, 32)[0]
            ordinals_rva = struct.unpack_from("<I", export_directory, 36)[0]
            if number_of_names > 16384:
                raise ValueError("The DLL export-name count is outside the supported range.")
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
        entries = self._entries_from_table(table)
        seen = set()
        for index, entry in enumerate(entries, start=1):
            if not entry["mod"] or not entry["path"]:
                QMessageBox.warning(
                    dialog,
                    "Incomplete native DLL entry",
                    f"Entry {index} needs both a mod name and a DLL path.",
                )
                return
            route = self._normalize_native_route(entry["path"])
            if route is None:
                QMessageBox.warning(
                    dialog,
                    "Invalid DLL path",
                    f"'{entry['path']}' is not a supported relative Elden Ring DLL path.",
                )
                return
            entry["path"] = route
            key = (entry["mod"].casefold(), route.casefold())
            if key in seen:
                QMessageBox.warning(
                    dialog,
                    "Duplicate DLL entry",
                    f"'{entry['path']}' from '{entry['mod']}' appears more than once.",
                )
                return
            seen.add(key)
            initializer = entry["initializer"]
            if initializer and (
                not initializer.isascii() or not initializer.isidentifier()
            ):
                QMessageBox.warning(
                    dialog,
                    "Invalid initializer name",
                    f"'{initializer}' is not a valid exported function name.",
                )
                return
            if entry["enabled"] and initializer:
                physical_path = self._physical_dll_path(entry["mod"], route)
                if physical_path is not None:
                    try:
                        exports = self._read_pe_exports(physical_path)
                    except (OSError, ValueError, struct.error) as error:
                        QMessageBox.warning(
                            dialog,
                            "Could not verify initializer export",
                            f"The export table for '{route}' could not be read.\n\n{error}",
                        )
                        return
                    if initializer not in exports:
                        QMessageBox.warning(
                            dialog,
                            "Initializer export not found",
                            f"'{initializer}' is not a named export of '{route}'. "
                            "Choose one of the detected exports or clear the initializer.",
                        )
                        return

        config_path = profile_root / self.ConfigFilename
        config = configparser.ConfigParser(interpolation=None)
        try:
            config.read(config_path, encoding="utf-8")
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
            QMessageBox.critical(
                dialog,
                "Could not save native DLL profile",
                f"MO2 could not save this profile's native DLL settings.\n\n{error}",
            )
            return

        QMessageBox.information(
            dialog,
            "Native DLL profile saved",
            f"Saved {len(entries)} ordered DLL entr{'y' if len(entries) == 1 else 'ies'} "
            f"for the MO2 profile file '{self.ConfigFilename}'.",
        )
        dialog.accept()

def createPlugin():
    return EldenRingMo2NativeProfileTool()
