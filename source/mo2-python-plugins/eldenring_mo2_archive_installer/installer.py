"""Offer a per-install layout choice for supported Elden Ring archives."""

from __future__ import annotations

from typing import Optional, Union
import uuid

import mobase
from PyQt6 import QtWidgets
from PyQt6.QtCore import qCritical, qInfo


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
    _archive_preview_entry_limit = 250
    _supported_payload_extensions = {
        ".acb",
        ".anibnd",
        ".bin",
        ".bnd",
        ".chrbnd",
        ".cfg",
        ".dcx",
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
        ".partsbnd",
        ".png",
        ".tga",
        ".toml",
        ".tpf",
        ".wav",
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
        self._pending_choice: Optional[tuple[bool, bool, bool]] = None

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        qInfo("Elden Ring MO2: archive layout installer alpha.57 initialized.")
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
        return mobase.VersionInfo(0, 5, 0, 57)

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
        self._default_preserve = False
        self._default_omit_txt = False
        self._default_omit_md = False
        if self._organizer is not None:
            self._organizer.setPluginSetting(
                self._game_plugin_name, self._game_override_key, False
            )

        if current_mod is not None:
            settings = current_mod.pluginSettings(self.name())
            self._default_preserve = bool(settings.get(self._mod_choice_key, False))
            self._default_omit_txt = bool(
                settings.get(self._omit_txt_files_key, False)
            )
            self._default_omit_md = bool(
                settings.get(self._omit_md_files_key, False)
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
                preserve, omit_txt, omit_md = self._pending_choice
                settings_to_store = (
                    (self._mod_choice_key, preserve),
                    (self._omit_txt_files_key, omit_txt),
                    (self._omit_md_files_key, omit_md),
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
                        f"omit .md={omit_md}."
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

        qInfo(
            "Elden Ring MO2: opening archive layout choice for "
            f"{str(name)}."
        )
        choice = self._ask_archive_layout(str(name), tree)
        if choice is None:
            return mobase.InstallResult.CANCELED

        preserve, omit_txt, omit_md = choice
        self._pending_choice = choice
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

        if preserve:
            qInfo(
                "Elden Ring MO2: preserving original archive layout for "
                f"{str(name)}; automatic DLL organization is skipped."
            )
        else:
            # A simple installer returns the in-memory archive tree directly.
            # Normalize it here so the selected layout is applied before MO2
            # installs the files, independently of any later data-check pass.
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
                tree = checker.normalize_standard_layout(tree)
            except Exception as error:
                qCritical(
                    "Elden Ring MO2: could not apply standard archive layout "
                    f"for {str(name)}: {error}"
                )
                return mobase.InstallResult.FAILED

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
    ) -> Optional[tuple[bool, bool, bool]]:
        dialog = QtWidgets.QDialog(self._parentWidget())
        dialog.setWindowTitle(self.tr("Elden Ring archive layout"))
        dialog.setMinimumWidth(640)
        dialog.setSizeGripEnabled(True)

        layout = QtWidgets.QVBoxLayout(dialog)
        layout.setContentsMargins(20, 18, 20, 16)
        layout.setSpacing(12)

        prompt = QtWidgets.QLabel(
            self.tr("Choose how to install {0}.").format(mod_name)
        )
        prompt.setWordWrap(True)
        layout.addWidget(prompt)

        selected: dict[str, Optional[tuple[bool, bool, bool]]] = {"choice": None}

        def choose_layout(preserve: bool) -> None:
            selected["choice"] = (
                preserve,
                omit_txt_box.isChecked(),
                omit_md_box.isChecked(),
            )
            dialog.accept()

        filters_group = QtWidgets.QGroupBox(self.tr("Text files"))
        filters_layout = QtWidgets.QVBoxLayout(filters_group)
        filters_layout.setContentsMargins(14, 12, 14, 12)
        filters_layout.setSpacing(8)
        filters_label = QtWidgets.QLabel(
            self.tr(
                "Choose whether to omit .txt or .md files. They may contain "
                "documentation or files a mod needs. For a new mod, both options "
                "start unchecked and the files are kept. Choices are saved with "
                "the mod and preselected on reinstall."
            )
        )
        filters_label.setWordWrap(True)
        filters_layout.addWidget(filters_label)
        omit_txt_box = QtWidgets.QCheckBox(self.tr("Omit .txt files"))
        omit_txt_box.setChecked(self._default_omit_txt)
        filters_layout.addWidget(omit_txt_box)
        omit_txt_description = QtWidgets.QLabel(
            self.tr("Removes every .txt file from this archive before installation.")
        )
        omit_txt_description.setWordWrap(True)
        filters_layout.addWidget(omit_txt_description)
        omit_md_box = QtWidgets.QCheckBox(self.tr("Omit .md files"))
        omit_md_box.setChecked(self._default_omit_md)
        filters_layout.addWidget(omit_md_box)
        omit_md_description = QtWidgets.QLabel(
            self.tr("Removes every Markdown .md file from this archive before installation.")
        )
        omit_md_description.setWordWrap(True)
        filters_layout.addWidget(omit_md_description)
        layout.addWidget(filters_group)

        preview_button = QtWidgets.QPushButton(
            self.tr("Show archive contents")
        )
        preview_button.setCheckable(True)
        layout.addWidget(preview_button)
        preview_note = QtWidgets.QLabel()
        preview_note.setWordWrap(True)
        preview_tree = QtWidgets.QTreeWidget()
        preview_tree.setHeaderLabels(
            [self.tr("Original archive path"), self.tr("Type")]
        )
        preview_tree.setColumnWidth(1, 70)
        preview_tree.setAlternatingRowColors(True)
        preview_tree.setMaximumHeight(190)
        preview_tree.setVisible(False)
        preview_note.setVisible(False)
        layout.addWidget(preview_note)
        layout.addWidget(preview_tree)
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
            preview_tree.setVisible(visible)
            preview_note.setVisible(visible)
            preview_button.setText(
                self.tr("Hide archive contents")
                if visible
                else self.tr("Show archive contents")
            )

        preview_button.toggled.connect(toggle_archive_preview)

        def add_layout_option(
            title: str,
            description: str,
            preserve: bool,
        ) -> QtWidgets.QPushButton:
            group = QtWidgets.QGroupBox(self.tr(title))
            group_layout = QtWidgets.QVBoxLayout(group)
            group_layout.setContentsMargins(14, 12, 14, 12)
            group_layout.setSpacing(10)

            description_label = QtWidgets.QLabel(self.tr(description))
            description_label.setWordWrap(True)
            group_layout.addWidget(description_label)

            choose_button = QtWidgets.QPushButton(self.tr("Choose this layout"))
            choose_button.setMinimumHeight(34)
            choose_button.clicked.connect(
                lambda _checked=False, value=preserve: choose_layout(value)
            )
            group_layout.addWidget(choose_button)
            layout.addWidget(group)
            return choose_button

        preserve_button = add_layout_option(
            "Keep original archive structure",
            "Preserves files and folders at their packaged paths. Choose this "
            "when a mod expects that arrangement. MO2 explicitly loads "
            "root-level DLLs from this mod while it is active.",
            True,
        )
        standard_button = add_layout_option(
            "Use standard MO2 layout",
            "Unwraps common package folders. Files already at the archive root, "
            "including DLL, INI, and .log files, stay at the game root. DLLs "
            "inside recognized native-mod folders are organized under the game's "
            "DLLs folder with their matching INI and .log files. Other folders "
            "and bundled logs are preserved at their virtual paths.",
            False,
        )

        default_label = QtWidgets.QLabel(
            self.tr(
                "MO2 asks on every supported installation. For an existing mod, "
                "its previous layout and text-file choices are preselected."
            )
        )
        default_label.setWordWrap(True)
        layout.addWidget(default_label)

        buttons = QtWidgets.QDialogButtonBox(
            QtWidgets.QDialogButtonBox.StandardButton.Cancel
        )
        buttons.rejected.connect(dialog.reject)
        layout.addWidget(buttons)

        default_button = preserve_button if self._default_preserve else standard_button
        default_button.setDefault(True)
        default_button.setFocus()
        dialog.exec()

        if dialog.result() != QtWidgets.QDialog.DialogCode.Accepted:
            return None
        return selected["choice"]

    def tr(self, value: str) -> str:
        return QtWidgets.QApplication.translate(
            "EldenRingArchiveInstaller", value
        )
