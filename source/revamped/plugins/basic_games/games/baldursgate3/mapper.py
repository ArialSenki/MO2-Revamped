"""The game's documents folder under the VFS: each mod's paks to
<documents>/Mods, its Script Extender files to <documents>/Script Extender,
the profile's modsettings.lsx to PlayerProfiles/Public, and what the game
writes there to Overwrite.

From basic_games' Baldur's Gate 3 plugin (daescha, BG3FileMapper), with MO2
Revamped's fixes: the game folders at a mod's top level (Wabbajack lists),
Overwrite's Data/, empty folders removed after the run, YAML converted whole
or not at all.
"""

from __future__ import annotations

import os
import time
from pathlib import Path
from typing import Callable, Optional

import mobase

from .loadorder import is_workspace
from .paks import scan_mod_paks
from .util import (
    SE_CONFIG_FOLDER,
    TOP_LEVEL_DATA_FOLDERS,
    debug_enabled,
    log_debug,
    log_info,
    log_warning,
    subfolders,
)

_top_level_data_folders = {f.casefold() for f in TOP_LEVEL_DATA_FOLDERS}
# a mod's folders that are mapped by their own rules
_mod_root_folders = {"bin", "root", "script extender", SE_CONFIG_FOLDER.casefold(), "pak_files"}

# what the game writes to its documents folder goes to Overwrite;
# ModCrashSanityCheck is the game's marker for a start that crashed with mods
OVERWRITE_FOLDERS = (
    "Script Extender",
    "Stats",
    "Temp",
    "LevelCache",
    "Mods",
    "GMCampaigns",
    "ModCrashSanityCheck",
)


class FileMapper:
    def __init__(self, game) -> None:
        # game: the BG3 game plugin (organizer, settings, paths)
        self._game = game
        self.mappings_list: list[mobase.Mapping] = []
        # the folders the mappings made that weren't there before, in
        # Overwrite and Documents; removed after the run if still empty
        self.created_dirs: set[Path] = set()
        self._made_dirs: set[Path] = set()
        # the paks the load order found just before this start, by mod folder
        # (one walk of each mod instead of two); used once
        self.known_paks: dict[str, list[Path]] = {}

    @property
    def doc_path(self) -> Path:
        return Path(self._game.documentsDirectory().absolutePath())

    def mappings(self) -> list[mobase.Mapping]:
        game = self._game
        self.mappings_list = []
        self._made_dirs = set()
        active_mods = game.active_mods()
        known, self.known_paks = self.known_paks, {}
        if not active_mods:
            return []
        log_info("creating the Baldur's Gate 3 mappings")
        # no Cancel button: a start can't be stopped from here, and a cut
        # mapping would start the game with half the mods
        progress = game.progress_window(
            "Mapping files to documents folder", len(active_mods) + 1, cancelable=False
        )
        overwrite = game.overwrite_path
        docs_mods = self.doc_path / "Mods"
        docs_se = self.doc_path / "Script Extender"
        game_data = Path(game.gameDirectory().absolutePath()) / "Data"
        # without Root Builder, MO2 maps each mod's Root/ to the game folder
        root_builder = game.root_builder_enabled()
        convert = bool(game.setting("convert_yamls_to_json"))
        next_paint = 0.0
        try:
            for done, mod in enumerate(active_mods, 1):
                modpath = Path(mod.absolutePath())
                paks = known.get(str(modpath))
                for pak in paks if paks is not None else scan_mod_paks(modpath):
                    self.create_mapping(pak, docs_mods / pak.name)
                self.map_files(modpath / "Script Extender", dest=docs_se, convert=convert)
                self.map_files(modpath / SE_CONFIG_FOLDER, dest=docs_se, convert=convert)
                self.map_data_folders(modpath, game_data, not root_builder)
                if convert:
                    self.map_files(modpath / "bin", only_convert=True, convert=True)
                    if not root_builder:
                        self.map_files(modpath / "Root" / "bin", only_convert=True, convert=True)
                if progress is not None and time.monotonic() >= next_paint:
                    progress.setValue(done)
                    game.process_events()
                    next_paint = time.monotonic() + 0.05
            for folder in OVERWRITE_FOLDERS:
                self._make_dir(overwrite / folder)
            # usvfs only maps a folder into a folder it has in its tree; with
            # no mod having Data/, it checks <Overwrite>/Data in place of the
            # game's Data and the folder mappings below were dropped
            self._make_dir(overwrite / "Data" / "Mods")
            # Overwrite/Data is the game's (mapped below), not the documents'
            self.map_files(overwrite, skip_top=frozenset({"data"}), convert=convert)
            if (overwrite / "Data").is_dir():
                self.map_folder(overwrite / "Data", game_data)
            self.create_mapping(
                game.modsettings_path,
                self.doc_path / "PlayerProfiles" / "Public" / "modsettings.lsx",
            )
        finally:
            if progress is not None:
                progress.close()
        if debug_enabled():
            log_debug(
                f"resolved mappings: { {m.source: m.destination for m in self.mappings_list} }"
            )
        return self.mappings_list

    def map_files(
        self,
        path: Path,
        dest: Optional[Path] = None,
        only_convert: bool = False,
        skip_top: frozenset[str] = frozenset(),
        convert: bool = False,
    ) -> None:
        # most mods have no "Script Extender" or SE_CONFIG folder
        if not path.is_dir():
            return
        dest = dest if dest else self.doc_path
        found_jsons: set[Path] = set()
        for file in list(path.rglob("*")):
            if skip_top and file.relative_to(path).parts[0].casefold() in skip_top:
                continue
            name = file.name.casefold()
            if convert and name.endswith((".yaml", ".yml")):
                converted = convert_yaml(file)
                if converted is not None:
                    found_jsons.add(converted)
            elif name.endswith(".json"):
                found_jsons.add(file)
            elif not only_convert:
                self.create_mapping(file, dest / os.path.relpath(file, path))
        if only_convert:
            return
        for file in found_jsons:
            self.create_mapping(file, dest / os.path.relpath(file, path))

    def map_data_folders(self, modpath: Path, game_data: Path, map_root: bool) -> None:
        """Data/... and the game folders a mod has at its top level (Public/,
        Generated/, Video/, Mods/<folder>/... as Wabbajack lists leave them)
        go to <game>/Data, in the mods' order so the higher mod wins. Folders
        are mapped whole, like MO2 maps the mods, so nothing is made in the
        game's own folder."""
        for entry in subfolders(modpath):
            name = entry.name.casefold()
            if name == "data":
                self.map_folder(entry, game_data)
            elif name == "root" and map_root:
                for sub in subfolders(entry):
                    if sub.name.casefold() == "data":
                        self.map_folder(sub, game_data)
            elif name == "mods":
                # its paks go to the documents' Mods folder (above), its
                # folders (loose mods, ModBrowser/GUI/...) to Data/Mods
                for folder in subfolders(entry):
                    self.map_folder(folder, game_data / "Mods" / folder.name)
            elif name in _top_level_data_folders:
                self.map_folder(entry, game_data / entry.name)
            elif name not in _mod_root_folders and not is_workspace(entry):
                # loose files in a wrapper folder ("Smaller Lutes/Generated/..."),
                # which the original plugin packed with LSLib
                for sub in subfolders(entry):
                    if sub.name.casefold() in _top_level_data_folders:
                        self.map_folder(sub, game_data / sub.name)

    def map_folder(self, folder: Path, dest: Path) -> None:
        self.mappings_list.append(
            mobase.Mapping(
                source=str(folder),
                destination=str(dest),
                is_directory=True,
                create_target=False,
            )
        )

    def _make_dir(self, folder: Path) -> None:
        """The folder and its missing parents, made once per start, noting
        which ones weren't there (created_dirs)."""
        if folder in self._made_dirs:
            return
        missing: list[Path] = []
        current = folder
        while not current.exists() and current.parent != current:
            missing.append(current)
            current = current.parent
        try:
            folder.mkdir(parents=True, exist_ok=True)
        except OSError as e:
            log_warning(f"couldn't make {folder}: {e}")
        self.created_dirs.update(missing)
        self._made_dirs.add(folder)

    def create_mapping(self, file: Path, dest: Path) -> None:
        is_dir = file.is_dir()
        self._make_dir(dest if is_dir else dest.parent)
        self.mappings_list.append(
            mobase.Mapping(
                source=str(file),
                destination=str(dest),
                is_directory=is_dir,
                create_target=True,
            )
        )

    def remove_empty_folders(self, overwrite: Path, documents: bool = True) -> None:
        """The folders the mapper made for the program, and Overwrite's
        Data/Mods, if still empty, the deepest first. Empty folders of the
        user's own stay. Documents' only after the game: it may be running on
        its own after another program."""
        candidates = set(self.created_dirs) | {overwrite / "Data" / "Mods", overwrite / "Data"}
        doc = self.doc_path
        removed: list[Path] = []
        for folder in sorted(candidates, key=lambda f: len(f.parts), reverse=True):
            if folder.is_relative_to(doc) and not documents:
                continue
            if folder in (overwrite, doc):
                continue
            try:
                folder.rmdir()
                removed.append(folder)
            except OSError:
                pass  # gone already, or not empty: the program's or the user's
            self.created_dirs.discard(folder)
        if removed and debug_enabled():
            log_debug(f"removed the empty folders the mappings made: {removed}")


def convert_yaml(file: Path) -> Path | None:
    """A Script Extender YAML config as the JSON it reads, made again when
    the YAML is newer. Read first, so a file that isn't valid YAML leaves no
    empty JSON; written whole, then swapped in."""
    converted = file.with_suffix(".json")
    try:
        if converted.exists() and os.path.getmtime(file) <= os.path.getmtime(converted):
            return converted
        import json

        from . import mini_yaml

        with open(file, "r", encoding="utf-8") as yaml_file:
            data = mini_yaml.load_text(yaml_file.read())
        text = json.dumps(data, indent=2, allow_nan=False, default=str)
        part = converted.with_name(converted.name + ".part")
        try:
            with open(part, "w", encoding="utf-8") as json_file:
                json_file.write(text)
            os.replace(part, converted)
        finally:
            part.unlink(missing_ok=True)
        log_debug(f"converted {file} to JSON")
        return converted
    except OSError as e:
        log_warning(f"couldn't convert {file}: {e}")
    except Exception as e:  # noqa: BLE001 - not valid YAML: this file stays
        log_warning(f"couldn't convert {file} to JSON: {e}")
    return None
