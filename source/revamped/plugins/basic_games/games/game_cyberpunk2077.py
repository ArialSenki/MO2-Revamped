"""Cyberpunk 2077 support for MO2 Revamped.

MO2 Revamped's own plugin, replacing basic_games' game_cyberpunk2077.py and
keeping its identity (plugin name, game name, short names, settings), so
instances and Nexus collections made with it keep working.

Based on basic_games' Cyberpunk 2077 plugin by 6788 and Zash (Mod Organizer 2,
https://github.com/ModOrganizer2/modorganizer-basic_games, GPL-3.0): the save
metadata reader, the REDmod deployment and the r6/cache copy started there.
Install layouts follow the Cyberpunk modding wiki (wiki.redmodding.org) and
Vortex's Cyberpunk extension (cyberpunk2077_ext_redux).

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version. It is distributed WITHOUT ANY WARRANTY; see the GNU General Public
License for more details.
"""

from __future__ import annotations

import json
import os
import re
import shutil
from collections import Counter
from pathlib import Path
from typing import Any

import mobase
from PyQt6.QtCore import QDateTime, QDir
from PyQt6.QtWidgets import QMessageBox

from ..basic_features import BasicLocalSavegames
from ..basic_features.basic_save_game_info import (
    BasicGameSaveGame,
    BasicGameSaveGameInfo,
    format_date,
)
from ..basic_game import BasicGame
from .cyberpunk2077.content import CyberpunkModDataContent
from .cyberpunk2077.layout import CyberpunkModDataChecker
from .cyberpunk2077.util import (
    local_app_data_folder,
    log_critical,
    log_info,
    log_warning,
    root_builder_enabled,
    saved_games_folder,
    tr,
)

# saves


def _time_from_seconds(seconds: int | float) -> str:
    minutes, seconds = divmod(int(seconds), 60)
    hours, minutes = divmod(minutes, 60)
    return f"{hours:02}:{minutes:02}:{seconds:02}"


def _metadata_file(save_path: Path) -> Path | None:
    """metadata.9.json, or the newest version of it a later game writes."""
    preferred = save_path / "metadata.9.json"
    if preferred.is_file():
        return preferred
    best: tuple[int, Path] | None = None
    for file in save_path.glob("metadata.*.json"):
        match = re.fullmatch(r"metadata\.(\d+)\.json", file.name, re.IGNORECASE)
        if match and (best is None or int(match[1]) > best[0]):
            best = (int(match[1]), file)
    return best[1] if best else None


def parse_cyberpunk_save_metadata(save_path: Path, save: mobase.ISaveGame):
    metadata_file = _metadata_file(Path(save_path))
    if metadata_file is None:
        return None
    try:
        # the game writes UTF-8 (accented quest names)
        with open(metadata_file, encoding="utf-8") as file:
            meta = json.load(file)["Data"]["metadata"]
        name = meta.get("name", "")
        if name != (save_name := save.getName()):
            name = f"{save_name}  ({name})" if name else save_name
        info: dict[str, Any] = {"Name": name}
        if "timestampString" in meta:
            info["Date"] = format_date(meta["timestampString"], "hh:mm:ss, d.M.yyyy")
        if "playthroughTime" in meta:
            info["Play Time"] = _time_from_seconds(meta["playthroughTime"])
        for label, key in (
            ("Quest", "trackedQuestEntry"),
            ("Life Path", "lifePath"),
            ("Difficulty", "difficulty"),
            ("Game version", "buildPatch"),
        ):
            if key in meta:
                info[label] = meta[key]
        for label, key in (("Level", "level"), ("Street Cred", "streetCred")):
            if key in meta:
                info[label] = int(meta[key])
        if "bodyGender" in meta:
            info["Gender"] = f'{meta["bodyGender"]} / {meta.get("brainGender", "")}'
        return info
    except (OSError, ValueError, KeyError, TypeError, AttributeError, OverflowError):
        # a save the game is still writing, or another version's format
        return None


class CyberpunkSaveGame(BasicGameSaveGame):
    _name_file = "NamedSave.txt"  # from the Named Saves mod
    _save_file = "sav.dat"

    def __init__(self, filepath: Path):
        super().__init__(filepath)
        try:
            with open(filepath / self._name_file, encoding="utf-8") as file:
                self._name = file.readline().strip()
        except (OSError, ValueError):
            self._name = ""
        try:
            self._mtime = (filepath / self._save_file).stat().st_mtime
        except OSError:
            self._mtime = 0.0

    def getName(self) -> str:
        return self._name or super().getName()

    def getCreationTime(self) -> QDateTime:
        return QDateTime.fromSecsSinceEpoch(int(self._mtime))

    def allFiles(self) -> list[str]:
        try:
            return [file.as_posix() for file in self._filepath.iterdir() if file.is_file()]
        except OSError:
            return []


# the game plugin


def _file_hash(path: Path) -> str:
    import hashlib

    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError:
        return ""


class Cyberpunk2077Game(BasicGame):
    # the plugin's name is its identity: instances keep its settings under it
    Name = "Cyberpunk 2077 Support Plugin"
    Author = "MO2 Revamped"
    Version = "3.0.0"
    Description = (
        "Cyberpunk 2077 support for MO2 Revamped: archive, REDmod, RED4ext, Cyber Engine "
        "Tweaks, redscript and TweakXL mods, load orders and saves. Based on the work of "
        "6788 and Zash."
    )
    DescriptionContext = "Cyberpunk2077Game"

    GameName = "Cyberpunk 2077"
    GameShortName = "cyberpunk2077"
    GameNexusName = "cyberpunk2077"
    GameNexusId = 3333
    GameBinary = "bin/x64/Cyberpunk2077.exe"
    GameLauncher = "REDprelauncher.exe"
    GameDataPath = "%GAME_PATH%"
    GameDocumentsDirectory = local_app_data_folder() + "/CD Projekt Red/Cyberpunk 2077"
    GameSavesDirectory = saved_games_folder() + "/CD Projekt Red/Cyberpunk 2077"
    GameSaveExtension = "dat"
    GameSteamId = 1091500
    GameGogId = 1423049311
    # the Epic launcher's manifests name it by AppName; basic_games looked for
    # the catalog ID
    GameEpicId = ["Ginger", "77f2b98e2cef40c8a7437518bf420e47"]
    GameSupportURL = "https://wiki.redmodding.org/cyberpunk-2077-modding"

    _redmod_binary = Path("tools/redmod/bin/redMod.exe")
    _redmod_log = Path("tools/redmod/bin/REDmodLog.txt")
    _redmod_deploy_path = Path("r6/cache/modded")
    _redmod_list_name = "MO_REDmod_load_order.txt"
    _redmod_deploy_args = "deploy -reportProgress"
    _archive_list = Path("archive/pc/mod/modlist.txt")
    _archive_pattern = "archive/pc/mod/*.archive"

    # kept in the instance's PluginPersistance, not in the settings
    _state_redmod = "redmod_deploy_fingerprint"
    _state_archive_list = "archive_modlist_hash"

    # Cyberpunk2077.exe imports version.dll (CET's loader) as it starts,
    # before the VFS is there, so Windows' own is taken; RED4ext's winmm.dll
    # comes only once the game first uses it, after RED4ext would load its
    # plugins. The VFS loads both as the game starts.
    _forced_loaders = ("bin/x64/winmm.dll", "bin/x64/version.dll")

    def __init__(self) -> None:
        super().__init__()
        self._window = None

    # IPlugin

    def init(self, organizer: mobase.IOrganizer) -> bool:
        super().init(organizer)
        self._register_feature(BasicLocalSavegames(self))
        self._register_feature(
            BasicGameSaveGameInfo(
                lambda p: Path(p or "", "screenshot.png"),
                parse_cyberpunk_save_metadata,
            )
        )
        self._register_feature(CyberpunkModDataChecker(lambda: root_builder_enabled(self._organizer)))
        # the Content column and filter: archives, REDmod, RED4ext, CET...
        self._register_feature(CyberpunkModDataContent())

        def ui_ready(window: Any) -> None:
            self._window = window
            self._configure_root_builder_once()

        organizer.onUserInterfaceInitialized(ui_ready)
        organizer.onPluginEnabled("RootBuilder", lambda *_: self._configure_root_builder_once())
        organizer.onAboutToRun(self._on_about_to_run)
        return True

    def settings(self) -> list[mobase.PluginSetting]:
        # the keys and defaults of basic_games' plugin, so instances keep theirs
        return [
            mobase.PluginSetting(
                "skipStartScreen",
                tr(
                    'Skips the "Breaching..." start screen on game launch'
                    " (can also skip loading of GOG rewards)"
                ),
                False,
            ),
            mobase.PluginSetting(
                "enforce_archive_load_order",
                tr(
                    "Write the mod list's order of .archive files to"
                    " <code>archive/pc/mod/modlist.txt</code> (in Overwrite) on each launch;"
                    " turning it off removes the file this plugin wrote"
                ),
                False,
            ),
            mobase.PluginSetting(
                "reverse_archive_load_order",
                tr(
                    "In <code>modlist.txt</code>, put the mod with the highest priority first."
                    " The game lets the first listed archive win, so this makes MO2's"
                    " priority win as it does for loose files"
                ),
                False,
            ),
            mobase.PluginSetting(
                "enforce_redmod_load_order",
                tr(
                    "Enforce the current load order on REDmod deployment"
                ),
                True,
            ),
            mobase.PluginSetting(
                "reverse_redmod_load_order",
                tr(
                    "Reverse MO2's order on REDmod deployment"
                    " (the first deployed REDmod wins: highest priority in MO2 first)"
                ),
                False,
            ),
            mobase.PluginSetting(
                "auto_deploy_redmod",
                tr(
                    "Deploy REDmods before the game starts when they changed"
                ),
                True,
            ),
            mobase.PluginSetting(
                "clear_cache_after_game_update",
                tr(
                    'Clears "overwrite/r6/cache" when the game\'s own cache files changed'
                    " (after an update)"
                ),
                True,
            ),
            mobase.PluginSetting(
                "configure_RootBuilder",
                tr(
                    "Configures Root Builder for Cyberpunk once, if it is installed and enabled"
                ),
                True,
            ),
        ]

    def _get_setting(self, key: str) -> mobase.MoVariant:
        return self._organizer.pluginSetting(self.name(), key)

    def _set_setting(self, key: str, value: mobase.MoVariant) -> None:
        self._organizer.setPluginSetting(self.name(), key, value)

    def _get_state(self, key: str) -> str:
        return str(self._organizer.persistent(self.name(), key, "") or "")

    def _set_state(self, key: str, value: str) -> None:
        # written only on a change: MO2 saves the instance's settings for it
        if self._get_state(key) != value:
            self._organizer.setPersistent(self.name(), key, value, True)

    # IPluginGame

    def iniFiles(self) -> list[str]:
        return ["UserSettings.json"]

    def gameVersion(self) -> str:
        """The patch players and Nexus know (2.5): the exe's product version
        string. Its numeric file version (3.0.88.46235) is the engine's and
        matched no collection's game version."""
        if not self.isInstalled():
            return ""
        exe = self.gameDirectory().absoluteFilePath(self.binaryName())
        try:
            product = str(mobase.getProductVersion(exe) or "").strip()
        except Exception:  # noqa: BLE001 - the exe is missing or unreadable
            product = ""
        if product and all(part.isdigit() for part in product.split(".")):
            return product
        try:
            return super().gameVersion()
        except Exception:  # noqa: BLE001 - the exe is missing or unreadable
            return ""

    def listSaves(self, folder: QDir) -> list[mobase.ISaveGame]:
        root = Path(folder.absolutePath())
        try:
            saves = [CyberpunkSaveGame(file.parent) for file in root.glob("*/sav.dat") if file.is_file()]
        except OSError:
            return []
        saves.sort(key=lambda save: save._mtime, reverse=True)
        return saves

    def executables(self) -> list[mobase.ExecutableInfo]:
        if not self.isInstalled():
            return []
        game_dir = self.gameDirectory()
        game = game_dir.absoluteFilePath(self.binaryName())
        skip = " -skipStartScreen" if self._get_setting("skipStartScreen") else ""
        # the titles and arguments of basic_games' plugin: MO2 matches an
        # instance's executables by title
        return [
            # deploys REDmods first when they changed
            mobase.ExecutableInfo(self.gameName(), game).withArgument(f"--launcher-skip -modded{skip}"),
            mobase.ExecutableInfo(f"{self.gameName()} - skip REDmod deploy", game).withArgument(
                f"--launcher-skip {skip}"
            ),
            mobase.ExecutableInfo("Manually deploy REDmod", str(self._redmod_path())).withArgument(
                "deploy -reportProgress -force %modlist%"
            ),
            mobase.ExecutableInfo(
                "REDprelauncher", game_dir.absoluteFilePath(self.getLauncherName())
            ).withArgument(skip.strip()),
        ]

    def executableForcedLoads(self) -> list[mobase.ExecutableForcedLoadSetting]:
        if not self.isInstalled():
            return []
        loads: list[mobase.ExecutableForcedLoadSetting] = []
        try:
            # Root Builder deploys root/ as real files, which Windows loads by
            # itself; loaders at the mod's top level (bin/x64) still need it
            root_builder = root_builder_enabled(self._organizer)
            tree = self._organizer.virtualFileTree()
            process = Path(self.binaryName()).name
            # a mod's root/ is the game folder too (Wabbajack lists made with
            # Root Builder keep the loaders in root/bin/x64). Read from the
            # active mods' folders, whatever the data tree lists
            in_root: set[str] = set()
            if not root_builder:
                mods = self._organizer.modList()
                # Overwrite's root/ too, as the data tree used to include it
                bases = [Path(self._organizer.overwritePath())]
                for name in mods.allModsByProfilePriority():
                    if not mods.state(name) & mobase.ModState.ACTIVE:
                        continue
                    mod = mods.getMod(name)
                    if mod is not None:
                        bases.append(Path(mod.absolutePath()))
                for base in bases:
                    for relative in self._forced_loaders:
                        if (base / "root" / relative).is_file():
                            in_root.add(relative)
            for relative in self._forced_loaders:
                if tree.find(relative) is not None or relative in in_root:
                    loads.append(
                        mobase.ExecutableForcedLoadSetting(
                            process, self.gameDirectory().absoluteFilePath(relative)
                        ).withEnabled(True)
                    )
        except Exception as error:  # noqa: BLE001 - never stop a launch here
            log_warning(f"Cyberpunk 2077: forced loads not set: {error}")
        return loads

    # paths

    def _game_path(self) -> Path:
        return Path(self.gameDirectory().absolutePath())

    def _redmod_path(self) -> Path:
        return self._game_path() / self._redmod_binary

    def _overwrite(self) -> Path:
        return Path(self._organizer.overwritePath())

    def _active_mod_paths(self) -> list[Path]:
        """The active mods' folders, lowest priority first."""
        mods_path = Path(self._organizer.modsPath())
        mod_list = self._organizer.modList()
        paths = []
        for name in mod_list.allModsByProfilePriority():
            if mod_list.state(name) & mobase.ModState.ACTIVE:
                mod = mod_list.getMod(name)
                if mod is not None and (mod.isSeparator() or mod.isForeign() or mod.isOverwrite()):
                    continue
                paths.append(mods_path / name)
        return paths

    @staticmethod
    def _ordered_names(found: list[tuple[int, str]]) -> list[str]:
        """Names in mod order, each once, where its winning (highest priority)
        mod puts it."""
        position: dict[str, tuple[int, str]] = {}
        for index, name in found:
            position[name.casefold()] = (index, name)
        return [name for _, name in sorted(position.values())]

    # launch

    def _on_about_to_run(self, app_path_str: str, wd: QDir, args: str) -> bool:
        if not self.isActive() or not self.isInstalled():
            return True
        app_path = Path(app_path_str)
        redmod = self._redmod_path()
        if _same_path(app_path, redmod):
            return self._manual_redmod_deploy(app_path_str, wd, args)
        # the game's caches go to Overwrite before anything writes them
        self._map_cache_files()
        is_game = _same_path(app_path, self._game_path() / self.binaryName())
        if is_game and "-modded" in args and self._get_setting("auto_deploy_redmod"):
            if not self._auto_redmod_deploy():
                return False
        try:
            self._update_archive_list()
        except OSError as error:
            log_warning(f"Cyberpunk 2077: archive load order not written: {error}")
        return True

    # archive load order

    def _archive_names(self) -> list[str]:
        found: list[tuple[int, str]] = []
        for index, mod_path in enumerate(self._active_mod_paths()):
            found.extend((index, file.name) for file in sorted(mod_path.glob(self._archive_pattern)))
        names = self._ordered_names(found)
        if self._get_setting("reverse_archive_load_order"):
            names.reverse()
        return names

    def _update_archive_list(self) -> None:
        """archive/pc/mod/modlist.txt in Overwrite (never a mod's or the game's
        own file): the game loads the listed archives first, in that order."""
        import hashlib

        path = self._overwrite() / self._archive_list
        names = self._archive_names() if self._get_setting("enforce_archive_load_order") else []
        if len(names) >= 2:
            text = "\r\n".join(names) + "\r\n"
            data = text.encode("utf-8")
            if not path.is_file() or path.read_bytes() != data:
                log_info(f"Cyberpunk 2077: archive load order of {len(names)} archives written to {path}")
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            self._set_state(self._state_archive_list, hashlib.sha256(data).hexdigest())
            return
        # no order to enforce: remove the list this plugin wrote, nothing else
        written = self._get_state(self._state_archive_list)
        if written and path.is_file() and _file_hash(path) == written:
            log_info(f"Cyberpunk 2077: removing the archive load order {path}")
            path.unlink()
        if written:
            self._set_state(self._state_archive_list, "")

    # REDmod

    def _redmod_names(self) -> tuple[list[str], list[Path]]:
        """The active REDmods' folder names in deploy order, and their folders."""
        found: list[tuple[int, str]] = []
        folders: dict[str, Path] = {}
        for index, mod_path in enumerate(self._active_mod_paths()):
            for info in sorted(mod_path.glob("mods/*/info.json")):
                found.append((index, info.parent.name))
                folders[info.parent.name.casefold()] = info.parent
        names = self._ordered_names(found)
        if self._get_setting("reverse_redmod_load_order"):
            names.reverse()
        return names, [folders[name.casefold()] for name in names]

    def _redmod_list_path(self) -> Path:
        return self._overwrite() / self._redmod_deploy_path / self._redmod_list_name

    def _write_redmod_list(self, names: list[str]) -> tuple[Path, list[str]]:
        """Writes the deploy order; returns its path and the previous order."""
        path = self._redmod_list_path()
        try:
            old = path.read_text(encoding="utf-8").splitlines()
        except (OSError, ValueError):
            old = []
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("\n".join(names), encoding="utf-8")
        return path, old

    def _deployed_files(self) -> list[Path]:
        """What REDmod deployed in Overwrite (the deploy order apart)."""
        folder = self._overwrite() / self._redmod_deploy_path
        if not folder.is_dir():
            return []
        keep = self._redmod_list_name.casefold()
        return [file for file in folder.rglob("*") if file.is_file() and file.name.casefold() != keep]

    def _clean_deployed_redmod(self) -> None:
        """Removes REDmod's output from Overwrite only: the game folder's own
        files are never touched."""
        for file in self._deployed_files():
            try:
                file.unlink()
            except OSError as error:
                log_warning(f"Cyberpunk 2077: could not remove {file}: {error}")

    def _redmod_fingerprint(self, names: list[str], folders: list[Path]) -> str:
        """What a deploy depends on: the REDmods, their order and files, the
        game and REDmod's own version, and whether its output is there."""
        import hashlib

        digest = hashlib.sha256()
        digest.update("\n".join(names).encode("utf-8"))
        for folder in folders:
            for file in sorted(folder.rglob("*")):
                try:
                    if file.is_file():
                        info = file.stat()
                        relative = file.relative_to(folder).as_posix()
                        digest.update(f"{relative}|{info.st_size}|{info.st_mtime_ns}\n".encode("utf-8"))
                except OSError:
                    continue
        for file in (self._game_path() / self.binaryName(), self._redmod_path()):
            try:
                info = file.stat()
                digest.update(f"{file.name}|{info.st_size}|{info.st_mtime_ns}\n".encode("utf-8"))
            except OSError:
                pass
        digest.update(b"deployed" if self._deployed_files() else b"empty")
        return digest.hexdigest()

    def _auto_redmod_deploy(self) -> bool:
        """Deploys the active REDmods before the game starts. False: don't
        start the game."""
        try:
            names, folders = self._redmod_names()
            if not names:
                if self._deployed_files():
                    log_info("Cyberpunk 2077: no active REDmod, removing their deployed files from Overwrite")
                    self._clean_deployed_redmod()
                self._set_state(self._state_redmod, "")
                return True
            if not self._redmod_path().is_file():
                log_warning(
                    "Cyberpunk 2077: REDmods are active but the REDmod DLC isn't installed"
                    f" ({self._redmod_binary} is missing); starting without deploying them"
                )
                return True
            fingerprint = self._redmod_fingerprint(names, folders)
            if fingerprint == self._get_state(self._state_redmod):
                log_info(f"Cyberpunk 2077: {len(names)} REDmods unchanged since the last deploy")
                return True
            args = self._redmod_deploy_args
            if self._get_setting("enforce_redmod_load_order"):
                path, old = self._write_redmod_list(names)
                if Counter(names) == Counter(old) and names != old:
                    # REDmod doesn't see an order change: deploy afresh
                    log_info("Cyberpunk 2077: REDmod order changed, deploying afresh")
                    self._clean_deployed_redmod()
                args += f' -modlist="{path}"'
            log_info(f"Cyberpunk 2077: deploying {len(names)} REDmods")
            result = self._run_redmod(args)
        except OSError as error:
            log_warning(f"Cyberpunk 2077: REDmod deploy not prepared: {error}")
            return self._ask_start_anyway(
                tr("REDmod deployment couldn't start: {0}").format(error)
            )
        if result == (True, 0):
            self._set_state(self._state_redmod, self._redmod_fingerprint(names, folders))
            return True
        self._set_state(self._state_redmod, "")
        code = result[1]
        log_critical(f"Cyberpunk 2077: REDmod deployment failed with exit code {code}")
        return self._ask_start_anyway(
            tr(
                "REDmod deployment failed (exit code {0}). Its log is {1} in the game folder."
            ).format(code, self._redmod_log.as_posix())
        )

    def _run_redmod(self, args: str) -> tuple[bool, int]:
        redmod = self._redmod_path()
        handle = self._organizer.startApplication(str(redmod), [args], str(redmod.parent))
        return self._organizer.waitForApplication(handle, False)

    def _manual_redmod_deploy(self, app_path: str, wd: QDir, args: str) -> bool:
        """The "Manually deploy REDmod" executable: %modlist% becomes the
        current order."""
        match = re.search(r"%modlist%", args, re.IGNORECASE)
        if match is None:
            return True  # our own start below, or a custom one
        names, _ = self._redmod_names()
        modlist = ""
        if names:
            path, _ = self._write_redmod_list(names)
            modlist = f'-modlist="{path}"'
        args = f"{args[: match.start()]}{modlist}{args[match.end() :]}"
        log_info(f"Cyberpunk 2077: manual REDmod deploy: {args}")
        handle = self._organizer.startApplication(app_path, [args], wd)
        ok, code = self._organizer.waitForApplication(handle, False)
        self._set_state(self._state_redmod, "")
        if (ok, code) != (True, 0):
            log_critical(f"Cyberpunk 2077: REDmod deployment failed with exit code {code}")
        return False  # started with the new arguments

    def _ask_start_anyway(self, reason: str) -> bool:
        answer = QMessageBox.question(
            self._window,
            tr("Cyberpunk 2077"),
            reason + "<br><br>" + tr("Start the game anyway, without the REDmods' latest changes?"),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        return answer == QMessageBox.StandardButton.Yes

    # r6/cache

    def _map_cache_files(self) -> None:
        """Copies the game's r6/cache files (final.redscripts, tweakdb.bin...)
        to Overwrite, so redscript and REDmod write their copies there instead
        of changing the game folder's (the VFS writes an existing file where
        it is)."""
        try:
            data_path = Path(self.dataDirectory().absolutePath())
            overwrite = self._overwrite()
            game_cache = data_path / "r6/cache"
            if not game_cache.is_dir():
                return
            cache_files = [file for file in game_cache.iterdir() if file.is_file()]
            if self._get_setting("clear_cache_after_game_update") and any(
                self._is_cache_file_updated(file.relative_to(data_path), data_path) for file in cache_files
            ):
                log_info('Cyberpunk 2077: the game\'s cache changed (an update), refreshing "overwrite/r6/cache"')
                shutil.rmtree(overwrite / "r6/cache", ignore_errors=True)
                (overwrite / "r6/cache").mkdir(parents=True, exist_ok=True)
                for file in cache_files:
                    shutil.copy2(file, overwrite / "r6/cache" / file.name)
                # REDmod's output went with it
                self._set_state(self._state_redmod, "")
                return
            for relative in self._unmapped_cache_files(data_path):
                log_info(f'Cyberpunk 2077: copying "{relative}" to Overwrite')
                target = overwrite / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(data_path / relative, target)
        except OSError as error:
            log_warning(f"Cyberpunk 2077: r6/cache not copied to Overwrite: {error}")

    def _is_cache_file_updated(self, file: Path, data_path: Path) -> bool:
        """Whether the game's own copy of a cache file differs from the one the
        VFS serves (and from its backup, which redscript makes)."""
        import filecmp

        game_file = data_path / file
        mapped = self._organizer.findFiles(file.parent.as_posix(), file.name)
        if not mapped:
            return False
        mapped_file = Path(mapped[0])
        try:
            if game_file.samefile(mapped_file) or filecmp.cmp(game_file, mapped_file):
                return False
            backups = self._organizer.findFiles(file.parent.as_posix(), f"{file.name}.bk")
            return not (backups and filecmp.cmp(game_file, backups[0]))
        except OSError:
            return False

    def _unmapped_cache_files(self, data_path: Path):
        """The cache files the VFS serves from the game folder itself."""
        for file in self._organizer.findFiles("r6/cache", "*"):
            try:
                yield Path(file).absolute().relative_to(data_path)
            except ValueError:
                continue

    # Root Builder

    _root_builder_settings = {
        "usvfsmode": False,
        "linkmode": False,
        # Root Builder 4.5+; doesn't work with MO2 2.5.2's Python 3.12
        # (https://github.com/Kezyma/ModOrganizer-Plugins/issues/36)
        "linkonlymode": False,
        "backup": True,
        "cache": True,
        "autobuild": True,
        "redirect": False,
        "installer": False,
        "exclusions": "archive,setup_redlauncher.exe,tools",
        "linkextensions": "dll,exe",
    }

    def _configure_root_builder_once(self) -> None:
        if not self.isActive() or not self._get_setting("configure_RootBuilder"):
            return
        if not self._organizer.isPluginEnabled("RootBuilder"):
            return
        for key, value in self._root_builder_settings.items():
            self._organizer.setPluginSetting("RootBuilder", key, value)
        log_info("Cyberpunk 2077: Root Builder configured")
        self._set_setting("configure_RootBuilder", False)


def _same_path(a: Path, b: Path) -> bool:
    return os.path.normcase(os.path.normpath(a)) == os.path.normcase(os.path.normpath(b))
