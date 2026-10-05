"""Offer a per-install layout choice for supported Elden Ring archives."""

from __future__ import annotations

from pathlib import Path
from typing import Optional, Union
import json
import os
import uuid

import mobase
from PyQt6 import QtWidgets
from PyQt6.QtCore import Qt, qCritical, qInfo


class EldenRingArchiveInstaller(mobase.IPluginInstallerSimple):
    """Choose how supported Elden Ring archive contents are installed."""

    _game_short_name = "eldenring"
    _archive_wrapper_depth_limit = 16
    _default_game_plugin_name = "Elden Ring MO2 Support"
    _game_override_key = "_archive_layout_override"
    _game_cache_epoch_key = "_archive_layout_cache_epoch"
    _mod_choice_key = "preserve_archive_layout"
    _omit_txt_files_key = "omit_txt_files"
    _omit_md_files_key = "omit_md_files"
    _recognize_mod_engine_paths_key = "recognize_mod_engine_paths"
    _installed_native_route_index_key = "installed_native_route_index"
    _archive_preview_entry_limit = 250
    _supported_payload_extensions = {
        ".acb",
        ".anibnd",
        ".bak",
        ".bin",
        ".bnk",
        ".bnd",
        ".chrbnd",
        ".cfg",
        ".dcx",
        ".dsaproj",
        ".dsasbak",
        ".dll",
        ".dds",
        ".emevd",
        ".fdbnd",
        ".ffxbnd",
        ".flac",
        ".flver",
        ".fmg",
        ".hkx",
        ".hks",
        ".gfx",
        ".ini",
        ".json",
        ".matbin",
        ".mp3",
        ".mtd",
        ".msgbnd",
        ".ogg",
        ".lua",
        ".otf",
        ".partsbnd",
        ".png",
        ".tga",
        ".toml",
        ".tpf",
        ".tpfbdt",
        ".tpfbhd",
        ".wav",
        ".prev",
        ".wem",
        ".xml",
        ".yaml",
        ".yml",
    }

    def __init__(self):
        super().__init__()
        self._organizer: Optional[mobase.IOrganizer] = None
        # MO2 initializes standalone installers before a managed game is
        # guaranteed to exist. Use the game plugin's stable name as a fallback
        # and refresh it once Elden Ring is the active managed game.
        self._game_plugin_name = self._default_game_plugin_name
        self._default_preserve = False
        self._default_omit_txt = False
        self._default_omit_md = False
        self._default_recognize_mod_engine_paths = True
        self._default_mod_name: Optional[str] = None
        self._pending_choice: Optional[tuple[bool, bool, bool, bool]] = None
        self._pending_source_dll_routes: list[str] = []

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        qInfo("Elden Ring MO2: archive layout installer alpha.72 initialized.")
        return True

    def name(self) -> str:
        return "Elden Ring Archive Layout Installer"

    def localizedName(self) -> str:
        return self.tr("Elden Ring Archive Layout Installer")

    def author(self) -> str:
        return "ArialSenki"

    def description(self) -> str:
        return self.tr(
            "Choose how supported Elden Ring mod archives are organized during installation."
        )

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(0, 5, 0, 72)

    def requirements(self):
        return [mobase.PluginRequirementFactory.gameDependency({"ELDEN RING"})]

    def settings(self) -> list[mobase.PluginSetting]:
        # MO2's plugin manager already provides the enable/disable switch.
        # Avoid a second hidden setting that can silently disable this installer.
        return []

    def isActive(self) -> bool:
        organizer = self._organizer
        if organizer is None:
            return False
        game = organizer.managedGame()
        if game is None:
            return False
        if game.gameShortName().casefold() != self._game_short_name:
            return False

        game_plugin_name = game.name()
        if game_plugin_name:
            self._game_plugin_name = game_plugin_name
        return True

    def priority(self) -> int:
        return 200

    def isManualInstaller(self) -> bool:
        return False

    def onInstallationStart(
        self,
        archive: str,
        reinstallation: bool,
        current_mod: Optional[mobase.IModInterface],
    ) -> None:
        self._pending_choice = None
        self._pending_source_dll_routes = []
        self._default_preserve = False
        self._default_omit_txt = False
        self._default_omit_md = False
        self._default_recognize_mod_engine_paths = True
        self._default_mod_name = None
        if self._organizer is not None:
            self._organizer.setPluginSetting(
                self._game_plugin_name, self._game_override_key, False
            )

        if current_mod is not None:
            self._default_mod_name = current_mod.name()
            settings = current_mod.pluginSettings(self.name())
            self._default_preserve = bool(settings.get(self._mod_choice_key, False))
            self._default_omit_txt = bool(
                settings.get(self._omit_txt_files_key, False)
            )
            self._default_omit_md = bool(
                settings.get(self._omit_md_files_key, False)
            )
            self._default_recognize_mod_engine_paths = bool(
                settings.get(self._recognize_mod_engine_paths_key, True)
            )

    def onInstallationEnd(
        self,
        result: mobase.InstallResult,
        new_mod: Optional[mobase.IModInterface],
    ) -> None:
        try:
            if (
                result == mobase.InstallResult.SUCCESS
                and new_mod is not None
                and self._pending_choice is not None
            ):
                preserve, omit_txt, omit_md, recognize_mod_engine_paths = (
                    self._pending_choice
                )
                settings_to_store = (
                    (self._mod_choice_key, preserve),
                    (self._omit_txt_files_key, omit_txt),
                    (self._omit_md_files_key, omit_md),
                    (
                        self._recognize_mod_engine_paths_key,
                        recognize_mod_engine_paths,
                    ),
                )
                installed_dll_routes = self._collect_installed_dll_routes(
                    Path(new_mod.absolutePath())
                )
                route_index = {
                    "version": 1,
                    "layout": "keep-original" if preserve else "mo2-standard",
                    "dlls": installed_dll_routes,
                    "source_to_installed": self._map_source_dll_routes(
                        self._pending_source_dll_routes, installed_dll_routes
                    ),
                }
                settings_to_store += (
                    (
                        self._installed_native_route_index_key,
                        json.dumps(route_index, ensure_ascii=False, separators=(",", ":")),
                    ),
                )
                store_results = [
                    new_mod.setPluginSetting(self.name(), key, value)
                    for key, value in settings_to_store
                ]
                stored = all(store_results)
                layout_name = (
                    "keep original layout"
                    if preserve
                    else "MO2 DLL organization"
                )
                if stored:
                    qInfo(
                        "Elden Ring MO2: saved per-mod archive layout for "
                        f"{new_mod.name()}: {layout_name}; omit .txt={omit_txt}, "
                        f"omit .md={omit_md}; Mod Engine path mapping preference="
                        f"{recognize_mod_engine_paths} (standard layout only); "
                        f"indexed {len(installed_dll_routes)} installed DLL route(s), "
                        f"mapped {len(route_index['source_to_installed'])} archive route(s)."
                    )
                else:
                    qCritical(
                        "Elden Ring MO2: could not save the per-mod archive "
                        f"layout for {new_mod.name()}."
                    )
                self._bump_archive_layout_cache_epoch()
        finally:
            if self._organizer is not None:
                self._organizer.setPluginSetting(
                    self._game_plugin_name, self._game_override_key, False
                )
            self._pending_choice = None
            self._pending_source_dll_routes = []

    @classmethod
    def _collect_tree_dll_routes(
        cls, filetree: mobase.IFileTree, prefix: str = ""
    ) -> list[str]:
        routes: list[str] = []
        for entry in filetree:
            route = f"{prefix}/{entry.name()}" if prefix else entry.name()
            if entry.isDir():
                routes.extend(cls._collect_tree_dll_routes(entry, route))
            elif entry.isFile() and Path(entry.name()).suffix.casefold() == ".dll":
                routes.append(route.replace("\\", "/"))
        return routes

    @staticmethod
    def _collect_installed_dll_routes(mod_root: Path) -> list[str]:
        """Index DLLs at their final mod-relative paths after installation."""
        try:
            mod_root = mod_root.resolve(strict=True)
            if not mod_root.is_dir():
                return []
        except OSError:
            return []

        routes: list[str] = []
        for current, directories, filenames in os.walk(mod_root, followlinks=False):
            current_path = Path(current)
            directories[:] = [
                directory
                for directory in directories
                if not (current_path / directory).is_symlink()
            ]
            for filename in filenames:
                if Path(filename).suffix.casefold() != ".dll":
                    continue
                path = current_path / filename
                try:
                    resolved = path.resolve(strict=True)
                    resolved.relative_to(mod_root)
                except (OSError, ValueError):
                    continue
                if not resolved.is_file() or path.is_symlink():
                    continue
                routes.append(resolved.relative_to(mod_root).as_posix())
        return sorted(set(routes), key=str.casefold)

    @staticmethod
    def _map_source_dll_routes(
        source_routes: list[str], installed_routes: list[str]
    ) -> dict[str, str]:
        """Map declared archive paths to unambiguous installed paths."""
        installed_by_key = {route.casefold(): route for route in installed_routes}
        source_counts: dict[str, int] = {}
        installed_counts: dict[str, list[str]] = {}
        for route in source_routes:
            source_counts[Path(route).name.casefold()] = (
                source_counts.get(Path(route).name.casefold(), 0) + 1
            )
        for route in installed_routes:
            installed_counts.setdefault(Path(route).name.casefold(), []).append(route)

        mapping: dict[str, str] = {}
        for source in source_routes:
            source = source.replace("\\", "/").strip("/")
            parts = Path(source).parts
            aliases = [source]
            for index, part in enumerate(parts):
                if part.casefold() in {"native", "natives"}:
                    aliases.append(Path(*parts[index:]).as_posix())

            exact_candidates: list[str] = []
            for alias in aliases:
                exact_candidates.append(alias)
                exact_candidates.append(f"DLLs/{alias}")
                alias_parts = Path(alias).parts
                if alias_parts and alias_parts[0].casefold() in {"native", "natives"}:
                    tail = Path(*alias_parts[1:]).as_posix()
                    if tail:
                        exact_candidates.extend((tail, f"DLLs/{tail}"))
            exact_matches = {
                installed_by_key[candidate.casefold()]
                for candidate in exact_candidates
                if candidate.casefold() in installed_by_key
            }
            if len(exact_matches) == 1:
                mapping[source] = next(iter(exact_matches))
                continue

            suffix_matches = {
                installed
                for installed in installed_routes
                if any(
                    installed.casefold().endswith("/" + alias.casefold())
                    for alias in aliases
                )
            }
            if len(suffix_matches) == 1:
                mapping[source] = next(iter(suffix_matches))
                continue

            basename = Path(source).name.casefold()
            basename_matches = installed_counts.get(basename, [])
            if source_counts.get(basename) == 1 and len(basename_matches) == 1:
                mapping[source] = basename_matches[0]

        return mapping

    def _bump_archive_layout_cache_epoch(self) -> None:
        organizer = self._organizer
        if organizer is None:
            return
        try:
            organizer.setPluginSetting(
                self._game_plugin_name,
                self._game_cache_epoch_key,
                uuid.uuid4().hex,
            )
            qInfo(
                "Elden Ring MO2: invalidated cached archive-layout data checks "
                "after saving the mod's layout choice."
            )
        except Exception as error:
            qCritical(
                "Elden Ring MO2: could not invalidate cached archive-layout "
                f"data checks after installation ({error})."
            )

    def isArchiveSupported(self, tree: mobase.IFileTree) -> bool:
        organizer = self._organizer
        if organizer is None or not self.isActive():
            return False

        # Leave archives with their own scripted installer to that installer.
        if tree.find("fomod/ModuleConfig.xml", mobase.FileTreeEntry.FILE):
            return False

        supported = self._contains_supported_payload(tree)
        if supported:
            qInfo(
                "Elden Ring MO2: archive layout installer recognized supported "
                "Elden Ring payload; installation choice will be shown."
            )
        return supported

    def _contains_supported_payload(
        self, filetree: mobase.IFileTree, depth: int = 0
    ) -> bool:
        if depth > self._archive_wrapper_depth_limit:
            return False
        for entry in filetree:
            if entry.isDir():
                if self._contains_supported_payload(entry, depth + 1):
                    return True
            elif entry.isFile():
                suffix = entry.name().rsplit(".", 1)
                if len(suffix) == 2:
                    extension = f".{suffix[1].casefold()}"
                    if extension in self._supported_payload_extensions:
                        return True
        return False

    def install(
        self,
        name: mobase.GuessedString,
        tree: mobase.IFileTree,
        version: str,
        nexus_id: int,
    ) -> Union[mobase.InstallResult, mobase.IFileTree]:
        organizer = self._organizer
        if organizer is None:
            return mobase.InstallResult.NOT_ATTEMPTED

        default_mod_name = self._default_mod_name or str(name)
        qInfo(
            "Elden Ring MO2: opening archive layout choice for "
            f"{default_mod_name}."
        )
        choice = self._ask_archive_layout(default_mod_name, tree)
        if choice is None:
            return mobase.InstallResult.CANCELED

        (
            preserve,
            omit_txt,
            omit_md,
            recognize_mod_engine_paths,
            selected_mod_name,
        ) = choice
        if preserve:
            # "Keep original" retains every archive file. Text exclusions are
            # only meaningful for standard layout and must not be carried over
            # from a previous choice when reinstalling in preserve mode.
            omit_txt = False
            omit_md = False
        selected_mod_name = selected_mod_name.strip() or default_mod_name
        if selected_mod_name and selected_mod_name != str(name):
            name.update(selected_mod_name, mobase.GuessQuality.USER)
        self._pending_source_dll_routes = self._collect_tree_dll_routes(tree)
        self._pending_choice = (
            preserve,
            omit_txt,
            omit_md,
            recognize_mod_engine_paths,
        )
        organizer.setPluginSetting(
            self._game_plugin_name, self._game_override_key, preserve
        )

        skipped_extensions = tuple(
            extension
            for extension, omit in ((".txt", omit_txt), (".md", omit_md))
            if omit
        )
        for extension in skipped_extensions:
            removed = self._remove_files_with_extension(tree, extension)
            qInfo(
                "Elden Ring MO2: per-mod archive option omitted "
                f"{removed} {extension} file(s) from {str(name)} before installation."
            )

        # Both choices peel only redundant outer wrappers. Keep original leaves
        # payload paths and files alone; standard layout also routes native
        # plugins into Game/DLLs.
        try:
            from basic_games.games.game_eldenring_mo2 import (
                EldenRingModDataChecker,
            )

            checker = EldenRingModDataChecker(
                organizer,
                self._game_plugin_name,
                preserve_extensions=tuple(
                    extension
                    for extension, omit in ((".txt", omit_txt), (".md", omit_md))
                    if not omit
                ),
            )
            if preserve:
                tree = checker.normalize_preserved_layout(tree)
                qInfo(
                    "Elden Ring MO2: mapped installation-source folders to the "
                    "virtual game root while preserving all payload-relative "
                    f"paths and files for {str(name)}; DLL routing was not applied."
                )
            else:
                # Apply the selected layout before MO2 installs the files,
                # independently of any later data-check pass.
                tree = checker.normalize_standard_layout(
                    tree,
                    recognize_mod_engine_paths=recognize_mod_engine_paths,
                )
        except Exception as error:
            qCritical(
                "Elden Ring MO2: could not apply the selected archive layout "
                f"for {str(name)}: {error}"
            )
            return mobase.InstallResult.FAILED

        if not preserve:
            qInfo(
                "Elden Ring MO2: applied standard MO2 layout to the archive "
                "tree before installation for "
                f"{str(name)}."
            )

        return tree

    @classmethod
    def _remove_files_with_extension(
        cls, filetree: mobase.IFileTree, extension: str
    ) -> int:
        removed = 0
        for entry in list(filetree):
            if entry.isDir():
                removed += cls._remove_files_with_extension(entry, extension)
                if len(entry) == 0:
                    entry.detach()
            elif entry.name().casefold().endswith(extension):
                entry.detach()
                removed += 1
        return removed

    def _ask_archive_layout(
        self, mod_name: str, archive_tree: mobase.IFileTree
    ) -> Optional[tuple[bool, bool, bool, bool, str]]:
        dialog = QtWidgets.QDialog(self._parentWidget())
        dialog.setWindowTitle(self.tr("Elden Ring archive layout"))
        dialog.setSizeGripEnabled(True)

        layout = QtWidgets.QVBoxLayout(dialog)
        layout.setContentsMargins(14, 12, 14, 10)
        layout.setSpacing(6)
        layout.setSizeConstraint(QtWidgets.QLayout.SizeConstraint.SetNoConstraint)

        scroll_area = QtWidgets.QScrollArea(dialog)
        scroll_area.setWidgetResizable(True)
        scroll_area.setFrameShape(QtWidgets.QFrame.Shape.NoFrame)
        scroll_area.setHorizontalScrollBarPolicy(
            Qt.ScrollBarPolicy.ScrollBarAlwaysOff
        )
        scroll_area.setMinimumSize(0, 0)
        content = QtWidgets.QWidget(scroll_area)
        content_layout = QtWidgets.QVBoxLayout(content)
        content_layout.setContentsMargins(0, 0, 0, 0)
        content_layout.setSpacing(7)
        scroll_area.setWidget(content)
        layout.addWidget(scroll_area, 1)

        prompt = QtWidgets.QLabel(
            self.tr("Choose how to install this mod.")
        )
        prompt.setWordWrap(True)
        content_layout.addWidget(prompt)

        mod_name_layout = QtWidgets.QHBoxLayout()
        mod_name_label = QtWidgets.QLabel(self.tr("Mod name:"))
        mod_name_edit = QtWidgets.QLineEdit(mod_name)
        mod_name_edit.setClearButtonEnabled(True)
        mod_name_edit.setToolTip(
            self.tr(
                "This is how the mod will appear in MO2. It does not rename "
                "the archive or downloaded file."
            )
        )
        mod_name_label.setBuddy(mod_name_edit)
        mod_name_layout.addWidget(mod_name_label)
        mod_name_layout.addWidget(mod_name_edit, 1)
        content_layout.addLayout(mod_name_layout)

        selected: dict[str, Optional[tuple[bool, bool, bool, bool, str]]] = {
            "choice": None
        }

        def choose_layout(preserve: bool) -> None:
            chosen_name = mod_name_edit.text().strip()
            if not chosen_name:
                mod_name_edit.setText(mod_name)
                mod_name_edit.setFocus()
                return
            selected["choice"] = (
                preserve,
                omit_txt_box.isChecked(),
                omit_md_box.isChecked(),
                mod_engine_paths_box.isChecked(),
                chosen_name,
            )
            dialog.accept()

        filters_group = QtWidgets.QGroupBox(self.tr("Text files"))
        filters_layout = QtWidgets.QVBoxLayout(filters_group)
        filters_layout.setContentsMargins(10, 7, 10, 7)
        filters_layout.setSpacing(4)
        filters_label = QtWidgets.QLabel(
            self.tr(
                "Text-file exclusions apply only to standard layout. Keep original "
                "always installs every archive file; choices are remembered for reinstall."
            )
        )
        filters_label.setWordWrap(True)
        filters_label.setToolTip(
            self.tr(
                "These exclusions apply only when you choose standard MO2 layout. "
                "Keep original always retains .txt and .md files, even if these "
                "boxes are checked. New mods keep them by default."
            )
        )
        filters_layout.addWidget(filters_label)
        omit_txt_box = QtWidgets.QCheckBox(self.tr("Skip .txt files"))
        omit_txt_box.setChecked(self._default_omit_txt)
        omit_txt_box.setToolTip(
            self.tr("Skips every .txt file in this archive before installation.")
        )
        filters_layout.addWidget(omit_txt_box)
        omit_md_box = QtWidgets.QCheckBox(self.tr("Skip .md files"))
        omit_md_box.setChecked(self._default_omit_md)
        omit_md_box.setToolTip(
            self.tr(
                "Skips every Markdown .md file in this archive before installation."
            )
        )
        filters_layout.addWidget(omit_md_box)
        content_layout.addWidget(filters_group)

        routes_group = QtWidgets.QGroupBox(
            self.tr("Elden Ring package paths")
        )
        routes_layout = QtWidgets.QVBoxLayout(routes_group)
        routes_layout.setContentsMargins(10, 7, 10, 7)
        routes_layout.setSpacing(3)
        mod_engine_paths_box = QtWidgets.QCheckBox(
            self.tr("Recognize Mod Engine 2 paths (standard layout only)")
        )
        mod_engine_paths_box.setChecked(
            self._default_recognize_mod_engine_paths
        )
        routes_layout.addWidget(mod_engine_paths_box)
        routes_description = QtWidgets.QLabel(
            self.tr(
                "With standard layout, maps Mod Engine 2 menu files and external "
                "DLLs to MO2's game folders. Keep original leaves every path in place."
            )
        )
        routes_description.setWordWrap(True)
        routes_description.setToolTip(
            self.tr(
                "Only standard layout maps mod/menu to menu and routes native files "
                "from mod/external_dlls to MO2's DLLs folder. Keep original does not "
                "move files or change paths."
            )
        )
        routes_layout.addWidget(routes_description)
        content_layout.addWidget(routes_group)

        preview_button = QtWidgets.QPushButton(
            self.tr("Show archive contents")
        )
        preview_button.setCheckable(True)
        preview_button.setMinimumHeight(28)
        content_layout.addWidget(preview_button)
        preview_note = QtWidgets.QLabel()
        preview_note.setWordWrap(True)
        preview_tree = QtWidgets.QTreeWidget()
        preview_tree.setHeaderLabels(
            [self.tr("Original archive path"), self.tr("Type")]
        )
        preview_header = preview_tree.header()
        preview_header.setStretchLastSection(False)
        preview_header.setSectionResizeMode(
            0, QtWidgets.QHeaderView.ResizeMode.Stretch
        )
        preview_header.setSectionResizeMode(
            1, QtWidgets.QHeaderView.ResizeMode.Fixed
        )
        preview_tree.setColumnWidth(1, 130)
        preview_tree.setAlternatingRowColors(True)
        preview_tree.setMinimumHeight(0)
        preview_tree.setMaximumHeight(260)
        preview_tree.setUniformRowHeights(True)
        preview_tree.setVisible(False)
        preview_note.setVisible(False)
        content_layout.addWidget(preview_note)
        content_layout.addWidget(preview_tree)
        preview_state = {"populated": False}

        def add_preview_entries(
            source_tree: mobase.IFileTree,
            parent_item: Optional[QtWidgets.QTreeWidgetItem],
            shown_count: list[int],
            truncated: list[bool],
        ) -> None:
            for entry in source_tree:
                if shown_count[0] >= self._archive_preview_entry_limit:
                    truncated[0] = True
                    return
                is_directory = entry.isDir()
                item = QtWidgets.QTreeWidgetItem(
                    [entry.name(), self.tr("Folder") if is_directory else self.tr("File")]
                )
                if parent_item is None:
                    preview_tree.addTopLevelItem(item)
                else:
                    parent_item.addChild(item)
                shown_count[0] += 1
                if is_directory:
                    add_preview_entries(entry, item, shown_count, truncated)
                    if truncated[0]:
                        return

        def toggle_archive_preview(visible: bool) -> None:
            if visible and not preview_state["populated"]:
                shown_count = [0]
                truncated = [False]
                add_preview_entries(archive_tree, None, shown_count, truncated)
                for index in range(preview_tree.topLevelItemCount()):
                    preview_tree.topLevelItem(index).setExpanded(True)
                if truncated[0]:
                    preview_note.setText(
                        self.tr(
                            "Showing the first {0} entries from the archive. "
                            "The preview is limited to keep large archives responsive."
                        ).format(shown_count[0])
                    )
                else:
                    preview_note.setText(
                        self.tr("Showing all {0} archive entries.").format(
                            shown_count[0]
                        )
                    )
                preview_state["populated"] = True
            if visible:
                preview_tree.setMinimumHeight(190)
            else:
                preview_tree.setMinimumHeight(0)
            preview_tree.setVisible(visible)
            preview_note.setVisible(visible)
            preview_button.setText(
                self.tr("Hide archive contents")
                if visible
                else self.tr("Show archive contents")
            )
            # Keep the top-level dialog inside the monitor's work area. The
            # scroll area absorbs the preview's extra height without sending
            # conflicting resize requests to the Windows window manager.
            content_layout.activate()

        preview_button.toggled.connect(toggle_archive_preview)

        def add_layout_option(
            title: str,
            description: str,
            details: str,
            preserve: bool,
        ) -> QtWidgets.QPushButton:
            group = QtWidgets.QGroupBox(self.tr(title))
            group_layout = QtWidgets.QHBoxLayout(group)
            group_layout.setContentsMargins(10, 7, 10, 7)
            group_layout.setSpacing(12)

            description_label = QtWidgets.QLabel(self.tr(description))
            description_label.setWordWrap(True)
            description_label.setToolTip(self.tr(details))
            group_layout.addWidget(description_label, 1)

            choose_button = QtWidgets.QPushButton(self.tr("Choose this layout"))
            choose_button.setMinimumSize(150, 30)
            choose_button.clicked.connect(
                lambda _checked=False, value=preserve: choose_layout(value)
            )
            group_layout.addWidget(
                choose_button, 0, Qt.AlignmentFlag.AlignVCenter
            )
            content_layout.addWidget(group)
            return choose_button

        preserve_button = add_layout_option(
            "Keep original archive structure",
            "Maps install-source folders such as 'Drag Files Into Here' to the "
            "game root, then keeps every file's internal path, including DLL locations.",
            "For FRZN Merge, the contents of 'Drag Files Into Here' land at the "
            "game root as intended; its natives/ tree stays unchanged. Keeps .txt "
            "and .md files and does not move DLLs into MO2's DLLs folder.",
            True,
        )
        standard_button = add_layout_option(
            "Use standard MO2 layout",
            "Finds mod files inside wrappers and routes DLLs, settings, fonts, "
            "and game assets to MO2's standard folders.",
            "Finds mod files inside package folders, moves native-mod DLLs and "
            "their INI files into the game's DLLs folder, puts FreecamMod's "
            "settings and fonts in DLLs/Freecam, keeps other mods' fonts "
            "beside a moved DLL when their location is unambiguous, and "
            "keeps Elden Ring asset folders such as action, chr, common, event, "
            "assets, map, menu, msg, param, parts, script, sd, and sfx at their game "
            "paths. Windows proxy loaders and their matching settings stay at "
            "the game root so MO2 can load them there.",
            False,
        )

        default_label = QtWidgets.QLabel(
            self.tr(
                "Shown for each supported install; existing mods reopen with "
                "their previous choices."
            )
        )
        default_label.setWordWrap(True)
        default_label.setToolTip(
            self.tr(
                "MO2 asks on every supported installation. For an existing mod, "
                "its previous layout, path-mapping, and text-file choices are "
                "preselected."
            )
        )
        content_layout.addWidget(default_label)

        buttons = QtWidgets.QDialogButtonBox(
            QtWidgets.QDialogButtonBox.StandardButton.Cancel
        )
        buttons.rejected.connect(dialog.reject)
        layout.addWidget(buttons)

        default_button = preserve_button if self._default_preserve else standard_button
        default_button.setDefault(True)
        default_button.setFocus()

        # Size from the complete, collapsed dialog. Doing this before adding
        # the layout choices leaves them below the fold on shorter displays.
        dialog.ensurePolished()
        content_layout.activate()
        layout.activate()
        screen = dialog.screen()
        if screen is None:
            screen = QtWidgets.QApplication.primaryScreen()
        if screen is not None:
            available = screen.availableGeometry()
            max_width = max(320, available.width() - 32)
            max_height = max(320, available.height() - 16)
            min_width = min(820, max_width)
            min_height = min(420, max_height)
            dialog.setMinimumSize(min_width, min_height)
            dialog.setMaximumSize(max_width, max_height)
            size_hint = dialog.sizeHint().expandedTo(dialog.minimumSizeHint())
            initial_width = min(
                max(min_width, 860, size_hint.width()), max_width
            )
            dialog.resize(initial_width, min_height)
            content_layout.activate()
            layout.activate()
            margins = layout.contentsMargins()
            fixed_height = (
                margins.top()
                + margins.bottom()
                + layout.spacing()
                + buttons.sizeHint().height()
            )
            complete_content_height = content_layout.sizeHint().height()
            target_height = max(
                size_hint.height(), complete_content_height + fixed_height
            )
            dialog.resize(
                initial_width,
                min(max(min_height, target_height), max_height),
            )
        else:
            dialog.resize(dialog.sizeHint().expandedTo(dialog.minimumSizeHint()))
        dialog.exec()

        if dialog.result() != QtWidgets.QDialog.DialogCode.Accepted:
            return None
        return selected["choice"]

    def tr(self, value: str) -> str:
        return QtWidgets.QApplication.translate(
            "EldenRingArchiveInstaller", value
        )
