import functools
import os
import time
from pathlib import Path
from typing import Callable, Optional

from PyQt6.QtCore import QDir, QLoggingCategory, qDebug, qInfo, qWarning
from PyQt6.QtWidgets import QApplication

import mobase

from . import bg3_utils

_top_level_data_folders = {f.casefold() for f in bg3_utils.top_level_data_folders}


def _subfolders(path: Path) -> list[Path]:
    try:
        return [e for e in path.iterdir() if e.is_dir()]
    except OSError:
        return []


class BG3FileMapper(mobase.IPluginFileMapper):
    current_mappings: list[mobase.Mapping] = []

    def __init__(self, utils: bg3_utils.BG3Utils, doc_dir: Callable[[], QDir]):
        super().__init__()
        self._utils = utils
        self.doc_dir = doc_dir

    @functools.cached_property
    def doc_path(self):
        return Path(self.doc_dir().path())

    def mappings(self) -> list[mobase.Mapping]:
        qInfo("creating custom bg3 mappings")
        self.current_mappings.clear()
        self._made_dirs: set[Path] = set()
        active_mods = self._utils.active_mods()
        if not active_mods:
            return []
        progress = self._utils.create_progress_window(
            "Mapping files to documents folder", len(active_mods) + 1
        )
        docs_path_mods = self.doc_path / "Mods"
        docs_path_se = self.doc_path / "Script Extender"
        game_data = Path(
            self._utils._organizer.managedGame().gameDirectory().absolutePath()
        ) / "Data"
        # MO2 Revamped: without Root Builder, MO2 maps each mod's Root/ to the
        # game folder itself (OrganizerCore::fileMapping)
        root_bin = "bin" if self._utils.root_builder_enabled() else "Root/bin"
        done = 0
        next_paint = 0.0
        for mod in active_mods:
            modpath = Path(mod.absolutePath())
            # MO2 Revamped: the same paks the load order has (bg3_utils.mod_paks)
            for pak in bg3_utils.mod_paks(modpath):
                self.create_mapping(pak, docs_path_mods / pak.name)
            self.map_files(modpath / "Script Extender", dest=docs_path_se)
            # MO2 Revamped: the unofficial BG3 plugin's folder for the same files
            self.map_files(modpath / bg3_utils.se_config_folder, dest=docs_path_se)
            self.map_data_folders(modpath, game_data, root_bin != "bin")
            if self._utils.convert_yamls_to_json:
                self.map_files(modpath / "bin", only_convert=True)
                if root_bin != "bin":
                    self.map_files(modpath / root_bin, only_convert=True)
            # MO2 Revamped: the window painted every 50 ms, not after every mod
            # (a repaint per mod adds up with hundreds of mods)
            done += 1
            if time.monotonic() >= next_paint:
                progress.setValue(done)
                QApplication.processEvents()
                next_paint = time.monotonic() + 0.05
            if progress.wasCanceled():
                qWarning("mapping canceled by user")
                bg3_utils.forget_pak_scans()
                return self.current_mappings
        # MO2 Revamped: the load order's scans are used up (bg3_utils.mod_paks)
        bg3_utils.forget_pak_scans()
        (self._utils.overwrite_path / "Script Extender").mkdir(
            parents=True, exist_ok=True
        )
        (self._utils.overwrite_path / "Stats").mkdir(parents=True, exist_ok=True)
        (self._utils.overwrite_path / "Temp").mkdir(parents=True, exist_ok=True)
        (self._utils.overwrite_path / "LevelCache").mkdir(parents=True, exist_ok=True)
        (self._utils.overwrite_path / "Stats").mkdir(parents=True, exist_ok=True)
        (self._utils.overwrite_path / "Mods").mkdir(parents=True, exist_ok=True)
        (self._utils.overwrite_path / "GMCampaigns").mkdir(parents=True, exist_ok=True)
        # MO2 Revamped: usvfs only maps a folder into a folder it has in its
        # tree, and when no mod has Data/ it checks <Overwrite>/Data in place of
        # the game's Data ("Overwrite\Data doesn't exist"), so the Public/,
        # Generated/, Video/ and Mods/<folder> mappings below were dropped. MO2
        # maps Overwrite to the game folder, so these empty folders put Data and
        # Data/Mods in the tree; _on_finished_run removes them if still empty.
        (self._utils.overwrite_path / "Data" / "Mods").mkdir(
            parents=True, exist_ok=True
        )
        # Overwrite/Data is the game's (mapped below), not the documents'
        self.map_files(self._utils.overwrite_path, skip_top=frozenset({"data"}))
        # MO2 Revamped: Overwrite's Data/ stays above the mods' (map_data_folders)
        if (self._utils.overwrite_path / "Data").is_dir():
            self.map_folder(self._utils.overwrite_path / "Data", game_data)
        self.create_mapping(
            self._utils.modsettings_path,
            self.doc_path
            / "PlayerProfiles"
            / "Public"
            / self._utils.modsettings_path.name,
        )
        progress.setValue(len(active_mods) + 1)
        QApplication.processEvents()
        progress.close()
        cat = QLoggingCategory.defaultCategory()
        if cat is not None and cat.isDebugEnabled():
            qDebug(
                f"resolved mappings: { {m.source: m.destination for m in self.current_mappings} }"
            )
        return self.current_mappings

    def map_files(
        self,
        path: Path,
        dest: Optional[Path] = None,
        pattern: str = "*",
        rel: bool = True,
        only_convert: bool = False,
        skip_top: frozenset[str] = frozenset(),
    ):
        dest = dest if dest else self.doc_path
        dest_func: Callable[[Path], str] = (
            (lambda f: os.path.relpath(f, path)) if rel else lambda f: f.name
        )
        found_jsons: set[Path] = set()
        for file in list(path.rglob(pattern)):
            if skip_top and file.relative_to(path).parts[0].casefold() in skip_top:
                continue
            if self._utils.convert_yamls_to_json and (
                file.name.endswith(".yaml") or file.name.endswith(".yml")
            ):
                converted_path = file.parent / file.name.replace(
                    ".yaml", ".json"
                ).replace(".yml", ".json")
                try:
                    if not converted_path.exists() or os.path.getmtime(
                        file
                    ) > os.path.getmtime(converted_path):
                        import json

                        import yaml

                        # MO2 Revamped: read first, so a file that isn't valid
                        # YAML leaves no empty JSON behind
                        with open(file, "r", encoding="utf-8") as yaml_file:
                            data = yaml.safe_load(yaml_file)
                        with open(converted_path, "w", encoding="utf-8") as json_file:
                            json.dump(data, json_file, indent=2)
                        qDebug(f"Converted {file} to JSON")
                    found_jsons.add(converted_path)
                except OSError as e:
                    qWarning(f"Error accessing file {converted_path}: {e}")
                except Exception as e:  # noqa: BLE001
                    # MO2 Revamped: PyYAML isn't bundled, or the file isn't valid
                    # YAML: this file stays as it is, the other mods still map
                    qWarning(f"Couldn't convert {file} to JSON: {e}")
            elif file.name.endswith(".json"):
                found_jsons.add(file)
            elif not only_convert:
                self.create_mapping(file, dest / dest_func(file))
        if only_convert:
            return
        for file in found_jsons:
            self.create_mapping(file, dest / dest_func(file))

    def map_data_folders(self, modpath: Path, game_data: Path, map_root: bool):
        """MO2 Revamped: Data/... and the game folders a mod has at its top level
        (Public/, Generated/, Video/, Mods/<folder>/... as Wabbajack lists leave
        them) go to <game>/Data, in the mods' order so the higher mod wins.

        Data/ is MO2's own mapping too; it's mapped here again so a top-level
        folder of a lower mod doesn't win over a higher mod's Data/ (or Root/Data/,
        which MO2 maps to the game folder when Root Builder is off). Folders are
        mapped whole, like MO2 maps the mods, so nothing is made in the game's
        own folder."""
        for entry in _subfolders(modpath):
            name = entry.name.casefold()
            if name == "data":
                self.map_folder(entry, game_data)
            elif name == "root" and map_root:
                for sub in _subfolders(entry):
                    if sub.name.casefold() == "data":
                        self.map_folder(sub, game_data)
            elif name == "mods":
                # its paks go to the documents' Mods folder (see mappings()),
                # its folders (loose mods, ModBrowser/GUI/...) to Data/Mods
                for folder in _subfolders(entry):
                    self.map_folder(folder, game_data / "Mods" / folder.name)
            elif name in _top_level_data_folders:
                self.map_folder(entry, game_data / entry.name)

    def map_folder(self, folder: Path, dest: Path):
        self.current_mappings.append(
            mobase.Mapping(
                source=str(folder),
                destination=str(dest),
                is_directory=True,
                create_target=False,
            )
        )

    def create_mapping(self, file: Path, dest: Path):
        # MO2 Revamped: each folder made once per start, not once per file
        made = getattr(self, "_made_dirs", None)
        key = dest if "." not in dest.name[1:] else dest.parent
        if made is None or key not in made:
            bg3_utils.create_dir_if_needed(dest)
            if made is not None:
                made.add(key)

        self.current_mappings.append(
            mobase.Mapping(
                source=str(file),
                destination=str(dest),
                is_directory=file.is_dir(),
                create_target=True,
            )
        )
